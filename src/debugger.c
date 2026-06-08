/* debugger.c — TypeEasy debugger sidecar.
 *
 * Speaks a tiny line-oriented JSON-ish protocol over a single TCP socket
 * with the Node debug adapter. Designed to keep zero overhead when not in
 * use: every interpreter dispatch checks one global int.
 *
 * Protocol (one message per line, '\n' terminated, host byte order):
 *   client -> server (commands):
 *     {"cmd":"set_breakpoints","file":"...","lines":[27,28]}
 *     {"cmd":"start"}                     -- finish configuration, run
 *     {"cmd":"continue"}
 *     {"cmd":"next"}                      -- step over
 *     {"cmd":"step_in"}
 *     {"cmd":"step_out"}
 *     {"cmd":"pause"}
 *     {"cmd":"stack"}
 *     {"cmd":"vars","frame":N}
 *     {"cmd":"disconnect"}
 *
 *   server -> client (events / responses):
 *     {"event":"stopped","reason":"breakpoint","line":N,"file":"..."}
 *     {"event":"output","stream":"stdout","text":"..."}
 *     {"event":"terminated","exit":N}
 *     {"resp":"stack","frames":[{"id":0,"name":"main","line":27,"file":"..."}, ...]}
 *     {"resp":"vars","vars":[{"name":"x","type":"int","value":"42"}]}
 *
 * Strings are escaped minimally (\\, \", \n, \t).
 */

#define _POSIX_C_SOURCE 200809L
#include "debugger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#ifdef _WIN32
  /* Windows native debug backend: Winsock + Win32 threads. The shims below map
   * the few primitives that differ from POSIX so the bulk of the code (parsing,
   * variable rendering, the command loop) is shared verbatim. Socket handles
   * are kept in `int`: valid Windows SOCKET values fit in 32 bits and
   * INVALID_SOCKET maps to -1 when cast to int, so the file's `fd < 0` / `= -1`
   * conventions remain valid. */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <process.h>            /* _beginthreadex */
  typedef int socklen_t;          /* winsock addr lengths are plain int */
  static int te_sock_read (int fd, void *buf, size_t n)       { return recv((SOCKET)fd, (char*)buf, (int)n, 0); }
  static int te_sock_write(int fd, const void *buf, size_t n) { return send((SOCKET)fd, (const char*)buf, (int)n, 0); }
  #define te_sock_close(fd) closesocket((SOCKET)(fd))
  #define TE_SLEEP_SEC(s)   Sleep((DWORD)(s) * 1000u)
  #define TE_EINTR          WSAEINTR
  #define TE_SOCK_ERRNO     WSAGetLastError()
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <signal.h>
  #include <pthread.h>
  static int te_sock_read (int fd, void *buf, size_t n)       { return (int)read(fd, buf, n); }
  static int te_sock_write(int fd, const void *buf, size_t n) { return (int)write(fd, buf, n); }
  #define te_sock_close(fd) close(fd)
  #define TE_SLEEP_SEC(s)   sleep((unsigned)(s))
  #define TE_EINTR          EINTR
  #define TE_SOCK_ERRNO     errno
#endif

#include "typeeasy_http.h"

/* One-time Winsock initialisation (no-op on POSIX). */
static void te_sock_startup(void) {
#ifdef _WIN32
    static int done = 0;
    if (!done) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        done = 1;
    }
#endif
}


/* ===== globals ===== */
int g_debug_enabled = 0;
const char *g_debug_source_file = NULL;

static int g_client_fd = -1;

/* Receive buffer for line-based reads. */
#define RX_CAP 8192
static char g_rx[RX_CAP];
static size_t g_rx_len = 0;

/* Breakpoints. Stored as (file_basename, line) pairs so multiple files can
 * coexist (attach mode hosts many .te). For standalone mode, file is "". */
#define MAX_BPS 256
static int  g_bp_lines[MAX_BPS];
static char g_bp_files[MAX_BPS][96];
static int  g_bp_count = 0;

/* Frame stack (call frames). v1: just names + call-site line for stack trace. */
#define MAX_FRAMES 256
typedef struct {
    const char *name;
    int call_line;     /* line of the call site (in caller frame) */
    int current_line;  /* updated by debugger_on_statement while inside */
    int depth;         /* == index */
} DbgFrame;
static DbgFrame g_frames[MAX_FRAMES];
static int g_frame_top = 0;     /* number of active frames; frames[0..top-1] live */

/* Step state. */
typedef enum { RUN, STEP_OVER, STEP_IN, STEP_OUT, PAUSED_PENDING } StepMode;
static StepMode g_step = RUN;
static int g_step_depth = 0;     /* frame depth captured when step was issued */
static int g_last_line = 0;      /* avoid re-stopping on same statement */
/* Re-arm a breakpoint after a 'continue' so the same line can re-trigger
 * (e.g. inside a loop). */
static int g_armed = 1;
/* Per-line stop de-duplication, reset at the top of each loop iteration by
 * debugger_on_loop_iteration() so a single-line loop body re-fires every
 * iteration (file-scope so the loop hook can clear them). */
static int g_last_stop_line = -1;
static int g_last_stop_depth = -1;

/* Forward decls of interpreter internals we touch from here. */
extern Variable vars[];
extern int var_count;

/* ===== low-level IO ===== */

static int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        int w = te_sock_write(fd, buf, n);
        if (w < 0) {
            if (TE_SOCK_ERRNO == TE_EINTR) continue;
            return -1;
        }
        buf += w; n -= (size_t)w;
    }
    return 0;
}

static void send_line(const char *s) {
    if (g_client_fd < 0) return;
    size_t n = strlen(s);
    if (write_all(g_client_fd, s, n) < 0) return;
    write_all(g_client_fd, "\n", 1);
}

