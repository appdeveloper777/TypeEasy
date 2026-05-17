#!/bin/bash
# =============================================================
# typeeasycode/_regression_api.sh
#
# Regresion HTTP para los endpoints de ejemplo:
#   apis/example_httpget.te
#   apis/example_httppost.te
#
# Se ejecuta DESDE EL HOST (necesita docker + curl).
# Levanta dos containers efimeros del servicio `typeeasy`,
# uno en :9100 (GET) y otro en :9101 (POST), curle-a cada
# ruta, valida y los apaga.
#
# Uso:
#   bash typeeasycode/_regression_api.sh
# =============================================================

set -u
PASS=0; FAIL=0
RED=$'\e[31m'; GRN=$'\e[32m'; NC=$'\e[0m'

assert_eq() {
  local label="$1"; local expected="$2"; local got="$3"
  if [ "$got" = "$expected" ]; then
    echo "${GRN}PASS${NC} $label"; PASS=$((PASS+1))
  else
    echo "${RED}FAIL${NC} $label"
    echo "       expected: $expected"
    echo "       got     : $got"
    FAIL=$((FAIL+1))
  fi
}

assert_contains() {
  local label="$1"; local needle="$2"; local hay="$3"
  if echo "$hay" | grep -q -- "$needle"; then
    echo "${GRN}PASS${NC} $label"; PASS=$((PASS+1))
  else
    echo "${RED}FAIL${NC} $label"
    echo "       needle: $needle"
    echo "       got   : $hay"
    FAIL=$((FAIL+1))
  fi
}

start_api() {
  # $1 = script (cwd inside container is /code = typeeasycode/)
  # $2 = host port
  local script="$1"; local port="$2"
  local name="te_api_test_${port}"
  docker rm -f "$name" >/dev/null 2>&1 || true
  # -d detached, --rm auto-clean, publish port, override entrypoint args.
  docker compose run -d --rm --name "$name" \
      -p "${port}:${port}" \
      typeeasy --api "$script" --host 0.0.0.0 --port "$port" \
      >/dev/null
  echo "$name"
}

wait_ready() {
  local port="$1"; local path="$2"
  for i in $(seq 1 40); do
    if curl -s -o /dev/null -w '%{http_code}' --max-time 2 \
            "http://127.0.0.1:${port}${path}" 2>/dev/null \
       | grep -qE '^(200|404|405)$'; then
      return 0
    fi
    sleep 0.5
  done
  return 1
}

stop_api() {
  local name="$1"
  # show debug logs (POST println output) before tearing down.
  echo "--- logs $name ---"
  docker logs "$name" 2>&1 | tail -20 || true
  echo "------------------"
  docker rm -f "$name" >/dev/null 2>&1 || true
}

# ---------------- GET ----------------
echo ""
echo "=== HttpGet examples (apis/example_httpget.te @ :9100) ==="
GET_NAME=$(start_api "apis/example_httpget.te" 9100)
if ! wait_ready 9100 "/api/ping"; then
  echo "${RED}FAIL${NC} server GET did not become ready"
  FAIL=$((FAIL+1))
  stop_api "$GET_NAME"
else
  out=$(curl -s http://localhost:9100/api/ping)
  assert_eq "GET /api/ping" '{"pong":1}' "$out"

  out=$(curl -s http://localhost:9100/api/saludo/Ana)
  assert_eq "GET /api/saludo/Ana" '{"saludo":"Hola Ana"}' "$out"

  out=$(curl -s "http://localhost:9100/api/sumar?a=3&b=4")
  assert_eq "GET /api/sumar?a=3&b=4" '{"a":"3","b":"4"}' "$out"

  stop_api "$GET_NAME"
fi

# ---------------- POST ----------------
echo ""
echo "=== HttpPost examples (apis/example_httppost.te @ :9101) ==="
POST_NAME=$(start_api "apis/example_httppost.te" 9101)
if ! wait_ready 9101 "/api/echo"; then
  echo "${RED}FAIL${NC} server POST did not become ready"
  FAIL=$((FAIL+1))
  stop_api "$POST_NAME"
fi

out=$(curl -s -X POST -H 'Content-Type: text/plain' -d 'hola' http://localhost:9101/api/echo)
assert_contains "POST /api/echo body" '"received":"hola"' "$out"
assert_contains "POST /api/echo CT"   '"contentType":"text/plain"' "$out"

out=$(curl -s -X POST -H 'Content-Type: application/json' \
       -d '{"name":"Ana","age":30}' http://localhost:9101/api/users)
assert_eq "POST /api/users (Ana,30)" '{"name":"Ana","age":30}' "$out"

out=$(curl -s -X POST -H 'Content-Type: application/json' \
       -d '{"age":99,"name":"Zoe"}' http://localhost:9101/api/users)
assert_eq "POST /api/users (Zoe,99 reverse order)" '{"name":"Zoe","age":99}' "$out"

# Caso degradado: JSON parcial -> defaults para campos faltantes.
out=$(curl -s -X POST -H 'Content-Type: application/json' \
       -d '{"name":"SinEdad"}' http://localhost:9101/api/users)
assert_eq "POST /api/users (defaults)" '{"name":"SinEdad","age":0}' "$out"

stop_api "$POST_NAME"

echo ""
echo "=== TOTAL: PASS=$PASS FAIL=$FAIL ==="
exit $FAIL
