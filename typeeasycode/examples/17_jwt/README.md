# 17 — JWT (JSON Web Tokens, HS256)

Autenticación con tokens firmados. Dos builtins:

| Builtin | Descripción |
|---------|-------------|
| `jwt_sign(payload_json, secret)` | Devuelve `header.payload.signature` (base64url). Header fijo `{"alg":"HS256","typ":"JWT"}`, firma `HMAC-SHA256`. |
| `jwt_verify(token, secret)` | Devuelve el payload JSON si la firma es válida **y** el token no expiró (claim numérico `exp` vs hora actual). Si no, devuelve `""`. |

## Ejecutar

```bash
# 1) Demo standalone: firmar, verificar, manipulación y expiración
docker compose run --rm typeeasy examples/17_jwt/jwt_basico.te

# 2) API con login + endpoint protegido (Bearer token)
docker compose up -d typeeasy
typeeasy --api examples/17_jwt/jwt_api_auth.te --port 9101
```

## Probar la API

```bash
# Login -> devuelve un token
curl -s -X POST -d '{"user":"ana","pass":"1234"}' http://localhost:9101/api/login

# Acceso protegido con el token
curl -s -H "Authorization: Bearer <TOKEN>" http://localhost:9101/api/perfil

# Sin token -> 401 Unauthorized
curl -s -i http://localhost:9101/api/perfil
```

## Patrón de middleware de auth (manual)

```typeeasy
let auth   = request_header("Authorization");
var token  = auth;
if (auth.starts_with("Bearer ")) { token = auth.substr(7); }

let claims = jwt_verify(token, "mi-clave-secreta");
if (claims == "") {
    response_status(401);
    return json("{\"error\":\"no_autorizado\"}");
}
// claims contiene el payload JSON (sub/role/exp).
```

## Decorador `@auth` (recomendado, sin boilerplate)

Desde v0.0.16 podés proteger una ruta con el decorador `@auth`. La verificación
del Bearer token (firma HS256 + claim `exp`) ocurre **antes** de entrar al
handler; si falla, responde `401 {"error":"unauthorized"}` y el método no se
ejecuta. El secret se toma de la variable de entorno **`JWT_SECRET`** (no se
hardcodea). Dentro del handler, `current_claims()` devuelve el payload validado.

Ejemplo completo: [`jwt_auth_decorator.te`](jwt_auth_decorator.te).

```typeeasy
@auth
[HttpGet("/api/perfil")]
perfil() {
    // current_claims() -> {"sub":"ana","role":"admin","exp":...}
    return json(concat("{\"ok\":true,\"claims\":", current_claims(), "}"));
}
```

```bash
# IMPORTANTE: exportar el secret ANTES de arrancar el servidor.
export JWT_SECRET="mi-clave-secreta"            # PowerShell: $env:JWT_SECRET="..."
typeeasy --api examples/17_jwt/jwt_auth_decorator.te --port 9102

# En Docker el secret se define en docker-compose.yml (env JWT_SECRET).

# 1) login con el MISMO secret -> token
curl -s -X POST -H "Content-Type: application/json" \
     -d '{"name":"ana","age":30}' http://localhost:9102/api/login

# 2) ruta protegida con el token -> 200 + claims
curl -s -H "Authorization: Bearer <TOKEN>" http://localhost:9102/api/perfil

# 3) sin token / inválido / expirado -> 401
curl -s -i http://localhost:9102/api/perfil
```

> El token de `/api/login` debe firmarse con el **mismo** `JWT_SECRET` que
> valida `@auth`. Si cambiás el secret, los tokens viejos dejan de ser válidos.

> Nota: usa `var` (no `let`) para variables que reasignas; `let`/`const` son
> constantes. El claim `exp` debe ser un epoch en segundos (`now_epoch()`).

## CORS (consumir la API desde un navegador / otro dominio)

El servidor `--api` envía cabeceras CORS y responde el preflight `OPTIONS`
automáticamente, así un frontend en otro dominio puede llamar a la API —
incluido el flujo JWT con `Authorization: Bearer ...`.

Por defecto el origen permitido es `*` (cualquier dominio). Hay **3 formas**
de configurarlo, con esta precedencia (mayor gana):

| Forma | Ejemplo | Cuándo |
|-------|---------|--------|
| Flag CLI | `--cors-origin "https://app.com"` | override puntual |
| Variable de entorno | `TYPEEASY_CORS_ORIGIN=https://app.com` | Docker / `.env` |
| `typeeasy.toml` | `cors_origin = "https://app.com"` | config del proyecto |

```bash
# Directo, abierto a cualquier dominio (default)
typeeasy --api examples/17_jwt/jwt_api_auth.te --port 9101

# Directo, restringido a un dominio
typeeasy --api examples/17_jwt/jwt_api_auth.te --port 9101 \
  --cors-origin "https://app.ejemplo.com"

# En un proyecto (typeeasy.toml define cors_origin por entorno)
typeeasy serve --prod
```

En `typeeasy.toml`:

```toml
[server]
cors_origin = "*"                       # default global

[env.prod]
cors_origin = "https://tu-frontend.com" # en prod, restringe a tu dominio
```