/* Append-escape a JSON string value (no surrounding quotes). */
static void json_escape_into(char *dst, size_t cap, const char *src) {
    size_t o = 0;
    if (!src) src = "";
    for (; *src && o + 2 < cap; ++src) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            if (o + 2 >= cap) break;
            dst[o++] = '\\'; dst[o++] = (char)c;
        } else if (c == '\n') {
            if (o + 2 >= cap) break;
            dst[o++] = '\\'; dst[o++] = 'n';
        } else if (c == '\t') {
            if (o + 2 >= cap) break;
            dst[o++] = '\\'; dst[o++] = 't';
        } else if (c < 0x20) {
            /* drop other control chars */
        } else {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

/* Read one '\n'-terminated line (line WITHOUT the newline). Blocks. */
static int recv_line(char *out, size_t cap) {
    for (;;) {
        /* serve from buffer */
        for (size_t i = 0; i < g_rx_len; ++i) {
            if (g_rx[i] == '\n') {
                size_t copy = i < cap - 1 ? i : cap - 1;
                memcpy(out, g_rx, copy);
                out[copy] = '\0';
                /* shift remainder */
                size_t rem = g_rx_len - (i + 1);
                memmove(g_rx, g_rx + i + 1, rem);
                g_rx_len = rem;
                return 0;
            }
        }
        if (g_rx_len >= RX_CAP) {
            /* line too long: drop */
            g_rx_len = 0;
            return -1;
        }
        int r = te_sock_read(g_client_fd, g_rx + g_rx_len, RX_CAP - g_rx_len);
        if (r == 0) return -1;          /* EOF */
        if (r < 0) {
            if (TE_SOCK_ERRNO == TE_EINTR) continue;
            return -1;
        }
        g_rx_len += (size_t)r;
    }
}

/* ===== tiny JSON helpers (not a real parser) =====
 * Commands are short and well-formed, so we use string scanning. */

static int json_str_field(const char *line, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p[1]) { out[o++] = p[1]; p += 2; }
        else                     { out[o++] = *p++; }
    }
    out[o] = '\0';
    return 0;
}

static int json_int_field(const char *line, const char *key, int *out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    *out = atoi(p);
    return 0;
}

/* Parse "lines":[1,2,3] for a given source file. We store BPs per-file so
 * multiple .te can have BPs simultaneously (needed for attach-to-API mode).
 * Each set_breakpoints REPLACES all BPs for that file (DAP semantics). */
static int parse_lines_array(const char *line) {
    char file_in[96] = "";
    json_str_field(line, "file", file_in, sizeof(file_in));

    /* In standalone mode, optionally enforce that BPs match our single source
     * (drop unrelated files to keep the array small). */
    const char *cur = g_debug_source_file ? g_debug_source_file : "";
    const char *cur_base = strrchr(cur, '/');
    cur_base = cur_base ? cur_base + 1 : cur;
    if (cur_base[0] && file_in[0] && strcmp(file_in, cur_base) != 0) {
        return 0;
    }

    /* Step 1: drop any existing BPs for this file (compact in place). */
    int w = 0;
    for (int r = 0; r < g_bp_count; r++) {
        if (strcmp(g_bp_files[r], file_in) != 0) {
            if (w != r) {
                g_bp_lines[w] = g_bp_lines[r];
                strncpy(g_bp_files[w], g_bp_files[r], sizeof(g_bp_files[w]) - 1);
                g_bp_files[w][sizeof(g_bp_files[w]) - 1] = '\0';
            }
            w++;
        }
    }
    g_bp_count = w;

    /* Step 2: parse the new line-list and append. */
    const char *p = strstr(line, "\"lines\"");
    if (!p) return 1;
    p = strchr(p, '[');
    if (!p) return 1;
    p++;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p >= '0' && *p <= '9') {
            int v = atoi(p);
            if (g_bp_count < MAX_BPS) {
                g_bp_lines[g_bp_count] = v;
                strncpy(g_bp_files[g_bp_count], file_in, sizeof(g_bp_files[g_bp_count]) - 1);
                g_bp_files[g_bp_count][sizeof(g_bp_files[g_bp_count]) - 1] = '\0';
                g_bp_count++;
            }
            while (*p >= '0' && *p <= '9') p++;
        } else if (*p) {
            p++;
        }
    }
    return 1;
}

static int line_has_breakpoint(int line) {
    for (int i = 0; i < g_bp_count; ++i) if (g_bp_lines[i] == line) return 1;
    return 0;
}

/* ===== command handlers ===== */

static void send_stopped(const char *reason, int line) {
    char buf[512];
    char esc_reason[64], esc_file[512];
    json_escape_into(esc_reason, sizeof(esc_reason), reason);
    json_escape_into(esc_file, sizeof(esc_file), g_debug_source_file ? g_debug_source_file : "");
    snprintf(buf, sizeof(buf),
             "{\"event\":\"stopped\",\"reason\":\"%s\",\"line\":%d,\"file\":\"%s\"}",
             esc_reason, line, esc_file);
    send_line(buf);
}

static void cmd_stack(void) {
    char buf[4096];
    size_t o = 0;
    o += (size_t)snprintf(buf + o, sizeof(buf) - o, "{\"resp\":\"stack\",\"frames\":[");
    /* Top of stack first (innermost). */
    for (int i = g_frame_top - 1; i >= 0; --i) {
        char esc[256];
        json_escape_into(esc, sizeof(esc), g_frames[i].name ? g_frames[i].name : "?");
        char esc_file[512];
        json_escape_into(esc_file, sizeof(esc_file), g_debug_source_file ? g_debug_source_file : "");
        o += (size_t)snprintf(buf + o, sizeof(buf) - o,
                              "%s{\"id\":%d,\"name\":\"%s\",\"line\":%d,\"file\":\"%s\"}",
                              (i == g_frame_top - 1) ? "" : ",",
                              g_frame_top - 1 - i,
                              esc,
                              g_frames[i].current_line,
                              esc_file);
        if (o + 64 >= sizeof(buf)) break;
    }
    snprintf(buf + o, sizeof(buf) - o, "]}");
    send_line(buf);
}

static const char *vtype_name(ValueType t) {
    switch (t) {
        case VAL_INT:    return "int";
        case VAL_FLOAT:  return "float";
        case VAL_STRING: return "string";
        case VAL_OBJECT: return "object";
        default:         return "unknown";
    }
}

