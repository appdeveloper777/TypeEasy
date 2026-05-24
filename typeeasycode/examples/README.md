# Ejemplos TypeEasy

Categorias organizadas por concepto. Cada carpeta tiene su propio README.

| Carpeta | Tema |
|---------|------|
| `01_basics/`     | Variables, printing, control de flujo, arrays/maps |
| `02_oop/`        | Clases, herencia, metodos                          |
| `03_functional/` | Lambdas y higher-order (`filter`, `map`)           |
| `04_stdlib_io/`  | JSON, file I/O, string interp, try/catch, imports  |
| `05_http_client/`| Cliente HTTP / HTTPS contra el server `--api`      |
| `06_csv/`        | Lectura CSV (`from "file.csv", Class`)             |
| `07_db_orm/`     | Bridges MySQL/Postgres/SQL Server (Dapper-style)   |
| `08_bytecode/`   | Backend de bytecode VM                             |
| `09_ml/`         | Dataset / model / train / predict / plot           |
| `10_errors/`     | Manejo de errores graves                           |
| `15_sqlite/`     | Plugin nativo SQLite (CRUD embebido)               |
| `_scratch/`      | Tests de debug / temporales (no en regression)     |

## Como correr un test
```
docker compose run --rm typeeasy examples/01_basics/test_arr.te
```
Para `05_http_client/*` levantar primero el server:
```
docker compose up -d typeeasy
docker compose run --rm typeeasy examples/05_http_client/test_http_full.te
```

## Regression suite
```
docker compose run --rm --entrypoint /code/_regression.sh typeeasy
```
