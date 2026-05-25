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
#include <ctype.h>
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
#include "../api_server/te_websocket.h"

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
        "<div class='subtitle'>%d endpoint%s registrado%s</div></header>"
        "<main>"
        "<div class='toolbar'>"
        "<input type='text' id='filter' placeholder='Filtrar por ruta o funcion' oninput='applyFilter()' autocomplete='off'>"
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
            "<div class='empty'>No hay rutas registradas. Define endpoints con <code>[HttpGet(\"/ruta\")]</code> en tus .te.</div>");
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
                    "<label>Query string (opcional)</label><input type='text' data-query placeholder='key=value&amp;k2=v2'>");
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

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        off);
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

    /* Serve the built-in Swagger-style UI at GET /. */
    if (strcmp(method, "GET") == 0 && strcmp(uri, "/") == 0) {
        return serve_root_swagger_ui(conn);
    }

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

    /* Register native WebSocket handlers for any [WebSocket("/path")] methods. */
    te_ws_register_routes(ctx);

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