/* ===== variable references for object/list expansion ===== */
typedef enum {
    REF_LIST,
    REF_OBJECT,
    REF_REQ_ROOT,
    REF_REQ_HEADERS,
    REF_REQ_QUERY,
    REF_REQ_PARAMS,
    REF_REQ_CLIENT
} RefKind;
/* Sentinel pointers used as identity for the synthetic $req refs. */
static char g_req_root_sentinel;
static char g_req_headers_sentinel;
static char g_req_query_sentinel;
static char g_req_params_sentinel;
static char g_req_client_sentinel;
typedef struct { RefKind kind; void *ptr; } DbgRef;
#define MAX_REFS 1024
static DbgRef g_refs[MAX_REFS];
static int g_ref_count = 0;

static void refs_reset(void) { g_ref_count = 0; }

static int register_ref(RefKind kind, void *ptr) {
    if (!ptr) return 0;
    for (int i = 0; i < g_ref_count; ++i) {
        if (g_refs[i].ptr == ptr && g_refs[i].kind == kind) return i + 1;
    }
    if (g_ref_count >= MAX_REFS) return 0;
    g_refs[g_ref_count].kind = kind;
    g_refs[g_ref_count].ptr = ptr;
    return ++g_ref_count;
}

/* Count items in a LIST ASTNode chain (cur->left->next->next...). */
static int list_count(ASTNode *listNode) {
    if (!listNode) return 0;
    int n = 0;
    for (ASTNode *cur = listNode->left; cur; cur = cur->next) n++;
    return n;
}

/* Render one Variable into name/type/value(/ref). Returns ref (0 if none). */
static int render_variable_value(const Variable *v, char *val, size_t valcap, const char **type_out) {
    int ref = 0;
    *type_out = vtype_name(v->vtype);
    switch (v->vtype) {
        case VAL_INT:
            snprintf(val, valcap, "%d", v->value.int_value);
            break;
        case VAL_FLOAT:
            snprintf(val, valcap, "%g", v->value.float_value);
            break;
        case VAL_STRING: {
            const char *sv = v->value.string_value;
            snprintf(val, valcap, "%.200s", sv ? sv : "");
            break;
        }
        case VAL_OBJECT:
            if (v->type && strcmp(v->type, "LIST") == 0) {
                ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
                int n = list_count(listNode);
                snprintf(val, valcap, "[%d items]", n);
                ref = register_ref(REF_LIST, listNode);
                *type_out = "list";
            } else {
                ObjectNode *obj = v->value.object_value;
                if (obj && obj->class && obj->class->name) {
                    snprintf(val, valcap, "<%s>", obj->class->name);
                    *type_out = obj->class->name;
                } else {
                    snprintf(val, valcap, "<object>");
                }
                ref = register_ref(REF_OBJECT, obj);
            }
            break;
        default:
            snprintf(val, valcap, "?");
    }
    return ref;
}

/* Append one var entry to buf. Returns new offset. */
static size_t emit_var_entry(char *buf, size_t cap, size_t o, int first,
                              const char *name, const char *type,
                              const char *value, int ref) {
    char esc_name[128], esc_val[512], esc_type[64];
    json_escape_into(esc_name, sizeof(esc_name), name);
    json_escape_into(esc_val, sizeof(esc_val), value);
    json_escape_into(esc_type, sizeof(esc_type), type);
    o += (size_t)snprintf(buf + o, cap - o,
                          "%s{\"name\":\"%s\",\"type\":\"%s\",\"value\":\"%s\",\"ref\":%d}",
                          first ? "" : ",", esc_name, esc_type, esc_val, ref);
    return o;
}

