/* typeeasy_api_server.c
 *
 * Embedded HTTP server for `typeeasy --api <file.te>`. Uses civetweb (the
 * same library already used by servidor_api/servidor_agent) so we don't
 * pull in a new dependency.
 *
 * Caller responsibilities (handled in typeeasy_main.c):
 *   1. parse_file() the script.
 *   2. interpret_ast() at global scope so classes/routes are registered.
 *   3. call typeeasy_run_api_server(host, port).
 *
 * Routing rules implemented here:
 *   - HTTP method match (GET/POST/PUT/DELETE/PATCH).
 *   - Exact path match first; falls back to `{param}` pattern matching
 *     (params get exposed to the .te body via typeeasy_http_add_param).
 *   - 404 on no match.
 *
 * Content-Type derived from `detect_response_type_embedded` of the method
 * body: "xml" -> application/xml, otherwise application/json.
 *
 * NOTE: keeps a simple global mutex around invoke; the interpreter holds a
 * lot of process-global state and is not thread-safe. */

/* Enable GNU extensions (REG_RIP in <ucontext.h>) so the crash handler can log
 * the exact faulting instruction pointer. Must precede any system header. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef _WIN32
  /* winsock2 must precede windows.h to avoid the legacy winsock.h clash. */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #define te_sleep_ms(ms) Sleep(ms)
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <sys/wait.h>
  #define te_sleep_ms(ms) usleep((ms) * 1000)
#endif

/* Runtime fatal-error recovery hook (defined in ast.c). When set to a valid
 * jmp_buf*, the interpreter longjmp's here instead of calling exit(1) on a
 * runtime error, letting the server answer HTTP 500 and stay alive. */
extern jmp_buf *g_runtime_recovery;
extern void runtime_reset_vars_to_initial_state(void);

/* Item 2.3: runtime error location captured by te_runtime_fatal[f]() in ast.c,
 * surfaced in the HTTP 500 body when dev mode is active. */
extern int  g_runtime_error_line;
extern char g_runtime_error_msg[256];
extern const char *g_debug_source_file;

/* Set by interpret_return_node when a handler returns a bare scalar/string
 * (e.g. `return "text"`) rather than json()/xml(); selects text/plain. */
extern int g_response_is_raw_text;

#include "civetweb.h"
#include "ast.h"
#include "typeeasy_api.h"
#include "typeeasy_http.h"
#include "typeeasy_api_server.h"
#include "../api_server/te_websocket.h"

/* Case-insensitive ASCII string compare. Local helper to avoid depending on
 * POSIX <strings.h> strcasecmp (not declared by default on Windows MinGW). */
static int te_strcasecmp(const char *a, const char *b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#ifdef _WIN32
static CRITICAL_SECTION g_invoke_lock;
static int g_invoke_lock_init = 0;
static void invoke_lock_init(void)   { if (!g_invoke_lock_init) { InitializeCriticalSection(&g_invoke_lock); g_invoke_lock_init = 1; } }
static void invoke_lock_acquire(void){ EnterCriticalSection(&g_invoke_lock); g_te_lock_held = 1; }
static void invoke_lock_release(void){ g_te_lock_held = 0; LeaveCriticalSection(&g_invoke_lock); }
#else
/* Fair FIFO "ticket" lock for the global invoke lock.
 *
 * glibc's default pthread_mutex is unfair: a thread that unlocks and then
 * immediately re-locks -- exactly what the cooperative await poll does every
 * ~1ms while a handler is parked -- almost always re-wins the lock before any
 * parked waiter is scheduled onto a CPU. The net effect is that concurrent
 * --api requests run strictly serially even though the lock is released on
 * every poll iteration. A ticket lock enforces FIFO handoff: when a yielding
 * handler re-acquires the lock it takes a *new* ticket and goes to the back of
 * the queue, so any other request waiting to run gets its turn first. This is
 * what makes cooperative request interleaving actually overlap on Linux.
 *
 * Single-request behaviour is byte-identical (acquire/release simply hand the
 * one ticket straight through). The lock is non-recursive, matching the prior
 * pthread_mutex contract. */
static pthread_mutex_t g_invoke_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_invoke_cv   = PTHREAD_COND_INITIALIZER;
static unsigned long   g_invoke_next = 0;   /* next ticket to hand out       */
static unsigned long   g_invoke_serv = 0;   /* ticket currently being served */
static void invoke_lock_init(void)   { /* static initialisers above */ }
static void invoke_lock_acquire(void){
    pthread_mutex_lock(&g_invoke_mtx);
    unsigned long my = g_invoke_next++;
    while (my != g_invoke_serv)
        pthread_cond_wait(&g_invoke_cv, &g_invoke_mtx);
    pthread_mutex_unlock(&g_invoke_mtx);
    g_te_lock_held = 1;
}
static void invoke_lock_release(void){
    g_te_lock_held = 0;
    pthread_mutex_lock(&g_invoke_mtx);
    g_invoke_serv++;
    pthread_cond_broadcast(&g_invoke_cv);
    pthread_mutex_unlock(&g_invoke_mtx);
}
#endif

static volatile sig_atomic_t g_stop_requested = 0;

/* ---- Dev hot-reload (item 2.2) -------------------------------------------
 * When `--dev` is active the caller registers the script path here. The serve
 * loop polls its mtime; on a stable change it sets g_reload_requested and
 * breaks out, so run_single_server tears down civetweb cleanly (closing the
 * listening socket) and returns TE_RC_RELOAD. typeeasy_main then re-exec's the
 * process, which re-parses the changed file from scratch. Re-exec avoids the
 * minefield of tearing down interpreter globals in-process. */
#define TE_RC_RELOAD 99
static volatile sig_atomic_t g_reload_requested = 0;
static const char *g_hotreload_path  = NULL;
static time_t      g_hotreload_mtime = 0;

static time_t te_file_mtime(const char *path) {
    struct stat st;
    if (path && stat(path, &st) == 0) return st.st_mtime;
    return 0;
}

void typeeasy_enable_hot_reload(const char *script_path) {
    g_hotreload_path  = script_path;
    g_hotreload_mtime = te_file_mtime(script_path);
}

/* ---- Observability / operational state (items #9-12) ---------------------
 * g_ready      : readiness gate. 0 until routes are registered and the
 *                interpreter is loaded; flipped back to 0 on shutdown so
 *                /readyz starts failing while in-flight requests drain.
 * g_inflight   : number of requests currently executing (atomic).
 * g_req_total  : total requests handled since start (atomic, monotonic).
 * g_start_time : process start, for uptime reporting. */
static volatile sig_atomic_t g_ready      = 0;
static volatile long         g_inflight   = 0;
static volatile long         g_req_total  = 0;
static time_t                g_start_time = 0;

static void on_signal(int sig) {
    (void)sig;
    g_ready = 0;            /* stop advertising readiness immediately */
    g_stop_requested = 1;
}

/* JSON-escape `src` into `dst` (size `dstsz`, always NUL-terminated). Used by
 * the structured logger so messages/URIs can't break the JSON line. */
static void te_json_escape(char *dst, size_t dstsz, const char *src) {
    size_t di = 0;
    if (dstsz == 0) return;
    for (; src && *src && di + 2 < dstsz; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') { dst[di++] = '\\'; if (di + 1 < dstsz) dst[di++] = (char)c; }
        else if (c == '\n')        { dst[di++] = '\\'; if (di + 1 < dstsz) dst[di++] = 'n'; }
        else if (c == '\r')        { dst[di++] = '\\'; if (di + 1 < dstsz) dst[di++] = 'r'; }
        else if (c == '\t')        { dst[di++] = '\\'; if (di + 1 < dstsz) dst[di++] = 't'; }
        else if (c < 0x20)         { /* drop other control chars */ }
        else                       { dst[di++] = (char)c; }
    }
    dst[di] = '\0';
}

/* Fill `buf` with the current UTC time in ISO-8601 (e.g. 2026-06-01T12:34:56Z). */
static void te_iso_time(char *buf, size_t bufsz) {
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

/* Structured (JSON-per-line) log to stderr. level is "info"/"warn"/"error". */
static void te_log(const char *level, const char *fmt, ...) {
    char ts[32];   te_iso_time(ts, sizeof(ts));
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    char esc[1024]; te_json_escape(esc, sizeof(esc), msg);
    fprintf(stderr, "{\"ts\":\"%s\",\"level\":\"%s\",\"pid\":%ld,\"msg\":\"%s\"}\n",
            ts, level, (long)getpid(), esc);
    fflush(stderr);
}

/* Structured access-log line for one HTTP request. */
static void te_log_request(const char *method, const char *uri, int status,
                           double dur_ms) {
    char ts[32]; te_iso_time(ts, sizeof(ts));
    char em[256]; te_json_escape(em, sizeof(em), method ? method : "");
    char eu[1024]; te_json_escape(eu, sizeof(eu), uri ? uri : "");
    fprintf(stderr,
            "{\"ts\":\"%s\",\"level\":\"info\",\"pid\":%ld,\"event\":\"request\","
            "\"method\":\"%s\",\"uri\":\"%s\",\"status\":%d,\"dur_ms\":%.2f}\n",
            ts, (long)getpid(), em, eu, status, dur_ms);
    fflush(stderr);
}

/* CORS: value(s) allowed in Access-Control-Allow-Origin. Default "*" (any
 * origin). Configurable via the --cors-origin <url> CLI flag / the
 * TYPEEASY_CORS_ORIGIN env var (see typeeasy_main.c). May hold:
 *   - "*"                              -> any origin
 *   - a single origin                  -> that origin only
 *   - a comma-separated list of origins -> the request Origin is reflected
 *                                          if it matches one of the entries. */
static char g_cors_origin[1024] = "*";

void typeeasy_set_cors_origin(const char *origin) {
    if (origin && *origin) {
        strncpy(g_cors_origin, origin, sizeof(g_cors_origin) - 1);
        g_cors_origin[sizeof(g_cors_origin) - 1] = '\0';
    }
}

/* Resolve the Access-Control-Allow-Origin value to send for this request.
 * Handles the single-value cases directly and, when g_cors_origin is a
 * comma-separated list, reflects the request's Origin header if it matches
 * one of the configured entries (otherwise falls back to the first entry,
 * which the browser will reject for a disallowed origin). The result is
 * always NUL-terminated. */
static void cors_resolve_origin(struct mg_connection *conn,
                                char *out, size_t outsz) {
    if (outsz == 0) return;
    /* "*" or a single origin: emit verbatim. */
    if (!strchr(g_cors_origin, ',')) {
        snprintf(out, outsz, "%s", g_cors_origin);
        return;
    }
    /* List: match the request Origin against the configured entries. */
    const char *req_origin = conn ? mg_get_header(conn, "Origin") : NULL;
    size_t req_len = req_origin ? strlen(req_origin) : 0;
    const char *first = NULL; size_t first_len = 0;
    const char *p = g_cors_origin;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ',') p++;
        const char *end = p;
        while (end > start && end[-1] == ' ') end--;
        size_t len = (size_t)(end - start);
        if (len == 0) continue;
        if (!first) { first = start; first_len = len; }
        if (req_origin && req_len == len && strncmp(req_origin, start, len) == 0) {
            size_t n = len < outsz - 1 ? len : outsz - 1;
            memcpy(out, start, n); out[n] = '\0';
            return;
        }
    }
    if (first) {
        size_t n = first_len < outsz - 1 ? first_len : outsz - 1;
        memcpy(out, first, n); out[n] = '\0';
    } else {
        snprintf(out, outsz, "*");
    }
}

