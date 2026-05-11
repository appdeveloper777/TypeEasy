#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "civetweb.h"
#include <mysql/mysql.h>

// Incluir el encabezado de TypeEasy
#include "typeeasy.h"
#include "../src/typeeasy_http.h"
#include "../src/typeeasy_api.h"
#include "../src/ast.h"
#include "../src/debugger.h"

// Estructura para la tabla de rutas dinámica
typedef struct RouteEntry {
    char *route_path;
    char *script_path;
    char *function_name;
    char *response_type;  // "json" or "xml"
    char *http_method;    // "GET" / "POST" / "PUT" / "DELETE" / "PATCH"
    int   cache_ttl;      // seconds; 0 = no cache
    void *method_node;    // MethodNode* cached at discover (hot-path O(1))
    struct RouteEntry *next;
} RouteEntry;

static RouteEntry *global_routes = NULL;
static TypeEasyContext *g_typeeasy_ctx = NULL;

/* Simple in-memory response cache keyed by method+full URI. Phase H. */
typedef struct CacheEntry {
    char  *key;           // "METHOD URI"
    char  *body;
    int    status;
    char  *content_type;
    time_t expires_at;
    struct CacheEntry *next;
} CacheEntry;
static CacheEntry *g_cache = NULL;

/* --- Hot-reload of .te scripts (dev mode, gated by TYPEEASY_HOTRELOAD=1) ---
 * On each request we stat every tracked .te file; if any mtime changed (or a
 * new file appeared), we drop all routes/cache, reinit the interpreter, and
 * re-run discover. Lets users edit .te files without `docker compose restart`. */
typedef struct ScriptFile {
    char  *path;
    time_t mtime;
    struct ScriptFile *next;
} ScriptFile;
static ScriptFile *g_scripts = NULL;
static int         g_hotreload = 0;
static char      **g_main_argv = NULL;  /* saved for execv self-restart */

static void track_script(const char *path, time_t m) {
    ScriptFile *s = (ScriptFile*)malloc(sizeof(ScriptFile));
    s->path = strdup(path); s->mtime = m; s->next = g_scripts; g_scripts = s;
}
static void free_scripts(void) {
    ScriptFile *s = g_scripts;
    while (s) { ScriptFile *n = s->next; free(s->path); free(s); s = n; }
    g_scripts = NULL;
}
static void free_routes_list(void) {
    RouteEntry *r = global_routes;
    while (r) {
        RouteEntry *n = r->next;
        free(r->route_path); free(r->script_path); free(r->function_name);
        free(r->response_type); free(r->http_method); free(r);
        r = n;
    }
    global_routes = NULL;
}
static void free_cache_list(void) {
    CacheEntry *c = g_cache;
    while (c) { CacheEntry *n = c->next; free(c->key); free(c->body); free(c->content_type); free(c); c = n; }
    g_cache = NULL;
}

void discover_routes(void); /* fwd */
void typeeasy_cleanup(TypeEasyContext* ctx); /* from typeeasy.c */

/* mg context handle (set in main) so the hot-reload thread can drain. */
static struct mg_context *g_mg_ctx = NULL;

/* Returns 1 if any tracked .te changed (mtime or new file), 0 otherwise.
 * Pure check — doesn't act. */
static int hotreload_changed(void) {
    if (!g_hotreload) return 0;
    struct stat st;
    for (ScriptFile *s = g_scripts; s; s = s->next) {
        if (stat(s->path, &st) != 0) return 1;
        if (st.st_mtime != s->mtime)  return 1;
    }
    FILE *fp = popen("ls -1 /app/apis/*.te 2>/dev/null", "r");
    if (fp) {
        char line[1035];
        int newfile = 0;
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = 0;
            int found = 0;
            for (ScriptFile *s = g_scripts; s; s = s->next) {
                if (strcmp(s->path, line) == 0) { found = 1; break; }
            }
            if (!found) { newfile = 1; break; }
        }
        pclose(fp);
        if (newfile) return 1;
    }
    return 0;
}

/* Replace this process with a fresh one (used by the forked child during
 * zero-downtime reload). */
static void child_execv_self(void) __attribute__((noreturn));
static void child_execv_self(void) {
    if (g_main_argv && g_main_argv[0]) {
        execv(g_main_argv[0], g_main_argv);
        execl("/proc/self/exe", "typeeasy_api", (char*)NULL);
    }
    fprintf(stderr, "[HOTRELOAD] execv falló — child saliendo\n");
    fflush(stderr);
    _exit(1);
}