static void cmd_vars(void) {
    char buf[16384];
    size_t o = 0;
    o += (size_t)snprintf(buf + o, sizeof(buf) - o, "{\"resp\":\"vars\",\"vars\":[");
    int first = 1;
    /* Dedupe by name keeping the LATEST entry. vars[] is append-only across
     * scopes (each `var x = ...` adds a new slot, the older `x` stays alive
     * but is unreachable via find_variable since the symtab hash points to
     * the newest index). Without dedupe we would emit stale shadowed copies
     * and VS Code's Locals view picks the first (oldest) one. Iterate
     * backward and skip any name we've already emitted. */
    for (int i = var_count - 1; i >= 0 && o + 512 < sizeof(buf); --i) {
        Variable *v = &vars[i];
        if (!v->id) continue;
        if (v->id[0] == '_' && v->id[1] == '_') continue; /* skip __ret__ etc */
        int dup = 0;
        for (int j = i + 1; j < var_count; ++j) {
            if (vars[j].id && strcmp(vars[j].id, v->id) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        char val[256] = "";
        const char *type = "unknown";
        int ref = render_variable_value(v, val, sizeof(val), &type);
        o = emit_var_entry(buf, sizeof(buf), o, first, v->id, type, val, ref);
        first = 0;
    }

    /* === HTTP request snapshot ===
     * Expose live request state as a single expandable $req entry. */
    {
        const char *m = typeeasy_http_get_method();
        const char *p = typeeasy_http_get_path();
        if (m || p) {
            char summary[256];
            snprintf(summary, sizeof(summary), "%s %s",
                     m ? m : "?", p ? p : "?");
            int ref = register_ref(REF_REQ_ROOT, &g_req_root_sentinel);
            o = emit_var_entry(buf, sizeof(buf), o, first, "$req", "request", summary, ref);
            first = 0;
        }
    }
    snprintf(buf + o, sizeof(buf) - o, "]}");
    send_line(buf);
}

/* Parse User-Agent + client hints into browser/os/mobile summary. */
static void parse_user_agent(const char *ua, const char *sec_pf, const char *sec_mob,
                              char *browser, size_t bcap,
                              char *osname,  size_t ocap,
                              int *is_mobile_out) {
    snprintf(browser, bcap, "Unknown");
    snprintf(osname,  ocap, "Unknown");
    *is_mobile_out = 0;
    if (!ua || !*ua) return;
    const char *q;
    if      ((q = strstr(ua, "Edg/")))       snprintf(browser, bcap, "Edge %.32s",    q + 4);
    else if ((q = strstr(ua, "OPR/")))       snprintf(browser, bcap, "Opera %.32s",   q + 4);
    else if ((q = strstr(ua, "Brave/")))     snprintf(browser, bcap, "Brave %.32s",   q + 6);
    else if ((q = strstr(ua, "Firefox/")))   snprintf(browser, bcap, "Firefox %.32s", q + 8);
    else if ((q = strstr(ua, "Chrome/")))    snprintf(browser, bcap, "Chrome %.32s",  q + 7);
    else if ((q = strstr(ua, "Version/")) && strstr(ua, "Safari/"))
                                              snprintf(browser, bcap, "Safari %.32s", q + 8);
    else if (strstr(ua, "curl/"))            snprintf(browser, bcap, "curl");
    else if (strstr(ua, "PostmanRuntime/"))  snprintf(browser, bcap, "Postman");
    else if (strstr(ua, "python-requests/")) snprintf(browser, bcap, "Python requests");
    {
        char *sp = strchr(browser, ' ');
        if (sp) {
            char *sp2 = strpbrk(sp + 1, " );,");
            if (sp2) *sp2 = '\0';
        }
    }
    if      (strstr(ua, "Windows NT 10.0")) snprintf(osname, ocap, "Windows 10/11");
    else if (strstr(ua, "Windows NT 6.3"))  snprintf(osname, ocap, "Windows 8.1");
    else if (strstr(ua, "Windows NT 6.1"))  snprintf(osname, ocap, "Windows 7");
    else if (strstr(ua, "Windows"))         snprintf(osname, ocap, "Windows");
    else if (strstr(ua, "Android"))         snprintf(osname, ocap, "Android");
    else if (strstr(ua, "iPhone"))          snprintf(osname, ocap, "iOS (iPhone)");
    else if (strstr(ua, "iPad"))            snprintf(osname, ocap, "iPadOS");
    else if (strstr(ua, "Mac OS X"))        snprintf(osname, ocap, "macOS");
    else if (strstr(ua, "CrOS"))            snprintf(osname, ocap, "ChromeOS");
    else if (strstr(ua, "Linux"))           snprintf(osname, ocap, "Linux");
    if (sec_pf && *sec_pf) {
        char clean[64]; size_t ci = 0;
        for (const char *s = sec_pf; *s && ci + 1 < sizeof(clean); ++s)
            if (*s != '"') clean[ci++] = *s;
        clean[ci] = '\0';
        if (clean[0]) snprintf(osname, ocap, "%s", clean);
    }
    *is_mobile_out = (sec_mob && strstr(sec_mob, "?1")) || strstr(ua, "Mobile") != NULL;
}

/* Emit children for a previously registered ref. */
static void cmd_get_children(const char *line) {
    int ref_id = 0;
    json_int_field(line, "ref", &ref_id);
    char buf[16384];
    size_t o = 0;
    o += (size_t)snprintf(buf + o, sizeof(buf) - o, "{\"resp\":\"children\",\"vars\":[");
    int first = 1;
    if (ref_id >= 1 && ref_id <= g_ref_count) {
        DbgRef *r = &g_refs[ref_id - 1];
        if (r->kind == REF_LIST) {
            ASTNode *listNode = (ASTNode *)r->ptr;
            int idx = 0;
            for (ASTNode *cur = listNode ? listNode->left : NULL;
                 cur && o + 512 < sizeof(buf); cur = cur->next, ++idx) {
                char name[32]; snprintf(name, sizeof(name), "[%d]", idx);
                char val[256] = "";
                const char *type = "unknown";
                int child_ref = 0;
                if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                    ObjectNode *obj = NULL;
                    if (cur->extra) obj = (ObjectNode *)cur->extra;
                    else obj = (ObjectNode *)(intptr_t)cur->value;
                    if (obj && obj->class && obj->class->name) {
                        snprintf(val, sizeof(val), "<%s>", obj->class->name);
                        type = obj->class->name;
                    } else {
                        snprintf(val, sizeof(val), "<object>");
                        type = "object";
                    }
                    child_ref = register_ref(REF_OBJECT, obj);
                } else if (cur->type && strcmp(cur->type, "STRING") == 0) {
                    snprintf(val, sizeof(val), "%.200s", cur->str_value ? cur->str_value : "");
                    type = "string";
                } else if (cur->type && strcmp(cur->type, "FLOAT") == 0) {
                    snprintf(val, sizeof(val), "%s", cur->str_value ? cur->str_value : "0");
                    type = "float";
                } else {
                    snprintf(val, sizeof(val), "%d", cur->value);
                    type = "int";
                }
                o = emit_var_entry(buf, sizeof(buf), o, first, name, type, val, child_ref);
                first = 0;
            }
        } else if (r->kind == REF_OBJECT) {
            ObjectNode *obj = (ObjectNode *)r->ptr;
            if (obj && obj->class && obj->attributes) {
                for (int i = 0; i < obj->class->attr_count && o + 512 < sizeof(buf); ++i) {
                    Variable *attr = &obj->attributes[i];
                    const char *name = obj->class->attributes[i].id;
                    if (!name) continue;
                    char val[256] = "";
                    const char *type = "unknown";
                    int child_ref = render_variable_value(attr, val, sizeof(val), &type);
                    o = emit_var_entry(buf, sizeof(buf), o, first, name, type, val, child_ref);
                    first = 0;
                }
            }
        } else if (r->kind == REF_REQ_ROOT) {
            const char *m = typeeasy_http_get_method();
            const char *p = typeeasy_http_get_path();
            const char *b = typeeasy_http_get_body();
            if (m) { o = emit_var_entry(buf, sizeof(buf), o, first, "method", "string", m, 0); first = 0; }
            if (p) { o = emit_var_entry(buf, sizeof(buf), o, first, "path",   "string", p, 0); first = 0; }
            if (b && *b) {
                char trunc[256]; snprintf(trunc, sizeof(trunc), "%.250s", b);
                o = emit_var_entry(buf, sizeof(buf), o, first, "body", "string", trunc, 0); first = 0;
            }
            /* client summary (always present if there is a UA) */
            const char *kk, *vvv;
            const char *ua = NULL, *sec_pf = NULL, *sec_mob = NULL;
            for (int i = 0; typeeasy_http_iter_header(i, &kk, &vvv); ++i) {
                if (!kk) continue;
                if (strcasecmp(kk, "User-Agent") == 0) ua = vvv;
                else if (strcasecmp(kk, "Sec-CH-UA-Platform") == 0) sec_pf = vvv;
                else if (strcasecmp(kk, "Sec-CH-UA-Mobile") == 0) sec_mob = vvv;
            }
            if (ua && *ua) {
                char browser[96], osname[96]; int mob = 0;
                parse_user_agent(ua, sec_pf, sec_mob, browser, sizeof(browser),
                                 osname, sizeof(osname), &mob);
                char summary[200];
                snprintf(summary, sizeof(summary), "%s on %s%s", browser, osname,
                         mob ? " (mobile)" : "");
                int cref = register_ref(REF_REQ_CLIENT, &g_req_client_sentinel);
                o = emit_var_entry(buf, sizeof(buf), o, first, "client", "client", summary, cref);
                first = 0;
            }
            /* groups */
            int qcount = 0; for (int i = 0; typeeasy_http_iter_query(i, &kk, &vvv); ++i) qcount++;
            int pcount = 0; for (int i = 0; typeeasy_http_iter_param(i, &kk, &vvv); ++i) pcount++;
            int hcount = 0; for (int i = 0; typeeasy_http_iter_header(i, &kk, &vvv); ++i) hcount++;
            char gv[64];
            int qref = register_ref(REF_REQ_QUERY, &g_req_query_sentinel);
            snprintf(gv, sizeof(gv), "{%d}", qcount);
            o = emit_var_entry(buf, sizeof(buf), o, first, "query", "group", gv, qref); first = 0;
            int pref = register_ref(REF_REQ_PARAMS, &g_req_params_sentinel);
            snprintf(gv, sizeof(gv), "{%d}", pcount);
            o = emit_var_entry(buf, sizeof(buf), o, first, "params", "group", gv, pref); first = 0;
            int href = register_ref(REF_REQ_HEADERS, &g_req_headers_sentinel);
            snprintf(gv, sizeof(gv), "{%d}", hcount);
            o = emit_var_entry(buf, sizeof(buf), o, first, "headers", "group", gv, href); first = 0;
        } else if (r->kind == REF_REQ_HEADERS) {
            const char *kk, *vvv;
            for (int i = 0; typeeasy_http_iter_header(i, &kk, &vvv) && o + 512 < sizeof(buf); ++i) {
                o = emit_var_entry(buf, sizeof(buf), o, first,
                                   kk ? kk : "?", "string", vvv ? vvv : "", 0);
                first = 0;
            }
        } else if (r->kind == REF_REQ_QUERY) {
            const char *kk, *vvv;
            for (int i = 0; typeeasy_http_iter_query(i, &kk, &vvv) && o + 512 < sizeof(buf); ++i) {
                o = emit_var_entry(buf, sizeof(buf), o, first,
                                   kk ? kk : "?", "string", vvv ? vvv : "", 0);
                first = 0;
            }
        } else if (r->kind == REF_REQ_PARAMS) {
            const char *kk, *vvv;
            for (int i = 0; typeeasy_http_iter_param(i, &kk, &vvv) && o + 512 < sizeof(buf); ++i) {
                o = emit_var_entry(buf, sizeof(buf), o, first,
                                   kk ? kk : "?", "string", vvv ? vvv : "", 0);
                first = 0;
            }
        } else if (r->kind == REF_REQ_CLIENT) {
            const char *kk, *vvv;
            const char *ua = NULL, *sec_ua = NULL, *sec_pf = NULL, *sec_mob = NULL;
            for (int i = 0; typeeasy_http_iter_header(i, &kk, &vvv); ++i) {
                if (!kk) continue;
                if (strcasecmp(kk, "User-Agent") == 0) ua = vvv;
                else if (strcasecmp(kk, "Sec-CH-UA") == 0) sec_ua = vvv;
                else if (strcasecmp(kk, "Sec-CH-UA-Platform") == 0) sec_pf = vvv;
                else if (strcasecmp(kk, "Sec-CH-UA-Mobile") == 0) sec_mob = vvv;
            }
            char browser[96] = "Unknown", osname[96] = "Unknown"; int mob = 0;
            parse_user_agent(ua, sec_pf, sec_mob, browser, sizeof(browser),
                             osname, sizeof(osname), &mob);
            o = emit_var_entry(buf, sizeof(buf), o, first, "browser", "string", browser, 0); first = 0;
            o = emit_var_entry(buf, sizeof(buf), o, first, "os",      "string", osname,  0); first = 0;
            o = emit_var_entry(buf, sizeof(buf), o, first, "mobile",  "bool",   mob?"true":"false", 0); first = 0;
            if (sec_ua && *sec_ua) {
                char trunc[200]; snprintf(trunc, sizeof(trunc), "%.196s", sec_ua);
                o = emit_var_entry(buf, sizeof(buf), o, first, "brands", "string", trunc, 0); first = 0;
            }
            if (ua && *ua) {
                char trunc[256]; snprintf(trunc, sizeof(trunc), "%.250s", ua);
                o = emit_var_entry(buf, sizeof(buf), o, first, "userAgent", "string", trunc, 0); first = 0;
            }
        }
    }
    snprintf(buf + o, sizeof(buf) - o, "]}");
    send_line(buf);
}

/* === cmd_eval ===
 * Evalúa una expresión simple (hover/watch/REPL):
 *   - "name"             → variable
 *   - "name.attr"        → atributo de objeto, .length / .size sobre lista/map
 *   - "name[idx]"        → item de lista por índice (idx literal numérico)
 * Para algo más complejo, devolvemos un mensaje claro.
 * Respuesta: {"resp":"eval","value":"...","type":"...","ref":N} */
static void cmd_eval(const char *line) {
    char expr[512] = "";
    json_str_field(line, "expr", expr, sizeof(expr));
    /* trim */
    char *p = expr;
    while (*p == ' ' || *p == '\t') p++;
    size_t L = strlen(p);
    while (L > 0 && (p[L-1] == ' ' || p[L-1] == '\t' || p[L-1] == '\n' || p[L-1] == '\r' || p[L-1] == ';')) {
        p[--L] = '\0';
    }

    char val[256] = "";
    char type[64] = "unknown";
    int ref = 0;

    if (L == 0) {
        /* nada */
    } else {
        /* Buscar primer '.' o '[' */
        char *dot = strchr(p, '.');
        char *br  = strchr(p, '[');
        char *sep = NULL;
        if (dot && br) sep = (dot < br) ? dot : br;
        else sep = dot ? dot : br;

        char base[128];
        if (sep) {
            size_t bl = (size_t)(sep - p);
            if (bl >= sizeof(base)) bl = sizeof(base) - 1;
            memcpy(base, p, bl); base[bl] = '\0';
        } else {
            snprintf(base, sizeof(base), "%s", p);
        }

        Variable *v = find_variable(base);
        if (!v) {
            snprintf(val, sizeof(val), "<not in scope>");
        } else if (!sep) {
            /* Solo nombre de variable: render directo */
            const char *t = "unknown";
            ref = render_variable_value(v, val, sizeof(val), &t);
            snprintf(type, sizeof(type), "%s", t);
        } else if (*sep == '.') {
            /* name.attr  o  name.length / .size */
            const char *attr_name = sep + 1;
            if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
                if (strcmp(attr_name, "length") == 0 || strcmp(attr_name, "size") == 0) {
                    ASTNode *listNode = (ASTNode*)(intptr_t)v->value.object_value;
                    int n = list_count(listNode);
                    snprintf(val, sizeof(val), "%d", n);
                    snprintf(type, sizeof(type), "int");
                } else {
                    snprintf(val, sizeof(val), "<list has no attr '%s'>", attr_name);
                }
            } else if (v->vtype == VAL_OBJECT && v->value.object_value) {
                ObjectNode *obj = v->value.object_value;
                int found = 0;
                if (obj && obj->class && obj->attributes) {
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        const char *id = obj->class->attributes[i].id;
                        if (id && strcmp(id, attr_name) == 0) {
                            const char *t = "unknown";
                            ref = render_variable_value(&obj->attributes[i], val, sizeof(val), &t);
                            snprintf(type, sizeof(type), "%s", t);
                            found = 1;
                            break;
                        }
                    }
                }
                if (!found) snprintf(val, sizeof(val), "<no attr '%s'>", attr_name);
            } else {
                snprintf(val, sizeof(val), "<not an object>");
            }
        } else if (*sep == '[') {
            /* name[idx]  — solo índices numéricos literales */
            int idx = atoi(sep + 1);
            if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
                ASTNode *listNode = (ASTNode*)(intptr_t)v->value.object_value;
                int n = list_count(listNode);
                if (idx < 0 || idx >= n) {
                    snprintf(val, sizeof(val), "<index %d out of range [0..%d)>", idx, n);
                } else {
                    ASTNode *cur = listNode->left;
                    for (int i = 0; i < idx && cur; i++) cur = cur->next;
                    if (cur && cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = cur->extra ? (ObjectNode*)cur->extra
                                                     : (ObjectNode*)(intptr_t)cur->value;
                        if (obj && obj->class && obj->class->name) {
                            snprintf(val, sizeof(val), "<%s>", obj->class->name);
                            snprintf(type, sizeof(type), "%s", obj->class->name);
                            ref = register_ref(REF_OBJECT, obj);
                        } else {
                            snprintf(val, sizeof(val), "<object>");
                        }
                    } else if (cur && cur->type && strcmp(cur->type, "STRING") == 0) {
                        snprintf(val, sizeof(val), "%.200s", cur->str_value ? cur->str_value : "");
                        snprintf(type, sizeof(type), "string");
                    } else if (cur) {
                        snprintf(val, sizeof(val), "%d", cur->value);
                        snprintf(type, sizeof(type), "int");
                    }
                }
            } else {
                snprintf(val, sizeof(val), "<not a list>");
            }
        }
    }

    char esc_val[1024], esc_type[64];
    json_escape_into(esc_val, sizeof(esc_val), val);
    json_escape_into(esc_type, sizeof(esc_type), type);
    char buf[1280];
    snprintf(buf, sizeof(buf),
             "{\"resp\":\"eval\",\"value\":\"%s\",\"type\":\"%s\",\"ref\":%d}",
             esc_val, esc_type, ref);
    send_line(buf);
}

