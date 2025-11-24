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

    int status;
#ifdef _WIN32
    status = _pclose(pipe);
#else
    status = pclose(pipe);
#endif

    // Comprueba si el script se ejecutó correctamente.
    if (status != 0) {
        fprintf(stderr, "[LOG-ERROR] ejecutarScript: El script terminó con un código de estado no cero: %d\n", status);
        // La salida puede estar incompleta o contener mensajes de error, así que la descartamos.
        free(resultado);
        return NULL;
    }

    return resultado;
}

// Manejador para la ruta /api/productos
static int manejadorProductos(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    const char *uri = req_info->local_uri;

    // LOG: Imprimir la URI recibida para depuración.
    fprintf(stderr, "[LOG] manejadorProductos: URI recibida: %s\n", uri);

    char comando[512];
    // Construimos el comando para ejecutar el enrutador, pasándole la URI solicitada.
    // CORRECCIÓN: Usar 'router.te' en lugar de 'get_productos.te' para que el enrutamiento funcione.
    snprintf(comando, sizeof(comando), "./typeeasy apis/product_endpoint.te");

    // LOG: Imprimir el comando que se va a ejecutar.
    fprintf(stderr, "[LOG] manejadorProductos: Ejecutando comando: %s\n", comando);

    char* respuesta_json = ejecutarScript(comando);

    if (respuesta_json) {
        // LOG: Imprimir la respuesta JSON obtenida del script.
        fprintf(stderr, "[LOG] manejadorProductos: Respuesta del script:\n---\n%s\n---\n", respuesta_json);
        mg_send_http_ok(conn, "application/json", strlen(respuesta_json));
        mg_write(conn, respuesta_json, strlen(respuesta_json));
        free(respuesta_json);
    } else {
        // LOG: Indicar que el script no devolvió ninguna respuesta.
        fprintf(stderr, "[LOG-ERROR] manejadorProductos: El script no devolvió ninguna respuesta (respuesta_json es NULL).\n");
        mg_send_http_error(conn, 500, "Error al ejecutar el script de productos.");
    }

    return 1;
}

// Nuevo manejador para la ruta /api/usuarios
static int manejadorUsuarios(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    char comando[512];
    // Con la estructura de archivos simplificada, la ruta dentro del contenedor será 'apis/user_endpoint.te'.
    snprintf(comando, sizeof(comando), "./typeeasy apis/user_endpoint.te");

    fprintf(stderr, "[LOG] manejadorUsuarios: Ejecutando comando: %s\n", comando);
    char* respuesta_json = ejecutarScript(comando);

    if (respuesta_json) {
        fprintf(stderr, "[LOG] manejadorUsuarios: Respuesta del script:\n---\n%s\n---\n", respuesta_json);
        mg_send_http_ok(conn, "application/json", strlen(respuesta_json));
        mg_write(conn, respuesta_json, strlen(respuesta_json));
        free(respuesta_json);
    } else {
        fprintf(stderr, "[LOG-ERROR] manejadorUsuarios: El script no devolvió ninguna respuesta (respuesta_json es NULL).\n");
        mg_send_http_error(conn, 500, "Error al ejecutar el script de usuarios.");
    }

    return 1;
}

// Nuevo manejador para la ruta /api/proveedores (XML)
static int manejadorProveedores(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    char comando[512];
    snprintf(comando, sizeof(comando), "./typeeasy apis/proveedores_endpoint.te");

    fprintf(stderr, "[LOG] manejadorProveedores: Ejecutando comando: %s\n", comando);
    char* respuesta_xml = ejecutarScript(comando);

    if (respuesta_xml) {
        fprintf(stderr, "[LOG] manejadorProveedores: Respuesta del script:\n---\n%s\n---\n", respuesta_xml);
        // Importante: Cambiar el Content-Type a "application/xml"
        mg_send_http_ok(conn, "application/xml; charset=utf-8", strlen(respuesta_xml));
        mg_write(conn, respuesta_xml, strlen(respuesta_xml));
        free(respuesta_xml);
    } else {
        fprintf(stderr, "[LOG-ERROR] manejadorProveedores: El script no devolvió ninguna respuesta (respuesta_xml es NULL).\n");
        mg_send_http_error(conn, 500, "Error al ejecutar el script de proveedores.");
    }

    return 1;
}

// Nuevo manejador para la ruta /api/facturas (XML)
static int manejadorFacturas(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Marcar como no utilizado para evitar warnings

    char comando[512];
    snprintf(comando, sizeof(comando), "./typeeasy apis/facturas_endpoint.te");

    fprintf(stderr, "[LOG] manejadorFacturas: Ejecutando comando: %s\n", comando);
    char* respuesta_xml = ejecutarScript(comando);

    if (respuesta_xml) {
        fprintf(stderr, "[LOG] manejadorFacturas: Respuesta del script:\n---\n%s\n---\n", respuesta_xml);
        // Importante: Cambiar el Content-Type a "application/xml"
        mg_send_http_ok(conn, "application/xml; charset=utf-8", strlen(respuesta_xml));
        mg_write(conn, respuesta_xml, strlen(respuesta_xml));
        free(respuesta_xml);
    } else {
        fprintf(stderr, "[LOG-ERROR] manejadorFacturas: El script no devolvió ninguna respuesta (respuesta_xml es NULL).\n");
        mg_send_http_error(conn, 500, "Error al ejecutar el script de facturas.");
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