/* Background thread (legacy, unused now): supervisor model handles reload. */
static void *hotreload_thread(void *arg) {
    (void)arg;
    return NULL;
}

static CacheEntry *cache_lookup(const char *key) {
    time_t now = time(NULL);
    CacheEntry *prev = NULL, *cur = g_cache;
    while (cur) {
        if (cur->expires_at <= now) {
            /* expired: drop */
            CacheEntry *dead = cur;
            if (prev) prev->next = cur->next; else g_cache = cur->next;
            cur = cur->next;
            free(dead->key); free(dead->body); free(dead->content_type); free(dead);
            continue;
        }
        if (strcmp(cur->key, key) == 0) return cur;
        prev = cur; cur = cur->next;
    }
    return NULL;
}
static void cache_store(const char *key, const char *body, int status, const char *ct, int ttl) {
    if (ttl <= 0) return;

    /* Si ya existe la key, actualizar in-place. */
    for (CacheEntry *e = g_cache; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            free(e->body); free(e->content_type);
            e->body = strdup(body ? body : "");
            e->content_type = strdup(ct ? ct : "application/json");
            e->status = status;
            e->expires_at = time(NULL) + ttl;
            return;
        }
    }

    /* L\u00edmite simple: si hay >1024 entries, dropear la cola (LRU aprox). */
    int n = 0; CacheEntry *prev = NULL, *cur = g_cache;
    while (cur) { n++; if (n > 1024 && cur->next == NULL && prev) {
        prev->next = NULL; free(cur->key); free(cur->body); free(cur->content_type); free(cur); break;
    } prev = cur; cur = cur->next; }

    CacheEntry *e = (CacheEntry*)malloc(sizeof(CacheEntry));
    e->key = strdup(key); e->body = strdup(body ? body : "");
    e->status = status; e->content_type = strdup(ct ? ct : "application/json");
    e->expires_at = time(NULL) + ttl;
    e->next = g_cache; g_cache = e;
}

// Función para añadir una ruta a la tabla
void add_route_full(const char *route, const char *script, const char *func,
                    const char *resp_type, const char *http_method, int cache_ttl);
void add_route(const char *route, const char *script, const char *func, const char *resp_type) {
    add_route_full(route, script, func, resp_type, "GET", 0);
}
void add_route_full(const char *route, const char *script, const char *func,
                    const char *resp_type, const char *http_method, int cache_ttl) {
    RouteEntry *entry = (RouteEntry*)malloc(sizeof(RouteEntry));
    entry->route_path = strdup(route);
    entry->script_path = strdup(script);
    entry->function_name = strdup(func);
    entry->response_type = strdup(resp_type ? resp_type : "json");
    entry->http_method = strdup(http_method ? http_method : "GET");
    entry->cache_ttl = cache_ttl;
    entry->method_node = (void*)typeeasy_find_method(func); /* O(1) hot path */
    entry->next = global_routes;
    global_routes = entry;
    printf("[LOG] Ruta registrada: %s %s -> %s() [%s, cache=%d]\n",
           entry->http_method, route, func, entry->response_type, cache_ttl);
}

// Función para cargar un script TypeEasy
int load_typeeasy_script(const char *script_path) {
    if (!g_typeeasy_ctx) {
        fprintf(stderr, "[ERROR] Contexto TypeEasy no inicializado\n");
        return 0;
    }
    
    FILE *fp = fopen(script_path, "r");
    if (!fp) {
        fprintf(stderr, "[ERROR] No se pudo abrir el script: %s\n", script_path);
        return 0;
    }
    
    // Obtener tamaño del archivo
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Leer contenido completo
    char *script_content = (char*)malloc(file_size + 1);
    if (!script_content) {
        fclose(fp);
        return 0;
    }
    
    size_t bytes_read = fread(script_content, 1, file_size, fp);
    script_content[bytes_read] = '\0';
    fclose(fp);
    
    // Cargar script en TypeEasy
    int result = typeeasy_load_script(g_typeeasy_ctx, script_path, script_content);
    free(script_content);
    
    if (result) {
        printf("[LOG] Script cargado: %s\n", script_path);
    } else {
        fprintf(stderr, "[ERROR] Error al cargar script: %s\n", script_path);
    }
    
    return result;
}

