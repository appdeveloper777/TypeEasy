#!/bin/bash
# Regression suite. Corre desde el container typeeasy con cwd=/code (= typeeasycode/).
# Tras la reorganizacion de mayo 2026 los tests viven bajo examples/<categoria>/.
PASS=0; FAIL=0; SKIP=0; XFAIL=0
run_test() {
  local f="$1"; local expect="$2"
  local out
  out=$(/typeeasy/typeeasy "/code/$f" 2>/dev/null)
  local code=$?
  if [ $code -ne 0 ]; then
    echo "FAIL [exit=$code] $f"; FAIL=$((FAIL+1))
  elif [ -n "$expect" ] && [ "$out" != "$expect" ]; then
    echo "FAIL [expected='$expect' got='$out'] $f"; FAIL=$((FAIL+1))
  else
    echo "PASS $f"; PASS=$((PASS+1))
  fi
}
# Tests intentionally designed to error (e.g. demonstrate `let` immutability).
# Counted as XFAIL if exit != 0, FAIL if they unexpectedly pass.
run_xfail() {
  local f="$1"
  /typeeasy/typeeasy "/code/$f" >/dev/null 2>&1
  local code=$?
  if [ $code -ne 0 ]; then
    echo "XFAIL $f"; XFAIL=$((XFAIL+1))
  else
    echo "FAIL [unexpected pass] $f"; FAIL=$((FAIL+1))
  fi
}

# 01_basics
for t in test_arr test_arr_mut test_compound test_concat test_if test_logic test_map test_map_has test_math1 test_null test_nullaware test_print test_while test_min2 bucle_for unario_menos; do
  run_test "examples/01_basics/${t}.te" ""
done
# Tests that intentionally demonstrate `let` immutability (must error out).
for t in test_let_objeto test_let_string; do
  run_xfail "examples/01_basics/${t}.te"
done

# 02_oop
for t in crear_clase herencia_de_clases test_method_return test_producto_methods; do
  run_test "examples/02_oop/${t}.te" ""
done

# 03_functional
for t in lambdas_higher_order _test_filter _test_filter2 _test_filter3; do
  run_test "examples/03_functional/${t}.te" ""
done

# 04_stdlib_io
for t in stdlib_json_file_http test_stdlib test_interp test_try smoke_test; do
  run_test "examples/04_stdlib_io/${t}.te" ""
done

# 05_http_client (requiere typeeasy --api en :9000)
for t in test_http_full test_post_echo t_get2 t_post2 t_wf2; do
  run_test "examples/05_http_client/${t}.te" ""
done

# 06_csv
for t in test_csv_check _test_access; do
  run_test "examples/06_csv/${t}.te" ""
done

# Raiz: referenciados externamente
run_test "link_to_objects.te" ""

# 08_bytecode
run_test "examples/08_bytecode/bytecode_ext_mod_bit_shift.te" ""

# 09_ml (machine_learning.te requires ML runtime — known broken, skipped)
run_test "examples/09_ml/crear_imagen_plot.te" ""

# 10_errors (designed to demonstrate syntax errors)
run_xfail "examples/10_errors/test_errores_graves.te"

# 11_linq_objects
run_test "examples/11_linq_objects/linq_objetos.te" ""

# 13_linq_concepts (full LINQ surface)
for t in linq_aggregates linq_distinct linq_first_any_all linq_fusion linq_groupby linq_join linq_lazy linq_orderby linq_reduce linq_select linq_take_skip linq_tomap linq_where linq_zip; do
  run_test "examples/13_linq_concepts/${t}.te" ""
done

# 14_types_bool_datetime_uuid (v1.0.0)
for t in 01_bool 02_datetime 03_uuid 04_linq 05_linq_safe 06_linq_datetime; do
  run_test "examples/14_types_bool_datetime_uuid/${t}.te" ""
done

echo ""
echo "=== TOTAL: PASS=$PASS FAIL=$FAIL XFAIL=$XFAIL SKIP=$SKIP ==="
exit $FAIL
