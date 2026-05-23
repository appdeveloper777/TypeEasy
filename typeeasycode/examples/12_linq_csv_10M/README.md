# 12 — LINQ sobre 10M filas (CSV)

Benchmark de operadores LINQ de TypeEasy v0.0.11 sobre 10 000 000 de
instancias `Producto` cargadas desde CSV. Hay un equivalente en Python para
comparación 1-a-1 (mismas agregaciones, mismo input).

## Requisitos

Coloca el archivo CSV en la raíz de `typeeasycode/`:

```
typeeasycode/productos_10000000.csv   # cabecera: nombre,precio
```

10M filas ≈ 180 MB. Puedes generar uno con:

```bash
python3 - <<'PY'
import csv
with open("typeeasycode/productos_10000000.csv","w",newline="") as f:
    w = csv.writer(f)
    w.writerow(["nombre","precio"])
    for i in range(10_000_000):
        w.writerow(["Producto", (i % 1000) + 1])
PY
```

## Correr

```bash
# TypeEasy — 10M (solo agregaciones)
time docker compose run --rm typeeasy examples/12_linq_csv_10M/bench_linq_10M.te

# TypeEasy — 1M (cobertura completa)
time docker compose run --rm typeeasy examples/12_linq_csv_10M/bench_linq_1M.te

# Python — desde typeeasycode/ para que el path relativo del CSV coincida
cd typeeasycode
time python3 examples/12_linq_csv_10M/bench_linq_10M.py
time python3 examples/12_linq_csv_10M/bench_linq_1M.py
```

## Qué mide

Solo agregaciones O(n) que **no materializan** listas intermedias:

- `sumBy`, `avgBy`
- `countWhere` (dos predicados distintos)
- `all`, `none`

Operadores que materializan (`where`, `select`, `orderBy`, `take`, `distinct`,
`minBy`, `maxBy` con OBJECT) deben benchmarkearse aparte con datasets menores
(p. ej. 1M filas) por consumo de RAM.

## Salida esperada

Ambos binarios deben imprimir las mismas líneas:

```
filas=10000000
sumBy.precio=...
countWhere>50=...
countWhere<10=...
OK
```

> Nota histórica (resuelto): versiones previas de `sumBy` acumulaban en
> `int` 32-bit y desbordaban a `-2147483648` con 10M filas de `precio∈[1,1000]`.
> El runtime actual entrega el total correcto (`5005000000`).

## Resultados de referencia

Medidos en este repo (Windows + Docker Desktop, mayo 2026), incluyendo carga
+ agregaciones, wall-clock end-to-end.

### 10M filas — TypeEasy vs Polars vs Python (LINQ-equivalente)

Operaciones: `read_csv` + `sumBy(precio)` + `countWhere(>50)` + `countWhere(<10)`.

Para reproducir: `bash typeeasycode/_bench_polars_vs_te_linq_10M.sh 3`

#### Cold start (primer run, disco frío + spin-up de container)

| Runtime                                                  | Tiempo          | sumBy.precio    |
| -------------------------------------------------------- | --------------- | --------------- |
| TypeEasy v0.0.12 — default                               | ~34 s           | `5005000000` ✓  |
| TypeEasy v0.0.12 — `TE_CSV_THREADS=8`                    | ~5 s            | `5005000000` ✓  |
| Polars 1.38                                              | ~17 s           | `5005000000` ✓  |

#### Warm (3 runs sucesivos, cache caliente, sin spin-up)

| Runtime                                                  | Median          | Min             | sumBy.precio    |
| -------------------------------------------------------- | --------------- | --------------- | --------------- |
| Polars 1.38                                              | **1.9 s** 🏆    | 1.6 s           | `5005000000` ✓  |
| TypeEasy v0.0.12 — default (serial)                      | 7.6 s           | 5.7 s           | `5005000000` ✓  |
| TypeEasy v0.0.12 — `TE_CSV_THREADS=8`                    | 7.9 s           | 7.3 s           | `5005000000` ✓  |

> **Lectura honesta:** Polars warm va ~4× más rápido que TypeEasy warm.
> `TE_CSV_THREADS=8` no ayuda con cache caliente porque la I/O ya no
> domina; el cuello es la invocación de lambdas por fila en el intérprete
> AST. Donde el parser paralelo brilla es en **cold start** (carga 133 MB
> de disco → 5 s en vez de 34 s, ~7× speedup sobre el path serial).

#### Modo experimental columnar (referencia, no comparable)

| Runtime                                                  | Tiempo (warm)   | sumBy.precio              |
| -------------------------------------------------------- | --------------- | ------------------------- |
| TypeEasy — `TE_CSV_DATAFRAME=1 TE_CSV_THREADS=8`         | 3.3 s ⚠️        | `0` (lambdas no soportadas) |

> ⚠️ El modo `TE_CSV_DATAFRAME=1` es el wrapper columnar tipo Arrow. Hoy
> solo soporta `df.length` en O(1); cualquier `sumBy`/`countWhere` con
> lambda devuelve `0` porque el iterador columnar aún no está conectado al
> intérprete. El número se incluye como **techo teórico** del parser CSV.

Cómo correrlo manualmente:

```bash
# Polars (host)
cd typeeasycode
time python3 examples/12_linq_csv_10M/bench_linq_10M_polars.py

# TypeEasy default (serial)
time docker compose run --rm typeeasy examples/12_linq_csv_10M/bench_linq_10M.te

# TypeEasy con CSV paralelo (mejor para cold start)
time docker compose run --rm -e TE_CSV_THREADS=8 \
  typeeasy examples/12_linq_csv_10M/bench_linq_10M.te
```

### 1M filas (cobertura completa: agregaciones + `where`/`minBy`/`maxBy`/`take`)

| Runtime           | Tiempo total   | sumBy.precio  |
| ----------------- | -------------- | ------------- |
| Python 3.x        | **7.2 s**      | `500500000` ✓ |
| TypeEasy v0.0.11  | **14.8 s**     | `500500000` ✓ |

A 1M filas **todos los valores coinciden 1-a-1** entre los dos runtimes y
TypeEasy va ~2× más lento que Python — proporción consistente con la prueba
de 10M. El cuello principal es la carga del CSV y la invocación masiva de
lambdas en el intérprete AST.

Operadores que materializan (`where`, `select`, `orderBy`, `take`, `distinct`,
`minBy`, `maxBy` con OBJECT) se prueban sin problema a **1M**; a **10M** el
container puede ser asesinado por OOM (exit 137), por eso el bench de 10M se
limita a agregaciones puras.