/* Block reading commands until one of them resumes execution.
 * Returns when client says continue/next/step_in/step_out/disconnect. */
static void wait_for_resume(void) {
    char line[2048];
    while (recv_line(line, sizeof(line)) == 0) {
        char cmd[64];
        if (json_str_field(line, "cmd", cmd, sizeof(cmd)) < 0) continue;

        if (strcmp(cmd, "continue") == 0) {
            g_step = RUN;
            return;
        } else if (strcmp(cmd, "next") == 0) {
            g_step = STEP_OVER;
            g_step_depth = g_frame_top;
            return;
        } else if (strcmp(cmd, "step_in") == 0) {
            g_step = STEP_IN;
            g_step_depth = g_frame_top;
            return;
        } else if (strcmp(cmd, "step_out") == 0) {
            g_step = STEP_OUT;
            g_step_depth = g_frame_top;
            return;
        } else if (strcmp(cmd, "set_breakpoints") == 0) {
            parse_lines_array(line);
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "stack") == 0) {
            cmd_stack();
        } else if (strcmp(cmd, "vars") == 0) {
            refs_reset();
            cmd_vars();
        } else if (strcmp(cmd, "get_children") == 0) {
            cmd_get_children(line);
        } else if (strcmp(cmd, "eval") == 0) {
            cmd_eval(line);
        } else if (strcmp(cmd, "pause") == 0) {
            /* Already paused; just ack. */
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "disconnect") == 0) {
            te_sock_close(g_client_fd);
            g_client_fd = -1;
            g_debug_enabled = 0;
            return;
        }
    }
    /* Connection closed: detach. */
    g_debug_enabled = 0;
}