/* Constant CORS headers sent alongside Access-Control-Allow-Origin. */
#define TE_CORS_EXTRA_HEADERS \
    "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"

/* Match `/users/{id}` against `/users/42`, populating params. */
static int match_route_pattern(const char *pattern, const char *uri) {
    const char *pp = pattern, *up = uri;
    while (*pp && *up) {
        if (*pp == '{') {
            const char *name_start = pp + 1;
            const char *name_end = strchr(name_start, '}');
            if (!name_end) return 0;
            const char *val_start = up;
            while (*up && *up != '/') up++;
            int nlen = (int)(name_end - name_start);
            int vlen = (int)(up - val_start);
            char name[64], val[256];
            if (nlen <= 0 || nlen >= (int)sizeof(name) || vlen >= (int)sizeof(val)) return 0;
            memcpy(name, name_start, nlen); name[nlen] = '\0';
            memcpy(val,  val_start,  vlen); val[vlen]   = '\0';
            typeeasy_http_add_param(name, val);
            pp = name_end + 1;
        } else if (*pp == *up) {
            pp++; up++;
        } else {
            return 0;
        }
    }
    return (*pp == '\0' && *up == '\0');
}

static MethodNode *find_route(const char *uri, const char *method) {
    /* Pass 1: exact match. */
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (!m->route_path) continue;
        const char *mh = m->http_method ? m->http_method : "GET";
        if (strcmp(mh, method) != 0) continue;
        if (strchr(m->route_path, '{') == NULL && strcmp(m->route_path, uri) == 0) {
            return m;
        }
    }
    /* Pass 2: pattern match. */
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (!m->route_path) continue;
        const char *mh = m->http_method ? m->http_method : "GET";
        if (strcmp(mh, method) != 0) continue;
        if (strchr(m->route_path, '{') == NULL) continue;
        typeeasy_http_reset();
        typeeasy_http_set_method(method);
        typeeasy_http_set_path(uri);
        if (match_route_pattern(m->route_path, uri)) return m;
    }
    return NULL;
}

/* Serve a built-in Swagger-style UI at GET / that lists every registered
 * route and lets the user invoke it directly from the browser. Kept compact
 * but interactive (method filter, "try it" with fetch, response viewer). */
static int serve_root_swagger_ui(struct mg_connection *conn) {
    size_t cap = 65536;
    char *html = (char *)malloc(cap);
    if (!html) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        return 1;
    }
    int off = 0;
