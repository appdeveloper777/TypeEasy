#include "civetweb.h"
#include <string.h>

// Esta función manejará las peticiones
int handle_request(struct mg_connection *conn) {
    const char *body = "<h1>Hola desde el servidor C!</h1>";
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              (int)strlen(body), body);
    return 1; // Indica que hemos manejado la petición
}

int main(void) {
    const char *options[] = {"listening_ports", "8080", NULL};
    struct mg_context *ctx = mg_start(NULL, 0, options);

    if (ctx == NULL) {
        printf("Error al iniciar el servidor.\n");
        return 1;
    }

    mg_set_request_handler(ctx, "/", handle_request, NULL);

    printf("Servidor escuchando en http://localhost:8080/\n");
    printf("Presiona Ctrl+C para detener.\n");

    // Bucle infinito para mantener el servidor corriendo
    while (1) {
        sleep(1);
    }

    mg_stop(ctx);
    return 0;
}