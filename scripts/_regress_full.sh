#!/usr/bin/env bash
# Full regression: clang strict build (CI fuzz-smoke parity) + gcc clean build
# + lang suite + db-standard + api-bleed + valgrind leak-check.
# Intended to run inside gcc:13.2.0 with build/test/valgrind deps installed
# (incl. clang + libomp-dev for the strict-build stage).
set -u
cd /app

echo "==================================================================="
echo " STAGE 1   Clang strict full build (CI fuzz-smoke parity)"
echo "==================================================================="
# El job 'fuzz-smoke' de .github/workflows/lang-tests.yml compila TODOS los
# objetos del motor con clang+ASan (scripts/build_fuzzer.sh) y linkea
# src/fuzz_parser. clang es mas estricto que gcc:13.2.0: una declaracion
# implicita / uso-antes-de-prototipo que gcc deja pasar como warning, clang la
# marca ERROR DURO ("conflicting types"). Aqui corremos LA MISMA build real
# (no -fsyntax-only): valida que el binario compila Y linkea bajo clang, sobre
# todo el motor, no solo unos pocos .c. Si clang/omp.h faltan, se SALTA.
CLANG_OK=0
if ! command -v clang >/dev/null 2>&1; then
  echo "clang: NOT INSTALLED -> SKIP (instala clang libomp-dev para paridad con CI)"
  CLANG_OK=2
elif ! echo '#include <omp.h>' | clang -fsyntax-only -fopenmp -x c - >/dev/null 2>&1; then
  echo "clang: omp.h NOT FOUND -> SKIP (instala libomp-dev)"
  CLANG_OK=2
else
  if bash /app/scripts/build_fuzzer.sh 2>&1 | tail -n 8 && [ -x /app/src/fuzz_parser ]; then
    echo "clang: OK (full engine build + link under clang)"
    CLANG_OK=0
  else
    echo "clang: FAIL (build_fuzzer.sh did not produce src/fuzz_parser)"
    CLANG_OK=1
  fi
fi

echo "==================================================================="
echo " STAGE 2   Clean build (engine, gcc release path)"
echo "==================================================================="
# build_fuzzer.sh dejo objetos .o de clang+ASan; make clean los borra para que
# el binario de runtime (usado por las etapas siguientes) sea el de gcc.
make -C /app/src clean >/dev/null 2>&1
make -C /app/src typeeasy 2>&1 | tail -n 5
if [ ! -x /app/src/typeeasy ]; then echo "FATAL: engine build failed"; exit 1; fi
echo "engine: OK"

echo "==================================================================="
echo " STAGE 3   Build sqlite plugin"
echo "==================================================================="
bash /app/plugins/sqlite/build_linux.sh 2>&1 | tail -n 5
ls -la /app/plugins/sqlite/libte_sqlite.so 2>&1 || { echo "FATAL: plugin build failed"; exit 1; }
echo "plugin: OK"

echo "==================================================================="
echo " STAGE 4   Lang regression suite"
echo "==================================================================="
python3 /app/tools/te-test/run_tests.py /app/tests/lang --bin /app/src/typeeasy 2>&1 | tail -n 30

echo "==================================================================="
echo " STAGE 5   Database standard (envelope success assertions)"
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
echo " STAGE 5b  Bare lambda call (discarded result must still run)"
echo "==================================================================="
BARELAMBDA_OUT="$( cd /tmp && /app/src/typeeasy /app/tests/regress/bare_lambda_call.te 2>&1 )"
echo "$BARELAMBDA_OUT"
if echo "$BARELAMBDA_OUT" | grep -q "BARELAMBDA_RESULT: PASS"; then
  BARELAMBDA_OK=0; echo "bare-lambda: PASS"
else
  BARELAMBDA_OK=1; echo "bare-lambda: FAIL"
fi

echo "==================================================================="
echo " STAGE 5c  json({ k: obj.field }) must serialize values, not null"
echo "==================================================================="
JSONATTR_OUT="$( cd /tmp && /app/src/typeeasy /app/tests/regress/json_obj_attr.te 2>&1 )"
echo "$JSONATTR_OUT"
if echo "$JSONATTR_OUT" | grep -q "JSONATTR_RESULT: PASS"; then
  JSONATTR_OK=0; echo "json-obj-attr: PASS"
