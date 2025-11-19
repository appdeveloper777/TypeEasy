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
                while (*p != '"') p++;
                *p = 0; // Terminate route string
                char *route = strdup(route_start);
                p++; // Move past quote
                
                // Buscar function
                p = strstr(p, "\"function\":");
                if (p) {
                    p += 11;
                    while (*p == ' ' || *p == '"') p++;
                    char *func_start = p;
                    while (*p != '"') p++;
                    *p = 0;
                    char *func = strdup(func_start);
                    
                    // Registrar ruta
                    add_route(route, path, func);
                    
                    free(func);
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

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html; charset=utf-8\r\n"
              "Connection: close\r\n"
              "\r\n"
              "<html><body>"
              "<h1>¡TypeEasy APIs!</h1>"
              "<p>El servidor está funcionando correctamente.</p>"
              "<p>Rutas dinámicas cargadas.</p>"
              "</body></html>");
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
