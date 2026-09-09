#!/usr/bin/env python3
"""Regresión: protocolo del debugger sidecar (`--debug-port`).

Cubre la regresión introducida en 0.1.0 (refactor Fase 3a) donde un rename
mecánico convirtió los literales del protocolo `"vars"` en `"g_vm.vars"`,
rompiendo la vista de variables de VS Code.

Flujo: arranca `typeeasy debug_vars.te --debug-port P`, se conecta, pone un
breakpoint en la línea 3, `start`, espera `stopped`, pide `stack` y `vars`
y verifica que la respuesta sea `{"resp":"vars","vars":[...]}` con `x`.

Uso:  python3 tests/regress/run_debugger_protocol.py --bin ./src/typeeasy
"""
from __future__ import annotations
import argparse, json, os, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "debug_vars.te")


def free_port() -> int:
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def connect(port: int, timeout: float = 10.0) -> socket.socket:
    t0 = time.time()
    while True:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5.0)
            s.settimeout(10.0)
            return s
        except OSError:
            if time.time() - t0 > timeout:
                raise
            time.sleep(0.1)


class Conn:
    def __init__(self, s: socket.socket):
        self.s = s; self.buf = b""
    def send(self, obj: dict) -> None:
        self.s.sendall((json.dumps(obj) + "\n").encode())
    def recv(self) -> dict:
        while b"\n" not in self.buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise EOFError("debugger cerró la conexión")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line.decode())
    def wait(self, key: str, val: str) -> dict:
        for _ in range(50):
            m = self.recv()
            if m.get(key) == val:
                return m
        raise AssertionError(f"no llegó {key}={val}")


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument("--bin", required=True); a = ap.parse_args()
    port = free_port()
    proc = subprocess.Popen([a.bin, SRC, "--debug-port", str(port)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    fails = []
    try:
        c = Conn(connect(port))
        c.wait("event", "initialized")
        # El protocolo identifica el archivo por su basename (así lo manda el adapter de VS Code).
        c.send({"cmd": "set_breakpoints", "file": os.path.basename(SRC), "lines": [3]})
        c.wait("resp", "ok")
        c.send({"cmd": "start"})
        st = c.wait("event", "stopped")
        if st.get("line") != 3:
            fails.append(f"stopped en línea {st.get('line')} (esperaba 3)")
        c.send({"cmd": "stack"})
        stack = c.wait("resp", "stack")
        if not stack.get("frames"):
            fails.append("stack sin frames")
        c.send({"cmd": "vars", "frame": 0})
        v = c.wait("resp", "vars")
        names = {e.get("name"): e for e in v.get("vars", [])}
        if "x" not in names:
            fails.append(f"vars sin 'x': {sorted(names)}")
        elif str(names["x"].get("value")) != "42":
            fails.append(f"x = {names['x'].get('value')!r} (esperaba 42)")
        c.send({"cmd": "continue"})
        c.wait("event", "terminated")
    except Exception as e:  # noqa: BLE001
        fails.append(f"excepción: {e!r}")
    finally:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    for f in fails:
        print("FAIL:", f)
    print("=== DEBUGGER PROTOCOL: " + ("PASS" if not fails else f"FAIL ({len(fails)})") + " ===")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
