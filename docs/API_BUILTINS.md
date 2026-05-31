# API Builtins Reference

> Los **~15 builtins que importan** cuando escribís una API con
> `typeeasy --api endpoint.te`. Todo lo demás es accesorio.
>
> Esta es la referencia canónica para el flujo HTTP. Para el tutorial paso a
> paso ver [CREAR_ENDPOINTS.md](CREAR_ENDPOINTS.md); para un ejemplo completo y
> ejecutable ver [`typeeasycode/endpoint.te`](../typeeasycode/endpoint.te).

Ejecutar un endpoint:

```bash
typeeasy --api endpoint.te                  # puerto 8080
typeeasy --api endpoint.te --port 9000      # otro puerto
typeeasy --api endpoint.te --host 127.0.0.1 # solo loopback
```

---

## 1. Request — leer lo que entra

### `request_param(name) -> string`
Devuelve un parámetro de ruta declarado con `{...}` en el atributo del handler.

```ts
[HttpGet("/users/{id}")]
get_user() {
    let id = request_param("id");      // "42" para GET /users/42
    return json({ id: id });
}
```

### `request_body() -> string`
Cuerpo crudo de la petición (típicamente JSON sin parsear).

```ts
[HttpPost("/echo")]
echo() {
    let body = request_body();         // '{"x":1}'
    return json(body);
}
```

### `request_header(name) -> string`
Valor de una cabecera HTTP (case-insensitive en el nombre).

```ts
let ct = request_header("Content-Type");   // "application/json"
```

### Model binding — `handler(u : Clase)`
Si un handler declara un parámetro tipado por una clase, el cuerpo JSON se
**deserializa y valida** automáticamente. Cuerpo inválido → **HTTP 422** sin
escribir una línea de validación.

```ts
class User { name : string; age : int; }

[HttpPost("/api/users")]
crearUsuario(u : User) {           // body {"name":"Ana","age":30} -> u
    return json(u);
}
```

---

## 2. Response — construir lo que sale

### `json(value) -> response`
Serializa la respuesta como `application/json`. Acepta:
- un **string** ya-JSON: `return json("{\"pong\":1}")`,
- un **object literal inline**: `return json({ ok: true })`,
- una **variable** (map / instancia de clase): `return json(u)`.

```ts
return json({ mensaje: "¡Hola desde TypeEasy!" });
```

### `xml(value) -> response`
Igual que `json` pero serializa como `application/xml`.

### `concat(a, b, ...) -> string`
Concatena strings de forma segura como argumento de builtins (prefer: usá esto
en vez de `"a" + b` cuando construís el cuerpo inline, para evitar gotchas de
concatenación dentro de argumentos).

```ts
let body = concat("{\"id\":\"", id, "\",\"name\":\"Demo\"}");
return json(body);
```

### Interpolación de strings `$"..."`
Dentro de un literal `$"..."`, `{expr}` y `${expr}` se evalúan e insertan.

```ts
return json({ mensaje: $"¡Hola {n * 3}!" });
```

---

## 3. Auth (JWT HS256)

### `jwt_sign(payload, secret) -> string`
Firma un payload JSON (string) con HS256 y devuelve el token `xxx.yyy.zzz`.

```ts
let secret  = env("JWT_SECRET");
let payload = concat("{\"sub\":\"", u.name, "\",\"exp\":1900000000}");
let token   = jwt_sign(payload, secret);
return json({ token: token });
```

### `jwt_verify(token, secret) -> bool`
Verifica firma + expiración. Normalmente **no la llamás a mano**: usá `@auth`.

### Decorador `@auth`
Exige `Authorization: Bearer <token>` válido **antes** de ejecutar el handler.
El secret se toma de `JWT_SECRET`. Token ausente/inválido/expirado → **HTTP 401
`{ "error": "unauthorized" }`** automáticamente.

```ts
@auth
[HttpGet("/api/perfil")]
perfil() {
    return json(concat("{\"ok\":true,\"claims\":", current_claims(), "}"));
}
```

### `current_claims() -> string`
Dentro de un handler `@auth`, devuelve el payload JSON ya validado.

---

## 4. Entorno y persistencia

### `env(name) -> string`
Lee una variable de entorno. Úsala para secretos (`JWT_SECRET`), DSNs, etc.
Nunca hardcodees secretos en el `.te`.

```ts
let secret = env("JWT_SECRET");
```

### SQLite (plugin `libte_sqlite`)
Cargá el plugin y usá la familia `sqlite_*`. Si el plugin quedó desactualizado
respecto al binario, el host **falla ruidosamente** al registrar (ABI guard);
no devuelve `[]` en silencio.

| Builtin | Qué hace |
|---------|----------|
| `sqlite_connect(path)` | abre/crea la DB, devuelve un handle |
| `sqlite_exec(db, sql)` | ejecuta INSERT/UPDATE/DELETE/DDL |
| `sqlite_query(db, sql)` | ejecuta SELECT, devuelve filas |
| `sqlite_last_id(db)` | último rowid insertado |
| `sqlite_close(db)` | cierra la conexión |

> Gotcha: pasá SQL concatenado **en una variable**, no inline:
> `let sql = "a" + "b"; sqlite_exec(db, sql);`

---

## Cheat sheet

| Builtin | Categoría | Devuelve |
|---------|-----------|----------|
| `request_param(name)` | request | string |
| `request_body()` | request | string |
| `request_header(name)` | request | string |
| `handler(u : Clase)` | request | model binding (422 si inválido) |
| `json(value)` | response | application/json |
| `xml(value)` | response | application/xml |
| `concat(...)` | response | string |
| `$"...{e}..."` | response | string interpolado |
| `jwt_sign(payload, secret)` | auth | token |
| `jwt_verify(token, secret)` | auth | bool |
| `@auth` | auth | gate 401 |
| `current_claims()` | auth | string (payload) |
| `env(name)` | entorno | string |
| `sqlite_connect/exec/query/last_id/close` | datos | handle / filas |
