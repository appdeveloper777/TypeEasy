#!/bin/bash
# Bench TE float vs Polars sobre CSV float (cat:string, precio:float, stock:int)
# countWhere(p.precio > 500.0).
#
# Uso:
#   bash typeeasycode/_bench_float_vs_polars.sh g_5m_float.csv
#   bash typeeasycode/_bench_float_vs_polars.sh g_10m_float.csv
#   RUNS=5 bash typeeasycode/_bench_float_vs_polars.sh g_10m_float.csv

set -e
CSV="${1:-g_5m_float.csv}"
RUNS="${RUNS:-3}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f "typeeasycode/$CSV" ]]; then
  echo "ERROR: typeeasycode/$CSV no existe" >&2
  exit 1
fi

bytes=$(stat -c %s "typeeasycode/$CSV" 2>/dev/null || stat -f %z "typeeasycode/$CSV")
echo "=== File: typeeasycode/$CSV ($bytes bytes) ==="
echo "=== Runs por config: $RUNS ==="
echo

bench() {
  local label="$1"; shift
  local times=()
  local out
  for i in $(seq 1 "$RUNS"); do
    local t0=$(date +%s%N)
    out=$("$@" 2>&1)
    local rc=$?
    local t1=$(date +%s%N)
    if [[ $rc -ne 0 ]]; then
      echo "[$label] run $i FAILED rc=$rc" >&2
      echo "$out" >&2
      return 1
    fi
    times+=( $(( (t1 - t0) / 1000000 )) )
  done
  local sorted=( $(printf '%s\n' "${times[@]}" | sort -n) )
  printf "%-25s runs=[%s]  median=%sms  best=%sms\n" \
    "$label" "${times[*]}" "${sorted[$(( ${#sorted[@]} / 2 ))]}" "${sorted[0]}"
  # Print last output line (cnt=...) for sanity
  echo "$out" | grep -E '^(cnt|filas|parse_ms|filter_ms|total_ms)=' | sed 's/^/   /'
}

# TE columnar (SIMD f64 ON)
bench "TE COLUMNAR+SIMD f64" \
  bash -c "MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' docker compose run --rm \
    -v \"$PWD/typeeasycode:/code\" \
    -v \"$PWD/typeeasycode/$CSV:/tmp/g.csv\" \
    -e TE_CSV_DATAFRAME=1 -e TE_CSV_COLUMNAR=1 -e TE_CSV_THREADS=8 \
    typeeasy /code/_bench_float_where.te"

# Polars
bench "Polars (Python)     " \
  bash -c "python typeeasycode/polars_bench_float.py typeeasycode/$CSV"

echo
echo "NOTA: el wall de TE incluye ~1.5-2s de Docker spin-up. Para tiempo neto del"
echo "      intérprete, usar TE_CSV_TIMING=1 y mirar parse_ms / colcache_eval_ms."