// Función para descubrir rutas al inicio
void discover_routes() {
    printf("[LOG] Iniciando descubrimiento de rutas...\n");
    
    // Inicializar contexto TypeEasy
    g_typeeasy_ctx = typeeasy_init();
    if (!g_typeeasy_ctx) {
        fprintf(stderr, "[ERROR] No se pudo inicializar TypeEasy\n");
        return;
    }
    
    FILE *fp;
    char path[1035];

    // Linux command to list files (for Docker)
    fp = popen("ls -1 /app/apis/*.te 2>/dev/null", "r");
    if (fp == NULL) {
        printf("[ERROR] No se pudo listar el directorio apis/\n");
        return;
    }

    while (fgets(path, sizeof(path), fp) != NULL) {
        // Eliminar salto de línea
        path[strcspn(path, "\r\n")] = 0;
        
        printf("[LOG] Descubriendo rutas en: %s\n", path);
        
        // Cargar script en TypeEasy
        if (!load_typeeasy_script(path)) {
            continue;
        }

        /* Track mtime for hot-reload */
        if (g_hotreload) {
            struct stat st;
            if (stat(path, &st) == 0) track_script(path, st.st_mtime);
        }
        
        // Ejecutar discover usando TypeEasy embebido
        char *discover_result = typeeasy_discover(g_typeeasy_ctx, path);
        if (!discover_result) {
            fprintf(stderr, "[ERROR] No se pudo descubrir rutas en: %s\n", path);
            continue;
        }
        
        printf("[LOG] Salida de --discover para %s: %s\n", path, discover_result);
        
        // Parsear JSON simple (ej: [{"route": "/api/x", ...}])
        char *p = discover_result;
        while ((p = strstr(p, "\"route\":"))) {
            p += 8; // Skip "route":
            while (*p == ' ' || *p == '"') p++; // Skip spaces and quote
            char *route_start = p;
            while (*p != '"' && *p != '\0') p++;
            if (*p == '\0') break;
            
            // Copy route without modifying original buffer
            int route_len = p - route_start;
            char *route = (char*)malloc(route_len + 1);
            strncpy(route, route_start, route_len);
            route[route_len] = '\0';
            p++; // Move past quote
            
            // Buscar function
            char *func_pos = strstr(p, "\"function\":");
            if (func_pos) {
                func_pos += 11;
                while (*func_pos == ' ' || *func_pos == '"') func_pos++;
                char *func_start = func_pos;
                while (*func_pos != '"' && *func_pos != '\0') func_pos++;
                if (*func_pos == '"') {
                    int func_len = func_pos - func_start;
                    char *func = (char*)malloc(func_len + 1);
                    strncpy(func, func_start, func_len);
                    func[func_len] = '\0';
                    
                    // Buscar response_type
                    char *resp_type = "json"; // default
                    char *resp_pos = strstr(p, "\"response_type\":");
                    if (resp_pos) {
                        resp_pos += 16;
                        while (*resp_pos == ' ' || *resp_pos == '"') resp_pos++;
                        char *resp_start = resp_pos;
                        while (*resp_pos != '"' && *resp_pos != '\0') resp_pos++;
                        if (*resp_pos == '"') {
                            int resp_len = resp_pos - resp_start;
                            char *resp_temp = (char*)malloc(resp_len + 1);
                            strncpy(resp_temp, resp_start, resp_len);
                            resp_temp[resp_len] = '\0';
                            resp_type = resp_temp;
                        }
                    }

                    /* Parse method */
                    char *http_method = "GET";
                    char *method_pos = strstr(p, "\"method\":");
                    if (method_pos) {
                        method_pos += 9;
                        while (*method_pos == ' ' || *method_pos == '"') method_pos++;
                        char *ms = method_pos;
                        while (*method_pos != '"' && *method_pos != '\0') method_pos++;
                        if (*method_pos == '"') {
                            int mlen = method_pos - ms;
                            char *tmp = (char*)malloc(mlen + 1);
                            strncpy(tmp, ms, mlen); tmp[mlen] = '\0';
                            http_method = tmp;
                        }
                    }

                    /* Parse cache_ttl */
                    int cache_ttl = 0;
                    char *cache_pos = strstr(p, "\"cache_ttl\":");
                    if (cache_pos) {
                        cache_pos += 12;
                        while (*cache_pos == ' ') cache_pos++;
                        cache_ttl = atoi(cache_pos);
                    }

                    // Registrar ruta
                    add_route_full(route, path, func, resp_type, http_method, cache_ttl);
                    
                    if (resp_pos && resp_type != (char*)"json") free((char*)resp_type);
                    if (method_pos && http_method != (char*)"GET") free((char*)http_method);
                    free(func);
                }
            }
            free(route);
        }
        
        free(discover_result);
    }
    pclose(fp);
}

