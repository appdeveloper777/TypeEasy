# 21 — Cookies (sesión con `request_cookie`)

Manejo de sesión basado en cookies. El builtin principal:

| Builtin | Descripción |
|---------|-------------|
| `request_cookie(nombre)` | Parsea el header `Cookie` de la petición y devuelve el valor de la cookie pedida. Devuelve `""` si no existe. |

Para **escribir** cookies se usa el builtin genérico de respuesta:

| Builtin | Descripción |
|---------|-------------|
| `response_header("Set-Cookie", "...")` | Añade una cabecera `Set-Cookie`. Se puede llamar varias veces para emitir varias cookies. |

## Cómo funciona `request_cookie`

El navegador (o `curl`) manda todas las cookies en un único header:

```
Cookie: user=ana; logkey=abc123; PHPSESSID=xyz
```

`request_cookie("logkey")` parsea esa cadena (separa por `;`, recorta espacios,
hace match exacto de `logkey=`) y devuelve `abc123`. Es un parser en C puro,
self-contained y cross-platform.

## Ejecutar

```bash
typeeasy --api examples/21_cookies/cookies_api.te --port 9103
```

## Probar la API

`curl -c` guarda las cookies que devuelve el servidor; `curl -b` las reenvía.

```bash
# 1) login -> el servidor responde con Set-Cookie (curl las guarda en cookies.txt)
curl -s -c cookies.txt -X POST \
     -d '{"user":"ana","pass":"1234"}' \
     http://localhost:9103/api/login

# 2) acceso protegido reenviando las cookies guardadas -> 200 + perfil
curl -s -b cookies.txt http://localhost:9103/api/perfil

# 3) sin cookie -> 401 no_session
curl -s -i http://localhost:9103/api/perfil

# 4) logout -> invalida la cookie (Max-Age=0)
curl -s -b cookies.txt -X POST http://localhost:9103/api/logout
```

## Patrón de guarda de sesión (manual)

```typeeasy
let user   = request_cookie("user");
let logkey = request_cookie("logkey");

if (user == "") {
    response_status(401);
    return json("{\"error\":\"no_session\"}");
}
// validar logkey contra la sesión guardada en la BD...
```

## Notas de seguridad

- **`HttpOnly`**: impide que el JavaScript del navegador lea la cookie (mitiga XSS).
- **`SameSite=Lax`** (o `Strict`): reduce el riesgo de CSRF.
- **`Secure`**: añádelo en producción (HTTPS) para que la cookie solo viaje cifrada.
- Nunca guardes datos sensibles en claro dentro de la cookie; usa un identificador
  de sesión opaco y guarda el estado en el servidor (o un JWT firmado, ver `17_jwt`).
