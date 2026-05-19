#!/bin/bash
# group_by 1M rows — TypeEasy LINQ vs Polars (Windows native, v0.0.10)
set -e
cd "$(dirname "$0")"
TE_BIN="/c/Users/FERNANDO INGUNZA/AppData/Local/Programs/TypeEasy/bin/typeeasy-bin.exe"
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
  printf "%-22s: " "$label"
  printf "%s " "${times[@]}"
  local sorted=( $(printf '%s\n' "${times[@]}" | sort -n) )
  local mid=$(( ${#sorted[@]} / 2 ))
  printf "  median=%s ms  min=%s ms\n" "${sorted[$mid]}" "${sorted[0]}"
}

echo "=== group_by + agg sobre productos_cat_1M.csv (1M filas, 10 categorías) ==="
echo "=== Runs per config: $RUNS ==="
echo

TE_SCRIPT="$PWD/examples/12_linq_csv_10M/bench_groupby_1M.te"
PY_SCRIPT="$PWD/examples/12_linq_csv_10M/bench_groupby_1M_polars.py"
bench "TE LINQ (OO)"        env -u TE_CSV_DATAFRAME "$TE_BIN" "$TE_SCRIPT"
bench "Polars (Python)"     python "$PY_SCRIPT"
