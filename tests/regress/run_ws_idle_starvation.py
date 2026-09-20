#!/usr/bin/env python3
"""Regresión 0.1.9: WebSockets ociosos/muertos NO deben agotar el pool de workers.

Cada WebSocket abierto ocupa un hilo worker de civetweb durante toda su vida.
Hasta 0.1.8 el servidor arrancaba con `num_threads=8` fijo y sin
`websocket_timeout_ms`/`enable_websocket_ping_pong`: bastaban 8 sockets
"medio muertos" (el cliente desapareció detrás de un proxy con read timeout de
24 h) para que el proceso siguiera vivo pero no respondiera NINGUNA petición
HTTP (caída del ajedrez en JunX, 2026-09-20: 8 conexiones ESTABLISHED con
keepalive de 73-116 min y cero requests atendidos).

Qué verifica (contra tests/regress/ws_idle_starvation.te):
  fase 1  Pool por defecto (TYPEEASY_NUM_THREADS=64): se abren 12 WS ociosos
          (> los 8 viejos) y GET /ok debe responder 200 en < 5 s.
  fase 2  Pool mínimo forzado (TYPEEASY_NUM_THREADS=3, TYPEEASY_WS_TIMEOUT_MS=400):
          3 WS que NUNCA contestan PING agotan el pool (GET /ok se cuelga: se
          comprueba que en 1.5 s no responde, prueba de que el escenario es real);
          el ping/pong debe cerrar esos sockets tras ~6×timeout y GET /ok vuelve
          a 200 antes de 15 s. Los 3 sockets deben quedar cerrados por el server.
  fase 3  Un WS SANO que responde PONG sobrevive > 6×timeout (no se corta a
          clientes vivos).

Uso:  python3 tests/regress/run_ws_idle_starvation.py --bin ./src/typeeasy [--port P]
Imprime "WS_IDLE_STARVATION_RESULT: PASS" / "FAIL".
"""
from __future__ import annotations
import argparse, functools, os, socket, subprocess, sys, time, urllib.request

print = functools.partial(print, flush=True)

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "ws_idle_starvation.te")


def free_port() -> int:
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def http_get(port: int, path: str, timeout: float):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # timeout / reset
        return 0, f"{type(e).__name__}: {e}"


def ws_open(port: int, path: str, timeout: float = 10.0) -> socket.socket | None:
    """Handshake WS crudo; devuelve el socket abierto (sin leer nada más) o None."""
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.sendall((f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
               "Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").encode())
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    if not buf.startswith(b"HTTP/1.1 101"):
        s.close()
        return None
    return s


def ws_frame(opcode: int, payload: bytes = b"") -> bytes:
    """Frame cliente→servidor (enmascarado, payload < 126)."""
    mask = b"\x12\x34\x56\x78"
    body = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + body


def pong_loop(s: socket.socket, seconds: float) -> tuple[int, bool]:
    """Lee frames del server durante `seconds`; contesta PONG a cada PING.
    Devuelve (pings_recibidos, server_cerró)."""
    s.settimeout(0.25)
    pings, closed, buf = 0, False, b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            closed = True; break
        if not chunk:
            closed = True; break
        buf += chunk
        while len(buf) >= 2:
            op, ln = buf[0] & 0x0F, buf[1] & 0x7F
            hdr = 2 + (2 if ln == 126 else 8 if ln == 127 else 0)
            if len(buf) < hdr:
                break
            if ln == 126: ln = int.from_bytes(buf[2:4], "big")
            elif ln == 127: ln = int.from_bytes(buf[2:10], "big")
            if len(buf) < hdr + ln:
                break
            buf = buf[hdr + ln:]
            if op == 0x9:
                pings += 1
                s.sendall(ws_frame(0xA))
            elif op == 0x8:
                closed = True
    return pings, closed


def is_closed_by_server(s: socket.socket, wait: float) -> bool:
    s.settimeout(wait)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                return True
    except socket.timeout:
        return False
    except OSError:
        return True