#define ENSURE_CAP(extra) do { \
        if ((size_t)off + (size_t)(extra) + 1 >= cap) { \
            size_t nc = cap; \
            while (nc < (size_t)off + (size_t)(extra) + 1) nc *= 2; \
            char *nb = (char *)realloc(html, nc); \
            if (!nb) { free(html); mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"); return 1; } \
            html = nb; cap = nc; \
        } \
    } while (0)

    int route_count = 0;
    for (MethodNode *m = global_methods; m; m = m->next) if (m->route_path) route_count++;

    ENSURE_CAP(8192);
    off += snprintf(html + off, cap - off,
        "<!DOCTYPE html>"
        "<html lang='en'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TypeEasy API</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;margin:0;background:#fafafa;color:#3b4151}"
        "header{background:linear-gradient(135deg,#1b1b1b,#2c3e50);color:#fff;padding:22px 36px;box-shadow:0 2px 8px rgba(0,0,0,.15)}"
        "header h1{margin:0;font-size:26px;font-weight:600;display:flex;align-items:center;gap:12px}"
        ".embedded-badge{background:#673ab7;padding:3px 10px;border-radius:12px;font-size:10px;font-weight:700;letter-spacing:.5px;text-transform:uppercase}"
        ".subtitle{margin-top:4px;font-size:13px;color:#bfc9d4}"
        "main{max-width:1100px;margin:22px auto;padding:0 22px}"
        ".toolbar{background:#fff;padding:12px 16px;border:1px solid #e3e3e3;border-radius:6px;margin-bottom:16px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
        ".toolbar input{flex:1;min-width:240px;padding:8px 11px;border:1px solid #ccc;border-radius:4px;font-size:14px}"
        ".toolbar input:focus{outline:none;border-color:#49cc90;box-shadow:0 0 0 2px rgba(73,204,144,.18)}"
        ".pill{background:#eee;color:#444;padding:5px 10px;border-radius:13px;font-size:12px;cursor:pointer;font-weight:600;letter-spacing:.3px;user-select:none}"
        ".pill.active{background:#3b4151;color:#fff}"
        ".pill[data-m=get].active{background:#61affe}"
        ".pill[data-m=post].active{background:#49cc90}"
        ".pill[data-m=put].active{background:#fca130}"
        ".pill[data-m=delete].active{background:#f93e3e}"
        ".pill[data-m=patch].active{background:#50e3c2}"
        ".endpoint{background:#fff;border:1px solid #e3e3e3;border-radius:6px;margin-bottom:8px;overflow:hidden}"
        ".ep-head{display:flex;align-items:center;gap:10px;padding:10px 14px;cursor:pointer;user-select:none}"
        ".ep-head:hover{background:#f7f7f7}"
        ".method{display:inline-block;min-width:60px;text-align:center;padding:4px 8px;border-radius:3px;font-weight:700;color:#fff;font-size:12px;font-family:Consolas,monospace}"
        ".method.get{background:#61affe}.method.post{background:#49cc90}.method.put{background:#fca130}.method.delete{background:#f93e3e}.method.patch{background:#50e3c2}"
        ".route{font-family:Consolas,monospace;font-size:14px;color:#3b4151;font-weight:600;flex:1;word-break:break-all}"
        ".route .param{color:#9012fe;font-weight:700}"
        ".func{color:#7a7a7a;font-size:12px;font-family:Consolas,monospace}"
        ".caret{color:#999;font-size:13px;width:14px;text-align:center;transition:transform .15s}"
        ".endpoint.open .caret{transform:rotate(90deg)}"
        ".endpoint.open .ep-body{display:block}"
        ".ep-body{display:none;padding:14px 16px;background:#fafafa;border-top:1px solid #f0f0f0}"
        ".ep-body label{display:block;font-size:11px;font-weight:600;color:#555;margin:8px 0 4px;text-transform:uppercase;letter-spacing:.3px}"
        ".ep-body input,.ep-body textarea{width:100%%;padding:7px 10px;border:1px solid #ccc;border-radius:3px;font-family:Consolas,monospace;font-size:13px}"
        ".ep-body textarea{resize:vertical;min-height:80px}"
        ".params-grid{display:grid;grid-template-columns:160px 1fr;gap:6px 10px;align-items:center;margin-bottom:6px}"
        ".params-grid label{margin:0;text-transform:none;letter-spacing:0;font-family:Consolas,monospace;color:#9012fe;font-size:13px}"
        ".ep-actions{margin-top:12px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
        ".btn{border:0;padding:8px 16px;border-radius:4px;font-size:13px;font-weight:600;cursor:pointer;background:#49cc90;color:#fff}"
        ".btn:hover{background:#3eb37e}"
        ".status-pill{padding:3px 9px;border-radius:10px;font-size:11px;font-weight:700;font-family:Consolas,monospace;display:none}"
        ".status-pill.ok{background:#d4edda;color:#155724}"
        ".status-pill.bad{background:#f8d7da;color:#721c24}"
        ".response{background:#1e1e1e;color:#dcdcdc;padding:12px;border-radius:4px;margin-top:10px;font-family:Consolas,monospace;font-size:12px;line-height:1.5;white-space:pre-wrap;display:none;overflow-x:auto;max-height:420px;overflow-y:auto}"
        ".empty{padding:24px;text-align:center;color:#888;background:#fff;border:1px dashed #ccc;border-radius:6px}"
        ".hidden{display:none !important}"
        "footer{text-align:center;padding:18px;color:#999;font-size:12px}"
        "</style></head><body>"
        "<header><h1>TypeEasy API <span class='embedded-badge'>Embedded</span></h1>"
        "<div class='subtitle'>%d endpoint%s registered</div></header>"
        "<main>"
        "<div class='toolbar'>"
        "<input type='text' id='filter' placeholder='Filter by route or function' oninput='applyFilter()' autocomplete='off'>"
        "<span class='pill active' data-m='all' onclick='togglePill(this)'>ALL</span>"
        "<span class='pill' data-m='get' onclick='togglePill(this)'>GET</span>"
        "<span class='pill' data-m='post' onclick='togglePill(this)'>POST</span>"
        "<span class='pill' data-m='put' onclick='togglePill(this)'>PUT</span>"
        "<span class='pill' data-m='delete' onclick='togglePill(this)'>DELETE</span>"
        "<span class='pill' data-m='patch' onclick='togglePill(this)'>PATCH</span>"
        "</div>",
        route_count, route_count == 1 ? "" : "s", route_count == 1 ? "" : "s");

    if (route_count == 0) {
        ENSURE_CAP(512);
        off += snprintf(html + off, cap - off,
            "<div class='empty'>No routes registered. Define endpoints with <code>[HttpGet(\"/path\")]</code> in your .te files.</div>");
    } else {
        int eid = 0;
        for (MethodNode *m = global_methods; m; m = m->next) {
            if (!m->route_path) continue;
            const char *http_m = m->http_method ? m->http_method : "GET";
            char lower_m[16] = {0};
            for (int i = 0; http_m[i] && i < 15; i++) lower_m[i] = (char)tolower((unsigned char)http_m[i]);
            const char *fname = m->name ? m->name : "?";
            ENSURE_CAP(4096 + (int)strlen(m->route_path) + (int)strlen(fname));

            /* Render path with {param} highlighted. */
            char path_html[1024]; int phl = 0;
            const char *p = m->route_path;
            while (*p && phl < (int)sizeof(path_html) - 64) {
                if (*p == '{') {
                    const char *e = strchr(p, '}');
                    if (!e) break;
                    phl += snprintf(path_html + phl, sizeof(path_html) - phl,
                                    "<span class='param'>{%.*s}</span>", (int)(e - p - 1), p + 1);
                    p = e + 1;
                } else {
                    path_html[phl++] = *p++;
                }
            }
            path_html[phl] = '\0';

            off += snprintf(html + off, cap - off,
                "<div class='endpoint' data-m='%s' data-route='%s' data-func='%s' id='ep-%d'>"
                "<div class='ep-head' onclick='toggleEp(%d)'>"
                "<span class='method %s'>%s</span>"
                "<span class='route'>%s</span>"
                "<span class='func'>%s()</span>"
                "<span class='caret'>&#9656;</span></div>"
                "<div class='ep-body'>",
                lower_m, m->route_path, fname, eid, eid, lower_m, http_m, path_html, fname);

            /* Collect path params for input form. */
            const char *pp = m->route_path;
            int has_params = 0;
            while ((pp = strchr(pp, '{'))) {
                const char *e = strchr(pp, '}');
                if (!e) break;
                if (!has_params) {
                    ENSURE_CAP(256);
                    off += snprintf(html + off, cap - off, "<label>Path params</label><div class='params-grid'>");
                    has_params = 1;
                }
                ENSURE_CAP(256);
                off += snprintf(html + off, cap - off,
                    "<label>%.*s</label><input type='text' data-param='%.*s' placeholder='%.*s' oninput='updatePreview(%d)'>",
                    (int)(e - pp - 1), pp + 1, (int)(e - pp - 1), pp + 1, (int)(e - pp - 1), pp + 1, eid);
                pp = e + 1;
            }
            if (has_params) { ENSURE_CAP(64); off += snprintf(html + off, cap - off, "</div>"); }

            int needs_body = (strcmp(http_m, "POST") == 0 || strcmp(http_m, "PUT") == 0 || strcmp(http_m, "PATCH") == 0);
            if (needs_body) {
                ENSURE_CAP(256);
                off += snprintf(html + off, cap - off,
                    "<label>Body (JSON)</label><textarea data-body placeholder='{\"key\":\"value\"}'></textarea>");
            } else {
                ENSURE_CAP(256);
                off += snprintf(html + off, cap - off,
                    "<label>Query string (optional)</label><input type='text' data-query placeholder='key=value&amp;k2=v2'>");
            }

            ENSURE_CAP(512);
            off += snprintf(html + off, cap - off,
                "<div class='ep-actions'>"
                "<button class='btn' onclick='tryIt(%d)'>Try it</button>"
                "<span class='status-pill' id='st-%d'></span>"
                "</div>"
                "<div class='response' id='res-%d'></div>"
                "</div></div>",
                eid, eid, eid);
            eid++;
        }
    }

    ENSURE_CAP(4096);
    off += snprintf(html + off, cap - off,
        "</main><footer>TypeEasy embedded server</footer>"
        "<script>"
        "function togglePill(el){document.querySelectorAll('.pill').forEach(p=>p.classList.remove('active'));el.classList.add('active');applyFilter();}"
        "function applyFilter(){"
        "var q=(document.getElementById('filter').value||'').toLowerCase();"
        "var active=document.querySelector('.pill.active');"
        "var m=active?active.dataset.m:'all';"
        "document.querySelectorAll('.endpoint').forEach(function(ep){"
        "var hit=true;"
        "if(m!=='all'&&ep.dataset.m!==m)hit=false;"
        "if(q&&!(ep.dataset.route.toLowerCase().indexOf(q)>=0||ep.dataset.func.toLowerCase().indexOf(q)>=0||ep.dataset.m.indexOf(q)>=0))hit=false;"
        "ep.classList.toggle('hidden',!hit);"
        "});}"
        "function toggleEp(i){document.getElementById('ep-'+i).classList.toggle('open');}"
        "function buildUrl(ep){"
        "var route=ep.dataset.route;"
        "ep.querySelectorAll('input[data-param]').forEach(function(inp){"
        "route=route.replace('{'+inp.dataset.param+'}',encodeURIComponent(inp.value||('{'+inp.dataset.param+'}')));"
        "});"
        "var q=ep.querySelector('input[data-query]');"
        "if(q&&q.value)route+='?'+q.value;"
        "return route;}"
        "function tryIt(i){"
        "var ep=document.getElementById('ep-'+i);"
        "var url=buildUrl(ep);"
        "var method=ep.dataset.m.toUpperCase();"
        "var opt={method:method,headers:{}};"
        "var bodyEl=ep.querySelector('textarea[data-body]');"
        "if(bodyEl&&bodyEl.value){opt.headers['Content-Type']='application/json';opt.body=bodyEl.value;}"
        "var resEl=document.getElementById('res-'+i);"
        "var stEl=document.getElementById('st-'+i);"
        "resEl.style.display='block';resEl.textContent='Loading...';"
        "stEl.style.display='none';"
        "var t0=performance.now();"
        "fetch(url,opt).then(function(r){"
        "var ct=r.headers.get('content-type')||'';"
        "return r.text().then(function(txt){return {status:r.status,ct:ct,txt:txt};});"
        "}).then(function(d){"
        "var ms=Math.round(performance.now()-t0);"
        "stEl.textContent=d.status+' ('+ms+' ms)';"
        "stEl.className='status-pill '+(d.status>=200&&d.status<300?'ok':'bad');"
        "stEl.style.display='inline-block';"
        "var pretty=d.txt;"
        "if(d.ct.indexOf('json')>=0){try{pretty=JSON.stringify(JSON.parse(d.txt),null,2);}catch(e){}}"
        "resEl.textContent=pretty;"
        "}).catch(function(e){resEl.textContent='Error: '+e.message;stEl.textContent='ERROR';stEl.className='status-pill bad';stEl.style.display='inline-block';});}"
        "function updatePreview(i){}"
        "</script></body></html>");

    char cors_org[1024];
    cors_resolve_origin(conn, cors_org, sizeof(cors_org));
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: %s\r\n"
        TE_CORS_EXTRA_HEADERS
        "Connection: close\r\n\r\n",
        off, cors_org);
    mg_write(conn, html, off);
    free(html);
#undef ENSURE_CAP
    return 1;
}

