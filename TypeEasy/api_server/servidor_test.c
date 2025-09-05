#include "civetweb.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep(x * 1000) // Windows usa Sleep() en milisegundos
#else
#include <unistd.h> // Para Linux/macOS
#endif

// NUEVO: Handler específico para /api/productos
// ===============================================
int handle_productos_request(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Ignorar parámetro no usado

    // Preparamos una respuesta en formato JSON
    const char *json_body = "[{\"id\": 1, \"nombre\": \"Laptop\"}, {\"id\": 2, \"nombre\": \"Mouse Inalámbrico\"}]";

    // Enviamos la respuesta con la cabecera "Content-Type" correcta para JSON
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              (int)strlen(json_body), json_body);
    return 1;
}

// Handler original para la ruta raíz "/"
// =====================================
int handle_root_request(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Ignorar el parámetro no usado

    const char *body = "<h1>Servidor principal funcionando sape?</h1><p>Prueba el endpoint <a href='/api/productos'>/api/productos</a></p>";

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              (int)strlen(body), body);
    return 1;
}

int main(void) {
    const char *options[] = {"listening_ports", "8080", NULL};
    struct mg_context *ctx;

    mg_init_library(0); // Inicializar la biblioteca

    ctx = mg_start(NULL, 0, options);
    if (ctx == NULL) {
        printf("Error al iniciar el servidor.\n");
        return 1;
    }

    // --- REGISTRO DE RUTAS ---
    // Registramos un manejador para la ruta raíz "/"
    mg_set_request_handler(ctx, "/", handle_root_request, NULL);

    // NUEVO: Registramos el manejador para la nueva ruta de API
    mg_set_request_handler(ctx, "/api/productos", handle_productos_request, NULL);

    printf("Servidor escuchando en http://localhost:8080/\n");
    printf("Endpoint de API disponible en http://localhost:8080/api/productos\n");
    printf("Presiona Ctrl+C para detener.\n");

    while (1) {
        sleep(1);
    }

    mg_stop(ctx);
    mg_exit_library();
    return 0;
}