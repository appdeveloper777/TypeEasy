#!/bin/bash
# Bench LINQ 10M: TypeEasy (serial) vs TypeEasy (CSV multi-thread) vs Polars.
#
# Requisitos:
#   - typeeasycode/productos_10000000.csv (10M filas, ver README de 12_linq_csv_10M)
#   - docker compose con servicio `typeeasy`
#   - python con polars en el entorno activo
#
# Uso:
#   bash typeeasycode/_bench_polars_vs_te_linq_10M.sh [RUNS]
#   RUNS por defecto = 3
set -e
cd "$(dirname "$0")/.."        # repo root
RUNS=${1:-3}
CSV="typeeasycode/productos_10000000.csv"
TE_BENCH="examples/12_linq_csv_10M/bench_linq_10M.te"
PY_BENCH="examples/12_linq_csv_10M/bench_linq_10M_polars.py"

if [[ ! -f "$CSV" ]]; then
  echo "ERROR: $CSV no existe. Genéralo con el snippet del README."
  exit 1
fi

bench() {
  local label="$1"; shift
  local cmd="$1"; shift   # comando completo como string para preservar prefijos VAR=x
  local times=()
  for i in $(seq 1 "$RUNS"); do
    local t0=$(date +%s%N)
    bash -c "$cmd" > /tmp/te_bench_out.$$ 2>&1
    local t1=$(date +%s%N)
    times+=( $(( (t1 - t0) / 1000000 )) )
  done
  printf "%-45s " "$label"
  printf "%s ms " "${times[@]}"
  local sorted=( $(printf '%s\n' "${times[@]}" | sort -n) )
  local mid=$(( ${#sorted[@]} / 2 ))
  printf "  median=%s ms  min=%s ms\n" "${sorted[$mid]}" "${sorted[0]}"
  echo "    sample-output:"
  sed 's/^/      /' /tmp/te_bench_out.$$ | head -6
  rm -f /tmp/te_bench_out.$$
}

echo "=== CSV: $(stat -c%s "$CSV") bytes, RUNS=$RUNS ==="
echo

# Nota: usamos prefijos VAR=x (no `env VAR=x`) para que MSYS_NO_PATHCONV
# afecte la conversión de paths que hace bash ANTES de invocar docker.
TE_DOCKER="MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' docker compose run --rm"

bench "TypeEasy (serial, default)" \
  "$TE_DOCKER --entrypoint /typeeasy/typeeasy -w //code typeeasy //code/${TE_BENCH}"

bench "TypeEasy (TE_CSV_THREADS=8)" \
  "$TE_DOCKER -e TE_CSV_THREADS=8 --entrypoint /typeeasy/typeeasy -w //code typeeasy //code/${TE_BENCH}"

bench "Polars 1.x (python)" \
  "cd typeeasycode && python ${PY_BENCH}"
