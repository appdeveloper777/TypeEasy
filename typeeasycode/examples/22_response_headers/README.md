# 22 — Cabeceras de respuesta personalizadas (`response_header`)

Cómo añadir **cualquier** cabecera HTTP a la respuesta de un endpoint `--api`.

| Builtin | Descripción |
|---------|-------------|
| `response_header(nombre, valor)` | Añade una cabecera a la respuesta. Se puede llamar varias veces, incluso con el mismo nombre (p.ej. `Set-Cookie`). |

> `Content-Type` y `Content-Length` los calcula el servidor automáticamente;
> no hace falta (ni conviene) emitirlos con `response_header`.

## Ejecutar

```bash
typeeasy --api examples/22_response_headers/response_headers_api.te --port 9104
```

## Probar la API

Usa `curl -i` para ver las cabeceras de la respuesta:

```bash
# Cabeceras informativas / de seguridad
curl -i http://localhost:9104/api/info
#   X-Request-Id: req-7f3a9c21
#   X-Powered-By: TypeEasy
#   X-Content-Type-Options: nosniff
#   X-Frame-Options: DENY

# Control de caché
curl -i http://localhost:9104/api/cacheable
#   Cache-Control: public, max-age=60
#   ETag: "v1-abc123"

# Rate-limit
curl -i http://localhost:9104/api/limited
#   X-RateLimit-Limit: 100
#   X-RateLimit-Remaining: 99
#   X-RateLimit-Reset: 60

# Forzar descarga de archivo
curl -i http://localhost:9104/api/download
#   Content-Disposition: attachment; filename="reporte.json"
```

## Casos de uso comunes

| Cabecera | Para qué sirve |
|----------|----------------|
| `X-Request-Id` | Correlacionar logs entre cliente y servidor. |
| `Cache-Control` / `ETag` | Permitir que clientes y proxies cacheen y revaliden la respuesta. |
| `X-RateLimit-*` | Informar al cliente del límite y peticiones restantes. |
| `Content-Disposition: attachment` | Hacer que el navegador descargue el cuerpo como archivo. |
| `X-Content-Type-Options`, `X-Frame-Options` | Endurecer la seguridad del navegador. |
| `Set-Cookie` | Emitir cookies (ver `21_cookies`). |

## Notas

- `response_header` se puede llamar **múltiples veces** para emitir varias
  cabeceras, o varias instancias de la misma (típico con `Set-Cookie`).
- La implementación es C puro y **cross-platform**: el mismo comportamiento en
  Windows nativo y en Linux/Docker.
