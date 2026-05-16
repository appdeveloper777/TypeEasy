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
# TypeEasy
time docker compose run --rm typeeasy examples/12_linq_csv_10M/bench_linq_10M.te

# Python (desde typeeasycode/ para que el path relativo del CSV coincida)
cd typeeasycode
time python3 examples/12_linq_csv_10M/bench_linq_10M.py
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

> Nota: `sumBy` agrega como `int` 32-bit en TypeEasy v0.0.11; sumas que
> excedan 2^31 desbordan. Para 10M filas con `precio ∈ [1,1000]` el total
> ronda 5×10¹³ y **desborda** (verás `-2147483648`). Mantén `precio` pequeño
> (≤ 200) para totales dentro de rango si la igualdad numérica importa.

## Resultados de referencia

Medidos en este repo (Windows + WSL2 + Docker, CSV de 10M filas, ~180 MB),
incluyendo carga + tres agregaciones:

| Runtime           | Tiempo total   | sumBy.precio        |
| ----------------- | -------------- | ------------------- |
| Python 3.x        | **45 s**       | `50000005000000` ✓  |
| TypeEasy v0.0.11  | **1 m 30 s**   | `-2147483648` (ovf) |

TypeEasy va ~2× más lento que Python en este escenario. La carga del CSV
domina el tiempo total en ambos; la diferencia en agregaciones se debe al
intérprete AST + creación/eval de lambda por fila (10M invocaciones).

Operadores que materializan (`where`, `select`, `orderBy`, `take`, `distinct`,
`minBy`, `maxBy` con OBJECT) deben benchmarkearse aparte con datasets menores
(p. ej. 1M filas) por consumo de RAM — con 10M filas el container puede ser
asesinado por OOM (exit 137).
