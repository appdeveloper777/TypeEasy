# 23 — Decorador `@<nombre>` definible (guard → 401 automático)

Define tu **propia** lógica de autorización como una función global y úsala como
decorador sobre los endpoints. A diferencia de `@auth` (que valida un JWT Bearer
fijo), aquí vos controlas qué significa "autorizado".

## Cómo funciona

1. Defines un guard como función/lambda global:

```typeeasy
let login = fn() => {
    let tok = request_cookie("session");
    if (tok == "") { return ""; }   // falsy -> 401
    return tok;                      // truthy -> pasa
};
```

2. Lo aplicas con `@<nombre>` (el nombre = el de la función):

```typeeasy
@login
[HttpGet("/api/perfil")]
perfil() { return json("{\"ok\":true}"); }
```

El guard se ejecuta **antes** del handler:

| Retorno del guard | Resultado |
|-------------------|-----------|
| Falsy (`""`, `0`, `false`, `null`) | Respuesta automática **HTTP 401** `{"error":"unauthorized"}`; el handler NO corre. |
| Truthy (string no vacío, número ≠ 0, objeto, etc.) | Pasa; el handler corre normalmente. |
| El nombre no es una función definida | **401 fail-closed** (se deniega y se loguea un warning). |

### Elegir el código de denegación (401 por defecto)

El guard puede fijar el status de la respuesta llamando a `response_status(N)`
**antes** de retornar un valor falsy. Si no lo llama, la denegación usa **401**:

```typeeasy
let login = fn() => {
    let tok = request_cookie("session");
    if (tok == "") { response_status(500); return ""; }  // deniega con 500
    return tok;
};
```

| Guard hace... | Respuesta al fallar |
|---------------|---------------------|
| `return "";` (sin `response_status`) | **401** |
| `response_status(403); return "";` | **403** |
| `response_status(500); return "";` | **500** |

Nota: dos `return` seguidos NO funcionan (el segundo es código muerto). El guard
solo decide truthy/falsy; el cuerpo `{"error":"unauthorized"}` lo pone el framework.

Esto elimina el boilerplate repetido en cada handler:

```typeeasy
// SIN decorador (repetido en cada método):
let g = session_nick();
if (g == "") { response_status(401); return json("{\"error\":\"unauthorized\"}"); }
```

## Proteger TODO un bloque de una sola vez

Dos formas equivalentes; aplican el guard a **todos** los métodos del bloque:

```typeeasy
@login endpoint { ... }      // decorador ANTES de endpoint
endpoint @login { ... }      // decorador DESPUÉS de endpoint
```

Un decorador **per-método** dentro del bloque **override** al del bloque para ese
método (ej. una ruta `@admin` dentro de un bloque `@login`).

## Ejecutar

```bash
typeeasy --api examples/23_guard_decorator/guard_decorator_api.te --port 9104
# o con Docker:
docker compose up -d typeeasy
```

## Probar la API

```bash
# ruta pública -> 200 sin cookie
curl -s http://localhost:9104/api/public

# ruta protegida sin cookie -> 401 automático
curl -s -i http://localhost:9104/api/perfil

# ruta protegida con cookie de sesión -> 200
curl -s --cookie "session=abc123" http://localhost:9104/api/perfil

# ruta admin: el guard @admin pide role=admin (override del bloque @login)
curl -s -i --cookie "session=abc123" http://localhost:9104/api/admin
curl -s --cookie "role=admin" http://localhost:9104/api/admin
```

## `@login` vs `@auth`

| | `@auth` | `@<nombre>` (este ejemplo) |
|--|---------|----------------------------|
| Qué valida | JWT Bearer HS256 con `JWT_SECRET` | Lo que vos programes (cookie, sesión en BD, header, IP, etc.) |
| Definido por | El núcleo de TypeEasy | Vos, con una función global |
| Respuesta al fallar | 401 `{"error":"unauthorized"}` | 401 `{"error":"unauthorized"}` |

Ambos se pueden combinar y aplicar a nivel de método o de bloque.