/* ===== public API ===== */

void debugger_init(int port, const char *source_file) {
    g_debug_source_file = source_file ? source_file : "";

    te_sock_startup();
#ifndef _WIN32
    /* Avoid SIGPIPE crashing the interpreter when adapter disconnects. */
    signal(SIGPIPE, SIG_IGN);
#endif

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("[debugger] socket"); return; }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[debugger] bind"); te_sock_close(listen_fd); return;
    }
    if (listen(listen_fd, 1) < 0) {
        perror("[debugger] listen"); te_sock_close(listen_fd); return;
    }

    if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] Listening on port %d, waiting for adapter...\n", port);
    fflush(stderr);

    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int fd = accept(listen_fd, (struct sockaddr*)&cli, &cli_len);
    te_sock_close(listen_fd);
    if (fd < 0) { perror("[debugger] accept"); return; }

    g_client_fd = fd;
    g_debug_enabled = 1;

    /* Push a synthetic top-level frame "<main>". */
    debugger_push_frame("<main>", NULL);

    if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] Adapter connected. Awaiting configuration...\n");
    fflush(stderr);

    /* Send 'initialized' so adapter sends breakpoints + start. */
    send_line("{\"event\":\"initialized\"}");

    /* Wait until adapter sends "start" — handle set_breakpoints meanwhile. */
    char line[2048];
    while (recv_line(line, sizeof(line)) == 0) {
        char cmd[64];
        if (json_str_field(line, "cmd", cmd, sizeof(cmd)) < 0) continue;
        if (strcmp(cmd, "set_breakpoints") == 0) {
            int applied = parse_lines_array(line);
            if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
                fprintf(stderr, "[typeeasy-debugger] set_breakpoints (applied=%d): %d lines [", applied, g_bp_count);
                for (int i = 0; i < g_bp_count; i++) fprintf(stderr, "%s%d", i?",":"", g_bp_lines[i]);
                fprintf(stderr, "]\n"); fflush(stderr);
            }
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "start") == 0) {
            send_line("{\"resp\":\"ok\"}");
            g_step = RUN;
            return;
        } else if (strcmp(cmd, "disconnect") == 0) {
            te_sock_close(g_client_fd);
            g_client_fd = -1;
            g_debug_enabled = 0;
            return;
        }
    }
    g_debug_enabled = 0;
}

