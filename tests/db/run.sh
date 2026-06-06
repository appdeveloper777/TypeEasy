#!/usr/bin/env bash
# run.sh — compila y corre la regresión unitaria de db_substitute_params.
#
# No necesita base de datos ni servidor: enlaza SOLO src/db_params.c con los
# stubs del propio test. Sirve igual en Windows nativo (MinGW gcc) y en
# Linux/Docker (gcc).
#
# Ejecutar desde la raíz del repo:
#   bash tests/db/run.sh
set -eu

# MinGW gcc usa TMP/TEMP/TMPDIR para sus archivos intermedios; si apuntan a
# C:\Windows\ (default heredado) falla con "Permission denied". Fijamos uno
# escribible. Inofensivo en Linux/Docker.
: "${TMPDIR:=/tmp}"
mkdir -p "$TMPDIR"
export TMPDIR TMP="$TMPDIR" TEMP="$TMPDIR"

CC="${CC:-gcc}"
OUT="tests/db/test_db_params"
case "${OS:-}" in Windows_NT) OUT="tests/db/test_db_params.exe";; esac

"$CC" -O2 -Wall -Isrc tests/db/test_db_params.c src/db_params.c -o "$OUT"
"./$OUT"
