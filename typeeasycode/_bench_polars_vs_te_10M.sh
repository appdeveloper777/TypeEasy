#!/bin/bash
# Polars vs TypeEasy DataFrame — 10M rows, wall-clock, Windows native v0.0.10
set -e
cd "$(dirname "$0")"
TE_BIN="/c/Users/FERNANDO INGUNZA/AppData/Local/Programs/TypeEasy/bin/typeeasy-bin.exe"
CSV="productos_10000000.csv"
RUNS=${RUNS:-5}

bench() {
  local label="$1"; shift
  local times=()
  for i in $(seq 1 "$RUNS"); do
    local t0=$(date +%s%N)
    "$@" > /dev/null 2>&1
    local t1=$(date +%s%N)
    times+=( $(( (t1 - t0) / 1000000 )) )
  done
  printf "%s: " "$label"
  printf "%s " "${times[@]}"
  local sorted=( $(printf '%s\n' "${times[@]}" | sort -n) )
  local mid=$(( ${#sorted[@]} / 2 ))
  printf "  median=%s ms  min=%s ms\n" "${sorted[$mid]}" "${sorted[0]}"
}

echo "=== File: $CSV ($(stat -c %s "$CSV") bytes) ==="
echo "=== Runs per config: $RUNS ==="
echo

bench "TE OO            " env -u TE_CSV_DATAFRAME "$TE_BIN" bench_csv_10M.te
bench "TE DataFrame     " env TE_CSV_DATAFRAME=1 TE_CSV_THREADS=8 "$TE_BIN" bench_csv_10M.te
bench "Polars (Python)  " python polars_bench_10M.py
