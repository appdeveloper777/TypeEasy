# 15_sqlite

Ejemplos usando el plugin nativo **libte_sqlite** (SQLite embebido,
sin servidor externo). Ideal para prototipos, juegos, caches locales
y demos que necesiten persistencia.

## Plugin

Codigo y build en [`plugins/sqlite/`](../../../plugins/sqlite/README.md).

API expuesta:

| Builtin                                 | Devuelve                                       |
|-----------------------------------------|------------------------------------------------|
| `sqlite_connect(path)`                  | slot id (int) — `:memory:` o ruta a `.db`      |
| `sqlite_exec(slot, sql)`                | filas afectadas (int)                          |
| `sqlite_query(slot, sql, "json"\|"xml")`| string serializado                             |
| `sqlite_last_id(slot)`                  | ultimo `INTEGER PRIMARY KEY AUTOINCREMENT`     |
| `sqlite_close(slot)`                    | 0                                              |

> Nota: v1 sin binding parametrizado. Escapar input de usuario manualmente.

## Ejemplos

| Archivo                  | Demuestra                                          |
|--------------------------|----------------------------------------------------|
| `hello_sqlite.te`        | Conexion en memoria, CREATE/INSERT/UPDATE/SELECT   |
| `crud_persistente.te`    | Base en archivo `.db`, contador de visitas         |

## Como correr

### Docker

```
docker compose run --rm typeeasy examples/15_sqlite/hello_sqlite.te
```

### Windows nativo

```
# Asegurarse que libte_sqlite.dll este junto a typeeasy.exe
src\typeeasy.exe typeeasycode\examples\15_sqlite\hello_sqlite.te
```

### Variables de entorno

`TE_PLUGIN_PATH` acepta una lista (`;` en Windows, `:` en Linux) de
directorios extra donde buscar `libte_<name>.{dll,so}`.
