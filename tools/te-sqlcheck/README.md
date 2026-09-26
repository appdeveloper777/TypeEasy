# te-sqlcheck — validar el SQL de tus `.te` contra la base real

`te_sqlcheck.py` extrae el SQL que tus archivos `.te` pasan a `sql_query`, `sql_exec`,
`mysql_query`, `sqlite_query`/`sqlite_exec` (y a helpers con nombre `db_query`/`db_exec`)
y lo **prepara** en la base sin ejecutarlo. Así una columna renombrada, una tabla que falta o
un error de sintaxis aparece en el build con `archivo:línea`, y no como un `{"error":...}`
dentro de un 200 en producción.

Solo Python 3 (sin dependencias). Nada se ejecuta: MySQL/MariaDB usa `PREPARE`, SQLite usa
`EXPLAIN`, y únicamente se validan `SELECT/INSERT/UPDATE/DELETE/REPLACE/WITH`.

```bash
# MySQL / MariaDB (cualquier comando de cliente que acepte SQL por stdin)
python tools/te-sqlcheck/te_sqlcheck.py --mysql-cmd "mysql -uroot mi_db" "src/**/*.te"

# SQLite
python tools/te-sqlcheck/te_sqlcheck.py --sqlite app.db "src/**/*.te"

# Ver qué SQL se extrajo (sin base)
python tools/te-sqlcheck/te_sqlcheck.py --list "src/**/*.te"
```

Salida:

```text
src/clientes.te:15: error: 1054: Unknown column 'telefono' in 'field list'
    sql_query: SELECT id, telefono FROM clientes
te_sqlcheck: 6 consultas validadas, 1 errores, 0 warnings, 1 dinámicas omitidas
```

Exit `1` si hay errores (útil en CI), `--json` para integrarlo en otras herramientas.

## Qué se puede validar

| Forma en el `.te` | ¿Se valida? |
|---|---|
| `sql_query(conn, "SELECT ... WHERE id = ?", [id], "mysql")` | sí |
| `let SQL = "SELECT " + COLS + " FROM t";` y luego `sql_query(conn, SQL, ...)` | sí (constantes `let` y `+`/`concat()` de literales) |
| parámetros `@nombre` o `?` | sí (se preparan como placeholders) |
| `"... ORDER BY " + col` (valor de runtime) | no: se cuenta como **dinámica** |

Con `--loose` las partes dinámicas se reemplazan por `?` y lo que falle sale como *warning*
(no cambia el exit code). Sirve para revisar a mano, pero da falsos positivos cuando lo
dinámico es un fragmento de SQL (un `FROM` o un `WHERE` armado).

Recomendación: preferí SQL **literal con parámetros** — además de más seguro (sin inyección),
queda validado en el build.

Self-test: `python tools/te-sqlcheck/tests/test_te_sqlcheck.py` (corre en CI).
