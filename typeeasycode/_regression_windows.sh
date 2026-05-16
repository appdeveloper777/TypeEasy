#!/usr/bin/env bash
# Windows native regression — mirrors _regression.sh but runs the installer's
# typeeasy-bin.exe against the same file list. Run from typeeasycode/ in Git Bash.
#
# Usage:
#   bash _regression_windows.sh
#
# Override the binary path with TE_EXE if installed elsewhere:
#   TE_EXE="/c/Program Files/TypeEasy/bin/typeeasy-bin.exe" bash _regression_windows.sh
#
# Expected baseline (matches Linux Docker): PASS=36 FAIL=4
# Pre-existing FAILs (treated as known): test_if.te, test_let_objeto.te,
# test_let_string.te, test_csv_check.te — all related to let-reassignment semantics.

EXE="${TE_EXE:-/c/Users/$USER/AppData/Local/Programs/TypeEasy/bin/typeeasy-bin.exe}"
if [ ! -x "$EXE" ]; then
  echo "ERROR: typeeasy-bin.exe not found at: $EXE"
  echo "Set TE_EXE=/path/to/typeeasy-bin.exe or install from the v0.0.10 release."
  exit 2
fi

PASS=0; FAIL=0
run_test() {
  local f="$1"
  "$EXE" "$f" >/dev/null 2>&1
  local code=$?
  if [ $code -ne 0 ]; then
    echo "FAIL [exit=$code] $f"; FAIL=$((FAIL+1))
  else
    echo "PASS $f"; PASS=$((PASS+1))
  fi
}

for t in test_arr test_arr_mut test_compound test_concat test_if test_logic test_map test_map_has test_math1 test_null test_nullaware test_print test_while test_let_objeto test_let_string test_min2 bucle_for unario_menos; do
  run_test "examples/01_basics/${t}.te"
done
for t in crear_clase herencia_de_clases test_method_return test_producto_methods; do
  run_test "examples/02_oop/${t}.te"
done
for t in lambdas_higher_order _test_filter _test_filter2 _test_filter3; do
  run_test "examples/03_functional/${t}.te"
done
for t in stdlib_json_file_http test_stdlib test_interp test_try smoke_test; do
  run_test "examples/04_stdlib_io/${t}.te"
done
for t in test_http_full test_post_echo t_get2 t_post2 t_wf2; do
  run_test "examples/05_http_client/${t}.te"
done
for t in test_csv_check _test_access; do
  run_test "examples/06_csv/${t}.te"
done
run_test "link_to_objects.te"
run_test "examples/08_bytecode/bytecode_ext_mod_bit_shift.te"

echo ""
echo "=== TOTAL: PASS=$PASS FAIL=$FAIL ==="
exit $FAIL
