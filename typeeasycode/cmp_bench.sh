#!/bin/bash
echo "=== TypeEasy DF columnar (threads=12) best-of-20 internal timing ==="
for i in $(seq 1 20); do
  TE_CSV_DATAFRAME=1 TE_CSV_THREADS=12 TE_CSV_TIMING=1 ./typeeasy /code/bench_csv_big.te 2>&1 | grep -o "total=[0-9]*" | sed 's/total=//'
done | sort -n | awk 'NR<=5 {print "  "$1" us"}'
echo ""
echo "=== Polars best-of-20 internal timing ==="
python3 /code/polars_bench.py
