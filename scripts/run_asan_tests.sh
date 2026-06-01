#!/usr/bin/env bash
# AddressSanitizer memory check (production-ready bloqueante #3).
#
# Compila `src/typeeasy` con -fsanitize=address y corre la suite de lenguaje
# bajo el sanitizer. Cualquier use-after-free / heap-overflow / doble-free que
# hoy se manifiesta como "segfault intermitente (exit 139)" se vuelve un fallo
# determinista y con stack trace.
#
# Solo Linux (ASan necesita gcc/clang con runtime ASan; no aplica a MinGW).
# Asume que las dependencias de build ya estan instaladas (flex bison libssl-dev
# default-libmysqlclient-dev libcurl4-openssl-dev libpq-dev freetds-dev). El job
# de CI las instala antes de invocar este script; en local usa el contenedor
# gcc:13.2.0 (ver README de tests) o una maquina con las deps presentes.
#
# Uso:
#   bash scripts/run_asan_tests.sh
#
# Exit code = numero de tests FAIL (0 = verde). ASan aborta el proceso del test
# con codigo != 0 si detecta un error de memoria, que el runner reporta como FAIL.

set -euo pipefail

cd "$(dirname "$0")/.."

echo "=== [asan] build src/typeeasy con AddressSanitizer ==="
make -C src clean >/dev/null 2>&1 || true
make -C src typeeasy CFLAGS_EXTRA="-fsanitize=address -fno-omit-frame-pointer"

# detect_leaks=0: los leaks se auditan aparte (no fallan el run; el interprete
#   es de vida-corta por diseno y aun no libera todo al salir).
# abort_on_error=1: que un error de memoria mate el test con codigo != 0 para
#   que el runner lo cuente como FAIL.
# halt_on_error=0: seguir reportando dentro de un mismo proceso.
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:halt_on_error=0:print_stacktrace=1"

echo "=== [asan] corriendo suite tests/lang bajo ASan ==="
python3 tools/te-test/run_tests.py tests/lang \
  --bin src/typeeasy \
  --junit tests/.report/junit-asan.xml \
  --verbose
