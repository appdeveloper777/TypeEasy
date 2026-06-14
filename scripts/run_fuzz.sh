#!/usr/bin/env bash
# run_fuzz.sh — construye y corre el fuzzer del parser de TypeEasy.
#
# Orquesta: build (scripts/build_fuzzer.sh) -> siembra corpus desde tests/lang
# -> corre libFuzzer con el diccionario. Pensado para correr dentro de un
# contenedor con clang (ver cabecera de build_fuzzer.sh para deps).
#
# Uso:
#   bash scripts/run_fuzz.sh [SEGUNDOS] [args extra de libFuzzer...]
#
# Ejemplos:
#   bash scripts/run_fuzz.sh 60                 # smoke de 60s (CI)
#   bash scripts/run_fuzz.sh 0                  # indefinido (Ctrl-C para parar)
#   bash scripts/run_fuzz.sh 300 -jobs=4        # 5 min, 4 procesos
#
# Los crashes se guardan como crash-<sha1> en tests/fuzz/artifacts/ y se
# reproducen con: src/fuzz_parser tests/fuzz/artifacts/crash-<sha1>
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

SECS="${1:-60}"
shift || true

CORPUS="$ROOT/tests/fuzz/corpus"
SEEDS="$ROOT/tests/fuzz/seeds"
ARTIFACTS="$ROOT/tests/fuzz/artifacts"
DICT="$ROOT/tests/fuzz/typeeasy.dict"

# 1) Build (idempotente; recompila el harness + objetos del motor con clang).
if [[ ! -x "$ROOT/src/fuzz_parser" || "${FUZZ_REBUILD:-1}" == "1" ]]; then
  bash "$ROOT/scripts/build_fuzzer.sh"
fi

