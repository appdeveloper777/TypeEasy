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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
  #include <windows.h>
  #define te_sleep_ms(ms) Sleep(ms)
#else
  #include <unistd.h>
  #include <pthread.h>
  #define te_sleep_ms(ms) usleep((ms) * 1000)
#endif

#include "civetweb.h"
#include "ast.h"
#include "typeeasy_api.h"
#include "typeeasy_http.h"
#include "typeeasy_api_server.h"

#ifdef _WIN32
static CRITICAL_SECTION g_invoke_lock;
static int g_invoke_lock_init = 0;
static void invoke_lock_init(void)   { if (!g_invoke_lock_init) { InitializeCriticalSection(&g_invoke_lock); g_invoke_lock_init = 1; } }
static void invoke_lock_acquire(void){ EnterCriticalSection(&g_invoke_lock); }
static void invoke_lock_release(void){ LeaveCriticalSection(&g_invoke_lock); }
#else
static pthread_mutex_t g_invoke_lock = PTHREAD_MUTEX_INITIALIZER;
static void invoke_lock_init(void)   { /* static init */ }
static void invoke_lock_acquire(void){ pthread_mutex_lock(&g_invoke_lock); }
static void invoke_lock_release(void){ pthread_mutex_unlock(&g_invoke_lock); }
#endif

static volatile sig_atomic_t g_stop_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop_requested = 1;
}

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

static int request_handler(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    const struct mg_request_info *req = mg_get_request_info(conn);
    const char *uri    = req->local_uri ? req->local_uri : "/";
    const char *method = req->request_method ? req->request_method : "GET";
    const char *qs     = req->query_string;

    invoke_lock_acquire();

    typeeasy_http_reset();
    typeeasy_http_set_method(method);
    typeeasy_http_set_path(uri);

    MethodNode *m = find_route(uri, method);
    if (!m) {
        invoke_lock_release();
        mg_send_http_error(conn, 404, "Endpoint no encontrado: %s %s", method, uri);
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

    /* Body (cap 1 MiB). */
    long long clen = req->content_length;
    if (clen > 0 && clen < (1 << 20)) {
        char *body = (char *)malloc((size_t)clen + 1);
        if (body) {
            int n = mg_read(conn, body, (size_t)clen);
            if (n < 0) n = 0;
            body[n] = '\0';
            typeeasy_http_set_body(body);
            free(body);
        }
    }

    char *result = typeeasy_embedded_invoke_method(m);
    const char *ctype = (strcmp(detect_response_type_embedded(m->body), "xml") == 0)
                        ? "application/xml" : "application/json";

    if (!result) result = strdup("");
    int len = (int)strlen(result);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "\r\n",
              ctype, len);
    if (len > 0) mg_write(conn, result, len);

    free(result);
    invoke_lock_release();
    return 1;
}

int typeeasy_run_api_server(const char *host, int port) {
    invoke_lock_init();

    /* Banner: list registered routes. */
    int route_count = 0;
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (m->route_path) route_count++;
    }
    if (route_count == 0) {
        fprintf(stderr,
                "[typeeasy --api] Aviso: el script no declara endpoints.\n"
                "                 Usa `endpoint get \"/ruta\" func() { ... }` para definir uno.\n");
    }

    char port_spec[64];
    if (host && *host && strcmp(host, "0.0.0.0") != 0) {
        snprintf(port_spec, sizeof(port_spec), "%s:%d", host, port);
    } else {
        snprintf(port_spec, sizeof(port_spec), "%d", port);
    }

    const char *options[] = {
        "listening_ports", port_spec,
        "num_threads", "8",
        NULL
    };

    mg_init_library(0);

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    struct mg_context *ctx = mg_start(&callbacks, NULL, options);
    if (!ctx) {
        fprintf(stderr, "[typeeasy --api] Error: no se pudo arrancar civetweb en %s\n", port_spec);
        mg_exit_library();
        return 1;
    }

    /* Catch-all handler; routing happens inside. "**" matches every URI. */
    mg_set_request_handler(ctx, "**", request_handler, NULL);

    printf("[typeeasy --api] Escuchando en http://%s:%d (%d ruta%s)\n",
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
    fflush(stdout);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    while (!g_stop_requested) {
        te_sleep_ms(200);
    }

    printf("\n[typeeasy --api] Detenido.\n");
    mg_stop(ctx);
    mg_exit_library();
    return 0;
}
