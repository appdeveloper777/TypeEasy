# libte_sqlite — Plugin SQLite para TypeEasy

Driver nativo de SQLite cargable en tiempo de ejecución vía `load_native("sqlite")`.

## API

| Builtin                                  | Retorno                       | Descripción                              |
|------------------------------------------|-------------------------------|------------------------------------------|
| `sqlite_connect("ruta.db" \| ":memory:")` | `int` slot (≥0) o `-1`        | Abre la BD y reserva un slot del pool.   |
| `sqlite_exec(slot, "SQL")`                | `int` filas afectadas o `-1`  | DDL / INSERT / UPDATE / DELETE.          |
| `sqlite_query(slot, "SQL", "json"\|"xml")`| `string` con el resultset     | SELECT u otra query con filas.           |
| `sqlite_last_id(slot)`                    | `int` `last_insert_rowid`     | Útil tras INSERT.                        |
| `sqlite_close(slot)`                      | `0`                           | Libera el slot del pool.                 |

Pool fijo de **10 conexiones** por proceso. Cada conexión se configura con:

- `journal_mode=WAL`
- `synchronous=NORMAL`
- `foreign_keys=ON`
- `busy_timeout=5000`

## Compilar

### Linux (Docker)

```bash
docker build -f plugins/sqlite/Dockerfile -t te-sqlite-builder .
docker run --rm -v "$PWD/plugins/sqlite:/out" te-sqlite-builder
# → plugins/sqlite/libte_sqlite.so
```

### Windows nativo (MSYS2 MINGW64)

```bash
pacman -S --needed mingw-w64-x86_64-sqlite3
cd plugins/sqlite
gcc -shared -O2 -I../../src sqlite_plugin.c -lsqlite3 -o libte_sqlite.dll
```

## Instalar

```bash
# Sistema (Linux):
tools/te-install/te-install sqlite --from plugins/sqlite/libte_sqlite.so
# Resultado: ~/.te/packages/sqlite/libte_sqlite.so

# Windows: copiar libte_sqlite.dll junto a typeeasy.exe, o
#   %USERPROFILE%\.te\packages\sqlite\libte_sqlite.dll
```

## Ejemplo

```typeeasy
load_native("sqlite");

let db = sqlite_connect("game.db");
sqlite_exec(db, "CREATE TABLE IF NOT EXISTS jugadores (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    nick  TEXT UNIQUE NOT NULL,
    oro   INTEGER NOT NULL DEFAULT 100
)");

sqlite_exec(db, "INSERT OR IGNORE INTO jugadores (nick) VALUES ('alice')");
sqlite_exec(db, "INSERT OR IGNORE INTO jugadores (nick) VALUES ('bob')");

let rows = sqlite_query(db, "SELECT id, nick, oro FROM jugadores ORDER BY id", "json");
println(rows);
# → [{"id":1,"nick":"alice","oro":100},{"id":2,"nick":"bob","oro":100}]

sqlite_close(db);
```

## Notas

- **No hay binding parametrizado todavía**: la SQL se ejecuta literal. Para
  v1 cualquier valor proveniente del usuario debe escaparse a mano (o usar
  `?` no funciona — agréguese en v2 con un arg `params` JSON/map). Si la
  app expone endpoints HTTP, validá los inputs en el handler antes de
  componer la SQL.
- **BLOBs**: se serializan como `""` en JSON. Si necesitás los bytes,
  usá `hex(col)` o `quote(col)` en el SELECT.
- **Hilos**: SQLite está compilado en modo `serialized` por defecto en la
  mayoría de distros — cada slot del pool es seguro de usar desde un solo
  hilo. Si vas a paralelizar, abrí una conexión por worker.
