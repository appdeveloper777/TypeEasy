#!/usr/bin/env bash
# Regression: json(fnCall()) and xml(fnCall()) must NOT return empty body
# when the function returns an object literal { ... }.
# Prints "JSON_FNCALL_RESULT: PASS" / "FAIL".
set -u
BIN="${1:-/app/src/typeeasy}"
REPRO="${2:-/app/tests/regress/json_fn_call_object.te}"
PORT="${3:-8098}"

"$BIN" --api -p "$PORT" "$REPRO" >/tmp/jsonfncall_srv.log 2>&1 &
SRV=$!
# wait for server to be ready
for i in $(seq 1 30); do
  if curl -s "http://127.0.0.1:$PORT/a" >/dev/null 2>&1; then break; fi
  sleep 0.2
done

A="$(curl -s http://127.0.0.1:$PORT/a)"
C="$(curl -s http://127.0.0.1:$PORT/c)"
D="$(curl -s http://127.0.0.1:$PORT/d)"
X="$(curl -s http://127.0.0.1:$PORT/x)"
Z="$(curl -s http://127.0.0.1:$PORT/z)"

kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true

echo "A (json fnCall):  $A"
echo "C (json var):     $C"
echo "D (json inline):  $D"
echo "X (xml  fnCall):  $X"
echo "Z (xml  inline):  $Z"

FAILS=0
# json cases — booleans serialize as true/false in JSON
echo "$A" | grep -q '"ok":true'        || { echo "FAIL A: json(fnCall()) -> $A"; FAILS=$((FAILS+1)); }
echo "$A" | grep -q '"codigo":"ABC"'   || { echo "FAIL A: codigo missing"; FAILS=$((FAILS+1)); }
echo "$A" | grep -q '"n":5'            || { echo "FAIL A: n missing"; FAILS=$((FAILS+1)); }
[ -n "$A" ]                             || { echo "FAIL A: empty body"; FAILS=$((FAILS+1)); }
echo "$C" | grep -q '"ok":true'        || { echo "FAIL C: json(var) broken"; FAILS=$((FAILS+1)); }
echo "$D" | grep -q '"ok":true'        || { echo "FAIL D: json(inline) broken"; FAILS=$((FAILS+1)); }
# xml cases — booleans serialize as empty string in TypeEasy XML (existing behaviour);
# check string/int fields and non-empty body only.
echo "$X" | grep -q '<codigo>ABC</codigo>' || { echo "FAIL X: xml(fnCall()) codigo missing -> $X"; FAILS=$((FAILS+1)); }
echo "$X" | grep -q '<n>5</n>'         || { echo "FAIL X: xml(fnCall()) n missing"; FAILS=$((FAILS+1)); }
[ -n "$X" ]                             || { echo "FAIL X: empty body"; FAILS=$((FAILS+1)); }
echo "$Z" | grep -q '<codigo>ABC</codigo>' || { echo "FAIL Z: xml(inline) codigo missing -> $Z"; FAILS=$((FAILS+1)); }

if [ "$FAILS" -eq 0 ]; then
  echo "JSON_FNCALL_RESULT: PASS"
else
  echo "JSON_FNCALL_RESULT: FAIL ($FAILS checks failed)"
fi
