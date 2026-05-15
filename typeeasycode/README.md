# typeeasycode/

Carpeta `cwd` cuando se corre `docker compose run --rm typeeasy <file.te>`
(montada como `/code` en el container).

## Estructura

| Path | Proposito |
|------|-----------|
| `endpoint.te`              | Server `--api` por defecto (puerto 9000)        |
| `main.te`                  | Punto de entrada generico                       |
| `mod_calc.te`              | Modulo importable de ejemplo                    |
| `crear_const_variable.te`  | Demo `const` (referenciado por BUILD.md/install)|
| `link_to_objects.te`       | Demo OOP + DB (referenciado por README)         |
| `agente_*_whatsapp.te`     | Agentes chatbot (README_CHATBOT)                |
| `test_orm_fast.te`         | Usado por `_orm_regression.sh`                  |
| `bench_csv_big.te`         | Usado por `cmp_bench.sh`                        |
| `productos*.csv`           | Datasets para tests CSV                         |
| `examples/`                | Tests categorizados (ver `examples/README.md`)  |
| `apis/`, `apis_broken/`    | Endpoints para `servidor_api`                   |
| `models/`, `settings/`     | ORM models y configuracion (importables)        |
| `wasm/`                    | Ejemplos WebAssembly                            |
| `_regression.sh`           | Suite de regression (corre tests categorizados) |
| `_orm_regression.sh`       | Smoke ORM                                       |
| `cmp_bench.sh`             | Benchmark CSV vs polars                         |