static int request_handler(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *req = mg_get_request_info(conn);
    const char *uri    = req->local_uri ? req->local_uri : "/";
    const char *method = req->request_method ? req->request_method : "GET";
    const char *qs     = req->query_string;

    /* CORS preflight: answer OPTIONS immediately with the allow headers so
     * browsers permit cross-origin requests carrying Authorization /
     * Content-Type: application/json (e.g. the JWT auth flow). */
    if (strcmp(method, "OPTIONS") == 0) {
        char cors_org[1024];
        cors_resolve_origin(conn, cors_org, sizeof(cors_org));
        mg_printf(conn,
                  "HTTP/1.1 204 No Content\r\n"
                  "Access-Control-Allow-Origin: %s\r\n"
                  TE_CORS_EXTRA_HEADERS
                  "Access-Control-Max-Age: 86400\r\n"
                  "Content-Length: 0\r\n"
                  "Connection: close\r\n\r\n",
                  cors_org);
        return 1;
    }

    /* Serve the built-in Swagger-style UI at GET /. */
    if (strcmp(method, "GET") == 0 && strcmp(uri, "/") == 0) {
        return serve_root_swagger_ui(conn);
    }

    /* ---- Operational probes (items #9-12) --------------------------------
     * /healthz (liveness): the process is up and able to answer. Cheap, never
     *                      touches the interpreter lock.
     * /readyz  (readiness): the process is ready to serve traffic, i.e. routes
     *                      are registered and we are not draining for shutdown.
     * Both are GET-only, return JSON and are explicitly non-cacheable. */
    if (strcmp(method, "GET") == 0 &&
        (strcmp(uri, "/healthz") == 0 || strcmp(uri, "/readyz") == 0)) {
        long uptime = (g_start_time > 0) ? (long)(time(NULL) - g_start_time) : 0;
        char body[256];
        int code; const char *reason;
        if (strcmp(uri, "/healthz") == 0) {
            code = 200; reason = "OK";
            snprintf(body, sizeof(body),
                     "{\"status\":\"ok\",\"uptime_s\":%ld}", uptime);
        } else {
            int routes_ok = (global_methods != NULL);
            /* The embedded server only reaches this point after the caller has
             * parsed and interpreted the script at global scope, so by
             * construction the interpreter is initialized whenever we serve. */
            int interp_ok = 1;
            int ready = g_ready && routes_ok && interp_ok;
            code = ready ? 200 : 503;
            reason = ready ? "OK" : "Service Unavailable";
            snprintf(body, sizeof(body),
                     "{\"status\":\"%s\",\"ready\":%s,\"interpreter\":%s,\"routes\":%s,"
                     "\"inflight\":%ld,\"req_total\":%ld,\"uptime_s\":%ld}",
                     ready ? "ready" : "not_ready",
                     ready ? "true" : "false",
                     interp_ok ? "true" : "false",
                     routes_ok ? "true" : "false",
                     g_inflight, g_req_total, uptime);
        }
        int blen = (int)strlen(body);
        mg_printf(conn,
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n"
                  "Content-Length: %d\r\n"
                  "Connection: close\r\n\r\n",
                  code, reason, blen);
        mg_write(conn, body, blen);
        return 1;
    }

    /* Request instrumentation: count and time every routed request. */
    clock_t _req_t0 = clock();
    __atomic_add_fetch(&g_req_total, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&g_inflight, 1, __ATOMIC_SEQ_CST);
#define TE_REQ_DONE(st) do { \
        double _ms = (double)(clock() - _req_t0) * 1000.0 / CLOCKS_PER_SEC; \
        __atomic_sub_fetch(&g_inflight, 1, __ATOMIC_SEQ_CST); \
        te_log_request(method, uri, (st), _ms); \
    } while (0)

    invoke_lock_acquire();

    typeeasy_http_reset();
    typeeasy_http_set_method(method);
    typeeasy_http_set_path(uri);

    MethodNode *m = find_route(uri, method);
    if (!m) {
        invoke_lock_release();
        TE_REQ_DONE(404);
        mg_send_http_error(conn, 404, "Endpoint not found: %s %s", method, uri);
        return 1;
    }

    /* Query string -> http params. */
    if (qs && *qs) {
        const char *p = qs;
        while (*p) {
            const char *eq = strchr(p, '=');
            const char *amp = strchr(p, '&');
            if (!amp) amp = p + strlen(p);
            char key[128], val[1024];
            if (eq && eq < amp) {
                int kl = (int)(eq - p);
                int vl = (int)(amp - (eq + 1));
                if (kl > 0 && kl < (int)sizeof(key) && vl < (int)sizeof(val)) {
                    memcpy(key, p, kl); key[kl] = '\0';
                    memcpy(val, eq + 1, vl); val[vl] = '\0';
                    typeeasy_http_add_query(key, val);
                }
            }
            if (*amp == '\0') break;
            p = amp + 1;
        }
    }

    /* Headers. */
    for (int i = 0; i < req->num_headers; i++) {
        typeeasy_http_add_header(req->http_headers[i].name,
                                 req->http_headers[i].value);
    }

    /* Body. Reject oversized payloads explicitly with HTTP 413 instead of
     * silently ignoring them (item #7: limits/backpressure). The previous
     * code dropped any body >= 1 MiB on the floor and ran the handler with an
     * empty body — confusing and unbounded-looking to clients. The cap is now
     * configurable via TYPEEASY_MAX_BODY (bytes, default 1 MiB). */
    static long long s_max_body = -1;
    if (s_max_body < 0) {
        const char *e = getenv("TYPEEASY_MAX_BODY");
        long long v = e ? strtoll(e, NULL, 10) : 0;
        s_max_body = (v > 0) ? v : (1 << 20);
    }
    long long clen = req->content_length;
    if (clen > s_max_body) {
        invoke_lock_release();
        TE_REQ_DONE(413);
        char cors_org[1024];
        cors_resolve_origin(conn, cors_org, sizeof(cors_org));
        const char *body413 = "{\"error\":\"payload_too_large\"}";
        mg_printf(conn,
                  "HTTP/1.1 413 Payload Too Large\r\n"
                  "Access-Control-Allow-Origin: %s\r\n"
                  TE_CORS_EXTRA_HEADERS
                  "Content-Type: application/json\r\n"
                  "Connection: close\r\n"
                  "Content-Length: %d\r\n\r\n"
                  "%s",
                  cors_org, (int)strlen(body413), body413);
        return 1;
    }
    if (clen > 0) {
        char *body = (char *)malloc((size_t)clen + 1);
        if (body) {
            int n = mg_read(conn, body, (size_t)clen);
            if (n < 0) n = 0;
            body[n] = '\0';
            /* Binary-safe: uploads (xlsx/zip) contain NUL bytes. */
            typeeasy_http_set_body_n(body, (size_t)n);
            free(body);
        }
    }

    /* Install a runtime recovery point: if the interpreter hits a fatal
     * runtime error (undefined function, const reassignment, etc) it
     * longjmp's back here instead of killing the whole server. We answer
     * HTTP 500, reset interpreter state and keep serving. */
    jmp_buf recovery;
    if (setjmp(recovery) != 0) {
        g_runtime_recovery = NULL;
        runtime_reset_vars_to_initial_state();
        /* v0.0.30 (estabilidad): el longjmp se salto los returns de
         * typeeasy_embedded_invoke_method, asi que liberamos aqui el registro
         * request-owned (ObjectNodes de model-bind + arboles json_parse) y
         * rebalanceamos g_te_request_active. Sin esto, cada 500 acumulaba
         * memoria hasta el proximo request exitoso. */
        extern void te_req_abort_cleanup(void);
        te_req_abort_cleanup();

        /* Item 2.3: in dev mode expose the runtime error location (file:line)
         * in the 500 body to aid debugging. NEVER in production: default is the
         * opaque {"error":"internal_error"}. Dev mode is opt-in via TYPEEASY_DEV. */
        static int s_dev_mode = -1;
        if (s_dev_mode < 0) s_dev_mode = getenv("TYPEEASY_DEV") ? 1 : 0;

        char devbuf[640];
        const char *err;
        if (s_dev_mode) {
            /* JSON-escape the captured message (may contain user identifiers). */
            char esc[256]; size_t ei = 0;
            const char *src = g_runtime_error_msg;
            for (; *src && ei + 2 < sizeof(esc); src++) {
                unsigned char c = (unsigned char)*src;
                if (c == '"' || c == '\\') { esc[ei++] = '\\'; esc[ei++] = (char)c; }
                else if (c == '\n') { esc[ei++] = '\\'; esc[ei++] = 'n'; }
                else if (c == '\t') { esc[ei++] = '\\'; esc[ei++] = 't'; }
                else if (c < 0x20) { /* skip other control chars */ }
                else esc[ei++] = (char)c;
            }
            esc[ei] = '\0';
            const char *file = g_debug_source_file ? g_debug_source_file : "";
            snprintf(devbuf, sizeof(devbuf),
                     "{\"error\":\"internal_error\",\"message\":\"%s\",\"file\":\"%s\",\"line\":%d}",
                     esc, file, g_runtime_error_line);
            err = devbuf;
        } else {
            err = "{\"error\":\"internal_error\"}";
        }
        int elen = (int)strlen(err);
        char cors_org[1024];
        cors_resolve_origin(conn, cors_org, sizeof(cors_org));
        invoke_lock_release();
        TE_REQ_DONE(500);
        mg_printf(conn,
                  "HTTP/1.1 500 Internal Server Error\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n"
                  "Access-Control-Allow-Origin: %s\r\n"
                  TE_CORS_EXTRA_HEADERS
                  "\r\n",
                  elen, cors_org);
        mg_write(conn, err, elen);
        return 1;
    }
    g_runtime_recovery = &recovery;

    char *result = typeeasy_embedded_invoke_method(m);

    g_runtime_recovery = NULL;

    /* Binary download channel: if the handler staged raw file bytes (xlsx/pdf
     * via xlsx_download()/pdf_download()/response_file()), copy them into a
     * local buffer BEFORE releasing the interpreter lock — the globals holding
     * them are reset on the next request. When present, these bytes become the
     * response body instead of the textual `result`. */
    size_t bin_len = 0;
    const char *bin_src = typeeasy_http_get_response_bytes(&bin_len);
    char *bin_body = NULL; char *bin_ct = NULL;
    if (bin_src) {
        bin_body = (char *)malloc(bin_len ? bin_len : 1);
        if (bin_body && bin_len) memcpy(bin_body, bin_src, bin_len);
        const char *ct = typeeasy_http_get_response_content_type();
        bin_ct = strdup(ct ? ct : "application/octet-stream");
    }

    const char *ctype;
    if (g_response_is_raw_text) {
        ctype = "text/plain; charset=utf-8";
    } else {
        ctype = (strcmp(detect_response_type_embedded(m->body), "xml") == 0)
                ? "application/xml" : "application/json";
    }

    if (!result) result = strdup("");
    int len = (int)strlen(result);

    /* Binary body overrides the textual one (and its content-type). */
    const char *body_ptr = result;
    int body_len = len;
    if (bin_body) { ctype = bin_ct; body_ptr = bin_body; body_len = (int)bin_len; }

    /* Honor the response status set by the interpreter (response_status())
     * or by automatic typed-body validation (HTTP 422). */
    int status = typeeasy_http_get_status();
    if (status <= 0) status = 200;
    const char *reason = "OK";
    switch (status) {
        case 200: reason = "OK"; break;
        case 201: reason = "Created"; break;
        case 204: reason = "No Content"; break;
        case 400: reason = "Bad Request"; break;
        case 401: reason = "Unauthorized"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 422: reason = "Unprocessable Entity"; break;
        case 500: reason = "Internal Server Error"; break;
        default:  reason = "OK"; break;
    }

    /* Capture any custom response headers set by the handler via
     * response_header(k, v) (e.g. Set-Cookie). These live in interpreter-global
     * state (g_resp_headers) which is reset on the next request, so we must
     * serialize them into a local heap buffer BEFORE releasing the lock. We
     * skip Content-Type and Content-Length (they are computed above) to avoid
     * duplicate/conflicting headers; everything else (including multiple
     * Set-Cookie lines) is emitted verbatim. */
    char *extra_headers = NULL;
    {
        size_t cap = 0, used = 0;
        const char *hk, *hv;
        for (int hi = 0; typeeasy_http_iter_response_header(hi, &hk, &hv); hi++) {
            if (!hk) continue;
            if (te_strcasecmp(hk, "Content-Type") == 0 ||
                te_strcasecmp(hk, "Content-Length") == 0) continue;
            size_t need = strlen(hk) + 2 /* ": " */ + (hv ? strlen(hv) : 0) + 2 /* CRLF */;
            if (used + need + 1 > cap) {
                size_t newcap = (cap ? cap * 2 : 256);
                while (newcap < used + need + 1) newcap *= 2;
                char *grown = (char *)realloc(extra_headers, newcap);
                if (!grown) break; /* out of memory: emit what we have */
                extra_headers = grown;
                cap = newcap;
            }
            used += (size_t)snprintf(extra_headers + used, cap - used,
                                     "%s: %s\r\n", hk, hv ? hv : "");
        }
        if (extra_headers) extra_headers[used] = '\0';
    }

    /* The interpreter is done and `result`/`status`/`ctype`/`extra_headers`
     * are now local copies (ctype is a string literal). Release the global
     * interpreter lock BEFORE the network write so slow clients / large bodies
     * don't keep the single-threaded interpreter blocked. This lets request
     * I/O overlap. */
    char cors_org[1024];
    cors_resolve_origin(conn, cors_org, sizeof(cors_org));
    invoke_lock_release();

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: %s\r\n"
              TE_CORS_EXTRA_HEADERS
              "%s"
              "\r\n",
              status, reason, ctype, body_len, cors_org,
              extra_headers ? extra_headers : "");
    if (body_len > 0) mg_write(conn, body_ptr, body_len);

    free(extra_headers);
    free(result);
    free(bin_body);
    free(bin_ct);
    TE_REQ_DONE(status);
    return 1;
}
#undef TE_REQ_DONE

