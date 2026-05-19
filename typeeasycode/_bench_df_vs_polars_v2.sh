#!/usr/bin/env bash
# Mide tiempo INTERNO (binario typeeasy) excluyendo overhead de docker run.
# Estrategia: ejecutar 5 iteraciones DENTRO de un solo container.
set -e
cd "$(dirname "$0")"

RUNS=5
SCRIPT='
for i in $(seq 1 '"$RUNS"'); do
  time -f "%e" /typeeasy/typeeasy /code/bench_df_groupby_1M.te > /dev/null 2>__t
  cat __t
done
'

echo "=== TypeEasy DataFrame (engine-only, no docker startup) ==="
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
  docker compose run --rm \
    -e TE_CSV_DATAFRAME=1 -e TE_CSV_THREADS=8 \
    --entrypoint bash typeeasy -c "$SCRIPT"

echo
echo "=== TypeEasy DataFrame (with TE_CSV_TIMING breakdown) ==="
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
  docker compose run --rm \
    -e TE_CSV_DATAFRAME=1 -e TE_CSV_THREADS=8 -e TE_CSV_TIMING=1 \
    typeeasy /code/bench_df_groupby_1M.te 2>&1 | tail -10

echo
echo "=== Polars (engine-only, separate python process per run, no docker) ==="
for i in $(seq 1 $RUNS); do
  time -f "%e" python examples/12_linq_csv_10M/bench_groupby_1M_polars.py > /dev/null 2>__t || true
  cat __t
done
rm -f __t

echo
echo "=== Polars warm (parse+groupby+min/max+filter) ==="
python examples/12_linq_csv_10M/bench_groupby_1M_polars_pure.py
