"""
End-to-end validation of the TypeEasy debugger in --api mode.

Flow:
  1. Spawn `docker compose run --rm -p 4712:4712 -p 8081:8081 typeeasy
     --debug-port 4712 --api /code/apis/example_httppost.te
     --host 0.0.0.0 --port 8081` in the background.
  2. Connect TCP to 127.0.0.1:4712 and set breakpoints at several
     places (handler body, inside method called from handler, top-level
     class field, etc.) using the line-based JSON protocol.
  3. Send `start`, fire a POST against the published HTTP port, and
     verify a `stopped` event arrives with the right line.
  4. Ask for `stack` and `vars` to make sure they're populated.
  5. Send `continue`, expect the HTTP request to complete.
  6. Repeat for a second breakpoint location to prove multi-hit works.

Run from the repo root (Git Bash / WSL on Windows):
    python .debug/test_breakpoint_api.py
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.error

DBG_PORT = 4712
HTTP_PORT = 8082
SRC_FILE = "example_httppost.te"
CONTAINER_NAME = "typeeasy_dbg_e2e"

# Lines we want to break on inside apis/example_httppost.te.
# (1-based, matching the file as it exists on disk)
BREAKPOINTS = [42, 56, 57]  # echo() debug_log, crearUsuario debug_log, next one


def _read_lines(sock: socket.socket, buf: bytearray, timeout: float = 5.0):
    """Yield one JSON object per terminator '\\n'. Blocks until at least one
    line is available or timeout elapses (then raises)."""
    sock.settimeout(timeout)
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("debugger socket closed")
        buf.extend(chunk)
    while b"\n" in buf:
        idx = buf.index(b"\n")
        line = bytes(buf[:idx]).decode("utf-8", errors="replace").strip()
        del buf[: idx + 1]
        if not line:
            continue
        try:
            yield json.loads(line)
        except json.JSONDecodeError:
            print(f"[client] non-json line: {line!r}")


def _send(sock: socket.socket, obj: dict) -> None:
    payload = (json.dumps(obj) + "\n").encode("utf-8")
    sock.sendall(payload)
    print(f"[client] -> {obj}")


def _wait_event(sock: socket.socket, buf: bytearray, *,
                resp: str | None = None,
                event: str | None = None,
                timeout: float = 8.0) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, deadline - time.time())
        for msg in _read_lines(sock, buf, timeout=remaining):
            print(f"[client] <- {msg}")
            if event and msg.get("event") == event:
                return msg
            if resp and msg.get("resp") == resp:
                return msg
    raise TimeoutError(f"timeout waiting for resp={resp} event={event}")


def kill_container() -> None:
    subprocess.run(
        ["docker", "rm", "-f", CONTAINER_NAME],
        capture_output=True, text=True, check=False,
    )


def spawn_api() -> subprocess.Popen:
    kill_container()
    cmd = [
        "docker", "compose", "run", "--rm",
        "--name", CONTAINER_NAME,
        "-p", f"{DBG_PORT}:{DBG_PORT}",
        "-p", f"{HTTP_PORT}:{HTTP_PORT}",
        "typeeasy",
        "--debug-port", str(DBG_PORT),
        "--api", f"/code/apis/{SRC_FILE}",
        "--host", "0.0.0.0",
        "--port", str(HTTP_PORT),
    ]
    print("[spawn] " + " ".join(cmd))
    env = dict(os.environ)
    env["MSYS_NO_PATHCONV"] = "1"
    env["MSYS2_ARG_CONV_EXCL"] = "*"
    p = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1, env=env,
    )
    # Drain container output continuously so the pipe never blocks, prefixed.
    def _pump():
        try:
            for line in p.stdout:
                sys.stdout.write("[container] " + line)
                sys.stdout.flush()
        except Exception:
            pass
    threading.Thread(target=_pump, daemon=True).start()
    return p


def connect_debugger(timeout: float = 25.0) -> socket.socket:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", DBG_PORT), timeout=2.0)
            print(f"[client] connected to debugger on port {DBG_PORT}")
            return s
        except OSError as e:
            last = e
            time.sleep(0.4)
    raise TimeoutError(f"could not connect to debugger: {last}")


def post_json_async(path: str, payload: dict, results: list, key: str) -> threading.Thread:
    def _do():
        url = f"http://127.0.0.1:{HTTP_PORT}{path}"
        body = json.dumps(payload).encode()
        req = urllib.request.Request(
            url, data=body, method="POST",
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                results.append((key, r.status, r.read().decode()))
        except Exception as e:
            results.append((key, -1, repr(e)))
    th = threading.Thread(target=_do, daemon=True)
    th.start()
    return th


def main() -> int:
    proc = spawn_api()
    sock = None
    try:
        sock = connect_debugger()
        buf = bytearray()

        # The interpreter emits {"event":"initialized"} on connect.
        _wait_event(sock, buf, event="initialized", timeout=5.0)

        # Set breakpoints (basename-matched).
        _send(sock, {
            "cmd": "set_breakpoints",
            "file": SRC_FILE,
            "lines": BREAKPOINTS,
        })
        # Server may ack — drain anything available briefly.
        try:
            _wait_event(sock, buf, resp="set_breakpoints", timeout=1.0)
        except TimeoutError:
            pass

        # Release the interpreter (it was blocked in debugger_init).
        _send(sock, {"cmd": "start"})

        # Give the API server ~3s to start listening, then fire a POST.
        time.sleep(3.0)
        results: list = []
        th = post_json_async("/api/users", {"name": "Ana", "age": 30},
                             results, "crearUsuario")

        # Expect a stopped event at line 56 (first debug_log of crearUsuario).
        stopped = _wait_event(sock, buf, event="stopped", timeout=15.0)
        assert stopped.get("line") == 56, \
            f"expected stop at line 56, got {stopped.get('line')}"

        # Ask for stack + vars.
        _send(sock, {"cmd": "stack"})
        stack = _wait_event(sock, buf, resp="stack", timeout=5.0)
        assert stack.get("frames"), "stack frames empty"

        _send(sock, {"cmd": "vars", "frame": 0})
        vars_resp = _wait_event(sock, buf, resp="vars", timeout=5.0)
        var_names = [v.get("name") for v in vars_resp.get("vars") or []]
        print(f"[client] vars at crearUsuario: {var_names}")
        assert "u" in var_names, f"expected 'u' in vars, got {var_names}"

        # Continue — should hit the next BP at line 57.
        _send(sock, {"cmd": "continue"})
        stopped2 = _wait_event(sock, buf, event="stopped", timeout=10.0)
        assert stopped2.get("line") == 57, \
            f"expected stop at line 57, got {stopped2.get('line')}"

        # Continue to end of handler. HTTP request should complete.
        _send(sock, {"cmd": "continue"})
        th.join(timeout=15.0)
        assert results, "no HTTP response captured"
        key, status, body = results[0]
        print(f"[client] HTTP {status}: {body}")
        assert status == 200, f"expected 200, got {status}"
        assert '"name":"Ana"' in body and '"age":30' in body, \
            f"unexpected body: {body}"

        # --- 2nd request: hit echo() at line 42 ---
        results.clear()
        th = post_json_async("/api/echo", {"hello": "world"},
                             results, "echo")
        stopped3 = _wait_event(sock, buf, event="stopped", timeout=15.0)
        assert stopped3.get("line") == 42, \
            f"expected stop at line 42, got {stopped3.get('line')}"
        _send(sock, {"cmd": "continue"})
        th.join(timeout=15.0)
        assert results and results[0][1] == 200, \
            f"echo response failed: {results}"
        print(f"[client] echo HTTP {results[0][1]}: {results[0][2]}")

        # Clean disconnect.
        _send(sock, {"cmd": "disconnect"})
        print("[client] OK — all breakpoint asserts passed")
        return 0
    finally:
        if sock:
            try: sock.close()
            except Exception: pass
        kill_container()
        try: proc.wait(timeout=3)
        except Exception:
            try: proc.kill()
            except Exception: pass


if __name__ == "__main__":
    sys.exit(main())
