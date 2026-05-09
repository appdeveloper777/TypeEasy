#!/usr/bin/env bash
# ============================================================
# TypeEasy benchmark runner
# Corre cada .te en benchmarks/ N veces y registra el tiempo
# en PERFORMANCE_LOG.md bajo una sección con etiqueta.
#
# Uso:
#   ./benchmarks/run_bench.sh "baseline-tree-walker"
#   ./benchmarks/run_bench.sh "fase1-enums"
#   ./benchmarks/run_bench.sh "fase2-slots"
#   ./benchmarks/run_bench.sh "fase3-bytecode"
# ============================================================

set -e

LABEL="${1:-unlabeled}"
RUNS="${RUNS:-3}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/PERFORMANCE_LOG.md"

cd "$ROOT"

# En Git Bash (Windows) hay que evitar que MSYS convierta paths Unix a Windows
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Comando docker base — montamos benchmarks/ como volumen extra y
# corremos directamente el binario para tener control total sobre el path
DOCKER_RUN=(docker compose run --rm
    -v "$ROOT/benchmarks:/benchmarks"
    --entrypoint /typeeasy/typeeasy
    typeeasy)

# Verificación temprana: el binario debe poder parsear y ejecutar bench_noop.te
if ! "${DOCKER_RUN[@]}" /benchmarks/bench_noop.te > /tmp/bench_check.out 2>&1; then
    echo "ERROR: bench_noop.te no pudo ejecutarse:" >&2
    cat /tmp/bench_check.out >&2
    exit 1
fi

# Lista de benchmarks (paths absolutos dentro del contenedor)
BENCHES=(
    "/benchmarks/bench_loop.te"
    "/benchmarks/bench_arith.te"
    "/benchmarks/bench_while.te"
    "/benchmarks/bench_strings.te"
    "/benchmarks/bench_loop_heavy.te"
)

echo ""
echo "========================================"
echo " TypeEasy benchmark — etiqueta: $LABEL"
echo " Runs por benchmark:  $RUNS"
echo " Fecha:               $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"
echo ""

# Medir overhead fijo de Docker con un .te trivial
echo "  [calibrating Docker overhead with bench_noop.te ...]"
overhead=""
for r in $(seq 1 "$RUNS"); do
    s=$(date +%s.%N)
    "${DOCKER_RUN[@]}" /benchmarks/bench_noop.te > /dev/null 2>&1
    e=$(date +%s.%N)
    el=$(awk "BEGIN { printf \"%.3f\", $e - $s }")
    if [[ -z "$overhead" ]] || awk "BEGIN { exit !($el < $overhead) }"; then
        overhead="$el"
    fi
done
echo "  Docker baseline overhead (best): ${overhead}s"
echo ""

# Encabezado en el log
{
    echo ""
    echo "## Run: \`$LABEL\` — $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
    echo "Docker overhead (best of $RUNS, bench_noop.te): **${overhead}s**"
    echo ""
    echo "| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |"
    echo "|---|---|---|---|---|---|"
} >> "$LOG"

for bench in "${BENCHES[@]}"; do
    name="$(basename "$bench" .te)"
    times=()
    best=""

    printf "  %-22s " "$name"

    for r in $(seq 1 "$RUNS"); do
        start=$(date +%s.%N)
        if ! "${DOCKER_RUN[@]}" "$bench" > /tmp/bench_err.out 2>&1; then
            echo ""
            echo "ERROR ejecutando $bench (run $r):" >&2
            cat /tmp/bench_err.out >&2
            exit 1
        fi
        end=$(date +%s.%N)
        elapsed=$(awk "BEGIN { printf \"%.3f\", $end - $start }")
        times+=("$elapsed")
        if [[ -z "$best" ]] || awk "BEGIN { exit !($elapsed < $best) }"; then
            best="$elapsed"
        fi
        printf "%ss " "$elapsed"
    done
    net=$(awk "BEGIN { v=$best - $overhead; if (v<0) v=0; printf \"%.3f\", v }")
    printf "(best: %ss net: %ss)\n" "$best" "$net"

    while [[ ${#times[@]} -lt 3 ]]; do times+=("-"); done

    echo "| $name | ${times[0]} | ${times[1]} | ${times[2]} | $best | **${net}** |" >> "$LOG"
done

echo "" >> "$LOG"
echo ""
echo "Resultados añadidos a: $LOG"
echo ""
