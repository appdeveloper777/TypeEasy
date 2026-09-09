#!/usr/bin/env bash
# Regresión Fase 4: tras un fatal del runtime (longjmp al punto de recuperación
# del request handler) el estado de la VM queda limpio y el siguiente request
# responde correctamente. Alterna /fatal (500) y /ok (200) varias veces.
# Imprime "FATAL_RECOVERY_RESULT: PASS" / "FAIL".
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${1:-$HERE/../../src/typeeasy}"
PORT="${2:-8089}"

"$BIN" --api -p "$PORT" "$HERE/api_fatal_recovery.te" >/tmp/fatal_recovery_srv.log 2>&1 &
SRV=$!
for i in $(seq 1 40); do
  if curl -s "http://127.0.0.1:$PORT/ok" >/dev/null 2>&1; then break; fi
  sleep 0.25
done

fails=0
for round in 1 2 3; do
  code_f="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/fatal")"
  body_ok="$(curl -s "http://127.0.0.1:$PORT/ok")"
  code_ok="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/ok")"
  echo "round $round: /fatal=$code_f  /ok=$code_ok $body_ok"
  [[ "$code_f" == "500" ]] || fails=$((fails+1))
  [[ "$code_ok" == "200" ]] || fails=$((fails+1))
  echo "$body_ok" | grep -q '"suma":6' || fails=$((fails+1))
  echo "$body_ok" | grep -q '"caught":"si"' || fails=$((fails+1))
  echo "$body_ok" | grep -q 'OK_LIMPIO' || fails=$((fails+1))
done
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true

if [[ $fails -eq 0 ]]; then echo "FATAL_RECOVERY_RESULT: PASS"; else echo "FATAL_RECOVERY_RESULT: FAIL ($fails)"; fi
[[ $fails -eq 0 ]]