/* Match a registered route pattern (possibly containing {name} segments)
 * against an actual URI. Returns 1 on match and populates path params via
 * typeeasy_http_add_param(). Returns 0 on mismatch. */
static int match_route_pattern(const char *pattern, const char *uri) {
    const char *pp = pattern, *up = uri;
    while (*pp && *up) {
        if (*pp == '{') {
            /* extract param name */
            const char *name_start = pp + 1;
            const char *name_end = strchr(name_start, '}');
            if (!name_end) return 0;
            /* consume up to next '/' or end in uri */
            const char *val_start = up;
            while (*up && *up != '/') up++;
            int nlen = name_end - name_start;
            int vlen = up - val_start;
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

/* Strip XML declaration / leading log noise from interpreter output. */
static char *strip_leading_garbage(char *result, const char **content_type_out) {
    char *actual_content = result;
    char *xml_start = strstr(result, "<?xml");
    if (xml_start) {
        actual_content = xml_start;
        *content_type_out = "application/xml";
        return actual_content;
    }
    char *p = result;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '<') { *content_type_out = "application/xml"; return p; }
    char *ja = strstr(result, "[{");
    char *jo = strstr(result, "{\"");
    if (ja && (!jo || ja < jo)) { *content_type_out = "application/json"; return ja; }
    if (jo)                     { *content_type_out = "application/json"; return jo; }
    *content_type_out = "application/json";
    return result;
}

static int manejadorApiDinamico(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    const char *uri    = req_info->local_uri;
    const char *method = req_info->request_method ? req_info->request_method : "GET";
    const char *qs     = req_info->query_string;

    fprintf(stderr, "[LOG] %s %s\n", method, uri); fflush(stderr);

    /* Reset HTTP state. We always reset before matching: params get added
     * during the match attempt and we need a clean slate per try. */
    typeeasy_http_reset();
    typeeasy_http_set_method(method);
    typeeasy_http_set_path(uri);

    /* Find a matching route (exact first, then pattern). Also enforce method. */
    RouteEntry *match = NULL;
    for (RouteEntry *entry = global_routes; entry && !match; entry = entry->next) {
        if (entry->http_method && strcmp(entry->http_method, method) != 0) continue;
        if (strchr(entry->route_path, '{') == NULL) {
            if (strcmp(uri, entry->route_path) == 0) match = entry;
        } else {
            /* Pattern match. Params get added inside; if it fails we clear them. */
            typeeasy_http_reset();
            typeeasy_http_set_method(method);
            typeeasy_http_set_path(uri);
            if (match_route_pattern(entry->route_path, uri)) match = entry;
        }
    }
    if (!match) {
        typeeasy_http_reset();
        mg_send_http_error(conn, 404, "Endpoint no encontrado");
        return 1;
    }

    /* Populate query params */
    if (qs && *qs) {
        const char *p = qs;
        while (*p) {
            const char *eq = strchr(p, '=');
            const char *amp = strchr(p, '&');
            if (!amp) amp = p + strlen(p);
            char key[128]; char val[1024];
            if (eq && eq < amp) {
                int kl = eq - p; int vl = amp - (eq + 1);
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

    /* Populate headers from the parsed civetweb header array. */
    for (int i = 0; i < req_info->num_headers; i++) {
        typeeasy_http_add_header(req_info->http_headers[i].name,
                                 req_info->http_headers[i].value);
    }

    /* Populate body (POST/PUT/PATCH). Cap at 1 MiB. */
    long long clen = req_info->content_length;
    if (clen > 0 && clen < (1 << 20)) {
        char *body = (char*)malloc((size_t)clen + 1);
        if (body) {
            int n = mg_read(conn, body, (size_t)clen);
            if (n < 0) n = 0;
            body[n] = '\0';
            typeeasy_http_set_body(body);
            free(body);
        }
    }

    /* Cache lookup */
    char cache_key[1024];
    snprintf(cache_key, sizeof(cache_key), "%s %s%s%s", method, uri,
             qs ? "?" : "", qs ? qs : "");
    if (match->cache_ttl > 0 && !g_debug_enabled) {
        CacheEntry *hit = cache_lookup(cache_key);
        if (hit) {
            fprintf(stderr, "[CACHE] HIT %s\n", cache_key);
            mg_printf(conn,
                      "HTTP/1.1 %d OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "X-Cache: HIT\r\n\r\n%s",
                      hit->status, hit->content_type,
                      (int)strlen(hit->body), hit->body);
            return 1;
        }
    }

    /* Invoke (fast path: cached MethodNode*) */
    clock_t start_time = clock();
    char *result = NULL;
    if (match->method_node) {
        result = typeeasy_embedded_invoke_method((MethodNode*)match->method_node);
    } else {
        result = typeeasy_invoke_with_script(g_typeeasy_ctx, match->script_path,
                                             match->function_name, NULL);
    }
    clock_t end_time = clock();
    double time_taken = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    if (!result) {
        typeeasy_http_reset();
        mg_send_http_error(conn, 500, "Error interno al ejecutar funcion TypeEasy");
        return 1;
    }

    const char *content_type = "application/json";
    char *actual_content = strip_leading_garbage(result, &content_type);
    int status = typeeasy_http_get_status();
    if (status <= 0) status = 200;

    /* Build extra headers from response_header() */
    char extra_headers[1024] = {0};
    int hi = 0; const char *hk, *hv;
    while (typeeasy_http_iter_response_header(hi, &hk, &hv)) {
        if (hk && hv) {
            size_t cur = strlen(extra_headers);
            snprintf(extra_headers + cur, sizeof(extra_headers) - cur, "%s: %s\r\n", hk, hv);
        }
        hi++;
    }

    mg_printf(conn,
              "HTTP/1.1 %d OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "X-Execution-Time: %.4f s\r\n"
              "X-Cache: MISS\r\n"
              "%s"
              "\r\n%s",
              status, content_type, (int)strlen(actual_content), time_taken,
              extra_headers, actual_content);

    /* Cache store */
    if (match->cache_ttl > 0 && status >= 200 && status < 300) {
        cache_store(cache_key, actual_content, status, content_type, match->cache_ttl);
        fprintf(stderr, "[CACHE] STORE %s ttl=%d\n", cache_key, match->cache_ttl);
    }

    typeeasy_http_reset();
    free(result);
    return 1;
}

// Variable global para indicar que debemos salir.
volatile int exit_flag = 0;

// Manejador de señales para detener el servidor de forma segura con Ctrl+C.
void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);
    exit_flag = 1;
}

// Función de limpieza al salir
void cleanup() {
    if (g_typeeasy_ctx) {
        typeeasy_cleanup(g_typeeasy_ctx);
        g_typeeasy_ctx = NULL;
    }
    
    // Liberar rutas
    RouteEntry *entry = global_routes;
    while (entry) {
        RouteEntry *next = entry->next;
        free(entry->route_path);
        free(entry->script_path);
        free(entry->function_name);
        free(entry->response_type);
        free(entry);
        entry = next;
    }
    global_routes = NULL;
}

// Nuevo manejador para la ruta raíz ("/")
static int manejadorRaiz(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;

    // Construir HTML dinámicamente con las rutas descubiertas
    char html[16384];
    int offset = 0;
    
    offset += snprintf(html + offset, sizeof(html) - offset,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>TypeEasy API Documentation</title>"
        "<style>"
        "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 40px; background: #f5f5f5; color: #333; }"
        "h1 { color: #2c3e50; }"
        ".container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }"
        ".endpoint { background: #fff; padding: 20px; margin: 15px 0; border: 1px solid #e0e0e0; border-left: 5px solid #4CAF50; border-radius: 4px; transition: box-shadow 0.2s; }"
        ".endpoint:hover { box-shadow: 0 2px 8px rgba(0,0,0,0.1); }"
        ".method { display: inline-block; padding: 5px 10px; border-radius: 4px; font-weight: bold; color: white; margin-right: 10px; font-size: 14px; }"
        ".get { background: #61affe; }"
        ".post { background: #49cc90; }"
        ".route { font-family: 'Consolas', 'Monaco', monospace; font-size: 16px; color: #333; font-weight: 600; }"
        ".json-badge { display: inline-block; padding: 3px 8px; border-radius: 12px; font-size: 11px; font-weight: bold; background: #f0ad4e; color: white; margin-left: 10px; vertical-align: middle; }"
        ".xml-badge { display: inline-block; padding: 3px 8px; border-radius: 12px; font-size: 11px; font-weight: bold; background: #5bc0de; color: white; margin-left: 10px; vertical-align: middle; }"
        ".embedded-badge { background: #673ab7; color: white; padding: 4px 10px; border-radius: 20px; font-size: 12px; font-weight: bold; vertical-align: middle; margin-left: 10px; text-transform: uppercase; letter-spacing: 0.5px; }"
        ".function { color: #7f8c8d; font-size: 14px; margin-top: 8px; font-family: monospace; }"
        ".count { color: #666; font-size: 14px; margin-bottom: 20px; }"
        ".controls { margin-top: 15px; display: flex; align-items: center; gap: 10px; }"
        ".try-btn { background: #4CAF50; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: 500; transition: background 0.2s; }"
        ".try-btn:hover { background: #43A047; }"
        ".try-btn:disabled { background: #a5d6a7; cursor: not-allowed; }"
        ".copy-btn { background: #2196F3; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: 500; display: none; transition: background 0.2s; }"
        ".copy-btn:hover { background: #1976D2; }"
        ".exec-time { font-size: 13px; color: #555; font-weight: 600; font-family: monospace; }"
        ".response { background: #2d2d2d; color: #f8f8f2; padding: 15px; border-radius: 4px; margin-top: 15px; font-family: 'Consolas', 'Monaco', monospace; white-space: pre-wrap; display: none; font-size: 13px; line-height: 1.5; overflow-x: auto; }"
        ".loading { color: #aaa; font-style: italic; }"
        "</style>"
        "<script>"
        "async function tryEndpoint(route, btnId) {"
        "  const btn = document.getElementById(btnId);"
        "  const responseDiv = document.getElementById(btnId + '-response');"
        "  const timeSpan = document.getElementById(btnId + '-time');"
        "  const copyBtn = document.getElementById(btnId + '-copy');"
        "  "
        "  btn.disabled = true;"
        "  btn.textContent = 'Loading...';"
        "  responseDiv.style.display = 'block';"
        "  responseDiv.className = 'response loading';"
        "  responseDiv.textContent = 'Ejecutando...';"
        "  timeSpan.textContent = '';"
        "  copyBtn.style.display = 'none';"
        "  "
        "  try {"
        "    const response = await fetch(route);"
        "    const execTime = response.headers.get('X-Execution-Time');"
        "    const data = await response.text();"
        "    "
        "    responseDiv.className = 'response';"
        "    let output = '';"
        "    "
        "    try {"
        "      const json = JSON.parse(data);"
        "      output = JSON.stringify(json, null, 2);"
        "    } catch(e) {"
        "      output = data;"
        "    }"
        "    "
        "    responseDiv.textContent = output;"
        "    "
        "    if (execTime) {"
        "      timeSpan.textContent = '⏱ ' + execTime + 's';"
        "    }"
        "    "
        "    copyBtn.style.display = 'inline-block';"
        "    copyBtn.onclick = function() {"
        "      navigator.clipboard.writeText(output).then(function() {"
        "        const originalText = copyBtn.textContent;"
        "        copyBtn.textContent = 'Copied!';"
        "        setTimeout(() => { copyBtn.textContent = originalText; }, 2000);"
        "      });"
        "    };"
        "    "
        "  } catch(error) {"
        "    responseDiv.className = 'response';"
        "    responseDiv.style.color = '#ff5252';"
        "    responseDiv.textContent = 'Error: ' + error.message;"
        "  }"
        "  "
        "  btn.disabled = false;"
        "  btn.textContent = 'Try it out';"
        "}"
        "</script>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>TypeEasy API Server <span class='embedded-badge'>EMBEDDED MODE</span></h1>"
        "<p>El servidor está funcionando correctamente.</p>");
    
    // Contar rutas
    int route_count = 0;
    RouteEntry *entry = global_routes;
    while (entry) {
        route_count++;
        entry = entry->next;
    }
    
    offset += snprintf(html + offset, sizeof(html) - offset,
        "<p class='count'><strong>%d</strong> ruta(s) dinámica(s) cargada(s):</p>",
        route_count);
    
    // Listar todas las rutas
    entry = global_routes;
    int endpoint_id = 0;
    if (entry == NULL) {
        offset += snprintf(html + offset, sizeof(html) - offset,
            "<p>No hay rutas descubiertas. Crea archivos .te con bloques 'endpoint { }' en /app/apis/</p>");
    } else {
        while (entry && offset < sizeof(html) - 1000) {
            // Determinar badge color basado en response type
            const char *badge_class = (entry->response_type && strcmp(entry->response_type, "xml") == 0) ? "xml-badge" : "json-badge";
            const char *badge_text = (entry->response_type && strcmp(entry->response_type, "xml") == 0) ? "XML" : "JSON";
            
            offset += snprintf(html + offset, sizeof(html) - offset,
                "<div class='endpoint'>"
                "<span class='method get'>GET</span>"
                "<span class='route'>%s</span>"
                "<span class='%s'>%s</span>"
                "<div class='function'>→ %s()</div>"
                "<div class='controls'>"
                "<button class='try-btn' id='btn-%d' onclick='tryEndpoint(\"%s\", \"btn-%d\")'>Try it out</button>"
                "<button class='copy-btn' id='btn-%d-copy'>Copy Response</button>"
                "<span class='exec-time' id='btn-%d-time'></span>"
                "</div>"
                "<div class='response' id='btn-%d-response'></div>"
                "</div>",
                entry->route_path,
                badge_class,
                badge_text,
                entry->function_name,
                endpoint_id,
                entry->route_path,
                endpoint_id,
                endpoint_id,
                endpoint_id,
                endpoint_id,
                endpoint_id);
            entry = entry->next;
            endpoint_id++;
        }
    }        
    
    offset += snprintf(html + offset, sizeof(html) - offset,
        "<hr>"
        "<p style='color: #999; font-size: 12px;'>TypeEasy Dynamic API Server - Embedded Mode - Endpoints auto-discovered from .te files</p>"
        "</div>"
        "</body>"
        "</html>");
    
    mg_write(conn, html, offset);
    return 1;
}

/* === Worker: serves HTTP. Same as old main(). On SIGTERM/SIGINT it drains
 * via mg_stop and exits. Supervisor spawns one of these. ===
 * If enable_debug != 0, opens the debugger listener (only slot 0). */
static int run_worker_ext(int enable_debug) {
    struct mg_context *ctx;
    const char *options[] = {"listening_ports", "8080", NULL};

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    atexit(cleanup);

    mg_init_library(0);
    discover_routes();

    if (enable_debug) {
        const char *p = getenv("TYPEEASY_DEBUG_PORT");
        int port = (p && *p) ? atoi(p) : 4711;
        if (port > 0 && port < 65536) {
            debugger_listen_async(port, NULL);
        }
    }

    ctx = mg_start(NULL, NULL, options);
    if (ctx == NULL) {
        fprintf(stderr, "[WORKER %d] Error al iniciar civetweb (puerto en uso?)\n", (int)getpid());
        return 1;
    }
    g_mg_ctx = ctx;
    printf("[WORKER %d] listo en :8080%s\n", (int)getpid(),
           enable_debug ? " [DEBUG ON]" : ""); fflush(stdout);

    mg_set_request_handler(ctx, "/",      manejadorRaiz,        NULL);
    mg_set_request_handler(ctx, "/api/**", manejadorApiDinamico, NULL);

    while (exit_flag == 0) sleep(1);

    printf("[WORKER %d] SIGTERM recibido — drenando + saliendo\n", (int)getpid()); fflush(stdout);
    mg_stop(ctx);
    mg_exit_library();
    return 0;
}

static int run_worker(void) { return run_worker_ext(0); }

/* Supervisor's own .te tracker: just stat, no parsing. */
static void supervisor_track_scripts(void) {
    free_scripts();
    FILE *fp = popen("ls -1 /app/apis/*.te 2>/dev/null", "r");
    if (!fp) return;
    char line[1035];
    struct stat st;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (stat(line, &st) == 0) track_script(line, st.st_mtime);
    }
    pclose(fp);
}

/* === Multi-worker supervisor (nginx-style) ===
 * N workers servidos por SO_REUSEPORT. El kernel balancea conexiones nuevas.
 * Defaults: TYPEEASY_WORKERS env var, else nproc, capped at MAX_WORKERS. */
#define MAX_WORKERS 64
static pid_t g_worker_pids[MAX_WORKERS];
static int   g_num_workers = 0;

static int detect_num_workers(void) {
    /* Debug mode: force 1 worker so EVERY request goes to the debugged process.
     * Otherwise the kernel (SO_REUSEPORT) load-balances away from the debug worker. */
    if (getenv("TYPEEASY_DEBUG_PORT") != NULL) {
        fprintf(stderr, "[SUPERVISOR] TYPEEASY_DEBUG_PORT set -> forzando 1 worker\n");
        return 1;
    }
    const char *env = getenv("TYPEEASY_WORKERS");
    if (env && *env) {
        int n = atoi(env);
        if (n >= 1 && n <= MAX_WORKERS) return n;
    }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > MAX_WORKERS) n = MAX_WORKERS;
    return (int)n;
}

static pid_t spawn_worker(int slot) {
    /* Slot 0 hosts the debugger listener (single bind). */
    int enable_debug = (slot == 0 && getenv("TYPEEASY_DEBUG_PORT") != NULL) ? 1 : 0;
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) { _exit(run_worker_ext(enable_debug)); }
    g_worker_pids[slot] = pid;
    return pid;
}