/* ---- Fix B: crash signal handler (async-signal-safe) ----------------------
 * Guarantees a diagnostic LOG LINE on every hard fault (SIGSEGV/SIGBUS/...), so
 * a worker crash is never a fully silent socket reset: operators always get a
 * journald line with the signal + faulting address + pid. We do NOT resume (the
 * heap may be corrupt); after logging we restore the default disposition and
 * re-raise so a core dump is still produced and systemd restarts the unit.
 * Uses only write(2) and stack buffers — async-signal-safe. */
#ifndef _WIN32
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>   /* backtrace(), backtrace_symbols_fd() — glibc/macOS only */
#endif
#include <ucontext.h>   /* ucontext_t / REG_RIP for the exact faulting PC */
static void te_ssafe_write(const char *s) {
    size_t n = 0; while (s[n]) n++;
    ssize_t rc = write(2, s, n); (void)rc;
}
static void te_ssafe_write_hex(unsigned long v) {
    char buf[2 + sizeof(unsigned long) * 2];
    char *p = buf; *p++ = '0'; *p++ = 'x';
    int started = 0;
    for (int shift = (int)(sizeof(unsigned long) * 8) - 4; shift >= 0; shift -= 4) {
        unsigned d = (unsigned)((v >> shift) & 0xF);
        if (d || started || shift == 0) { *p++ = "0123456789abcdef"[d]; started = 1; }
    }
    ssize_t rc = write(2, buf, (size_t)(p - buf)); (void)rc;
}
static void te_ssafe_write_dec(long v) {
    char buf[24]; char *p = buf + sizeof(buf);
    int neg = v < 0; unsigned long uv = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (uv == 0) *--p = '0';
    while (uv) { *--p = (char)('0' + (uv % 10)); uv /= 10; }
    if (neg) *--p = '-';
    ssize_t rc = write(2, p, (size_t)(buf + sizeof(buf) - p)); (void)rc;
}
static void te_crash_handler(int sig, siginfo_t *info, void *uctx) {
    (void)uctx;
    const char *name = "SIG";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGABRT: name = "SIGABRT"; break;
        default: break;
    }
    te_ssafe_write("{\"level\":\"fatal\",\"event\":\"crash\",\"signal\":");
    te_ssafe_write_dec(sig);
    te_ssafe_write(",\"name\":\"");
    te_ssafe_write(name);
    te_ssafe_write("\",\"addr\":\"");
    te_ssafe_write_hex((unsigned long)(size_t)(info ? info->si_addr : (void *)0));
    te_ssafe_write("\",\"pid\":");
    te_ssafe_write_dec((long)getpid());
    te_ssafe_write(",\"msg\":\"hard fault in request handler; worker died (core dumped), systemd will restart. NOT a silent reset.\"}\n");
    /* Exact faulting instruction pointer from the trap frame. On the SAME
     * (non-stripped) binary this resolves precisely with
     * `addr2line -fie <binary> <pc>`. Guarded so non-glibc/x86_64 builds (where
     * REG_RIP is not defined) compile this out cleanly. */
