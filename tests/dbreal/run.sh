#!/usr/bin/env bash
# run.sh — banco de bases de datos REALES (MySQL/MariaDB + PostgreSQL).
#
# Corre tests/dbreal/*.te con el runner estándar (tools/te-test/run_tests.py); cada test lee su
# servidor de variables de entorno y se SALTA (imprime la línea esperada) si no están definidas,
# así que sin servidores el script pasa en verde pero no prueba nada -> por eso en CI los
# servicios son obligatorios (ver job db-real en .github/workflows/lang-tests.yml).
#
#   TE_TEST_MYSQL_HOST / _PORT (3306) / _USER (root) / _PASS ("") / _DB (te_test)
#   TE_TEST_PG_HOST    / _PORT (5432) / _USER (postgres) / _PASS (postgres) / _DB (te_test)
#
# Uso: bash tests/dbreal/run.sh [--bin src/typeeasy] [--require]   (--require: falla si falta un servidor)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
BIN="src/typeeasy"; REQUIRE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --bin) BIN="$2"; shift 2;;
    --require) REQUIRE=1; shift;;
    *) echo "arg desconocido: $1" >&2; exit 2;;
  esac
done
[ -x "$BIN" ] || [ -x "$BIN.exe" ] || { echo "no existe $BIN" >&2; exit 2; }
if [ "$REQUIRE" = 1 ]; then
  [ -n "${TE_TEST_MYSQL_HOST:-}" ] || { echo "FAIL: TE_TEST_MYSQL_HOST no definido (--require)" >&2; exit 1; }
  [ -n "${TE_TEST_PG_HOST:-}" ]    || { echo "FAIL: TE_TEST_PG_HOST no definido (--require)" >&2; exit 1; }
fi
echo "=== [dbreal] MySQL=${TE_TEST_MYSQL_HOST:-<skip>} PG=${TE_TEST_PG_HOST:-<skip>} ==="
PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
"$PY" tools/te-test/run_tests.py tests/dbreal --bin "$BIN" --verbose "$@"
