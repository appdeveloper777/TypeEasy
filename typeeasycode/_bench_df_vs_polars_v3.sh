#!/usr/bin/env bash
# Engine-only timing v3: usa `time -p` (POSIX) que sí funciona con bash builtin.
set -e
cd "$(dirname "$0")"
RUNS=7

echo "=== TypeEasy DataFrame (engine-only, dentro de 1 container) ==="
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
  docker compose run --rm \
    -e TE_CSV_DATAFRAME=1 -e TE_CSV_THREADS=8 \
    --entrypoint bash typeeasy -c '
for i in $(seq 1 '"$RUNS"'); do
  { time -p /typeeasy/typeeasy /code/bench_df_groupby_1M.te >/dev/null; } 2>&1 | awk "/real/ {printf \"  run=%.3f s\n\", \$2}"
done
'

echo
echo "=== Polars (engine-only, sin Python startup: warm) ==="
python examples/12_linq_csv_10M/bench_groupby_1M_polars_pure.py

echo
echo "=== TE_CSV_TIMING breakdown (1 run) ==="
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
  docker compose run --rm \
    -e TE_CSV_DATAFRAME=1 -e TE_CSV_THREADS=8 -e TE_CSV_TIMING=1 \
    typeeasy /code/bench_df_groupby_1M.te 2>&1 | grep -E "(CSV-COL|filas|sum|min|max|groups|groupSum|groupRows)"