#if defined(__linux__) && defined(__x86_64__) && defined(REG_RIP)
    {
        ucontext_t *uc = (ucontext_t *)uctx;
        unsigned long pc = (unsigned long)uc->uc_mcontext.gregs[REG_RIP];
        te_ssafe_write("{\"level\":\"fatal\",\"event\":\"crash_pc\",\"pc\":\"");
        te_ssafe_write_hex(pc);
        te_ssafe_write("\"}\n");
    }
#endif
    /* Async-signal-safe backtrace: the faulting function appears in the chain
     * (e.g. te_expr_is_null <- evaluate_condition <- interpret_if). Even for
     * `static` functions and without -rdynamic, the absolute frame addresses
     * print and resolve exactly with addr2line on this build. backtrace() is a
     * glibc/macOS extension; skipped on libcs without it (musl, etc.). */
#if defined(__GLIBC__) || defined(__APPLE__)
    {
        void *bt_frames[40];
        int bt_n = backtrace(bt_frames, 40);
        te_ssafe_write("{\"level\":\"fatal\",\"event\":\"crash_backtrace\",\"frames\":");
        te_ssafe_write_dec((long)bt_n);
        te_ssafe_write("}\n");
        backtrace_symbols_fd(bt_frames, bt_n, 2);
    }
#endif
    /* Restore default disposition and re-raise so a core is produced. */
    signal(sig, SIG_DFL);
    raise(sig);
}
static void te_install_crash_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = te_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}
#else
static void te_install_crash_handlers(void) { }
#endif

/* Run a single in-process civetweb server bound to host:port. When part of a
 * worker pool, `worker_index` >= 0 tweaks the banner; -1 means standalone. */
