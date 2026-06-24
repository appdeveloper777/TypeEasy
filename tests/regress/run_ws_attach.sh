#!/usr/bin/env bash
# Regression: native [WebSocket] endpoints must actually be SERVED.
#
# Reproduces the 0.0.29/0.0.30 bug where a `[WebSocket("/ws/test")]` endpoint was
# parsed into the route table (shown in the route dump) but the WS subsystem
# printed "[WS] no WebSocket routes found" and every handshake returned HTTP 404.
# Root cause: the `MN` struct mirror in api_server/te_websocket.c had drifted from
# `struct MethodNode` (missing `guard_name`), so the route loop read `bc_body` as
# `next` -> stopped after the first method (and segfaulted on small apps).
#
# This script asserts, against a *small* standalone WS app:
#   1. the server starts WITHOUT crashing (the small-app segfault),
#   2. the startup log reports a registered WS route and NOT "no routes",
#   3. a raw WebSocket Upgrade handshake returns HTTP 101 (not 404).
#
# Prints "WSATTACH_RESULT: PASS" / "FAIL".
set -u
BIN="${1:-/app/src/typeeasy}"
REPRO="${2:-/app/tests/regress/ws_attach.te}"
PORT="${3:-8097}"

LOG=/tmp/wsattach_srv.log
rm -f "$LOG"
"$BIN" --api -p "$PORT" --host 127.0.0.1 "$REPRO" >"$LOG" 2>&1 &
SRV=$!

# wait for listen (HTTP control endpoint)
UP=""
for i in $(seq 1 40); do
  if ! kill -0 "$SRV" 2>/dev/null; then break; fi   # process died (segfault?)
  if curl -s "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then UP=1; break; fi
  sleep 0.25
done

ALIVE=0
kill -0 "$SRV" 2>/dev/null && ALIVE=1

# Raw WebSocket handshake: expect "HTTP/1.1 101" on a working build, "404" on the bug.
# Only attempted when a curl client is available; the no-crash / route-registered
# assertions below run regardless so the test stays meaningful in minimal images.
HAVE_CURL=0
command -v curl >/dev/null 2>&1 && HAVE_CURL=1
HANDSHAKE=""
if [ "$HAVE_CURL" = "1" ]; then
  HANDSHAKE="$(curl -s -i -N --max-time 5 \
    -H "Connection: Upgrade" -H "Upgrade: websocket" \
    -H "Sec-WebSocket-Version: 13" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    "http://127.0.0.1:$PORT/ws/test" 2>/dev/null | head -n1)"
fi

kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true

echo "alive=$ALIVE up=${UP:-0}"
echo "handshake=$HANDSHAKE"
echo "--- server log (ws lines) ---"
grep -i "\[WS\]" "$LOG" || true
echo "-----------------------------"

PASS=1
[ "$ALIVE" = "1" ] || { echo "FAIL: server crashed on startup (small-app segfault)"; PASS=0; }
if grep -qi "no WebSocket routes found" "$LOG"; then
  echo "FAIL: '[WS] no WebSocket routes found' (attach phase did not see the route)"
  PASS=0
fi
if ! grep -qi "\[WS\] registered .*ws/test" "$LOG"; then
  echo "FAIL: WS route /ws/test was never registered"
  PASS=0
fi
if [ "$HAVE_CURL" = "1" ]; then
  case "$HANDSHAKE" in
    *101*) : ;;
    *) echo "FAIL: handshake did not upgrade (got: ${HANDSHAKE:-<none>})"; PASS=0 ;;
  esac
else
  echo "NOTE: curl not available — skipping handshake (101) assertion"
fi

if [ "$PASS" = "1" ]; then
  echo "WSATTACH_RESULT: PASS"
else
  echo "WSATTACH_RESULT: FAIL"
fi
