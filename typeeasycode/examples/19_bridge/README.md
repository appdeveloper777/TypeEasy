# Ejemplo 19 — Bridge a otros lenguajes (subproceso)

TypeEasy puede delegar trabajo a programas escritos en **otros lenguajes**
(Java, C#, Python, Node, Rust, Go...) lanzandolos como subproceso y hablando
con ellos por un protocolo simple de **lineas JSON** sobre stdin/stdout.

No necesitas un servidor HTTP ni microservicios: el proceso vive mientras lo
necesites y la comunicacion es directa por tuberias del sistema operativo.

## Builtins

| Builtin | Descripcion |
|---|---|
| `lang_spawn(cmd)` | Lanza el comando. Devuelve un *slot* (int >= 0) o `-1` si falla. |
| `lang_call(slot, linea)` | Envia una linea y **espera una linea** de respuesta. |
| `lang_send(slot, linea)` | Envia una linea (sin esperar respuesta). Devuelve 1/0. |
| `lang_recv(slot)` | Lee una linea de respuesta. |
| `lang_close(slot)` | Cierra el proceso y libera el slot. |

> Tambien existen los alias `proc_spawn` / `proc_call` / `proc_send` /
> `proc_recv` / `proc_close` para quienes prefieran nombres genericos.

## Protocolo

- **Una peticion = una linea** terminada en `\n` escrita al *stdin* del hijo.
- **Una respuesta = una linea** terminada en `\n` leida de su *stdout*.
- El contenido es convencionalmente JSON, pero el bridge solo se ocupa del
  *framing* por saltos de linea: puedes usar el formato que quieras.

## Ejecutar

```bash
te run typeeasycode/examples/19_bridge/bridge_demo.te
```

Salida esperada:

```
=== Bridge TypeEasy <-> Python ===
upper('hola mundo') -> HOLA MUNDO
add(7, 35) -> 42
echo('ping') -> ping
Proceso cerrado. Bridge OK.
```

## El worker

[`worker.py`](worker.py) es un ejemplo minimo: lee JSON de stdin, responde
JSON por stdout. Para usar Java/C#/Rust basta con que tu programa siga el
mismo contrato (leer una linea, escribir una linea). Por ejemplo, en Java:

```java
import java.util.Scanner;
public class Worker {
    public static void main(String[] a) {
        Scanner in = new Scanner(System.in);
        while (in.hasNextLine()) {
            String line = in.nextLine();
            // ... procesar JSON ...
            System.out.println("{\"ok\": true, \"result\": \"...\"}");
            System.out.flush();
        }
    }
}
```

Luego: `lang_spawn("java -jar worker.jar")`.

> **Nota:** recuerda hacer `flush()` tras cada respuesta en el worker; de lo
> contrario `lang_call` se quedara esperando una linea que sigue en el buffer
> del proceso hijo.

## Ejemplos en esta carpeta

| Archivo | Que muestra |
|---|---|
| [`bridge_demo.te`](bridge_demo.te) + [`worker.py`](worker.py) | Demo basico: upper / add / echo en Python. |
| [`math_demo.te`](math_demo.te) + [`math_worker.py`](math_worker.py) | Funciones Python reales: factorial, sqrt, reverse, is_prime. |
| [`perl_demo.te`](perl_demo.te) + [`perl_worker.pl`](perl_worker.pl) | El mismo bridge contra un worker en **Perl** (texto plano). |
| [`universal_demo.te`](universal_demo.te) | Auto-detecta el interprete disponible (python/python3/perl) y corre sin cambios en Windows y Linux. |
| [`secure_bridge_api.te`](secure_bridge_api.te) + [`secure_worker.py`](secure_worker.py) | **API segura (JWT + CORS) que delega el calculo al worker** (ver abajo). |

---

# Bridge dentro de una API segura: JWT + CORS

El servidor `--api` de TypeEasy soporta **autenticacion con JWT (HS256)** y
**CORS**, y desde un endpoint puedes **delegar el trabajo a otro lenguaje** via
el bridge. El ejemplo [`secure_bridge_api.te`](secure_bridge_api.te) combina las
tres piezas: el cliente hace login (JWT), llama a una ruta protegida, y el
calculo lo resuelve un worker en Python ([`secure_worker.py`](secure_worker.py)).

## 1. JWT (JSON Web Token, HS256)

Dos funciones nativas en TypeEasy:

| Funcion | Firma | Devuelve |
|---------|-------|----------|
| `jwt_sign(payload_json, secret)` | crea un token | `"header.payload.signature"` (base64url) |
| `jwt_verify(token, secret)` | valida firma + expiracion | el payload JSON si es valido, o `""` si no |

- Algoritmo: **HS256** (HMAC-SHA256). Header fijo `{"alg":"HS256","typ":"JWT"}`.
- `jwt_verify` comprueba la firma (comparacion *constant-time*) y el claim
  numerico `exp` (segundos epoch) contra la hora actual. Si el token expiro o la
  firma no coincide, devuelve cadena vacia.

### Ejemplo basico: firmar y verificar

```ts
let secret = "mi-clave-super-secreta";

// exp = ahora + 3600s (1 hora)
let payload = "{\"sub\":\"123\",\"name\":\"Ana\",\"exp\":" + (now_epoch() + 3600) + "}";

let token = jwt_sign(payload, secret);
print("Token: " + token);

let claims = jwt_verify(token, secret);
if (claims == "") {
    print("Token invalido o expirado");
} else {
    print("Token valido. Payload: " + claims);
}
```

### Middleware de autenticacion + bridge en un endpoint

Asi luce la ruta protegida de [`secure_bridge_api.te`](secure_bridge_api.te):
valida el JWT y, si es valido, **delega el calculo al worker**.

```ts
[HttpGet("/api/calc")]
calc() {
    // El cliente envia:  Authorization: Bearer <token>
    let auth = request_header("Authorization");
    var token = auth;
    if (auth.starts_with("Bearer ")) { token = auth.substr(7); }

    let claims = jwt_verify(token, SECRET);
    if (claims == "") {
        response_status(401);
        return json("{\"error\":\"no_autorizado\"}");
    }

    // Autenticado -> delega el trabajo a otro lenguaje via bridge.
    let slot = lang_spawn(WORKER);
    let req  = json_stringify({ "op": request_query("op"), "n": request_query("n") });
    let resp = lang_call(slot, req);
    lang_close(slot);
    return json(resp);  // el worker ya devolvio JSON
}
```

### Probar con curl

```bash
# 1) login -> obtienes un token
curl -s -X POST -d '{"user":"ana","pass":"1234"}' \
  http://localhost:9103/api/login

# 2) ruta protegida que delega al worker (factorial de 20)
curl -s -H "Authorization: Bearer <TOKEN>" \
  "http://localhost:9103/api/calc?op=factorial&n=20"
# -> {"ok": true, "result": 2432902008176640000}

# 3) sin token -> 401
curl -s -i "http://localhost:9103/api/calc?op=fib&n=10"
```

---

## 2. CORS (Cross-Origin Resource Sharing)

Permite que un frontend en **otro dominio** consuma la API desde el navegador.
El servidor envia las cabeceras CORS en todas las respuestas y resuelve el
**preflight `OPTIONS`** automaticamente.

Cabeceras que envia:

| Cabecera | Valor |
|----------|-------|
| `Access-Control-Allow-Origin`  | el origen configurado (default `*`) |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, PATCH, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization` |

### Configurar el origen permitido

Precedencia (mayor gana):

| Forma | Ejemplo | Cuando |
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
| **lista** por comas | `https://a.com, https://b.com` — el server **refleja** el `Origin` de la peticion si coincide con uno de la lista; si no, lo bloquea |

> Por especificacion de CORS, `Access-Control-Allow-Origin` solo puede llevar
> **un** valor por respuesta. Con una lista, el server compara el `Origin` de la
> peticion y refleja solo ese (si esta permitido). La lista NO se envia tal cual.

### Ejecutar

```bash
# Abierto a cualquier dominio (default *)
typeeasy --api examples/19_bridge/secure_bridge_api.te --port 9103

# Restringido a un dominio concreto
typeeasy --api examples/19_bridge/secure_bridge_api.te --port 9103 \
  --cors-origin "https://app.ejemplo.com"

# Varios dominios (lista separada por comas)
typeeasy --api examples/19_bridge/secure_bridge_api.te --port 9103 \
  --cors-origin "https://web.ejemplo.com, https://admin.ejemplo.com"

# Via variable de entorno
TYPEEASY_CORS_ORIGIN="https://app.ejemplo.com" \
  typeeasy --api examples/19_bridge/secure_bridge_api.te --port 9103
```

### En `typeeasy.toml`

La clave `cors_origin` se resuelve por entorno; si el entorno activo no la
define, hereda la del `[server]`.

```toml
[server]
port = 9103
cors_origin = "*"                       # default global

[env.dev]
cors_origin = "*"                       # en dev, abierto a cualquier origen

[env.prod]
cors_origin = "https://tu-frontend.com" # en prod, restringe a tu dominio
# o varios:  cors_origin = "https://web.com, https://admin.com"
```

```bash
typeeasy serve --dev    # usa [env.dev]  -> cors_origin = "*"
typeeasy serve --prod   # usa [env.prod] -> cors_origin = "https://tu-frontend.com"
```

### Probar con curl

```bash
# Preflight OPTIONS (lo que envia el navegador antes de un GET con Authorization)
curl -s -i -X OPTIONS http://localhost:9103/api/calc \
  -H "Origin: https://app.ejemplo.com" \
  -H "Access-Control-Request-Method: GET" \
  -H "Access-Control-Request-Headers: Authorization"

# Respuesta normal (mira la cabecera Access-Control-Allow-Origin)
curl -s -i "http://localhost:9103/api/calc?op=fib&n=10" \
  -H "Origin: https://app.ejemplo.com"
```

### Llamada desde el navegador (fetch con JWT)

```js
// Frontend en https://app.ejemplo.com consumiendo la API en otro dominio
const res = await fetch("https://api.ejemplo.com/api/calc?op=factorial&n=20", {
  method: "GET",
  headers: {
    "Content-Type": "application/json",
    "Authorization": "Bearer " + localStorage.getItem("token"),
  },
});
const data = await res.json();
console.log(data); // el resultado lo calculo el worker en otro lenguaje
```

---

> **Produccion:** restringe `cors_origin` a tu dominio (no uses `*`), combina
> JWT + CORS para una API autenticada consumible desde el navegador, y usa el
> bridge para delegar tareas pesadas o especificas a otro lenguaje.
