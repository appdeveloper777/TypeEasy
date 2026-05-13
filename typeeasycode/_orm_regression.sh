#!/bin/bash
# Regression del fast-path ORM. Requiere acceso de red a TiDB Cloud.
# Uso: docker compose run --rm --entrypoint bash typeeasy /code/_orm_regression.sh
set -u
PASS=0; FAIL=0
expect_line() {
    local marker="$1"
    if echo "$OUT" | grep -q "^${marker}$"; then
        echo "PASS  ${marker}"
        PASS=$((PASS+1))
    else
        echo "FAIL  ${marker}"
        FAIL=$((FAIL+1))
    fi
}

OUT=$(/typeeasy/typeeasy /code/test_orm_fast.te 2>&1)
echo "----- raw output -----"
echo "$OUT"
echo "----------------------"
expect_line FAST_OK_LIST
expect_line FAST_OK_LEN
expect_line FAST_OK_TYPES
expect_line FAST_OK_ITER
expect_line FAST_OK_PARAMS

echo ""
echo "=== ORM-FAST REGRESSION: PASS=$PASS FAIL=$FAIL ==="
[ $FAIL -eq 0 ]
