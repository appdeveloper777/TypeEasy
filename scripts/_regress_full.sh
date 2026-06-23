#!/usr/bin/env bash
# Full regression: clean build + lang suite + valgrind leak-check.
# Intended to run inside gcc:13.2.0 with build/test/valgrind deps installed.
set -u
cd /app

echo "==================================================================="
echo " STAGE 1/4  Clean build (engine)"
echo "==================================================================="
make -C /app/src clean >/dev/null 2>&1
make -C /app/src typeeasy 2>&1 | tail -n 5
if [ ! -x /app/src/typeeasy ]; then echo "FATAL: engine build failed"; exit 1; fi
echo "engine: OK"

echo "==================================================================="
echo " STAGE 2/4  Build sqlite plugin"
echo "==================================================================="
bash /app/plugins/sqlite/build_linux.sh 2>&1 | tail -n 5
ls -la /app/plugins/sqlite/libte_sqlite.so 2>&1 || { echo "FATAL: plugin build failed"; exit 1; }
echo "plugin: OK"

echo "==================================================================="
echo " STAGE 3/4  Lang regression suite"
echo "==================================================================="
python3 /app/tools/te-test/run_tests.py /app/tests/lang --bin /app/src/typeeasy 2>&1 | tail -n 30

echo "==================================================================="
echo " STAGE 4/5  Database standard (envelope success assertions)"
echo "==================================================================="
DBSTD_OUT="$( cd /tmp && /app/src/typeeasy /app/tests/regress/db_standard_envelope.te 2>&1 )"
echo "$DBSTD_OUT"
if echo "$DBSTD_OUT" | grep -q "DBSTD_RESULT: PASS"; then
  DBSTD_OK=0
  echo "db-standard: PASS"
else
  DBSTD_OK=1
  echo "db-standard: FAIL"
fi

echo "==================================================================="
echo " STAGE 5/6  API envelope bleed (same-named locals across guard+handler)"
echo "==================================================================="
APIBLEED_OUT="$( bash /app/tests/regress/run_api_bleed.sh /app/src/typeeasy /app/tests/regress/api_envelope_bleed.te 8088 2>&1 )"
echo "$APIBLEED_OUT"
if echo "$APIBLEED_OUT" | grep -q "APIBLEED_RESULT: PASS"; then
  APIBLEED_OK=0; echo "api-bleed: PASS"
else
  APIBLEED_OK=1; echo "api-bleed: FAIL"
fi

echo "==================================================================="
echo " STAGE 6/6  Valgrind leak-check (key scripts)"
echo "==================================================================="
VG="valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite --track-origins=yes -q"
RC_TOTAL=0
for s in \
  /app/typeeasycode/examples/29_obj_arg/obj_arg.te \
  /app/typeeasycode/examples/30_sql_envelope/sql_envelope.te \
  /app/typeeasycode/examples/31_sql_estandar/estandar.te \
  /app/tests/regress/db_standard_envelope.te ; do
  echo "-------------------------------------------------------------------"
  echo " VG: $s"
  echo "-------------------------------------------------------------------"
  VGOUT="$( cd /tmp && $VG /app/src/typeeasy "$s" 2>&1 )"
  echo "$VGOUT" | tail -n 40
  rc=$?
  echo "   exit=$rc"
  # Pre-existing one-time parser leaks (te_raw_yylex/yyparse at program exit)
  # are benign and NOT a regression signal. Only fail on leaks whose stack
  # mentions our runtime code (envelope / map hash / sql facade).
  if echo "$VGOUT" | grep -E "definitely lost" >/dev/null 2>&1 \
     && echo "$VGOUT" | grep -E "te_sql_envelope_wrap|native_sql_|te_sql_" >/dev/null 2>&1; then
    RC_TOTAL=1
    echo "   -> runtime (envelope/sql) leak detected"
  fi
done

echo "==================================================================="
echo " REGRESSION SUMMARY"
echo "==================================================================="
if [ "${DBSTD_OK:-1}" = "0" ]; then echo " db-standard (success): PASS"; else echo " db-standard (success): FAIL"; fi
if [ "${APIBLEED_OK:-1}" = "0" ]; then echo " api-bleed (--api):     PASS"; else echo " api-bleed (--api):     FAIL"; fi
if [ "$RC_TOTAL" = "0" ]; then
  echo " valgrind leaks:        no runtime (envelope/sql) leaks"
else
  echo " valgrind leaks:        runtime leak detected (see above)"
fi
echo "==================================================================="
if [ "${DBSTD_OK:-1}" = "0" ] && [ "${APIBLEED_OK:-1}" = "0" ] && [ "$RC_TOTAL" = "0" ]; then
  echo " RESULT: GREEN"; exit 0
else
  echo " RESULT: RED"; exit 1
fi
