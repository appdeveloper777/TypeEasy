# 18 — CORS (Cross-Origin Resource Sharing)

El servidor `--api` permite que un frontend alojado en **otro dominio**
consuma la API desde el navegador. Envía las cabeceras CORS en todas las
respuestas y responde el **preflight `OPTIONS`** automáticamente.

Cabeceras que envía:

| Cabecera | Valor |
|----------|-------|
| `Access-Control-Allow-Origin`  | el origen configurado (default `*`) |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, PATCH, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization` |

## Configurar el origen permitido

3 formas, con esta precedencia (mayor gana):

| Forma | Ejemplo | Cuándo |
|-------|---------|--------|
| Flag CLI | `--cors-origin "https://app.com"` | override puntual |
| Variable de entorno | `TYPEEASY_CORS_ORIGIN=https://app.com` | Docker / `.env` |
| `typeeasy.toml` | `cors_origin = "https://app.com"` | config del proyecto |
| (default) | `*` | cualquier dominio |

El valor admite 3 modos:

| Valor | Comportamiento |
|-------|----------------|
| `*` | cualquier dominio (default) |
| un solo origen | `https://app.com` — solo ese dominio |
| **lista** separada por comas | `https://a.com, https://b.com` — el server **refleja** el `Origin` de la petición si coincide con uno de la lista; si no, lo bloquea |

> Con una lista, cada respuesta lleva un único `Access-Control-Allow-Origin`
> (el dominio que hizo la petición, si está permitido). El preflight `OPTIONS`
> se resuelve igual. Un origen no listado recibe un valor que el navegador
> rechaza.

## Ejecutar

```bash
# Abierto a cualquier dominio (default *)
typeeasy --api examples/18_cors/cors_api.te --port 9102

# Restringido a un dominio concreto
typeeasy --api examples/18_cors/cors_api.te --port 9102 \
  --cors-origin "https://app.ejemplo.com"

# Varios dominios (lista separada por comas)
typeeasy --api examples/18_cors/cors_api.te --port 9102 \
  --cors-origin "https://web.ejemplo.com, https://admin.ejemplo.com"

# Vía variable de entorno
TYPEEASY_CORS_ORIGIN="https://app.ejemplo.com" \
  typeeasy --api examples/18_cors/cors_api.te --port 9102

# En un proyecto (typeeasy.toml define cors_origin por entorno)
typeeasy serve --prod
```

En `typeeasy.toml` (la clave `cors_origin` se resuelve por entorno; si el
entorno activo no la define, hereda la del `[server]`):

```toml
[server]
port = 9102
cors_origin = "*"                       # default global

[env.dev]
cors_origin = "*"                       # en dev, abierto a cualquier origen

[env.prod]
cors_origin = "https://tu-frontend.com" # en prod, restringe a tu dominio
# o varios:  cors_origin = "https://web.com, https://admin.com"
```

> **Un solo origen, `*`, o una lista.** Por especificación de CORS la cabecera
> `Access-Control-Allow-Origin` solo puede llevar **un** valor por respuesta.
> Por eso, con una lista, el server compara el `Origin` de la petición y
> refleja solo ese (si está permitido). Una lista NO se envía tal cual al
> navegador.

Y se ejecuta seleccionando el entorno:

```bash
typeeasy serve --dev    # usa [env.dev]  -> cors_origin = "*"
typeeasy serve --prod   # usa [env.prod] -> cors_origin = "https://tu-frontend.com"
```

## Probar

```bash
# Preflight OPTIONS (lo que envía el navegador antes de un POST/PUT)
curl -s -i -X OPTIONS http://localhost:9102/api/saludo \
  -H "Origin: https://app.ejemplo.com" \
  -H "Access-Control-Request-Method: POST" \
  -H "Access-Control-Request-Headers: Content-Type, Authorization"

# Respuesta normal (mira la cabecera Access-Control-Allow-Origin)
curl -s -i http://localhost:9102/api/saludo -H "Origin: https://app.ejemplo.com"

# POST real
curl -s -X POST -d '{"x":1}' http://localhost:9102/api/eco
```

> Para producción, restringe `cors_origin` a tu dominio en lugar de `*`.
> Combínalo con el ejemplo `17_jwt/` para una API autenticada y consumible
> desde el navegador.
