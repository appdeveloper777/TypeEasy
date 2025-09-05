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

// Manejador para la ruta /api/productos
static int manejadorProductos(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    // --- Ejecutar el script de TypeEasy ---
    //const char* comando = "./typeeasy apis/scripts/get_productos.te";
    //char* respuesta_json = ejecutarScript(comando);

    const char* respuesta_json =
        "[\n"
        "  {\n"
        "    \"id\": 101,\n"
        "    \"nombre\": \"Teclado Mecánico RGB\",\n"
        "    \"precio\": 79.99,\n"
        "    \"stock\": 50\n"
        "  },\n"
        "  {\n"
        "    \"id\": 102,\n"
        "    \"nombre\": \"Mouse Gamer Inalámbrico\",\n"
        "    \"precio\": 49.50,\n"
        "    \"stock\": 120\n"
        "  },\n"
        "  {\n"
        "    \"id\": 205,\n"
        "    \"nombre\": \"Monitor Curvo 27 pulgadas\",\n"
        "    \"precio\": 299.00,\n"
        "    \"stock\": 35\n"
        "  }\n"
        "]";

    if (respuesta_json) {
        mg_send_http_ok(conn, "application/json", strlen(respuesta_json));
        mg_write(conn, respuesta_json, strlen(respuesta_json));
        // No se debe liberar 'respuesta_json' aquí porque ahora es una cadena literal (stack/static).
        // La llamada a free() solo es necesaria cuando la memoria es asignada dinámicamente
        // con malloc/calloc/realloc, como lo hacía la función 'ejecutarScript'.
        // free(respuesta_json);
    } else {
        mg_send_http_error(conn, 500, "Error al ejecutar el script del servidor.");
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
    mg_set_request_handler(ctx, "/", manejador_raiz, NULL);
    mg_set_request_handler(ctx, "/api/productos", manejadorProductos, NULL);

    // Espera indefinidamente (en un programa real, aquí iría un bucle o manejo de señales)
    while (1) {
        mg_sleep(1000);
    }

    // Detiene el servidor (este código no se alcanzará en este ejemplo simple)
    mg_stop(ctx);
    mg_exit_library();

    return 0;
}