void debugger_push_frame(const char *name, ASTNode *call_site) {
    if (g_frame_top >= MAX_FRAMES) return;
    DbgFrame *f = &g_frames[g_frame_top];
    f->name = name ? name : "?";
    f->call_line = call_site ? call_site->line : 0;
    f->current_line = f->call_line;
    f->depth = g_frame_top;
    g_frame_top++;
}

void debugger_pop_frame(void) {
    if (g_frame_top > 0) g_frame_top--;
    /* If stepping out and we just left target depth, arm a stop. */
    if (g_debug_enabled && g_step == STEP_OUT && g_frame_top < g_step_depth) {
        g_step = STEP_OVER;        /* will stop at next statement in caller */
        g_step_depth = g_frame_top;
    }
}

void debugger_on_statement(ASTNode *node) {
    if (!g_debug_enabled || !node) return;
    int line = node->line;
    if (line <= 0) {
        if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
            fprintf(stderr, "[typeeasy-debugger] SKIP line<=0 kind=%d type=%s\n",
                    (int)node->kind, node->type ? node->type : "(null)");
            fflush(stderr);
        }
        return;
    }

    static int dbg_log_once = 0;
    if (!dbg_log_once) {
        dbg_log_once = 1;
        if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
            fprintf(stderr, "[typeeasy-debugger] FIRST hit: line=%d, g_bp_count=%d, kind=%d\n",
                    line, g_bp_count, (int)node->kind);
            fflush(stderr);
        }
    }

    /* Update current line of innermost frame. */
    if (g_frame_top > 0) {
        g_frames[g_frame_top - 1].current_line = line;
    }

    /* De-duplicate: if executor calls the hook multiple times for the same
     * source line within the same frame depth, only stop once. (File-scope so
     * debugger_on_loop_iteration() can reset it between loop iterations.) */

    int should_stop = 0;
    const char *reason = "step";

    /* If line changes vs last stop, re-arm so the same line can hit again
     * later (e.g. loop body). */
    if (line != g_last_stop_line) g_armed = 1;

    if (line_has_breakpoint(line)) {
        if (g_armed) {
            should_stop = 1;
            reason = "breakpoint";
        }
    } else if (g_step == STEP_IN) {
        if (line != g_last_line || g_frame_top != g_step_depth) {
            should_stop = 1;
        }
    } else if (g_step == STEP_OVER) {
        /* Stop on next statement at same-or-shallower depth. */
        if (g_frame_top <= g_step_depth &&
            (line != g_last_line || g_frame_top != g_step_depth)) {
            should_stop = 1;
        }
    }
    /* STEP_OUT is handled in pop_frame which converts to STEP_OVER. */

    g_last_line = line;

    if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
        fprintf(stderr, "[typeeasy-debugger] hook line=%d kind=%d step=%d armed=%d has_bp=%d should_stop=%d g_client_fd=%d\n",
                line, (int)node->kind, (int)g_step, g_armed,
                line_has_breakpoint(line), should_stop, g_client_fd);
        fflush(stderr);
    }

    if (!should_stop) return;

    g_last_stop_line = line;
    g_last_stop_depth = g_frame_top;
    g_step = RUN;
    g_armed = 0;        /* require a different line before re-firing same BP */
    send_stopped(reason, line);
    if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
        fprintf(stderr, "[typeeasy-debugger] sent stopped, entering wait_for_resume\n");
        fflush(stderr);
    }
    wait_for_resume();
    if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
        fprintf(stderr, "[typeeasy-debugger] wait_for_resume returned\n");
        fflush(stderr);
    }
}