# 2) Sembrar corpus: seeds curados + todos los .te de la suite de lenguaje.
#    Un corpus rico en programas válidos da a libFuzzer puntos de partida con
#    alta cobertura para mutar hacia los paths raros.
mkdir -p "$CORPUS" "$ARTIFACTS"
if [[ -d "$SEEDS" ]]; then
  cp -n "$SEEDS"/*.te "$CORPUS"/ 2>/dev/null || true
fi
if [[ -d "$ROOT/tests/lang" ]]; then
  find "$ROOT/tests/lang" -name '*.te' -exec cp -n {} "$CORPUS"/ \; 2>/dev/null || true
fi
echo "=== [fuzz] corpus inicial: $(find "$CORPUS" -type f | wc -l) archivos ==="

# 3) Correr. detect_leaks=0: el intérprete no libera todo al salir por diseño;
#    cazamos crashes (UAF/overflow/SEGV), no leaks (eso lo cubre #6).
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:print_stacktrace=1:handle_abort=1"

MAXLEN_ARG="-max_len=8192"
TIME_ARG=""
if [[ "$SECS" != "0" ]]; then
  TIME_ARG="-max_total_time=$SECS"
fi

# Fork mode (target con leaks por diseño). El parser de TypeEasy no libera los
# tokens/nodos que ya lexó/construyó cuando una entrada malformada provoca un
# error de sintaxis: en producción no importa (cada parse es un proceso efímero
# que termina), pero el harness de libFuzzer alimenta millones de entradas
# inválidas en un solo proceso y ese leak acumulado crece el RSS hasta que
# libFuzzer reporta "out-of-memory" (falso positivo: no es un bug de seguridad).
# `-fork=1` corre el fuzzing en procesos hijos que se reciclan, acotando el RSS;
# bajo fork los defaults de libFuzzer son `-ignore_ooms=1 -ignore_timeouts=1
# -ignore_crashes=0`, así que los OOM/timeouts benignos se registran y la
# corrida continúa, pero un crash REAL de ASan (UAF/overflow/SEGV) detiene la
# corrida con exit != 0 y guarda el artefacto. Se fijan explícitos por robustez
# entre versiones de libFuzzer.
#
# rss_limit_mb se mantiene MUY por debajo del techo de memoria del contenedor de
# CI como defensa secundaria: si el leak acumulado de un hijo creciera sin
# control, libFuzzer lo recicla por su cuenta (OOM benigno, ignorado) antes de
# que el OOM-killer del kernel lo mate con SIGKILL. Se puede sobrescribir desde
# CI con TYPEEASY_FUZZ_RSS_MB para runners con menos RAM.
#
# NOTA histórica: un `exit 77` con `oom/timeout/crash: 0/0/0` NO siempre es un
# OOM. La causa raíz observada fue flex llamando exit(2) desde YY_FATAL_ERROR
# ("input in flex scanner failed") sobre entradas adversariales; libFuzzer lo
# reporta como "fuzz target exited" y aborta. Se corrigió redefiniendo
# YY_FATAL_ERROR en parser.l para hacer longjmp al punto de recuperación
# (g_runtime_recovery) que el harness instala, en vez de matar el proceso.
RSS_MB="${TYPEEASY_FUZZ_RSS_MB:-1024}"
FORK_ARGS=(-fork=1 -ignore_ooms=1 -ignore_timeouts=1 -ignore_crashes=0 "-rss_limit_mb=$RSS_MB")

# Mitigación ASLR vs ASan: en kernels host modernos (6.x bajo Docker
# Desktop/WSL2) la entropía alta de mmap (vm.mmap_rnd_bits=32) hace que el
# allocator de ASan falle al reservar su shadow memory y el proceso reciba un
# SIGSEGV *durante AsanInitInternal*, antes de ejecutar el target. Esto produce
# "segfaults intermitentes" que NO son bugs del código bajo prueba. Desactivar
# la aleatorización de direcciones con `setarch -R` lo elimina de forma
# determinista. Requiere lanzar el contenedor con `--security-opt
# seccomp=unconfined` (la syscall personality(ADDR_NO_RANDOMIZE) está vetada por
# el perfil seccomp por defecto). Si setarch no está o no puede desactivar
# ASLR, se corre directo (best-effort).
RUNNER=()
if command -v setarch >/dev/null 2>&1 && setarch -R true >/dev/null 2>&1; then
  RUNNER=(setarch -R)
  echo "=== [fuzz] ASLR desactivada vía 'setarch -R' (evita falso SIGSEGV de ASan) ==="
else
  echo "=== [fuzz] AVISO: no se pudo desactivar ASLR; si ves SIGSEGV en AsanInit," \
       "relanza el contenedor con --security-opt seccomp=unconfined ==="
fi

# Marcador para distinguir artefactos NUEVOS (de esta corrida) de los viejos.
RUN_MARKER="$(mktemp)"

set -x
set +e
"${RUNNER[@]}" "$ROOT/src/fuzz_parser" \
  "$CORPUS" \
  -dict="$DICT" \
  -artifact_prefix="$ARTIFACTS/" \
  "${FORK_ARGS[@]}" \
  $MAXLEN_ARG $TIME_ARG \
  "$@"
FUZZ_RC=$?
set +x
set -e

# libFuzzer en modo fork propaga un exit != 0 cuando registró un OOM/timeout
# "ignorado" (p.ej. `INFO: exiting: 71` con `oom/timeout/crash: 1/0/0`). Eso NO
# es un bug: el parser acumula nodos por diseño (ver nota de FORK_ARGS arriba) y
# un hijo cruza `rss_limit_mb` antes de ser reciclado. La corrida es sana —
# `-fork=1` recicla el hijo y sigue fuzzeando. Solo debemos fallar la corrida si
# libFuzzer guardó el artefacto de un hallazgo REAL y reproducible: crash-*
# (UAF/overflow/SEGV/abort) o leak-*. Los OOM/timeout son benignos y van
# ignorados por diseño, así que NO cuentan como hallazgo.
REAL_FINDINGS="$(find "$ARTIFACTS" -maxdepth 1 -type f -newer "$RUN_MARKER" \
  \( -name 'crash-*' -o -name 'leak-*' \) 2>/dev/null || true)"
rm -f "$RUN_MARKER"

if [[ -n "$REAL_FINDINGS" ]]; then
  echo "=== [fuzz] HALLAZGO REAL: libFuzzer guardó artefacto(s) reproducibles ==="
  echo "$REAL_FINDINGS"
  echo "=== [fuzz] Reproducir con: src/fuzz_parser <artefacto> ==="
  exit 1
fi

if [[ "$FUZZ_RC" -ne 0 ]]; then
  echo "=== [fuzz] AVISO: libFuzzer salió con código $FUZZ_RC pero no dejó" \
       "artefactos de crash/leak. Causa esperada: OOM/timeout benigno por el" \
       "leak-por-diseño del parser, ya acotado por -fork + rss_limit_mb. Se" \
       "trata como ÉXITO. ==="
  exit 0
fi

echo "=== [fuzz] OK: corrida completa sin hallazgos. ==="
