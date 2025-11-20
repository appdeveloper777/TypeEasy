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

// Estructura para la tabla de rutas dinámica
typedef struct RouteEntry {
    char *route_path;
    char *script_path;
    char *function_name;
    struct RouteEntry *next;
} RouteEntry;

static RouteEntry *global_routes = NULL;

// Función para añadir una ruta a la tabla
void add_route(const char *route, const char *script, const char *func) {
    RouteEntry *entry = (RouteEntry*)malloc(sizeof(RouteEntry));
    entry->route_path = strdup(route);
    entry->script_path = strdup(script);
    entry->function_name = strdup(func);
    entry->next = global_routes;
    global_routes = entry;
    printf("[LOG] Ruta registrada: %s -> %s::%s\n", route, script, func);
}

// Función para descubrir rutas al inicio
void discover_routes() {
    printf("[LOG] Iniciando descubrimiento de rutas...\n");
    
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
        
        // path ya contiene la ruta completa (/app/apis/archivo.te)
        printf("[LOG] Descubriendo rutas en: %s\n", path);
        
        // Ejecutar typeeasy con --discover
        char command[512];
        snprintf(command, sizeof(command), "/app/typeeasy \"%s\" --discover 2>&1", path);
        
        FILE *cmd_fp = popen(command, "r");
        if (cmd_fp) {
            char json_output[4096] = {0};
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), cmd_fp) != NULL) {
                strcat(json_output, buffer);
            }
            pclose(cmd_fp);
            
            printf("[LOG] Salida de --discover para %s: %s\n", path, json_output);
            
            // Parsear JSON simple (ej: [{"route": "/api/x", ...}])
            char *p = json_output;
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
                        
                        // Registrar ruta
                        add_route(route, path, func);
                        
                        free(func);
                    }
                }
                free(route);
            }
        }
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
            // Encontrado!
            char command[512];
            // Usar --invoke para ejecutar la función específica
            snprintf(command, sizeof(command), "/app/typeeasy \"%s\" --invoke %s 2>&1", entry->script_path, entry->function_name);
            
            fprintf(stderr, "[LOG] Ejecutando: %s\n", command);
            
            FILE *fp = popen(command, "r");
            if (!fp) {
                mg_send_http_error(conn, 500, "Error interno al ejecutar script");
                return 1;
            }

            char response_buffer[8192] = {0};
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                strcat(response_buffer, buffer);
            }
            int ret = pclose(fp);

            if (ret != 0) {
                 mg_send_http_error(conn, 500, "Error en la ejecución del script TypeEasy");
                 return 1;
            }

            // Determinar Content-Type (simple heurística)
            const char *content_type = "application/json";
            if (response_buffer[0] == '<') {
                content_type = "application/xml";
            }

            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "\r\n"
                      "%s",
                      content_type, (int)strlen(response_buffer), response_buffer);
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

// Nuevo manejador para la ruta raíz ("/")
static int manejadorRaiz(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

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
        ".function { color: #666; font-size: 14px; margin-top: 5px; }"
        ".count { color: #666; font-size: 14px; }"
        ".try-btn { background: #4CAF50; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; margin-top: 10px; font-size: 14px; }"
        ".try-btn:hover { background: #45a049; }"
        ".response { margin-top: 10px; padding: 10px; background: #263238; color: #aed581; font-family: monospace; font-size: 12px; border-radius: 4px; display: none; white-space: pre-wrap; max-height: 300px; overflow-y: auto; }"
        ".loading { color: #ffa726; }"
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
        "<p>El servidor está funcionando correctamente.</p>");
    
    // Contar rutas
    int route_count = 0;
    RouteEntry *entry = global_routes;
    while (entry) {
        route_count++;
        entry = entry->next;
    }
    
    // Agregar 1 por el endpoint MySQL nativo
    route_count++;
    
    offset += snprintf(html + offset, sizeof(html) - offset,
        "<p class='count'><strong>%d</strong> ruta(s) dinámica(s) cargada(s):</p>",
        route_count);
    
    // Listar todas las rutas
    entry = global_routes;
    int endpoint_id = 0;
    if (entry == NULL) {
        offset += snprintf(html + offset, sizeof(html) - offset,
            "<p>No hay rutas descubiertas. Crea archivos .te con bloques 'endpoint { }' en typeeasycode/apis/</p>");
    } else {
        while (entry && offset < sizeof(html) - 1000) {
            offset += snprintf(html + offset, sizeof(html) - offset,
                "<div class='endpoint'>"
                "<span class='method get'>GET</span>"
                "<span class='route'>%s</span>"
                "<div class='function'>→ %s()</div>"
                "<button class='try-btn' id='btn-%d' onclick='tryEndpoint(\"%s\", \"btn-%d\")'>Try it out</button>"
                "<div class='response' id='btn-%d-response'></div>"
                "</div>",
                entry->route_path,
                entry->function_name,
                endpoint_id,
                entry->route_path,
                endpoint_id,
                endpoint_id);
            entry = entry->next;
            endpoint_id++;
        }
    }
    
    // Agregar endpoint MySQL nativo
    offset += snprintf(html + offset, sizeof(html) - offset,
        "<div class='endpoint'>"
        "<span class='method get'>GET</span>"
        "<span class='route'>/api/mysql/usuarios</span>"
        "<div class='function'>→ MySQL Native (C)</div>"
        "<button class='try-btn' id='btn-%d' onclick='tryEndpoint(\"/api/mysql/usuarios\", \"btn-%d\")'>Try it out</button>"
        "<div class='response' id='btn-%d-response'></div>"
        "</div>",
        endpoint_id,
        endpoint_id,
        endpoint_id);
    
    offset += snprintf(html + offset, sizeof(html) - offset,
        "<hr>"
        "<p style='color: #999; font-size: 12px;'>TypeEasy Dynamic API Server - Endpoints auto-discovered from .te files</p>"
        "</div>"
        "</body>"
        "</html>");
    
    mg_write(conn, html, offset);
    return 1;
}