static int find_worker_slot(pid_t pid) {
    for (int i = 0; i < g_num_workers; i++)
        if (g_worker_pids[i] == pid) return i;
    return -1;
}

int main(void) {
    /* Save argv for child re-exec (legacy; no longer used by reload path). */
    static char *argv0_buf = NULL;
    static char *fake_argv[2];
    {
        char buf[1024];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; argv0_buf = strdup(buf); }
        else argv0_buf = strdup("/app/typeeasy_api");
        fake_argv[0] = argv0_buf;
        fake_argv[1] = NULL;
        g_main_argv = fake_argv;
    }

    const char *hr = getenv("TYPEEASY_HOTRELOAD");
    g_hotreload = (hr && (*hr == '1' || *hr == 't' || *hr == 'T' || *hr == 'y' || *hr == 'Y')) ? 1 : 0;
    g_num_workers = detect_num_workers();

    /* Single-worker + no hot-reload => no supervisor (lean prod single proc). */
    if (g_num_workers == 1 && !g_hotreload) {
        return run_worker();
    }

    /* === SUPERVISOR === PID 1 del contenedor, nunca muere. */
    printf("[SUPERVISOR %d] iniciado — workers=%d  hot-reload=%s\n",
           (int)getpid(), g_num_workers, g_hotreload ? "ON" : "OFF");
    fflush(stdout);

    signal(SIGINT,  SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    if (g_hotreload) supervisor_track_scripts();

    /* Spawn pool inicial */
    for (int i = 0; i < g_num_workers; i++) {
        if (spawn_worker(i) < 0) return 1;
    }

    while (1) {
        sleep(1);

        /* Reap todos los hijos muertos y respawn en su slot. */
        for (;;) {
            int status;
            pid_t reaped = waitpid(-1, &status, WNOHANG);
            if (reaped <= 0) break;
            int slot = find_worker_slot(reaped);
            if (slot >= 0) {
                fprintf(stderr, "[SUPERVISOR] worker %d (slot %d) murio (status=%d) — respawn\n",
                        (int)reaped, slot, status);
                fflush(stderr);
                spawn_worker(slot);
            }
            /* slot<0 => worker viejo terminado por reload, ignorar. */
        }

        if (!g_hotreload || !hotreload_changed()) continue;

        /* === RELOAD ROLLING (nginx -s reload) ===
         * 1) Snapshot de pids viejos.
         * 2) Spawn N workers nuevos en slots nuevos (temporales).
         * 3) Esperar 2s para que liguen al puerto via SO_REUSEPORT.
         * 4) SIGTERM a los viejos, waitpid hasta drenar.
         * 5) Compactar slots: los nuevos pasan a las posiciones definitivas. */
        printf("[SUPERVISOR] cambio en .te — rolling restart de %d workers\n", g_num_workers);
        fflush(stdout);

        pid_t old_pids[MAX_WORKERS];
        int old_n = g_num_workers;
        for (int i = 0; i < old_n; i++) old_pids[i] = g_worker_pids[i];

        pid_t new_pids[MAX_WORKERS];
        int spawn_failed = 0;
        for (int i = 0; i < g_num_workers; i++) {
            int enable_debug = (i == 0 && getenv("TYPEEASY_DEBUG_PORT") != NULL) ? 1 : 0;
            pid_t p = fork();
            if (p < 0) { perror("fork"); spawn_failed = 1; new_pids[i] = 0; continue; }
            if (p == 0) { _exit(run_worker_ext(enable_debug)); }
            new_pids[i] = p;
        }
        if (spawn_failed) {
            fprintf(stderr, "[SUPERVISOR] reload parcial — algunos forks fallaron\n");
            fflush(stderr);
        }

        /* Dejar que los nuevos bindeen y empiecen a aceptar. */
        sleep(2);

        printf("[SUPERVISOR] SIGTERM a %d workers viejos (graceful drain)\n", old_n);
        fflush(stdout);
        for (int i = 0; i < old_n; i++) {
            if (old_pids[i] > 0) kill(old_pids[i], SIGTERM);
        }
        for (int i = 0; i < old_n; i++) {
            if (old_pids[i] > 0) waitpid(old_pids[i], NULL, 0);
        }

        /* Promover los nuevos al pool oficial. */
        for (int i = 0; i < g_num_workers; i++) g_worker_pids[i] = new_pids[i];

        supervisor_track_scripts(); /* baseline para el siguiente cambio */
        printf("[SUPERVISOR] reload completo\n");
        fflush(stdout);
    }
}