# 06_csv

Lectura CSV con `from "file.csv", Class`. Benchmarks viven
en la raiz (`bench_csv_big.te`, `bench_csv_1M.te`).

> **Alcance:** CSV es una *feature de soporte* para mover datos **dentro de tus
> endpoints** (cargar un dataset, devolverlo como JSON/XML, alimentar un
> handler). **No** es un reemplazo de Polars/pandas ni un motor de analitica.
> Los benchmarks solo sirven para verificar que el parsing no se degrade.
