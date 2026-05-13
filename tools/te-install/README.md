# te-install — Gestor de paquetes nativos de TypeEasy

Equivalente a `npm install` / `cargo add` / `dotnet add package` para drivers
nativos (`.so`) que extienden el intérprete con builtins (mongo, redis, etc.)
sin recompilar el motor.

## Cómo funciona

Un paquete es una librería `libte_<nombre>.so` que exporta:

```c
void te_module_register(const TEHostAPI *host);
```

y registra builtins en runtime. El instalador la coloca en
`~/.te/packages/<nombre>/libte_<nombre>.so`, una de las rutas que
`load_native("nombre")` busca por defecto.

## Uso

### Instalar un paquete (futuro: índice central)

Hoy aún no hay un índice central, así que se instala por URL directa:

```bash
tools/te-install/te-install mongo \
    --from https://github.com/typeeasy/te-mongo/releases/download/v1.0.0/libte_mongo.so \
    --sha256 abc123...
```

### Instalar todo desde manifiesto

Crea `te-packages.json` en la raíz del proyecto (ver
[te-packages.example.json](te-packages.example.json)):

```json
{
  "dependencies": {
    "mongo": {
      "version": "^1.0.0",
      "url": "https://github.com/typeeasy/te-mongo/releases/download/v1.0.0/libte_mongo.so",
      "sha256": "abc123..."
    }
  }
}
```

Y luego:

```bash
tools/te-install/te-install
```

### Listar / desinstalar

```bash
tools/te-install/te-install --list
tools/te-install/te-install --remove mongo
```

### Variables

| Variable          | Default              | Descripción                       |
| ----------------- | -------------------- | --------------------------------- |
| `TE_PACKAGES_DIR` | `~/.te/packages`     | Carpeta donde se instalan los .so |
| `TE_MANIFEST`     | `te-packages.json`   | Ruta al manifiesto                |
| `TE_INDEX_URL`    | (vacío)              | Reservado para índice central     |

## Después de instalar

Desde cualquier script `.te`:

```typeeasy
load_native("mongo");

let conn = mongo_connect("mongodb://localhost:27017/meri");
let r    = mongo_query(conn, "usuarios", { "activo": 1 }, "json");
println(r);
mongo_close(conn);
```

`load_native` devuelve `0` en éxito y un código negativo si el `.so` no se
encuentra o no expone `te_module_register`.

## Crear tu propio paquete

```c
/* mi_plugin.c */
#include "te_builtins.h"

/* IMPORTANTE: copia la struct, no guardes el puntero del host —
 * el host puede pasarla por referencia a su propio stack. */
static TEHostAPI g_host;
#define H (&g_host)

static int te_hello(ASTNode *node, ASTNode *args) {
    H->set_ret_str("hola desde el plugin");
    return 1;
}

void te_module_register(const TEHostAPI *host) {
    if (host->abi_version != TE_HOST_API_VERSION) return;
    g_host = *host;                                /* copia por valor */
    host->register_builtin("hello", te_hello);
}
```

Compila:

```bash
gcc -shared -fPIC -I/path/to/typeeasy/src \
    mi_plugin.c -o libte_hello.so
```

Instala:

```bash
tools/te-install/te-install hello --from ./libte_hello.so
```

Usa:

```typeeasy
load_native("hello");
println(hello());
```
