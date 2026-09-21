#!/usr/bin/env bash
# Regresión B8 (ERP): al morir el padre del pool (`--workers N`) de forma abrupta
# (SIGKILL, crash), los workers quedaban huérfanos compartiendo el puerto por
# SO_REUSEPORT; el siguiente arranque convivía con binarios viejos ("intermitencia").
# Desde 0.1.8 re-tag #4 los workers llevan PR_SET_PDEATHSIG(SIGTERM) (Linux).
#   1. arranca --workers 2, espera /ok 200, cuenta procesos del pool (esperado 3)
#   2. kill -9 al padre → en < 3 s no queda NINGÚN proceso del pool ni listener en el puerto
#   3. arranque normal + SIGTERM al padre → cierra todo (ruta graceful + escalado)
# Uso: bash tests/regress/run_workers_pdeathsig.sh <typeeasy-bin> [puerto]
set -u
BIN="${1:-./src/typeeasy}"
PORT="${2:-8879}"
HERE="$(cd "$(dirname "$0")" && pwd)"
FIX="$HERE/ws_idle_starvation.te"
fails=0
count_pool() { pgrep -f -- "--port $PORT --host 127.0.0.1 $FIX" | wc -l | tr -d ' '; }
listeners() { if command -v ss >/dev/null 2>&1; then ss -tlnH "sport = :$PORT" 2>/dev/null | wc -l | tr -d ' '; else echo 0; fi; }
wait_ok() { for i in $(seq 1 60); do curl -s -m 1 -o /dev/null http://127.0.0.1:$PORT/ok && return 0; sleep 0.25; done; return 1; }

for round in kill9 term; do
  setsid "$BIN" --api --workers 2 --port "$PORT" --host 127.0.0.1 "$FIX" > /tmp/pdeathsig_$round.log 2>&1 < /dev/null &
  PARENT=$!
  wait_ok || { echo "FAIL[$round]: pool never answered /ok"; fails=$((fails+1)); kill -9 $PARENT 2>/dev/null; continue; }
  sleep 0.5
  before=$(count_pool); lb=$(listeners)
  if [ "$round" = kill9 ]; then kill -9 "$PARENT"; else kill -TERM "$PARENT"; fi
  for i in $(seq 1 12); do [ "$(count_pool)" = "0" ] && break; sleep 0.25; done
  after=$(count_pool); la=$(listeners)
  echo "$round: procesos antes=$before listeners=$lb -> despues=$after listeners=$la"
  [ "$before" -ge 3 ] || { echo "FAIL[$round]: expected parent+2 workers, got $before"; fails=$((fails+1)); }
  [ "$after" = "0" ] && [ "$la" = "0" ] || { echo "FAIL[$round]: orphaned workers/listeners left ($after procs, $la listeners)"; fails=$((fails+1)); pkill -9 -f -- "--port $PORT --host 127.0.0.1 $FIX"; }
  wait $PARENT 2>/dev/null
done
echo "WORKERS_PDEATHSIG_RESULT: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
[ $fails = 0 ]
