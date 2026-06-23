#!/usr/bin/env bash
# Regression: SQL envelope must NOT bleed result-sets across same-named locals
# in --api mode (a guard lambda using `c/r/d` then a handler using `c/r/d`).
# Asserts the handler endpoint returns HANDLER, not the guard's GUARD rows.
# Prints "APIBLEED_RESULT: PASS" / "FAIL".
set -u
BIN="${1:-/app/src/typeeasy}"
REPRO="${2:-/app/_repro/api_envelope_bleed.te}"
PORT="${3:-8088}"

rm -f /tmp/bugtest.db
"$BIN" --api -p "$PORT" "$REPRO" >/tmp/apibleed_srv.log 2>&1 &
SRV=$!
# wait for listen
for i in $(seq 1 20); do
  if curl -s "http://127.0.0.1:$PORT/test" >/dev/null 2>&1; then break; fi
  sleep 0.25
done
R1="$(curl -s http://127.0.0.1:$PORT/test)"
R2="$(curl -s http://127.0.0.1:$PORT/test)"
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true

echo "req1=$R1"
echo "req2=$R2"
if echo "$R1" | grep -q "HANDLER" && echo "$R2" | grep -q "HANDLER" \
   && ! echo "$R1$R2" | grep -q "GUARD"; then
  echo "APIBLEED_RESULT: PASS"
else
  echo "APIBLEED_RESULT: FAIL"
fi
