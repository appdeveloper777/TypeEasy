# 05_http_client

Cliente HTTP (`http_get`, `http_post`, `http_request`)
contra el servidor `--api` propio (`endpoint.te` en :9000) o
endpoints externos sobre HTTPS (libcurl en Linux/Docker, OpenSSL en Windows).

Levantar el servidor primero:
  docker compose up -d typeeasy

## Cabeceras personalizadas + código de estado

Ver [`custom_headers.te`](custom_headers.te) para un ejemplo completo.

**Cabeceras custom** (Authorization, X-API-Key, ...) como 2.º/3.er/4.º argumento,
en dos formatos:

```
// Como STRING ("Nombre: valor" separadas por "\r\n" o "\n")
http_get("https://api.ejemplo.com/x", "X-API-Key: SECRETO\r\nAuthorization: Bearer TOK");

// Como MAP { "Nombre": "valor", ... }  (más legible)
http_post("https://api.ejemplo.com/x", "{\"a\":1}", { "X-API-Key": "SECRETO" });

http_request("PUT", "https://api.ejemplo.com/x", "{\"e\":1}", { "Authorization": "Bearer TOK" });
```

**Código de estado** de la última llamada con `http_last_status()`:

| Valor | Significado |
|-------|-------------|
| `200`..`299` | éxito |
| `400`..`499` | error del cliente (request rechazado) |
| `500`..`599` | error del servidor |
| `0` | no hubo respuesta (fallo de red / host inexistente) |

```
let body = http_get("https://httpbin.org/status/404");
let code = http_last_status();   // 404
if (code == 0) { /* sin conexión: reintentar */ }
else if (code >= 200 && code < 300) { /* OK */ }
else if (code >= 400 && code < 500) { /* request rechazado */ }
else { /* error del servidor */ }
```
