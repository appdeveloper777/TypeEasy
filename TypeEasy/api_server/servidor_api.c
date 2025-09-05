#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "civetweb.h"

// Esta función ejecuta un comando en la consola y devuelve su salida.
// IMPORTANTE: La cadena devuelta debe ser liberada con free() por quien la llama.
char* ejecutarScript(const char* comando) {
    char buffer[256];
    char* resultado = NULL;    

#ifdef _WIN32
    FILE* pipe = _popen(comando, "r");
#else
    FILE* pipe = popen(comando, "r");
#endif

    if (!pipe) {
        fprintf(stderr, "Error al ejecutar el comando.\n");
        return NULL;    
    }

    // Usar un buffer temporal para acumular la salida
    size_t temp_capacity = 1024;
    resultado = (char*)malloc(temp_capacity);
    size_t resultado_size = 0;

    if (!resultado) {
        fprintf(stderr, "Error de asignación de memoria.\n");
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return NULL;
    }
    resultado[0] = '\0';

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t buffer_len = strlen(buffer);
        if (resultado_size + buffer_len + 1 > temp_capacity) {
            temp_capacity *= 2;
            char* new_temp = (char*)realloc(resultado, temp_capacity);
            if (!new_temp) {
                fprintf(stderr, "Error de reasignación de memoria.\n");
                free(resultado);
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
                return NULL;
            }
            resultado = new_temp;
        }
        memcpy(resultado + resultado_size, buffer, buffer_len);
        resultado_size += buffer_len;
    }
    resultado[resultado_size] = '\0';

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return resultado;
}

// Nuevo manejador genérico para todas las rutas de la API
static int apiRouterHandler(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    const char *uri = req_info->local_uri;

    // LOG: Imprimir la URI recibida para depuración.
    fprintf(stderr, "[LOG] apiRouterHandler: URI recibida: %s\n", uri);

    char comando[512];
    // Construimos el comando para ejecutar el enrutador, pasándole la URI solicitada.
    // CORRECCIÓN: Usar 'router.te' en lugar de 'get_productos.te' para que el enrutamiento funcione.
    snprintf(comando, sizeof(comando), "./typeeasy apis/router.te %s", uri);

    // LOG: Imprimir el comando que se va a ejecutar.
    fprintf(stderr, "[LOG] apiRouterHandler: Ejecutando comando: %s\n", comando);

    char* respuesta_json = ejecutarScript(comando);

    if (respuesta_json) {
        // LOG: Imprimir la respuesta JSON obtenida del script.
        fprintf(stderr, "[LOG] apiRouterHandler: Respuesta del script:\n---\n%s\n---\n", respuesta_json);
        mg_send_http_ok(conn, "application/json", strlen(respuesta_json));
        mg_write(conn, respuesta_json, strlen(respuesta_json));
        free(respuesta_json);
    } else {
        // LOG: Indicar que el script no devolvió ninguna respuesta.
        fprintf(stderr, "[LOG-ERROR] apiRouterHandler: El script no devolvió ninguna respuesta (respuesta_json es NULL).\n");
        mg_send_http_error(conn, 500, "Error al ejecutar el script del enrutador.");
    }

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
              "<p>Prueba el endpoint de la API en: <a href=\"/api/productos\">/api/productos</a></p>"
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

    // Inicia el servidor
    ctx = mg_start(NULL, NULL, options);
    if (ctx == NULL) {
        printf("Error al iniciar el servidor.\n");
        return 1;
    }

    printf("Servidor iniciado en http://localhost:8080\n");

    // --- ¡Paso Clave! Registra los manejadores para cada ruta ---
    mg_set_request_handler(ctx, "/", manejadorRaiz, NULL);
    // Registramos el manejador genérico para cualquier ruta que empiece con /api/
    mg_set_request_handler(ctx, "/api/**", apiRouterHandler, NULL);

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