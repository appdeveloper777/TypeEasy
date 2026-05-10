#!/bin/bash
PASS=0; FAIL=0; SKIP=0
run_test() {
  local f="$1"; local expect="$2"
  local out
  out=$(./typeeasy "/code/$f" 2>/dev/null)
  local code=$?
  if [ $code -ne 0 ]; then
    echo "FAIL [exit=$code] $f"
    FAIL=$((FAIL+1))
  elif [ -n "$expect" ] && [ "$out" != "$expect" ]; then
    echo "FAIL [expected='$expect' got='$out'] $f"
    FAIL=$((FAIL+1))
  else
    echo "PASS $f"
    PASS=$((PASS+1))
  fi
}

# Core language tests
run_test "test_arr.te"       ""
run_test "test_arr_mut.te"   ""
run_test "test_compound.te"  ""
run_test "test_concat.te"    ""
run_test "test_if.te"        ""
run_test "test_logic.te"     ""
run_test "test_map.te"       ""
run_test "test_map_has.te"   ""
run_test "test_math1.te"     ""
run_test "test_null.te"      ""
run_test "test_nullaware.te" ""
run_test "test_print.te"     ""
run_test "test_try.te"       ""
run_test "test_while.te"     ""
run_test "test_interp.te"    ""
run_test "test_stdlib.te"    ""
run_test "test_let_objeto.te" ""
run_test "test_let_string.te" ""
run_test "test_method_return.te" ""
run_test "test_producto_methods.te" ""
run_test "crear_clase.te"    ""

# CSV tests
run_test "test_csv_check.te" "10000
Panetón_0,2"
run_test "bench_csv_big.te"  "200000"
run_test "link_to_objects.te" ""

# Filter tests
run_test "_test_filter.te"   ""
run_test "_test_access.te"   ""

echo ""
echo "=== TOTAL: PASS=$PASS FAIL=$FAIL SKIP=$SKIP ==="