void debugger_on_loop_iteration(void) {
    if (!g_debug_enabled) return;
    /* Reset the per-line stop de-duplication so the SAME source line (a
     * single-line loop body) is treated as fresh on each iteration. Without
     * this, after stopping once on the body line, g_last_line / g_last_stop_line
     * still equal that line, so a step (F10/F11) or a breakpoint on it would not
     * re-fire on the next iteration and the loop appears to "skip" its rounds. */
    g_last_line = -1;
    g_last_stop_line = -1;
    g_last_stop_depth = -1;
    g_armed = 1;
    if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
        fprintf(stderr, "[typeeasy-debugger] loop-iteration: dedup reset\n");
        fflush(stderr);
    }
}

void debugger_terminate(int exit_code) {
    if (g_client_fd < 0) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"event\":\"terminated\",\"exit\":%d}", exit_code);
    send_line(buf);
    te_sock_close(g_client_fd);
    g_client_fd = -1;
    g_debug_enabled = 0;
}

void debugger_emit_output(const char *category, const char *text) {
    if (!g_debug_enabled || g_client_fd < 0 || !text) return;
    if (!category) category = "stdout";
    /* Heap-buffer because user output can be arbitrarily large. */
    size_t tlen = strlen(text);
    size_t cap = tlen * 2 + 128;
    char *esc = (char*)malloc(cap);
    if (!esc) return;
    json_escape_into(esc, cap, text);
    size_t bcap = strlen(esc) + 128;
    char *buf = (char*)malloc(bcap);
    if (!buf) { free(esc); return; }
    snprintf(buf, bcap, "{\"event\":\"output\",\"category\":\"%s\",\"text\":\"%s\"}", category, esc);
    send_line(buf);
    free(esc);
    free(buf);
}

/* ===== Async listener for embedded servers (API server) =====
 * Spawns a thread that listens on `port`, accepts a single client at a time,
 * runs the handshake (set_breakpoints + start), then returns control. The
 * interpreter executes BPs in the calling thread (HTTP request thread) via
 * debugger_on_statement -> wait_for_resume which reads from the same socket.
 *
 * When the client disconnects, the thread loops back to accept again so the
 * adapter can reconnect without restarting the server. */
static int g_dbg_listen_fd = -1;
static int g_dbg_port = 0;

static void *dbg_acceptor_thread(void *arg) {
    (void)arg;
    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int fd = accept(g_dbg_listen_fd, (struct sockaddr*)&cli, &cli_len);
        if (fd < 0) {
            if (TE_SOCK_ERRNO == TE_EINTR) continue;
            perror("[debugger] accept (async)");
            TE_SLEEP_SEC(1);
            continue;
        }
        g_client_fd = fd;
        g_debug_enabled = 1;

        /* Reset frame stack (a fresh adapter session). */
        g_frame_top = 0;
        g_bp_count  = 0;
        g_step      = RUN;
        g_armed     = 1;
        g_last_line = -1;

        if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] (async) Adapter connected on port %d\n", g_dbg_port);
        fflush(stderr);

        send_line("{\"event\":\"initialized\"}");

        /* Handshake: set_breakpoints + start. After 'start', return so the
         * thread idles in this accept loop while requests in other threads
         * hit BPs. */
        char line[2048];
        while (recv_line(line, sizeof(line)) == 0) {
            char cmd[64];
            if (json_str_field(line, "cmd", cmd, sizeof(cmd)) < 0) continue;
            if (strcmp(cmd, "set_breakpoints") == 0) {
            parse_lines_array(line);
            if (getenv("TYPEEASY_DEBUG_VERBOSE")) {
                fprintf(stderr, "[typeeasy-debugger] (async) set_breakpoints: %d lines\n", g_bp_count);
                for (int i = 0; i < g_bp_count; i++) fprintf(stderr, "  bp[%d]=%d\n", i, g_bp_lines[i]);
                fflush(stderr);
            }
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "start") == 0) {
            send_line("{\"resp\":\"ok\"}");
            g_step = RUN;
            if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] (async) start, armed with %d BPs\n", g_bp_count);
            fflush(stderr);
            break; /* fall through: armed, waiting for requests to hit BPs */
            } else if (strcmp(cmd, "disconnect") == 0) {
                te_sock_close(g_client_fd);
                g_client_fd = -1;
                g_debug_enabled = 0;
                break;
            }
        }

        /* While the adapter is connected and armed, BPs are processed in
         * other threads via debugger_on_statement -> wait_for_resume. We
         * monitor the socket for disconnect by sleeping; the read in
         * wait_for_resume detects EOF and clears g_debug_enabled. */
        while (g_debug_enabled && g_client_fd >= 0) {
            TE_SLEEP_SEC(1);
        }
        if (g_client_fd >= 0) { te_sock_close(g_client_fd); g_client_fd = -1; }
        g_debug_enabled = 0;
        if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] (async) Adapter disconnected, listening again\n");
        fflush(stderr);
    }
    return NULL;
}

#ifdef _WIN32
static unsigned __stdcall dbg_acceptor_thread_win(void *arg) {
    dbg_acceptor_thread(arg);
    return 0;
}
#endif

void debugger_listen_async(int port, const char *source_file) {
    g_debug_source_file = source_file ? source_file : "";
    g_dbg_port = port;

    te_sock_startup();
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    g_dbg_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_dbg_listen_fd < 0) { perror("[debugger] socket"); return; }

    int yes = 1;
    setsockopt(g_dbg_listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(g_dbg_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[debugger] bind (async)");
        te_sock_close(g_dbg_listen_fd); g_dbg_listen_fd = -1;
        return;
    }
    if (listen(g_dbg_listen_fd, 1) < 0) {
        perror("[debugger] listen (async)");
        te_sock_close(g_dbg_listen_fd); g_dbg_listen_fd = -1;
        return;
    }

#ifdef _WIN32
    uintptr_t th = _beginthreadex(NULL, 0, dbg_acceptor_thread_win, NULL, 0, NULL);
    if (th) CloseHandle((HANDLE)th);
#else
    pthread_t th;
    pthread_create(&th, NULL, dbg_acceptor_thread, NULL);
    pthread_detach(th);
#endif

    if (getenv("TYPEEASY_DEBUG_VERBOSE")) fprintf(stderr, "[typeeasy-debugger] async listener up on port %d\n", port);
    fflush(stderr);
}