static int run_single_server(const char *host, int port, int worker_index) {
    invoke_lock_init();
    te_install_crash_handlers();   /* Fix B: never a silent reset (log + core). */
    /* Register the invoke lock so handlers parked in an `await` can cooperatively
     * release it (and have it re-acquired on resume) — see te_coop_yield_* in
     * ast.c. This is what lets multiple requests overlap while one is waiting on
     * async I/O, without ever running two interpreters at once. */
    te_coop_register_lock(invoke_lock_acquire, invoke_lock_release);
    g_start_time = time(NULL);

    /* Banner: list registered routes. */
    int route_count = 0;
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (m->route_path) route_count++;
    }
    if (route_count == 0) {
        te_log("warn", "script declares no endpoints; use "
                       "`endpoint get \"/path\" func() { ... }` to define one");
    }

    char port_spec[64];
    if (host && *host && strcmp(host, "0.0.0.0") != 0) {
        snprintf(port_spec, sizeof(port_spec), "%s:%d", host, port);
    } else {
        snprintf(port_spec, sizeof(port_spec), "%d", port);
    }

    /* item #7: bound how long a single request may tie up a connection so a
     * slow/stalled client cannot hold a worker thread forever. Configurable
     * via TYPEEASY_REQUEST_TIMEOUT_MS (default 30000). */
    char timeout_spec[32];
    {
        const char *e = getenv("TYPEEASY_REQUEST_TIMEOUT_MS");
        long v = e ? strtol(e, NULL, 10) : 0;
        snprintf(timeout_spec, sizeof(timeout_spec), "%ld", (v > 0) ? v : 30000L);
    }

    const char *options[] = {
        "listening_ports", port_spec,
        "num_threads", "8",
        "request_timeout_ms", timeout_spec,
        /* CORS is handled entirely by request_handler (see cors_resolve_origin)
         * so a comma-separated origin list can be matched against the request's
         * Origin header. We force access_control_allow_methods to "" so
         * civetweb does NOT auto-answer the OPTIONS preflight (it would emit a
         * single literal origin), letting our handler pick the right one. */
        "access_control_allow_methods", "",
        NULL
    };

    mg_init_library(0);

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    struct mg_context *ctx = mg_start(&callbacks, NULL, options);
    if (!ctx) {
        te_log("error", "failed to start civetweb on %s", port_spec);
        mg_exit_library();
        return 1;
    }

    /* Catch-all handler; routing happens inside. "**" matches every URI. */
    mg_set_request_handler(ctx, "**", request_handler, NULL);

    /* Register native WebSocket handlers for any [WebSocket("/path")] methods. */
    te_ws_register_routes(ctx);

    /* Routes and interpreter are wired: start advertising readiness. */
    g_ready = 1;

    if (worker_index >= 0) {
        printf("[typeeasy --api] worker #%d ready (pid %ld) at http://%s:%d\n",
               worker_index, (long)getpid(),
               (host && *host) ? host : "0.0.0.0", port);
    } else {
        printf("[typeeasy --api] Listening on http://%s:%d (%d route%s)\n",
               (host && *host) ? host : "0.0.0.0", port,
               route_count, route_count == 1 ? "" : "s");
        for (MethodNode *m = global_methods; m; m = m->next) {
            if (m->route_path) {
                printf("    %-6s %s -> %s()\n",
                       m->http_method ? m->http_method : "GET",
                       m->route_path,
                       m->name ? m->name : "?");
            }
        }
    }
    fflush(stdout);
    te_log("info", "server started on %s, %d route(s)", port_spec, route_count);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    if (g_hotreload_path) {
        te_log("info", "hot-reload watching %s", g_hotreload_path);
    }

    while (!g_stop_requested) {
        te_sleep_ms(200);
        /* Dev hot-reload: detect a changed script and request a re-exec. The
         * mtime must settle (two consecutive identical reads) so we don't
         * reload mid-write while an editor is still flushing the file. */
        if (g_hotreload_path) {
            time_t m = te_file_mtime(g_hotreload_path);
            if (m != 0 && m != g_hotreload_mtime) {
                te_sleep_ms(150);
                if (te_file_mtime(g_hotreload_path) == m) {
                    g_hotreload_mtime = m;
                    te_log("info", "change detected in %s, reloading", g_hotreload_path);
                    printf("[typeeasy --dev] change detected, reloading...\n");
                    fflush(stdout);
                    g_reload_requested = 1;
                    break;
                }
            }
        }
    }

    /* Graceful shutdown (item #9-12): stop advertising readiness, then give
     * in-flight requests a window to finish before tearing down civetweb.
     * The drain window is configurable via TYPEEASY_DRAIN_SECONDS (default 0,
     * i.e. stop immediately, preserving previous behavior). */
    g_ready = 0;
    te_log("info", "shutdown requested, draining (inflight=%ld)", g_inflight);
    {
        const char *e = getenv("TYPEEASY_DRAIN_SECONDS");
        long drain = e ? strtol(e, NULL, 10) : 0;
        if (drain < 0) drain = 0;
        for (long i = 0; i < drain && g_inflight > 0; i++) {
            te_sleep_ms(1000);
        }
    }
    mg_stop(ctx);
    te_log("info", "server stopped (req_total=%ld)", g_req_total);
    mg_exit_library();
    return g_reload_requested ? TE_RC_RELOAD : 0;
}

int typeeasy_run_api_server(const char *host, int port) {
    return run_single_server(host, port, -1);
}

int typeeasy_run_api_server_worker(const char *host, int port, int worker_index) {
    return run_single_server(host, port, worker_index);
}

#if defined(_WIN32)
/* ----- Windows worker pool via internal TCP load-balancer (Option A) -------
 * Windows lacks fork() and SO_REUSEPORT connection balancing, so we emulate
 * gunicorn's prefork model:
 *   - The master re-spawns N child processes of this same binary, each a
 *     normal single server bound to 127.0.0.1:<internal port>.
 *   - The master binds the PUBLIC host:port and acts as a round-robin TCP
 *     reverse proxy, forwarding each accepted connection to one worker.
 * Each worker is a separate process with its own interpreter state, so they
 * run scripts in true parallel. */

typedef struct { SOCKET src; SOCKET dst; } te_relay_ctx;

/* Pump bytes src->dst until src closes, then half-close dst for writing. */
static DWORD WINAPI te_relay_thread(LPVOID arg) {
    te_relay_ctx *c = (te_relay_ctx *)arg;
    char buf[16384];
    for (;;) {
        int n = recv(c->src, buf, (int)sizeof(buf), 0);
        if (n <= 0) break;
        int off = 0;
        while (off < n) {
            int w = send(c->dst, buf + off, n - off, 0);
            if (w <= 0) goto done;
            off += w;
        }
    }
done:
    shutdown(c->dst, SD_SEND);
    free(c);
    return 0;
}

typedef struct { SOCKET client; int worker_port; } te_conn_ctx;

/* Handle one client connection: connect to the assigned worker and relay
 * bytes in both directions until both ends close. */
static DWORD WINAPI te_conn_thread(LPVOID arg) {
    te_conn_ctx *cc = (te_conn_ctx *)arg;
    SOCKET client = cc->client;
    int wport = cc->worker_port;
    free(cc);

    SOCKET worker = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (worker == INVALID_SOCKET) { closesocket(client); return 0; }

    struct sockaddr_in wa;
    memset(&wa, 0, sizeof(wa));
    wa.sin_family = AF_INET;
    wa.sin_port = htons((unsigned short)wport);
    wa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(worker, (struct sockaddr *)&wa, sizeof(wa)) != 0) {
        closesocket(worker);
        closesocket(client);
        return 0;
    }

    /* worker -> client on a helper thread; client -> worker inline here. */
    te_relay_ctx *w2c = (te_relay_ctx *)malloc(sizeof(*w2c));
    HANDLE h = NULL;
    if (w2c) {
        w2c->src = worker; w2c->dst = client;
        h = CreateThread(NULL, 0, te_relay_thread, w2c, 0, NULL);
        if (!h) free(w2c);
    }

    char buf[16384];
    for (;;) {
        int n = recv(client, buf, (int)sizeof(buf), 0);
        if (n <= 0) break;
        int off = 0;
        while (off < n) {
            int wr = send(worker, buf + off, n - off, 0);
            if (wr <= 0) goto finish;
            off += wr;
        }
    }