def start_server(binary: str, port: int, env_extra: dict, log_path: str):
    env = dict(os.environ); env.update(env_extra)
    log = open(log_path, "wb")
    srv = subprocess.Popen([binary, "--api", "-p", str(port), "--host", "127.0.0.1", FIXTURE],
                           stdout=log, stderr=subprocess.STDOUT, env=env)
    for _ in range(80):
        if srv.poll() is not None or http_get(port, "/ok", 2.0)[0] == 200:
            break
        time.sleep(0.25)
    return srv, log


def stop_server(srv, log):
    if srv.poll() is None:
        srv.terminate()
        try:
            srv.wait(10)
        except subprocess.TimeoutExpired:
            srv.kill()
    log.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "..", "src", "typeeasy"))
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--idle", type=int, default=12, help="WS ociosos en fase 1 (> 8 viejos)")
    a = ap.parse_args()
    tmp = "/tmp" if os.name != "nt" else os.environ.get("TEMP", ".")
    fails: list[str] = []

    # --- Fase 1: pool por defecto aguanta > 8 WS ociosos ------------------------
    port = a.port or free_port()
    srv, log = start_server(a.bin, port, {}, os.path.join(tmp, "ws_idle_p1.log"))
    socks = []
    try:
        if srv.poll() is not None:
            fails.append(f"phase1: server died at startup rc={srv.returncode}")
        for k in range(a.idle):
            s = ws_open(port, "/ws/idle")
            if s is None:
                fails.append(f"phase1: ws #{k} handshake failed"); break
            socks.append(s)
        t0 = time.time()
        code, body = http_get(port, "/ok", 5.0)
        dt = time.time() - t0
        print(f"phase1: idle_ws={len(socks)}/{a.idle} /ok={code} in {dt:.2f}s")
        if code != 200:
            fails.append(f"phase1: /ok with {len(socks)} idle WS -> {code} {body[:80]} (pool starved)")
    finally:
        for s in socks:
            try: s.close()
            except OSError: pass
        stop_server(srv, log)

    # --- Fase 2: pool mínimo; sockets muertos se reaprovechan vía ping/pong ----
    port = a.port or free_port()
    env2 = {"TYPEEASY_NUM_THREADS": "3", "TYPEEASY_WS_TIMEOUT_MS": "400"}
    srv, log = start_server(a.bin, port, env2, os.path.join(tmp, "ws_idle_p2.log"))
    dead = []
    try:
        if srv.poll() is not None:
            fails.append(f"phase2: server died at startup rc={srv.returncode}")
        for k in range(3):
            s = ws_open(port, "/ws/idle")
            if s is None:
                fails.append(f"phase2: ws #{k} handshake failed"); break
            dead.append(s)
        starved = http_get(port, "/ok", 1.5)[0]
        t0 = time.time()
        code, body = http_get(port, "/ok", 15.0)
        dt = time.time() - t0
        closed = sum(1 for s in dead if is_closed_by_server(s, 3.0))
        print(f"phase2: starved_probe={starved} recovered=/ok {code} after {dt:.1f}s dead_closed={closed}/{len(dead)}")
        if starved == 200:
            print("phase2: WARN starvation probe answered 200 (pool not saturated?)")
        if code != 200:
            fails.append(f"phase2: /ok never recovered after dead WS -> {code} {body[:80]}")
        if closed != len(dead):
            fails.append(f"phase2: server kept {len(dead) - closed} dead WS open (no ping/pong reaping)")
        if srv.poll() is not None:
            fails.append(f"phase2: server died rc={srv.returncode}")

        # --- Fase 3: un WS vivo que contesta PONG no se corta ---------------------
        live = ws_open(port, "/ws/idle")
        if live is None:
            fails.append("phase3: ws handshake failed")
        else:
            pings, was_closed = pong_loop(live, 4.0)   # 4 s = 10× timeout de 400 ms
            print(f"phase3: pings={pings} closed_by_server={was_closed}")
            if pings < 2:
                fails.append(f"phase3: expected PINGs from server, got {pings} (ping/pong disabled?)")
            if was_closed:
                fails.append("phase3: server closed a live WS that answered PONG")
            live.close()
    finally:
        for s in dead:
            try: s.close()
            except OSError: pass
        stop_server(srv, log)

    for f in fails:
        print("FAIL:", f)
    print("WS_IDLE_STARVATION_RESULT:", "PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
