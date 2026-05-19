#!/usr/bin/env bash
# Head-to-head: TypeEasy DataFrame group_sum vs Polars
# Mide tiempo wall-clock end-to-end (load+groupby+sum)
set -e
cd "$(dirname "$0")"
RUNS=5
CSV="productos_cat_1M.csv"
TE_DOCKER='docker compose run --rm -e TE_CSV_DATAFRAME=1 -e TE_CSV_THREADS=8 typeeasy /code/bench_df_groupby_1M.te'

echo "=== TypeEasy DataFrame (group_sum + sum/min/max columnar+SIMD+pthread) ==="
ts=()
for i in $(seq 1 $RUNS); do
  t0=$(date +%s%N)
  MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' $TE_DOCKER > /dev/null 2>&1
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  ts+=($ms)
  printf "  run %d: %d ms\n" "$i" "$ms"
done
sorted=$(printf '%s\n' "${ts[@]}" | sort -n)
median=$(echo "$sorted" | awk -v n=$RUNS 'NR==int(n/2)+1')
echo "  median: $median ms"

echo
echo "=== Polars (python) full wall-clock ==="
ts2=()
for i in $(seq 1 $RUNS); do
  t0=$(date +%s%N)
  python examples/12_linq_csv_10M/bench_groupby_1M_polars.py > /dev/null 2>&1
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  ts2+=($ms)
  printf "  run %d: %d ms\n" "$i" "$ms"
done
sorted2=$(printf '%s\n' "${ts2[@]}" | sort -n)
median2=$(echo "$sorted2" | awk -v n=$RUNS 'NR==int(n/2)+1')
echo "  median: $median2 ms"