finish:
    shutdown(worker, SD_SEND);
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
    closesocket(worker);
    closesocket(client);
    return 0;
}

static int run_windows_pool(const char *host, int port, int num_workers,
                            const char *script_path) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[typeeasy --api] WSAStartup failed.\n");
        return 1;
    }

    char exe[MAX_PATH];
    DWORD elen = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (elen == 0 || elen >= sizeof(exe)) {
        fprintf(stderr, "[typeeasy --api] failed to obtain the executable path.\n");
        WSACleanup();
        return 1;
    }

    int *wports = (int *)calloc((size_t)num_workers, sizeof(int));
    PROCESS_INFORMATION *procs =
        (PROCESS_INFORMATION *)calloc((size_t)num_workers, sizeof(PROCESS_INFORMATION));
    SOCKET lsock = INVALID_SOCKET;
    struct sockaddr_in la;
    unsigned long rr = 0;
    if (!wports || !procs) { free(wports); free(procs); WSACleanup(); return 1; }

    printf("[typeeasy --api] Starting pool of %d workers (load-balancer) at http://%s:%d\n",
           num_workers, (host && *host) ? host : "0.0.0.0", port);
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (m->route_path) {
            printf("    %-6s %s -> %s()\n",
                   m->http_method ? m->http_method : "GET",
                   m->route_path, m->name ? m->name : "?");
        }
    }
    fflush(stdout);

    /* Spawn workers on 127.0.0.1:(port+1+i). */
    for (int i = 0; i < num_workers; i++) {
        wports[i] = port + 1 + i;
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" --api \"%s\" --host 127.0.0.1 --port %d --worker-index %d",
                 exe, script_path ? script_path : "", wports[i], i);

        STARTUPINFOA si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL,
                            &si, &procs[i])) {
            fprintf(stderr, "[typeeasy --api] CreateProcess for worker %d failed (err=%lu).\n",
                    i, (unsigned long)GetLastError());
            for (int j = 0; j < i; j++) {
                if (procs[j].hProcess) {
                    TerminateProcess(procs[j].hProcess, 1);
                    CloseHandle(procs[j].hProcess);
                    CloseHandle(procs[j].hThread);
                }
            }
            free(wports); free(procs); WSACleanup();
            return 1;
        }
    }

    /* Give workers a moment to bind their internal ports. */
    te_sleep_ms(800);

    lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lsock == INVALID_SOCKET) {
        fprintf(stderr, "[typeeasy --api] failed to create the public socket.\n");
        goto kill_workers;
    }
    {
        BOOL reuse = TRUE;
        setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    }
    memset(&la, 0, sizeof(la));
    la.sin_family = AF_INET;
    la.sin_port = htons((unsigned short)port);
    if (host && *host && strcmp(host, "0.0.0.0") != 0) {
        la.sin_addr.s_addr = inet_addr(host);
        if (la.sin_addr.s_addr == INADDR_NONE) la.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        la.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(lsock, (struct sockaddr *)&la, sizeof(la)) != 0 ||
        listen(lsock, SOMAXCONN) != 0) {
        fprintf(stderr, "[typeeasy --api] failed to listen on %s:%d\n",
                (host && *host) ? host : "0.0.0.0", port);
        closesocket(lsock);
        lsock = INVALID_SOCKET;
        goto kill_workers;
    }

    printf("[typeeasy --api] Pool ready: load-balancing to %d workers (ports %d-%d).\n",
           num_workers, wports[0], wports[num_workers - 1]);
    fflush(stdout);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* Short accept timeout so we can poll g_stop_requested. */
    {
        DWORD tmo = 500;
        setsockopt(lsock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
    }

    while (!g_stop_requested) {
        struct sockaddr_in ca;
        int calen = (int)sizeof(ca);
        SOCKET client = accept(lsock, (struct sockaddr *)&ca, &calen);
        if (client == INVALID_SOCKET) {
            continue; /* timeout or transient error: re-check stop flag */
        }
        te_conn_ctx *cc = (te_conn_ctx *)malloc(sizeof(*cc));
        if (!cc) { closesocket(client); continue; }
        cc->client = client;
        cc->worker_port = wports[rr++ % (unsigned long)num_workers];
        HANDLE h = CreateThread(NULL, 0, te_conn_thread, cc, 0, NULL);
        if (!h) { closesocket(client); free(cc); }
        else CloseHandle(h);
    }

    if (lsock != INVALID_SOCKET) closesocket(lsock);

kill_workers:
    printf("\n[typeeasy --api] Stopping pool of %d workers...\n", num_workers);
    for (int i = 0; i < num_workers; i++) {
        if (procs[i].hProcess) {
            TerminateProcess(procs[i].hProcess, 0);
            WaitForSingleObject(procs[i].hProcess, 2000);
            CloseHandle(procs[i].hProcess);
            CloseHandle(procs[i].hThread);
        }
    }
    free(wports);
    free(procs);
    WSACleanup();
    return 0;
}
#endif /* _WIN32 */

int typeeasy_run_api_server_pool(const char *host, int port, int num_workers,
                                 const char *script_path) {
    if (num_workers <= 1) {
        return run_single_server(host, port, -1);
    }

#if defined(_WIN32)
    /* Windows lacks fork()/SO_REUSEPORT; use the internal load-balancer pool
     * (Option A) which re-spawns worker processes and proxies to them. */
    return run_windows_pool(host, port, num_workers, script_path);
#else
    /* POSIX worker pool: the parent forks N children. Each child runs its own
     * civetweb server binding the same port; the kernel (SO_REUSEPORT, enabled
     * in civetweb.c) load-balances incoming connections across them. Because
     * each child has its own address space (copy-on-write of the already-parsed
     * AST + interpreter globals), they execute scripts in true parallel without
     * sharing the non-thread-safe interpreter state. Mirrors gunicorn -w N. */
    (void)script_path; /* not needed on POSIX (children inherit via fork). */
    printf("[typeeasy --api] Starting pool of %d workers at http://%s:%d\n",
           num_workers, (host && *host) ? host : "0.0.0.0", port);
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (m->route_path) {
            printf("    %-6s %s -> %s()\n",
                   m->http_method ? m->http_method : "GET",
                   m->route_path, m->name ? m->name : "?");
        }
    }
    fflush(stdout);

    pid_t *pids = (pid_t *)calloc((size_t)num_workers, sizeof(pid_t));
    if (!pids) return 1;

    for (int i = 0; i < num_workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("[typeeasy --api] fork");
            /* Reap any children already spawned, then bail. */
            for (int j = 0; j < i; j++) {
                if (pids[j] > 0) kill(pids[j], SIGTERM);
            }
            for (int j = 0; j < i; j++) {
                if (pids[j] > 0) waitpid(pids[j], NULL, 0);
            }
            free(pids);
            return 1;
        }
        if (pid == 0) {
            /* Child: become a worker and never return to the spawn loop. */
            free(pids);
            int rc = run_single_server(host, port, i);
            _exit(rc);
        }
        pids[i] = pid;
    }

    /* Parent: forward SIGINT/SIGTERM to the whole group, then reap. */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    while (!g_stop_requested) {
        te_sleep_ms(200);
        /* If a worker died unexpectedly, stop the pool. */
        int status;
        pid_t gone = waitpid(-1, &status, WNOHANG);
        if (gone > 0) {
            fprintf(stderr, "[typeeasy --api] worker pid %ld exited; shutting down pool.\n",
                    (long)gone);
            break;
        }
    }

    printf("\n[typeeasy --api] Stopping pool of %d workers...\n", num_workers);
    for (int i = 0; i < num_workers; i++) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    for (int i = 0; i < num_workers; i++) {
        if (pids[i] > 0) waitpid(pids[i], NULL, 0);
    }
    free(pids);
    return 0;
#endif
}