// Handler para endpoint MySQL /api/mysql/usuarios
static int manejadorMySQLUsuarios(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    
    // Inicializar conexión MySQL
    MYSQL *mysql_conn = mysql_init(NULL);
    if (!mysql_conn) {
        mg_send_http_error(conn, 500, "MySQL init failed");
        return 1;
    }
    
    // Conectar a MySQL
    if (!mysql_real_connect(mysql_conn, "mysql", "root", "rootpassword", "test_db", 3306, NULL, 0)) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "MySQL connection failed: %s", mysql_error(mysql_conn));
        mysql_close(mysql_conn);
        mg_send_http_error(conn, 500, error_msg);
        return 1;
    }
    
    // Configurar UTF-8 para caracteres acentuados
    mysql_set_character_set(mysql_conn, "utf8mb4");
    
    // Ejecutar query
    if (mysql_query(mysql_conn, "SELECT * FROM usuarios")) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "MySQL query failed: %s", mysql_error(mysql_conn));
        mysql_close(mysql_conn);
        mg_send_http_error(conn, 500, error_msg);
        return 1;
    }
    
    // Obtener resultados
    MYSQL_RES *result = mysql_store_result(mysql_conn);
    if (!result) {
        mysql_close(mysql_conn);
        mg_send_http_error(conn, 500, "Failed to store result");
        return 1;
    }
    
    // Convertir a JSON
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    
    char json_buffer[65536];
    int offset = 0;
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "[");
    
    MYSQL_ROW row;
    int first_row = 1;
    while ((row = mysql_fetch_row(result))) {
        if (!first_row) {
            offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, ",");
        }
        first_row = 0;
        
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "{");
        for (int i = 0; i < num_fields; i++) {
            if (i > 0) {
                offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, ",");
            }
            offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "\"%s\":", fields[i].name);
            
            if (row[i]) {
                if (fields[i].type == MYSQL_TYPE_STRING || 
                    fields[i].type == MYSQL_TYPE_VAR_STRING ||
                    fields[i].type == MYSQL_TYPE_VARCHAR) {
                    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "\"%s\"", row[i]);
                } else {
                    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "%s", row[i]);
                }
            } else {
                offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "null");
            }
        }
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "}");
    }
    
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "]");
    
    // Liberar recursos
    mysql_free_result(result);
    mysql_close(mysql_conn);
    
    // Enviar respuesta
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "\r\n"
              "%s",
              (int)strlen(json_buffer), json_buffer);
    
    return 1;
}

int main(void) {
    struct mg_context *ctx;
    // Opciones del servidor. Escuchará en el puerto 8080.
    const char *options[] = {"listening_ports", "8080", NULL};

    // Registrar el manejador de señales para Ctrl+C
    signal(SIGINT, signal_handler);

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

    printf("Servidor iniciado en http://localhost:8080\n");

    // --- ¡Paso Clave! Registra los manejadores para cada ruta ---
    mg_set_request_handler(ctx, "/", manejadorRaiz, NULL);
    // mg_set_request_handler(ctx, "/api/mysql/usuarios", manejadorMySQLUsuarios, NULL); // Comentado - usar TypeEasy dinámico
    mg_set_request_handler(ctx, "/api/**", manejadorApiDinamico, NULL);

    // Bucle principal del servidor. Se ejecuta hasta que exit_flag sea 1.
    while (exit_flag == 0) {
        // Pausa de 1 segundo. Usamos la función correcta para cada sistema operativo.
#if defined(_WIN32)
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    // Detiene el servidor de forma segura.
    printf("\nDeteniendo el servidor...\n");
    mg_stop(ctx);
    mg_exit_library();

    return 0;
}