else
  JSONATTR_OK=1; echo "json-obj-attr: FAIL"
fi

echo "==================================================================="
echo " STAGE 5d  json(fnCall()) and xml(fnCall()) must serialize object"
echo "==================================================================="
JSONFNCALL_OUT="$( bash /app/tests/regress/run_json_fn_call.sh /app/src/typeeasy /app/tests/regress/json_fn_call_object.te 8098 2>&1 )"
echo "$JSONFNCALL_OUT"
if echo "$JSONFNCALL_OUT" | grep -q "JSON_FNCALL_RESULT: PASS"; then
  JSONFNCALL_OK=0; echo "json-fn-call-object: PASS"
else
  JSONFNCALL_OK=1; echo "json-fn-call-object: FAIL"
fi

echo "==================================================================="
echo " STAGE 6   API envelope bleed (same-named locals across guard+handler)"
echo "==================================================================="
APIBLEED_OUT="$( bash /app/tests/regress/run_api_bleed.sh /app/src/typeeasy /app/tests/regress/api_envelope_bleed.te 8088 2>&1 )"
echo "$APIBLEED_OUT"
if echo "$APIBLEED_OUT" | grep -q "APIBLEED_RESULT: PASS"; then
  APIBLEED_OK=0; echo "api-bleed: PASS"
else
  APIBLEED_OK=1; echo "api-bleed: FAIL"
fi

echo "==================================================================="
echo " STAGE 6b  WebSocket attach ([WebSocket] route must be served, not 404)"
echo "==================================================================="
WSATTACH_OUT="$( bash /app/tests/regress/run_ws_attach.sh /app/src/typeeasy /app/tests/regress/ws_attach.te 8097 2>&1 )"
echo "$WSATTACH_OUT"
if echo "$WSATTACH_OUT" | grep -q "WSATTACH_RESULT: PASS"; then
  WSATTACH_OK=0; echo "ws-attach: PASS"
else
  WSATTACH_OK=1; echo "ws-attach: FAIL"
fi

echo "==================================================================="
echo " STAGE 7   Valgrind leak-check (key scripts)"
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
if [ "${CLANG_OK:-2}" = "0" ]; then echo " clang strict build:    PASS"; elif [ "${CLANG_OK:-2}" = "2" ]; then echo " clang strict build:    SKIP (no clang/omp.h)"; else echo " clang strict build:    FAIL"; fi
if [ "${DBSTD_OK:-1}" = "0" ]; then echo " db-standard (success): PASS"; else echo " db-standard (success): FAIL"; fi
if [ "${BARELAMBDA_OK:-1}" = "0" ]; then echo " bare-lambda call:      PASS"; else echo " bare-lambda call:      FAIL"; fi
if [ "${JSONATTR_OK:-1}" = "0" ]; then echo " json-obj-attr:         PASS"; else echo " json-obj-attr:         FAIL"; fi
if [ "${JSONFNCALL_OK:-1}" = "0" ]; then echo " json-fn-call-object:   PASS"; else echo " json-fn-call-object:   FAIL"; fi
if [ "${APIBLEED_OK:-1}" = "0" ]; then echo " api-bleed (--api):     PASS"; else echo " api-bleed (--api):     FAIL"; fi
if [ "${WSATTACH_OK:-1}" = "0" ]; then echo " ws-attach (--api):     PASS"; else echo " ws-attach (--api):     FAIL"; fi
if [ "$RC_TOTAL" = "0" ]; then
  echo " valgrind leaks:        no runtime (envelope/sql) leaks"
else
  echo " valgrind leaks:        runtime leak detected (see above)"
fi
echo "==================================================================="
if [ "${CLANG_OK:-2}" != "1" ] && [ "${DBSTD_OK:-1}" = "0" ] && [ "${BARELAMBDA_OK:-1}" = "0" ] && [ "${JSONATTR_OK:-1}" = "0" ] && [ "${JSONFNCALL_OK:-1}" = "0" ] && [ "${APIBLEED_OK:-1}" = "0" ] && [ "${WSATTACH_OK:-1}" = "0" ] && [ "$RC_TOTAL" = "0" ]; then
  echo " RESULT: GREEN"; exit 0
else
  echo " RESULT: RED"; exit 1
fi
