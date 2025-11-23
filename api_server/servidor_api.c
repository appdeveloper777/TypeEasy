#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
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

    fprintf(stderr, "[LOG] manejadorApiDinamico: URI recibida: %s\n", uri);

    // Buscar en la tabla de rutas dinámica
    RouteEntry *entry = global_routes;
    while (entry) {
        if (strcmp(uri, entry->route_path) == 0) {
            // Encontrado! Invocar función directamente usando TypeEasy embebido
            fprintf(stderr, "[LOG] Invocando función: %s desde %s\n", entry->function_name, entry->script_path);
            
            // CAMBIO PRINCIPAL: Usar typeeasy_invoke_with_script en lugar de typeeasy_invoke
            char *result = typeeasy_invoke_with_script(g_typeeasy_ctx, entry->script_path, entry->function_name, NULL);
            
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
                char *xml_tag_start = strchr(result, '<');
                if (xml_tag_start && xml_tag_start[1] != '!' && xml_tag_start[1] != '?') {
                    // Encontrada una etiqueta de apertura que no es comentario
                    actual_content = xml_tag_start;
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
            if (actual_content[0] == '<') {
                content_type = "application/xml";
            }

            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "\r\n"
                      "%s",
                      content_type, (int)strlen(actual_content), actual_content);
            
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
        "body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }"
        "h1 { color: #333; }"
        ".container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        ".endpoint { background: #f9f9f9; padding: 15px; margin: 10px 0; border-left: 4px solid #4CAF50; border-radius: 4px; }"
        ".method { display: inline-block; padding: 4px 12px; border-radius: 4px; font-weight: bold; color: white; margin-right: 10px; }"
        ".get { background: #61affe; }"
        ".post { background: #49cc90; }"
        ".route { font-family: monospace; font-size: 16px; color: #333; }"
        ".json-badge { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 11px; font-weight: bold; background: #f0ad4e; color: white; margin-left: 8px; }"
        ".xml-badge { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 11px; font-weight: bold; background: #5bc0de; color: white; margin-left: 8px; }"
        ".function { color: #666; font-size: 14px; margin-top: 5px; }"
        ".count { color: #666; font-size: 14px; }"
        ".try-btn { background: #4CAF50; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; margin-top: 10px; font-size: 14px; }"
        ".try-btn:hover { background: #45a049; }"
        ".response { margin-top: 10px; padding: 10px; background: #263238; color: #aed581; font-family: monospace; font-size: 12px; border-radius: 4px; display: none; white-space: pre-wrap; max-height: 300px; overflow-y: auto; }"
        ".loading { color: #ffa726; }"
        ".embedded-badge { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 11px; font-weight: bold; background: #4CAF50; color: white; margin-left: 8px; }"
        "</style>"
        "<script>"
        "async function tryEndpoint(route, btnId) {"
        "  const btn = document.getElementById(btnId);"
        "  const responseDiv = document.getElementById(btnId + '-response');"
        "  btn.disabled = true;"
        "  btn.textContent = 'Loading...';"
        "  responseDiv.style.display = 'block';"
        "  responseDiv.className = 'response loading';"
        "  responseDiv.textContent = 'Ejecutando...';"
        "  try {"
        "    const response = await fetch(route);"
        "    const data = await response.text();"
        "    responseDiv.className = 'response';"
        "    try {"
        "      const json = JSON.parse(data);"
        "      responseDiv.textContent = JSON.stringify(json, null, 2);"
        "    } catch(e) {"
        "      responseDiv.textContent = data;"
        "    }"
        "  } catch(error) {"
        "    responseDiv.className = 'response';"
        "    responseDiv.style.color = '#ef5350';"
        "    responseDiv.textContent = 'Error: ' + error.message;"
        "  }"
        "  btn.disabled = false;"
        "  btn.textContent = 'Try it out';"
        "}"
        "</script>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>🚀 TypeEasy API Documentation</h1>"
        "<p>El servidor está funcionando correctamente. <span class='embedded-badge'>EMBEDDED MODE</span></p>");
    
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
                "<button class='try-btn' id='btn-%d' onclick='tryEndpoint(\"%s\", \"btn-%d\")'>Try it out</button>"
                "<div class='response' id='btn-%d-response'></div>"
                "</div>",
                entry->route_path,
                badge_class,
                badge_text,
                entry->function_name,
                endpoint_id,
                entry->route_path,
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