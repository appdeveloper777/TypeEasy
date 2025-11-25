#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "civetweb.h"
#include <mysql/mysql.h>

// Incluir el encabezado de TypeEasy
#include "typeeasy.h"

// Estructura para la tabla de rutas dinámica
typedef struct RouteEntry {
    char *route_path;
    char *script_path;
    char *function_name;
    char *response_type;  // "json" or "xml"
    struct RouteEntry *next;
} RouteEntry;

static RouteEntry *global_routes = NULL;
static TypeEasyContext *g_typeeasy_ctx = NULL;

// Función para añadir una ruta a la tabla
void add_route(const char *route, const char *script, const char *func, const char *resp_type) {
    RouteEntry *entry = (RouteEntry*)malloc(sizeof(RouteEntry));
    entry->route_path = strdup(route);
    entry->script_path = strdup(script);
    entry->function_name = strdup(func);
    entry->response_type = strdup(resp_type ? resp_type : "json");
    entry->next = global_routes;
    global_routes = entry;
    printf("[LOG] Ruta registrada: %s -> %s() [%s]\n", route, func, entry->response_type);
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
                    
                    // Registrar ruta
                    add_route(route, path, func, resp_type);
                    
                    if (resp_pos && resp_type != "json") free((char*)resp_type);
                    free(func);
                }
            }
            free(route);
        }
        
        free(discover_result);
    }
    pclose(fp);
}

static int manejadorApiDinamico(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; 

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    const char *uri = req_info->local_uri;

    fprintf(stderr, "[LOG] manejadorApiDinamico: URI recibida: %s\n", uri); fflush(stderr);

    // Buscar en la tabla de rutas dinámica
    RouteEntry *entry = global_routes;
    while (entry) {
        if (strcmp(uri, entry->route_path) == 0) {
            // Encontrado! Invocar función directamente usando TypeEasy embebido
            fprintf(stderr, "[LOG] Invocando función: %s desde %s\n", entry->function_name, entry->script_path); fflush(stderr);
            
            // Medir tiempo de ejecución
            clock_t start_time = clock();
            fprintf(stderr, "[DEBUG] Starting execution timer\n"); fflush(stderr);
            
            // CAMBIO PRINCIPAL: Usar typeeasy_invoke_with_script en lugar de typeeasy_invoke
            char *result = typeeasy_invoke_with_script(g_typeeasy_ctx, entry->script_path, entry->function_name, NULL);
            fprintf(stderr, "[DEBUG] Execution finished, result: %p\n", result); fflush(stderr);
            
            clock_t end_time = clock();
            double time_taken = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
            
            if (!result) {
                mg_send_http_error(conn, 500, "Error interno al ejecutar función TypeEasy");
                return 1;
            }

            // Extraer solo contenido XML/JSON, filtrando logs de debug
            char *actual_content = result;
            
            // Buscar declaración XML
            char *xml_start = strstr(result, "<?xml");
            if (xml_start) {
                actual_content = xml_start;
            } else {
                // Buscar etiquetas XML (ej: <Usuarios>, <Usuario>, etc.)
                // Ignorar espacios en blanco iniciales
                char *p = result;
                while (*p && isspace((unsigned char)*p)) p++;
                
                if (*p == '<') {
                     // Es XML si empieza con < y no es un comentario obvio (aunque <?xml ya lo cubrimos)
                     actual_content = p;
                } else {
                    // Buscar inicio de array JSON
                    char *json_array_start = strstr(result, "[{");
                    if (json_array_start) {
                        actual_content = json_array_start;
                    } else {
                        // Buscar inicio de objeto JSON
                        char *json_obj_start = strstr(result, "{\"");
                        if (json_obj_start) {
                            actual_content = json_obj_start;
                        }
                    }
                }
            }

            // Determinar Content-Type (heurística simple)
            const char *content_type = "application/json";
            // Check if it looks like XML (starts with <)
            // We need to be careful about whitespace
            char *check_p = actual_content;
            while (*check_p && isspace((unsigned char)*check_p)) check_p++;
            
            if (*check_p == '<') {
                content_type = "application/xml";
            }

            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "X-Execution-Time: %.4f s\r\n"
                      "\r\n"
                      "%s",
                      content_type, (int)strlen(actual_content), time_taken, actual_content);
            
            free(result);
            return 1;
        }
        entry = entry->next;
    }
    
    // No encontrado
    mg_send_http_error(conn, 404, "Endpoint no encontrado");
    return 1;
}

// Variable global para indicar que debemos salir.
volatile int exit_flag = 0;

// Manejador de señales para detener el servidor de forma segura con Ctrl+C.
void signal_handler(int sig_num) {
    (void)sig_num;
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

int main(void) {
    struct mg_context *ctx;
    // Opciones del servidor. Escuchará en el puerto 8080.
    const char *options[] = {"listening_ports", "8080", NULL};

    // Registrar el manejador de señales para Ctrl+C
    signal(SIGINT, signal_handler);
    
    // Registrar cleanup al salir
    atexit(cleanup);

    // Inicia la biblioteca CivetWeb
    mg_init_library(0);

    // Iniciar descubrimiento de rutas dinámicas
    discover_routes();

    // Inicia el servidor
    ctx = mg_start(NULL, NULL, options);
    if (ctx == NULL) {
        printf("Error al iniciar el servidor.\n");
        return 1;
    }

    printf("Servidor iniciado en http://localhost:8080 (Modo Embebido TypeEasy)\n");

    // Registrar manejadores
    mg_set_request_handler(ctx, "/", manejadorRaiz, NULL);
    mg_set_request_handler(ctx, "/api/**", manejadorApiDinamico, NULL);

    // Bucle principal del servidor
    while (exit_flag == 0) {
#if defined(_WIN32)
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    // Detiene el servidor de forma segura
    printf("\nDeteniendo el servidor...\n");
    mg_stop(ctx);
    mg_exit_library();

    return 0;
}