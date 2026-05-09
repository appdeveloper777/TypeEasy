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
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#ifdef _WIN32
  /* Windows host build is not the supported debug target (we run inside the
   * Linux container). Stub out everything to avoid breaking other builds. */
  int g_debug_enabled = 0;
  const char *g_debug_source_file = NULL;
  void debugger_init(int port, const char *src) { (void)port; (void)src; }
  void debugger_push_frame(const char *n, ASTNode *c) { (void)n; (void)c; }
  void debugger_pop_frame(void) {}
  void debugger_on_statement(ASTNode *n) { (void)n; }
  void debugger_terminate(int e) { (void)e; }
  void debugger_emit_output(const char *c, const char *t) { (void)c; (void)t; }
#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

/* ===== globals ===== */
int g_debug_enabled = 0;
const char *g_debug_source_file = NULL;

static int g_client_fd = -1;

/* Receive buffer for line-based reads. */
#define RX_CAP 8192
static char g_rx[RX_CAP];
static size_t g_rx_len = 0;

/* Breakpoints. v1: single source file, simple sorted-ish list. */
#define MAX_BPS 256
static int g_bp_lines[MAX_BPS];
static int g_bp_count = 0;

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

/* Forward decls of interpreter internals we touch from here. */
extern Variable vars[];
extern int var_count;

/* ===== low-level IO ===== */

static int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
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
        ssize_t r = read(g_client_fd, g_rx + g_rx_len, RX_CAP - g_rx_len);
        if (r == 0) return -1;          /* EOF */
        if (r < 0) {
            if (errno == EINTR) continue;
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

/* Parse "lines":[1,2,3] into g_bp_lines/g_bp_count. */
static void parse_lines_array(const char *line) {
    g_bp_count = 0;
    const char *p = strstr(line, "\"lines\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p >= '0' && *p <= '9') {
            int v = atoi(p);
            if (g_bp_count < MAX_BPS) g_bp_lines[g_bp_count++] = v;
            while (*p >= '0' && *p <= '9') p++;
        } else if (*p) {
            p++;
        }
    }
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
typedef enum { REF_LIST, REF_OBJECT } RefKind;
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
    for (int i = 0; i < var_count && o + 512 < sizeof(buf); ++i) {
        Variable *v = &vars[i];
        if (!v->id) continue;
        if (v->id[0] == '_' && v->id[1] == '_') continue; /* skip __ret__ etc */
        char val[256] = "";
        const char *type = "unknown";
        int ref = render_variable_value(v, val, sizeof(val), &type);
        o = emit_var_entry(buf, sizeof(buf), o, first, v->id, type, val, ref);
        first = 0;
    }
    snprintf(buf + o, sizeof(buf) - o, "]}");
    send_line(buf);
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
        }
    }
    snprintf(buf + o, sizeof(buf) - o, "]}");
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
        } else if (strcmp(cmd, "pause") == 0) {
            /* Already paused; just ack. */
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "disconnect") == 0) {
            close(g_client_fd);
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

    /* Avoid SIGPIPE crashing the interpreter when adapter disconnects. */
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("[debugger] socket"); return; }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[debugger] bind"); close(listen_fd); return;
    }
    if (listen(listen_fd, 1) < 0) {
        perror("[debugger] listen"); close(listen_fd); return;
    }

    fprintf(stderr, "[typeeasy-debugger] Listening on port %d, waiting for adapter...\n", port);
    fflush(stderr);

    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int fd = accept(listen_fd, (struct sockaddr*)&cli, &cli_len);
    close(listen_fd);
    if (fd < 0) { perror("[debugger] accept"); return; }

    g_client_fd = fd;
    g_debug_enabled = 1;

    /* Push a synthetic top-level frame "<main>". */
    debugger_push_frame("<main>", NULL);

    fprintf(stderr, "[typeeasy-debugger] Adapter connected. Awaiting configuration...\n");
    fflush(stderr);

    /* Send 'initialized' so adapter sends breakpoints + start. */
    send_line("{\"event\":\"initialized\"}");

    /* Wait until adapter sends "start" — handle set_breakpoints meanwhile. */
    char line[2048];
    while (recv_line(line, sizeof(line)) == 0) {
        char cmd[64];
        if (json_str_field(line, "cmd", cmd, sizeof(cmd)) < 0) continue;
        if (strcmp(cmd, "set_breakpoints") == 0) {
            parse_lines_array(line);
            send_line("{\"resp\":\"ok\"}");
        } else if (strcmp(cmd, "start") == 0) {
            send_line("{\"resp\":\"ok\"}");
            g_step = RUN;
            return;
        } else if (strcmp(cmd, "disconnect") == 0) {
            close(g_client_fd);
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
    if (line <= 0) return;

    /* Update current line of innermost frame. */
    if (g_frame_top > 0) {
        g_frames[g_frame_top - 1].current_line = line;
    }

    /* De-duplicate: if executor calls the hook multiple times for the same
     * source line within the same frame depth, only stop once. */
    static int last_stop_line = -1;
    static int last_stop_depth = -1;

    int should_stop = 0;
    const char *reason = "step";

    /* If line changes vs last stop, re-arm so the same line can hit again
     * later (e.g. loop body). */
    if (line != last_stop_line) g_armed = 1;

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

    if (!should_stop) return;

    last_stop_line = line;
    last_stop_depth = g_frame_top;
    g_step = RUN;
    g_armed = 0;        /* require a different line before re-firing same BP */
    send_stopped(reason, line);
    wait_for_resume();
}

void debugger_terminate(int exit_code) {
    if (g_client_fd < 0) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"event\":\"terminated\",\"exit\":%d}", exit_code);
    send_line(buf);
    close(g_client_fd);
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

#endif /* !_WIN32 */
