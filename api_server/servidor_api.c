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
#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif
#else
#include <unistd.h>
#include <strings.h>
#endif
#include "civetweb.h"
#include "te_websocket.h"
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

/* === Runtime config ===
 * Resolution precedence: CLI flag > env var > built-in default.
 * Populated in main() before any subsystem starts. */
static char g_apis_dir[1024]         = ""; /* directory scanned for *.te endpoints */
static char g_single_api_file[1024]  = ""; /* if non-empty, only this .te is served */
static int  g_port                   = 8080; /* HTTP listen port */
static char g_host[128]              = ""; /* bind address (empty => civetweb default = all) */

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
    if (g_single_api_file[0]) return 0; /* single-file mode: only tracked file matters */
    char hr_cmd[1280];
    snprintf(hr_cmd, sizeof(hr_cmd), "ls -1 %s/*.te 2>/dev/null", g_apis_dir);
    FILE *fp = popen(hr_cmd, "r");
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
    const char *m = http_method ? http_method : "GET";
    /* Dedup: typeeasy_discover() acumula rutas de archivos previos en cada
     * llamada (estado global del intérprete). Evitamos registrar duplicados
     * por (route, method, function). */
    for (RouteEntry *it = global_routes; it; it = it->next) {
        if (strcmp(it->route_path, route) == 0 &&
            strcmp(it->http_method, m) == 0 &&
            strcmp(it->function_name, func) == 0) {
            return; /* ya registrada */
        }
    }
    RouteEntry *entry = (RouteEntry*)malloc(sizeof(RouteEntry));
    entry->route_path = strdup(route);
    entry->script_path = strdup(script);
    entry->function_name = strdup(func);
    entry->response_type = strdup(resp_type ? resp_type : "json");
    entry->http_method = strdup(m);
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
    int files_total = 0;
    int files_failed_load = 0;
    int files_failed_discover = 0;

    /* Single-file mode: skip ls, feed one path directly to the loop. */
    if (g_single_api_file[0]) {
        printf("[LOG] Escaneando endpoint unico: %s\n", g_single_api_file);
        fp = tmpfile();
        if (!fp) { fprintf(stderr, "[ERROR] tmpfile() falló\n"); return; }
        fprintf(fp, "%s\n", g_single_api_file);
        rewind(fp);
    } else {
        /* Build `ls` command using configured apis dir (CLI flag, env var, or default). */
        char ls_cmd[1280];
        snprintf(ls_cmd, sizeof(ls_cmd), "ls -1 %s/*.te 2>/dev/null", g_apis_dir);
        printf("[LOG] Escaneando endpoints en: %s\n", g_apis_dir);
        fp = popen(ls_cmd, "r");
        if (fp == NULL) {
            fprintf(stderr, "[ERROR] No se pudo listar el directorio: %s\n", g_apis_dir);
            return;
        }
    }

    while (fgets(path, sizeof(path), fp) != NULL) {
        // Eliminar salto de línea
        path[strcspn(path, "\r\n")] = 0;
        files_total++;

        printf("[LOG] Descubriendo rutas en: %s\n", path);

        // Cargar script en TypeEasy
        if (!load_typeeasy_script(path)) {
            files_failed_load++;
            fprintf(stderr, "[WARN] discover_routes: skip (load failed) %s\n", path);
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
            files_failed_discover++;
            fprintf(stderr, "[WARN] discover_routes: skip (discover failed) %s\n", path);
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
    if (g_single_api_file[0]) fclose(fp); else pclose(fp);

    {
        int ok = files_total - files_failed_load - files_failed_discover;
        printf("[LOG] discover_routes summary: %d/%d archivos OK (%d load-fail, %d discover-fail)\n",
               ok, files_total, files_failed_load, files_failed_discover);
    }
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
    /* HTML detection (case-insensitive): <!DOCTYPE html...> or <html... */
    if (*p == '<') {
        if ((strncasecmp(p, "<!DOCTYPE html", 14) == 0) ||
            (strncasecmp(p, "<html", 5) == 0)) {
            *content_type_out = "text/html; charset=utf-8";
            return p;
        }
        *content_type_out = "application/xml";
        return p;
    }
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

/* Comparator: sort RouteEntry pointers alphabetically by route_path, then
 * by HTTP method as tiebreaker. Used by manejadorRaiz to render endpoints
 * in stable order regardless of file-load order. */
static int route_cmp_alpha(const void *a, const void *b) {
    const RouteEntry *ra = *(const RouteEntry * const *)a;
    const RouteEntry *rb = *(const RouteEntry * const *)b;
    int c = strcmp(ra->route_path ? ra->route_path : "",
                   rb->route_path ? rb->route_path : "");
    if (c) return c;
    return strcmp(ra->http_method ? ra->http_method : "",
                  rb->http_method ? rb->http_method : "");
}

/* Compute the group key for a route: first two path segments, e.g.
 * "/api/mysql/usuarios" -> "/api/mysql". Falls back to "/api" or "/". */
static void route_group_key(const char *path, char *out, size_t outsz) {
    if (!path || !*path) { snprintf(out, outsz, "/"); return; }
    const char *p = path;
    if (*p == '/') p++;
    const char *seg1_end = strchr(p, '/');
    if (!seg1_end) { snprintf(out, outsz, "/%.*s", (int)strlen(p), p); return; }
    const char *p2 = seg1_end + 1;
    const char *seg2_end = strchr(p2, '/');
    int len2 = seg2_end ? (int)(seg2_end - p2) : (int)strlen(p2);
    /* Skip path params as group name */
    if (len2 > 0 && p2[0] == '{') {
        snprintf(out, outsz, "/%.*s", (int)(seg1_end - p), p);
        return;
    }
    snprintf(out, outsz, "/%.*s/%.*s", (int)(seg1_end - p), p, len2, p2);
}

// Nuevo manejador para la ruta raíz ("/") — UI estilo Swagger
static int manejadorRaiz(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;

    // Construir HTML dinámicamente con las rutas descubiertas (buffer dinámico:
    // soporta cientos de endpoints sin truncar).
    size_t cap = 262144; /* 256 KB inicial: cubre el header HTML+CSS+JS y ~300 rutas */
    char *html = (char*)malloc(cap);
    if (!html) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        return 1;
    }
    int offset = 0;
#define ENSURE_CAP(extra) do { \
        if ((size_t)offset + (size_t)(extra) + 1 >= cap) { \
            size_t nc = cap; \
            while (nc < (size_t)offset + (size_t)(extra) + 1) nc *= 2; \
            char *nb = (char*)realloc(html, nc); \
            if (!nb) { free(html); mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"); return 1; } \
            html = nb; cap = nc; \
        } \
    } while (0)
    
    ENSURE_CAP(16384);
    offset += snprintf(html + offset, cap - offset,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>TypeEasy API - Swagger</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;margin:0;background:#fafafa;color:#3b4151}"
        "header{background:linear-gradient(135deg,#1b1b1b 0%,#2c3e50 100%);color:#fff;padding:24px 40px;box-shadow:0 2px 8px rgba(0,0,0,0.15)}"
        "header h1{margin:0;font-size:28px;font-weight:600;display:flex;align-items:center;gap:14px}"
        "header .subtitle{margin-top:6px;font-size:14px;color:#bfc9d4}"
        ".embedded-badge{background:#673ab7;color:#fff;padding:4px 12px;border-radius:20px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:0.5px}"
        ".stats{display:flex;gap:12px;margin-top:14px;flex-wrap:wrap}"
        ".stat{background:rgba(255,255,255,0.08);padding:6px 14px;border-radius:14px;font-size:13px;border:1px solid rgba(255,255,255,0.12)}"
        ".stat b{color:#fff;margin-right:6px}"
        ".stat.get{border-color:#61affe}.stat.get b{color:#61affe}"
        ".stat.post{border-color:#49cc90}.stat.post b{color:#49cc90}"
        ".stat.put{border-color:#fca130}.stat.put b{color:#fca130}"
        ".stat.delete{border-color:#f93e3e}.stat.delete b{color:#f93e3e}"
        ".stat.patch{border-color:#50e3c2}.stat.patch b{color:#50e3c2}"
        "main{max-width:1200px;margin:24px auto;padding:0 24px}"
        ".toolbar{background:#fff;padding:14px 18px;border:1px solid #e3e3e3;border-radius:6px;margin-bottom:18px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}"
        ".toolbar input[type=text]{flex:1;min-width:260px;padding:8px 12px;border:1px solid #ccc;border-radius:4px;font-size:14px;font-family:inherit}"
        ".toolbar input[type=text]:focus{outline:none;border-color:#49cc90;box-shadow:0 0 0 2px rgba(73,204,144,0.18)}"
        ".toolbar .filter-pills{display:flex;gap:6px}"
        ".pill{background:#eee;border:1px solid transparent;color:#444;padding:5px 10px;border-radius:14px;font-size:12px;cursor:pointer;font-weight:600;letter-spacing:0.3px;user-select:none}"
        ".pill.active{background:#3b4151;color:#fff}"
        ".pill[data-m=get].active{background:#61affe}"
        ".pill[data-m=post].active{background:#49cc90}"
        ".pill[data-m=put].active{background:#fca130}"
        ".pill[data-m=delete].active{background:#f93e3e}"
        ".pill[data-m=patch].active{background:#50e3c2}"
        ".group{margin-bottom:24px}"
        ".group-head{display:flex;align-items:center;gap:10px;padding:10px 14px;background:#fff;border:1px solid #e3e3e3;border-radius:6px 6px 0 0;cursor:pointer;user-select:none}"
        ".group-head h2{margin:0;font-size:18px;color:#3b4151;font-weight:600;font-family:Consolas,monospace}"
        ".group-head .count-chip{background:#e8e8e8;color:#555;padding:2px 9px;border-radius:10px;font-size:11px;font-weight:700}"
        ".group-head .toggle{margin-left:auto;color:#888;font-size:13px;transition:transform 0.15s}"
        ".group.collapsed .toggle{transform:rotate(-90deg)}"
        ".group.collapsed .group-body{display:none}"
        ".group-body{border:1px solid #e3e3e3;border-top:0;border-radius:0 0 6px 6px;background:#fff}"
        ".endpoint{border-bottom:1px solid #f0f0f0;padding:0}"
        ".endpoint:last-child{border-bottom:0}"
        ".ep-head{display:flex;align-items:center;gap:10px;padding:10px 14px;cursor:pointer;user-select:none}"
        ".ep-head:hover{background:#f7f7f7}"
        ".method{display:inline-block;min-width:60px;text-align:center;padding:4px 8px;border-radius:3px;font-weight:700;color:#fff;font-size:12px;letter-spacing:0.4px;font-family:Consolas,monospace}"
        ".method.get{background:#61affe}.method.post{background:#49cc90}.method.put{background:#fca130}.method.delete{background:#f93e3e}.method.patch{background:#50e3c2}"
        ".route{font-family:Consolas,Menlo,monospace;font-size:15px;color:#3b4151;font-weight:600;flex:1;word-break:break-all}"
        ".route .param{color:#9012fe;font-weight:700}"
        ".badge{display:inline-block;padding:2px 7px;border-radius:10px;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:0.3px}"
        ".badge.json{background:#fff3cd;color:#856404}"
        ".badge.xml{background:#d1ecf1;color:#0c5460}"
        ".badge.cache{background:#e2d4ff;color:#5e35b1}"
        ".func{color:#7a7a7a;font-size:13px;font-family:Consolas,monospace;margin-right:8px}"
        ".caret{color:#999;font-size:13px;width:14px;text-align:center;transition:transform 0.15s}"
        ".endpoint.open .caret{transform:rotate(90deg)}"
        ".endpoint.open .ep-body{display:block}"
        ".ep-body{display:none;padding:14px 18px 18px 18px;background:#fafafa;border-top:1px solid #f0f0f0}"
        ".ep-body label{display:block;font-size:12px;font-weight:600;color:#555;margin:8px 0 4px 0;text-transform:uppercase;letter-spacing:0.3px}"
        ".ep-body input,.ep-body textarea{width:100%;padding:7px 10px;border:1px solid #ccc;border-radius:3px;font-family:Consolas,monospace;font-size:13px}"
        ".ep-body textarea{resize:vertical;min-height:90px}"
        ".params-grid{display:grid;grid-template-columns:160px 1fr;gap:6px 12px;align-items:center;margin-bottom:8px}"
        ".params-grid label{margin:0;text-transform:none;letter-spacing:0;font-family:Consolas,monospace;color:#9012fe;font-size:13px}"
        ".ep-actions{margin-top:14px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
        ".btn{border:0;padding:8px 18px;border-radius:4px;font-size:13px;font-weight:600;cursor:pointer;transition:opacity 0.15s,background 0.15s}"
        ".btn-try{background:#49cc90;color:#fff}.btn-try:hover{background:#3eb37e}"
        ".btn-try:disabled{background:#a5d6a7;cursor:not-allowed}"
        ".btn-copy{background:#2196F3;color:#fff;display:none}.btn-copy:hover{background:#1976D2}"
        ".exec-time{font-size:12px;color:#666;font-family:Consolas,monospace;font-weight:600}"
        ".status-pill{padding:3px 10px;border-radius:11px;font-size:11px;font-weight:700;font-family:Consolas,monospace;display:none}"
        ".status-pill.ok{background:#d4edda;color:#155724}"
        ".status-pill.bad{background:#f8d7da;color:#721c24}"
        ".response{background:#1e1e1e;color:#dcdcdc;padding:14px;border-radius:4px;margin-top:12px;font-family:Consolas,Menlo,monospace;font-size:12px;line-height:1.5;white-space:pre-wrap;display:none;overflow-x:auto;max-height:480px;overflow-y:auto}"
        ".response.error{color:#ff7676}"
        ".hidden{display:none !important}"
        ".empty{padding:30px;text-align:center;color:#888;background:#fff;border:1px dashed #ccc;border-radius:6px}"
        "footer{text-align:center;padding:20px;color:#999;font-size:12px}"
        "</style>"
        "</head>"
        "<body>"
        "<header>"
        "<h1>TypeEasy API <span class='embedded-badge'>Embedded</span></h1>"
        "<div class='subtitle'>Endpoints auto-descubiertos desde <code>/app/apis/*.te</code></div>");

    /* Collect routes into a sortable array */
    int route_count = 0;
    int cnt_get = 0, cnt_post = 0, cnt_put = 0, cnt_delete = 0, cnt_patch = 0, cnt_other = 0;
    {
        RouteEntry *e = global_routes;
        while (e) { route_count++; e = e->next; }
    }
    RouteEntry **arr = NULL;
    if (route_count > 0) {
        arr = (RouteEntry**)malloc(sizeof(RouteEntry*) * route_count);
        if (!arr) { free(html); mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"); return 1; }
        int i = 0;
        RouteEntry *e = global_routes;
        while (e) {
            arr[i++] = e;
            const char *m = e->http_method ? e->http_method : "GET";
            if (!strcmp(m,"GET")) cnt_get++;
            else if (!strcmp(m,"POST")) cnt_post++;
            else if (!strcmp(m,"PUT")) cnt_put++;
            else if (!strcmp(m,"DELETE")) cnt_delete++;
            else if (!strcmp(m,"PATCH")) cnt_patch++;
            else cnt_other++;
            e = e->next;
        }
        qsort(arr, route_count, sizeof(RouteEntry*), route_cmp_alpha);
    }

    /* Stats row */
    ENSURE_CAP(1024);
    offset += snprintf(html + offset, cap - offset,
        "<div class='stats'>"
        "<div class='stat'><b>%d</b>endpoints</div>", route_count);
    if (cnt_get) offset += snprintf(html + offset, cap - offset, "<div class='stat get'><b>%d</b>GET</div>", cnt_get);
    if (cnt_post) offset += snprintf(html + offset, cap - offset, "<div class='stat post'><b>%d</b>POST</div>", cnt_post);
    if (cnt_put) offset += snprintf(html + offset, cap - offset, "<div class='stat put'><b>%d</b>PUT</div>", cnt_put);
    if (cnt_delete) offset += snprintf(html + offset, cap - offset, "<div class='stat delete'><b>%d</b>DELETE</div>", cnt_delete);
    if (cnt_patch) offset += snprintf(html + offset, cap - offset, "<div class='stat patch'><b>%d</b>PATCH</div>", cnt_patch);
    if (cnt_other) offset += snprintf(html + offset, cap - offset, "<div class='stat'><b>%d</b>OTHER</div>", cnt_other);
    offset += snprintf(html + offset, cap - offset, "</div></header>");

    /* Toolbar */
    ENSURE_CAP(2048);
    offset += snprintf(html + offset, cap - offset,
        "<main>"
        "<div class='toolbar'>"
        "<input type='text' id='filter' placeholder='Filtrar por ruta o funcion (ej: mysql, usuario, GET)' oninput='applyFilter()' autocomplete='off'>"
        "<div class='filter-pills'>"
        "<span class='pill active' data-m='all' onclick='togglePill(this)'>ALL</span>"
        "<span class='pill' data-m='get' onclick='togglePill(this)'>GET</span>"
        "<span class='pill' data-m='post' onclick='togglePill(this)'>POST</span>"
        "<span class='pill' data-m='put' onclick='togglePill(this)'>PUT</span>"
        "<span class='pill' data-m='delete' onclick='togglePill(this)'>DELETE</span>"
        "<span class='pill' data-m='patch' onclick='togglePill(this)'>PATCH</span>"
        "</div>"
        "<button class='btn btn-try' style='background:#3b4151' onclick='expandAll(true)'>Expand all</button>"
        "<button class='btn btn-try' style='background:#888' onclick='expandAll(false)'>Collapse all</button>"
        "</div>");

    if (route_count == 0) {
        ENSURE_CAP(512);
        offset += snprintf(html + offset, cap - offset,
            "<div class='empty'>No hay rutas registradas. Crea archivos <code>.te</code> con bloques <code>endpoint { }</code> en <code>/app/apis/</code>.</div>");
    } else {
        /* Render grouped by prefix. arr is already sorted by route_path so
         * routes that share a prefix come together. */
        int endpoint_id = 0;
        int i = 0;
        while (i < route_count) {
            char group[256];
            route_group_key(arr[i]->route_path, group, sizeof(group));
            /* find end of this group */
            int j = i;
            while (j < route_count) {
                char g2[256];
                route_group_key(arr[j]->route_path, g2, sizeof(g2));
                if (strcmp(group, g2) != 0) break;
                j++;
            }
            int gcount = j - i;
            ENSURE_CAP(1024 + strlen(group) * 2);
            offset += snprintf(html + offset, cap - offset,
                "<div class='group' data-group='%s'>"
                "<div class='group-head' onclick='this.parentNode.classList.toggle(\"collapsed\")'>"
                "<h2>%s</h2>"
                "<span class='count-chip'>%d</span>"
                "<span class='toggle'>&#9662;</span>"
                "</div>"
                "<div class='group-body'>",
                group, group, gcount);

            for (int k = i; k < j; k++) {
                RouteEntry *r = arr[k];
                const char *method = r->http_method ? r->http_method : "GET";
                char method_lower[16];
                int mi;
                for (mi = 0; method[mi] && mi < (int)sizeof(method_lower) - 1; mi++)
                    method_lower[mi] = (char)tolower((unsigned char)method[mi]);
                method_lower[mi] = 0;

                const char *rtype = r->response_type ? r->response_type : "json";
                int has_body = (!strcmp(method, "POST") || !strcmp(method, "PUT") || !strcmp(method, "PATCH"));

                /* Build colorized route HTML highlighting {param} segments. */
                char route_html[1024];
                {
                    int ro = 0;
                    const char *rp = r->route_path ? r->route_path : "";
                    int in_param = 0;
                    while (*rp && ro < (int)sizeof(route_html) - 32) {
                        if (*rp == '{') {
                            ro += snprintf(route_html + ro, sizeof(route_html) - ro, "<span class='param'>{");
                            in_param = 1;
                            rp++;
                        } else if (*rp == '}' && in_param) {
                            ro += snprintf(route_html + ro, sizeof(route_html) - ro, "}</span>");
                            in_param = 0;
                            rp++;
                        } else {
                            route_html[ro++] = *rp++;
                        }
                    }
                    if (in_param) ro += snprintf(route_html + ro, sizeof(route_html) - ro, "</span>");
                    route_html[ro] = 0;
                }

                /* Collect path params from route_path */
                char params_html[2048] = {0};
                {
                    int po = 0;
                    const char *rp = r->route_path ? r->route_path : "";
                    while (*rp) {
                        if (*rp == '{') {
                            const char *e = strchr(rp, '}');
                            if (!e) break;
                            int nl = (int)(e - rp - 1);
                            if (nl > 0 && nl < 64 && po + 256 < (int)sizeof(params_html)) {
                                po += snprintf(params_html + po, sizeof(params_html) - po,
                                    "<label for='ep-%d-p-%.*s'>{%.*s}</label>"
                                    "<input type='text' id='ep-%d-p-%.*s' data-param='%.*s' placeholder='value for %.*s'>",
                                    endpoint_id, nl, rp + 1,
                                    nl, rp + 1,
                                    endpoint_id, nl, rp + 1,
                                    nl, rp + 1,
                                    nl, rp + 1);
                            }
                            rp = e + 1;
                        } else rp++;
                    }
                }

                ENSURE_CAP(4096 + strlen(route_html) + strlen(params_html));
                offset += snprintf(html + offset, cap - offset,
                    "<div class='endpoint' id='ep-%d' data-method='%s' data-route='%s' data-func='%s'>"
                    "<div class='ep-head' onclick='toggleEp(%d)'>"
                    "<span class='caret'>&#9656;</span>"
                    "<span class='method %s'>%s</span>"
                    "<span class='route'>%s</span>"
                    "<span class='func'>%s()</span>"
                    "<span class='badge %s'>%s</span>",
                    endpoint_id, method, r->route_path ? r->route_path : "",
                    r->function_name ? r->function_name : "",
                    endpoint_id,
                    method_lower, method,
                    route_html,
                    r->function_name ? r->function_name : "",
                    !strcmp(rtype, "xml") ? "xml" : "json",
                    !strcmp(rtype, "xml") ? "XML" : "JSON");
                if (r->cache_ttl > 0) {
                    offset += snprintf(html + offset, cap - offset,
                        "<span class='badge cache'>cache %ds</span>", r->cache_ttl);
                }
                offset += snprintf(html + offset, cap - offset,
                    "</div>"
                    "<div class='ep-body'>");
                if (params_html[0]) {
                    offset += snprintf(html + offset, cap - offset,
                        "<label>Path Parameters</label>"
                        "<div class='params-grid'>%s</div>", params_html);
                }
                if (has_body) {
                    offset += snprintf(html + offset, cap - offset,
                        "<label for='ep-%d-body'>Request Body (JSON)</label>"
                        "<textarea id='ep-%d-body' placeholder='{\\n  \"key\": \"value\"\\n}'></textarea>",
                        endpoint_id, endpoint_id);
                }
                offset += snprintf(html + offset, cap - offset,
                    "<div class='ep-actions'>"
                    "<button class='btn btn-try' id='ep-%d-btn' onclick='tryEp(%d)'>Execute</button>"
                    "<button class='btn btn-copy' id='ep-%d-copy'>Copy</button>"
                    "<span class='status-pill' id='ep-%d-status'></span>"
                    "<span class='exec-time' id='ep-%d-time'></span>"
                    "</div>"
                    "<pre class='response' id='ep-%d-resp'></pre>"
                    "</div>"
                    "</div>",
                    endpoint_id, endpoint_id, endpoint_id, endpoint_id, endpoint_id, endpoint_id);
                endpoint_id++;
            }
            ENSURE_CAP(64);
            offset += snprintf(html + offset, cap - offset, "</div></div>");
            i = j;
        }
    }

    if (arr) free(arr);

    /* JS block */
    ENSURE_CAP(4096);
    offset += snprintf(html + offset, cap - offset,
        "</main>"
        "<footer>TypeEasy Embedded API &middot; %d endpoint(s) &middot; <a href='/' style='color:#888'>refresh</a></footer>"
        "<script>"
        "let activeMethods=new Set(['all']);"
        "function togglePill(el){"
        " const m=el.dataset.m;"
        " const pills=document.querySelectorAll('.pill');"
        " if(m==='all'){activeMethods=new Set(['all']);pills.forEach(p=>p.classList.toggle('active',p.dataset.m==='all'));}"
        " else{activeMethods.delete('all');pills[0].classList.remove('active');"
        "  if(activeMethods.has(m)){activeMethods.delete(m);el.classList.remove('active');}else{activeMethods.add(m);el.classList.add('active');}"
        "  if(activeMethods.size===0){activeMethods.add('all');pills[0].classList.add('active');}}"
        " applyFilter();"
        "}"
        "function applyFilter(){"
        " const q=(document.getElementById('filter').value||'').toLowerCase().trim();"
        " document.querySelectorAll('.endpoint').forEach(ep=>{"
        "  const m=(ep.dataset.method||'').toLowerCase();"
        "  const r=(ep.dataset.route||'').toLowerCase();"
        "  const f=(ep.dataset.func||'').toLowerCase();"
        "  const matchM=activeMethods.has('all')||activeMethods.has(m);"
        "  const matchQ=!q||r.includes(q)||f.includes(q)||m.includes(q);"
        "  ep.classList.toggle('hidden',!(matchM&&matchQ));"
        " });"
        " document.querySelectorAll('.group').forEach(g=>{"
        "  const any=g.querySelectorAll('.endpoint:not(.hidden)').length>0;"
        "  g.classList.toggle('hidden',!any);"
        "  const chip=g.querySelector('.count-chip');"
        "  if(chip){chip.textContent=g.querySelectorAll('.endpoint:not(.hidden)').length;}"
        " });"
        "}"
        "function toggleEp(id){document.getElementById('ep-'+id).classList.toggle('open');}"
        "function expandAll(open){document.querySelectorAll('.endpoint').forEach(ep=>ep.classList.toggle('open',open));document.querySelectorAll('.group').forEach(g=>g.classList.toggle('collapsed',!open));}"
        "async function tryEp(id){"
        " const ep=document.getElementById('ep-'+id);"
        " const method=ep.dataset.method;"
        " let route=ep.dataset.route;"
        " ep.querySelectorAll('[data-param]').forEach(inp=>{"
        "  const v=encodeURIComponent(inp.value||'');"
        "  route=route.replace('{'+inp.dataset.param+'}',v||('{'+inp.dataset.param+'}'));"
        " });"
        " const btn=document.getElementById('ep-'+id+'-btn');"
        " const resp=document.getElementById('ep-'+id+'-resp');"
        " const tspan=document.getElementById('ep-'+id+'-time');"
        " const cbtn=document.getElementById('ep-'+id+'-copy');"
        " const status=document.getElementById('ep-'+id+'-status');"
        " btn.disabled=true;btn.textContent='Loading...';"
        " resp.style.display='block';resp.classList.remove('error');resp.textContent='Ejecutando...';"
        " tspan.textContent='';cbtn.style.display='none';status.style.display='none';"
        " const bodyEl=document.getElementById('ep-'+id+'-body');"
        " const opts={method:method,headers:{}};"
        " if(bodyEl&&bodyEl.value.trim()){opts.body=bodyEl.value;opts.headers['Content-Type']='application/json';}"
        " const t0=performance.now();"
        " try{"
        "  const r=await fetch(route,opts);"
        "  const dt=(performance.now()-t0).toFixed(0);"
        "  const xtime=r.headers.get('X-Execution-Time');"
        "  const txt=await r.text();"
        "  let out=txt;try{out=JSON.stringify(JSON.parse(txt),null,2);}catch(e){}"
        "  resp.textContent=out;"
        "  status.style.display='inline-block';status.className='status-pill '+(r.ok?'ok':'bad');status.textContent=r.status+' '+r.statusText;"
        "  tspan.textContent=(xtime?('server '+xtime+'s | '):'')+'wall '+dt+'ms';"
        "  cbtn.style.display='inline-block';"
        "  cbtn.onclick=()=>{navigator.clipboard.writeText(out).then(()=>{const o=cbtn.textContent;cbtn.textContent='Copied';setTimeout(()=>cbtn.textContent=o,1400);});};"
        " }catch(err){resp.classList.add('error');resp.textContent='Error: '+err.message;status.style.display='inline-block';status.className='status-pill bad';status.textContent='NETWORK';}"
        " btn.disabled=false;btn.textContent='Execute';"
        "}"
        "</script>"
        "</body></html>", route_count);

    mg_write(conn, html, offset);
    free(html);
#undef ENSURE_CAP
    return 1;
}

/* === Worker: serves HTTP. Same as old main(). On SIGTERM/SIGINT it drains
 * via mg_stop and exits. Supervisor spawns one of these. ===
 * If enable_debug != 0, opens the debugger listener (only slot 0). */
static int run_worker_ext(int enable_debug) {
    struct mg_context *ctx;
    /* Build civetweb listening_ports spec: "HOST:PORT" or just "PORT". */
    char port_spec[160];
    if (g_host[0]) snprintf(port_spec, sizeof(port_spec), "%s:%d", g_host, g_port);
    else           snprintf(port_spec, sizeof(port_spec), "%d", g_port);
    const char *options[] = {"listening_ports", port_spec, NULL};

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
    printf("[WORKER %d] listo en %s%s\n", (int)getpid(), port_spec,
           enable_debug ? " [DEBUG ON]" : ""); fflush(stdout);

    mg_set_request_handler(ctx, "/",      manejadorRaiz,        NULL);
    mg_set_request_handler(ctx, "/api/**", manejadorApiDinamico, NULL);

    /* Register native WebSocket handlers for any [WebSocket("/path")] methods. */
    te_ws_register_routes(ctx);

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
    struct stat st;
    if (g_single_api_file[0]) {
        if (stat(g_single_api_file, &st) == 0) track_script(g_single_api_file, st.st_mtime);
        return;
    }
    char ls_cmd[1280];
    snprintf(ls_cmd, sizeof(ls_cmd), "ls -1 %s/*.te 2>/dev/null", g_apis_dir);
    FILE *fp = popen(ls_cmd, "r");
    if (!fp) return;
    char line[1035];
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

static void print_usage(const char *prog) {
    fprintf(stderr,
        "TypeEasy API server\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --api <file>       Serve a single .te endpoint file\n"
        "                     (mutually exclusive with --api-dir)\n"
        "  --api-dir <path>   Directory containing .te endpoint files\n"
        "                     (aliases: --api-folder)\n"
        "                     (default: $TYPEEASY_APIS_DIR or /app/apis)\n"
        "  --port <n>         HTTP listen port\n"
        "                     (default: $TYPEEASY_PORT or 8080)\n"
        "  --host <addr>      Bind address, e.g. 127.0.0.1 or 0.0.0.0\n"
        "                     (default: $TYPEEASY_HOST or all interfaces)\n"
        "  --workers <n>      Worker processes\n"
        "                     (default: $TYPEEASY_WORKERS or auto-detect)\n"
        "  --hotreload        Reload .te files on change (dev mode)\n"
        "                     (default: $TYPEEASY_HOTRELOAD)\n"
        "  -h, --help         Show this help and exit\n"
        "  -v, --version      Show version and exit\n"
        "\n"
        "Examples:\n"
        "  %s                                 # defaults (Docker-style)\n"
        "  %s --api-dir /opt/mi-app/apis --port 9001\n"
        "  %s --api endpoint.te --port 9002    # single-file mode\n"
        "  %s --api-dir ./apis --hotreload     # dev local\n",
        prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    /* Save real argv globally so supervisor's execv preserves all flags. */
    g_main_argv = argv;

    /* --- Parse CLI args (precedence: flag > env > default) --- */
    const char *cli_apis_dir = NULL;
    const char *cli_api_file = NULL;
    const char *cli_host     = NULL;
    int  cli_port    = -1;
    int  cli_workers = -1;
    int  cli_hot     = -1;  /* -1 = unset */

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "-h") || !strcmp(a, "--help"))    { print_usage(argv[0]); return 0; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("typeeasy api-server (build " __DATE__ ")\n"); return 0; }
        else if (!strcmp(a, "--hotreload"))                   { cli_hot = 1; }
        else if ((!strcmp(a, "--api") || !strcmp(a, "--api-file")) && i + 1 < argc) { cli_api_file = argv[++i]; }
        else if ((!strcmp(a, "--api-dir") || !strcmp(a, "--api-folder")) && i + 1 < argc) { cli_apis_dir = argv[++i]; }
        else if (!strcmp(a, "--port")    && i + 1 < argc)     { cli_port     = atoi(argv[++i]); }
        else if (!strcmp(a, "--host")    && i + 1 < argc)     { cli_host     = argv[++i]; }
        else if (!strcmp(a, "--workers") && i + 1 < argc)     { cli_workers  = atoi(argv[++i]); }
        else {
            fprintf(stderr, "[ERROR] Unknown argument: %s\n", a);
            print_usage(argv[0]);
            return 2;
        }
    }

    /* --- Resolve single-file vs apis_dir --- */
    if (cli_api_file && cli_apis_dir) {
        fprintf(stderr, "[ERROR] --api y --api-dir son mutuamente exclusivos.\n");
        return 2;
    }
    if (cli_api_file) {
        snprintf(g_single_api_file, sizeof(g_single_api_file), "%s", cli_api_file);
        /* Derive parent dir for any consumers that still read g_apis_dir. */
        const char *slash = strrchr(cli_api_file, '/');
#ifdef _WIN32
        const char *bslash = strrchr(cli_api_file, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        if (slash) {
            size_t n = (size_t)(slash - cli_api_file);
            if (n >= sizeof(g_apis_dir)) n = sizeof(g_apis_dir) - 1;
            memcpy(g_apis_dir, cli_api_file, n); g_apis_dir[n] = '\0';
        } else {
            snprintf(g_apis_dir, sizeof(g_apis_dir), "%s", ".");
        }
    } else {
        const char *env_dir = getenv("TYPEEASY_APIS_DIR");
        if      (cli_apis_dir) snprintf(g_apis_dir, sizeof(g_apis_dir), "%s", cli_apis_dir);
        else if (env_dir)      snprintf(g_apis_dir, sizeof(g_apis_dir), "%s", env_dir);
        else                   snprintf(g_apis_dir, sizeof(g_apis_dir), "%s", "/app/apis");
    }

    /* --- Resolve port --- */
    const char *env_port = getenv("TYPEEASY_PORT");
    if      (cli_port > 0)         g_port = cli_port;
    else if (env_port && *env_port) g_port = atoi(env_port);
    /* else keep default 8080 from declaration */

    /* --- Resolve host --- */
    const char *env_host = getenv("TYPEEASY_HOST");
    if      (cli_host)             snprintf(g_host, sizeof(g_host), "%s", cli_host);
    else if (env_host && *env_host) snprintf(g_host, sizeof(g_host), "%s", env_host);
    /* else empty => civetweb listens on all interfaces */

    /* --- Resolve hot-reload --- */
    if (cli_hot >= 0) {
        g_hotreload = cli_hot;
    } else {
        const char *hr = getenv("TYPEEASY_HOTRELOAD");
        g_hotreload = (hr && (*hr == '1' || *hr == 't' || *hr == 'T' || *hr == 'y' || *hr == 'Y')) ? 1 : 0;
    }

    /* --- Workers: CLI > env (handled by detect_num_workers) > auto --- */
    if (cli_workers > 0) {
        char buf[16]; snprintf(buf, sizeof(buf), "%d", cli_workers);
        setenv("TYPEEASY_WORKERS", buf, 1);
    }
    g_num_workers = detect_num_workers();

    if (g_single_api_file[0]) {
        printf("[CONFIG] api_file=%s  port=%d  host=%s  workers=%d  hotreload=%s\n",
               g_single_api_file, g_port, g_host[0] ? g_host : "(all)",
               g_num_workers, g_hotreload ? "ON" : "OFF");
    } else {
        printf("[CONFIG] apis_dir=%s  port=%d  host=%s  workers=%d  hotreload=%s\n",
               g_apis_dir, g_port, g_host[0] ? g_host : "(all)",
               g_num_workers, g_hotreload ? "ON" : "OFF");
    }
    fflush(stdout);

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