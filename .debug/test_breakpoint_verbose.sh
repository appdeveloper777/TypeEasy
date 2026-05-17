#!/bin/bash
# Verbose breakpoint validation: shows what the debugger hook actually sees.
set -u
docker rm -f typeeasy_dbg_e2e >/dev/null 2>&1 || true
LOG=/tmp/te_bp_verbose.log
rm -f "$LOG"

MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' docker compose run --rm \
  --name typeeasy_dbg_e2e \
  -e TYPEEASY_DEBUG_VERBOSE=1 \
  -p 4712:4712 -p 8082:8082 \
  typeeasy \
  --debug-port 4712 \
  --api /code/apis/example_httppost.te \
  --host 0.0.0.0 --port 8082 \
  > "$LOG" 2>&1 &
SPAWN_PID=$!

# Give container time to start (Windows Docker port-forward can be slow)
for i in $(seq 1 30); do
  if (echo > /dev/tcp/127.0.0.1/4712) 2>/dev/null; then break; fi
  sleep 1
done

python <<'PY'
import socket, json, time, urllib.request, threading
s = socket.create_connection(("127.0.0.1", 4712), timeout=10)
buf = bytearray()
def readln(timeout=8.0):
    s.settimeout(timeout)
    while b"\n" not in buf:
        buf.extend(s.recv(4096))
    i = buf.index(b"\n"); line = bytes(buf[:i]); del buf[:i+1]
    return json.loads(line)
def send(o):
    s.sendall((json.dumps(o)+"\n").encode())
    print("CLIENT -> "+json.dumps(o))
print("CLIENT <-", readln())
# Try a wider range of lines just in case our numbering is off
send({"cmd":"set_breakpoints","file":"example_httppost.te","lines":[36,37,38,42,43,44,55,56,57,58,59,60,61,62,63,64,65]})
print("CLIENT <-", readln())
send({"cmd":"start"})
print("CLIENT <-", readln())
time.sleep(2)
results = []
def fire():
    try:
        req = urllib.request.Request("http://127.0.0.1:8082/api/users",
            data=b'{"name":"Ana","age":30}',
            headers={"Content-Type":"application/json"}, method="POST")
        results.append(("ok", urllib.request.urlopen(req, timeout=10).read().decode()))
    except Exception as e:
        results.append(("err", repr(e)))
threading.Thread(target=fire, daemon=True).start()
# Wait for stopped event for up to 6s
try:
    ev = readln(timeout=6.0)
    print("CLIENT <- STOPPED?", ev)
except Exception as e:
    print("CLIENT <- (no event)", e)
print("HTTP result:", results)
try: s.sendall(b'{"cmd":"continue"}\n')
except Exception: pass
time.sleep(1)
PY

echo "--- CONTAINER LOG (first 80 lines) ---"
head -80 "$LOG"
echo "--- CONTAINER LOG (TYPEEASY_DEBUG_VERBOSE lines) ---"
grep -E 'typeeasy-debugger|debug\]' "$LOG" || echo "(no debugger verbose lines found)"
docker rm -f typeeasy_dbg_e2e >/dev/null 2>&1 || true
wait $SPAWN_PID 2>/dev/null || true
