# apis/

Endpoints HTTP de TypeEasy servidos por `typeeasy --api <archivo>.te`.

Cada archivo declara un bloque `endpoint { ... }` con uno o mas handlers
decorados (`[HttpGet(...)]`, `[HttpPost(...)]`, etc.). El runtime parsea
las rutas, expone path params (`{nombre}`) via `request_param`, query
string via `request_query`, y headers/body via `request_header` /
`request_body`.

---

## Ejemplos de referencia

Dos archivos minimos cubren los dos verbos mas usados y son parte de la
regresion automatica (`typeeasycode/_regression_api.sh`, 8/8 PASS):

| Archivo                    | Puerto | Rutas                                                |
| -------------------------- | ------ | ---------------------------------------------------- |
| `example_httpget.te`       | 9100   | `GET /api/ping`, `GET /api/saludo/{nombre}`, `GET /api/sumar` |
| `example_httppost.te`      | 9101   | `POST /api/echo`, `POST /api/users` (model binding)  |

Levantar uno suelto:

```bash
docker compose run --rm -p 9101:9101 typeeasy \
    --api apis/example_httppost.te --host 0.0.0.0 --port 9101
```

---

## `[HttpGet]` — patrones cubiertos

```typeeasy
endpoint {
    [HttpGet("/api/ping")]
    ping() {
        return json("{\"pong\":1}");
    }

    [HttpGet("/api/saludo/{nombre}")]
    saludo() {
        let nombre = request_param("nombre");
        return json(concat("{\"saludo\":\"Hola ", nombre, "\"}"));
    }

    [HttpGet("/api/sumar")]
    sumar() {
        let a = request_query("a");
        let b = request_query("b");
        return json(concat("{\"a\":\"", a, "\",\"b\":\"", b, "\"}"));
    }
}
```

Pruebas:

```bash
curl -s http://localhost:9100/api/ping
curl -s http://localhost:9100/api/saludo/Ana
curl -s "http://localhost:9100/api/sumar?a=3&b=4"
```

---

## `[HttpPost]` — eco crudo + **model binding tipado**

El handler con firma `crearUsuario(u : User)` recibe el body JSON ya
deserializado en una instancia de la clase declarada como parametro.
El binding es por nombre de atributo (independiente del orden de claves)
y aplica valores por defecto (`int` -> `0`, `string` -> `""`) para claves
ausentes en el body.

```typeeasy
class User {
    name : string;
    age  : int;
}

endpoint {
    // (1) Eco crudo del body.
    [HttpPost("/api/echo")]
    echo() {
        let body = request_body();
        let ct   = request_header("Content-Type");
        return json(concat(
            "{\"received\":\"", body,
            "\",\"contentType\":\"", ct, "\"}"
        ));
    }

    // (2) Model binding: JSON -> clase tipada via la firma del metodo.
    [HttpPost("/api/users")]
    crearUsuario(u : User) {
        debug_log(concat("body=", request_body()));
        debug_log(concat("u.name=", u.name));
        debug_log(concat("u.age=",  u.age));

        return json(concat(
            "{\"name\":\"", u.name,
            "\",\"age\":",  u.age, "}"
        ));
    }
}
```

Pruebas:

```bash
curl -X POST -H 'Content-Type: application/json' \
     -d 'hola' http://localhost:9101/api/echo
# -> {"received":"hola","contentType":"application/json"}

curl -X POST -H 'Content-Type: application/json' \
     -d '{"name":"Ana","age":30}' http://localhost:9101/api/users
# -> {"name":"Ana","age":30}

curl -X POST -H 'Content-Type: application/json' \
     -d '{"age":99,"name":"Zoe"}' http://localhost:9101/api/users
# -> {"name":"Zoe","age":99}   (orden de claves irrelevante)

curl -X POST -H 'Content-Type: application/json' \
     -d '{"name":"SinEdad"}'    http://localhost:9101/api/users
# -> {"name":"SinEdad","age":0} (default para int faltante)
```

---

## Reglas de firma de handlers

| Forma                          | Significado                                          |
| ------------------------------ | ---------------------------------------------------- |
| `handler()`                    | Sin params; usa `request_param/query/body` manualmente. |
| `handler(x : string)`          | Bind escalar desde path param o query con nombre `x`. |
| `handler(n : int)`             | Idem, con coercion a `int`.                          |
| `handler(u : MiClase)`         | **Model binding**: el body JSON se mapea a `MiClase`. |

El tipo se declara en estilo TypeEasy: `nombre : Tipo` (no `Tipo nombre`).

---

## Debug de POSTs: `debug_log`

`debug_log("...")` escribe a **STDERR** del proceso `typeeasy`. NO entra
al pipeline que arma el cuerpo HTTP, asi que es seguro usarlo dentro de
cualquier handler sin contaminar la respuesta.

```bash
docker compose logs -f typeeasy
# [debug] body={"name":"Ana","age":30}
# [debug] u.name=Ana
# [debug] u.age=30
```

> No uses `println()` dentro de un handler para diagnostico: su salida
> se captura como cuerpo de la respuesta y se descarta despues de armar
> el JSON/XML.

---

## Breakpoints en handlers (debug interactivo)

Para inspeccionar variables dentro de un handler (`u`, `u.name`,
`request_body()`, etc.) en lugar de leer STDERR, usa la configuracion
**"TypeEasy: debug API (POST/GET handlers)"** de `.vscode/launch.json`:

1. Abri el `.te` del API (ej. `apis/example_httppost.te`).
2. Hace click en el margen izquierdo de la linea que querés pausar
   (ej. dentro del cuerpo de `crearUsuario(u : User) { ... }`) para
   poner el punto de interrupcion (circulo rojo).
3. En el panel **Run and Debug** elegi
   **"TypeEasy: debug API (POST/GET handlers)"** y aprieta F5.
   - Levanta `docker compose run` con `--api <archivo> --debug-port 4712`
     y publica el puerto HTTP `8081`.
   - VS Code se conecta por DAP al interprete (`127.0.0.1:4712`) y
     reenvia los breakpoints.
4. En otra terminal manda un request:
   ```bash
   curl -s -X POST http://localhost:8081/api/users \
       -H "Content-Type: application/json" \
       -d '{"name":"Ana","age":30}'
   ```
5. La ejecucion se frena en el breakpoint; en el panel **Variables**
   inspeccionas `u`, `u.name`, `u.age`. Usa F10 (step over), F11
   (step into) o F5 (continue) para avanzar.

> El puerto HTTP es configurable con `"httpPort": <n>` en la entrada de
> `launch.json`. El puerto del debugger (`port`) por defecto es `4712`.
> Si ya tenes una API corriendo a mano con `--debug-port 4711`, usa
> **"TypeEasy: ATTACH to running API"** en lugar de relanzarla.

---

## Regresion

```bash
# Suite API (curl + docker, corre desde el host):
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' bash typeeasycode/_regression_api.sh
# -> 8/8 PASS  (3 GET + 5 POST)
```

Cubre los handlers de `example_httpget.te` y `example_httppost.te`
incluyendo los tres asserts de model binding sobre `POST /api/users`.
