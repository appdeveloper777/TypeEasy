#!/usr/bin/env python3
"""Regresión 0.1.9: WebSocket y HTTP comparten UN solo lock de intérprete.

Hasta 0.1.8 los callbacks WS (api_server/te_websocket.c) ejecutaban el handler
`.te` bajo su propio mutex, en paralelo con un handler HTTP en curso. El reset de
fin de request del hilo WS borraba las variables del handler HTTP y cerraba sus
conexiones DB request-scoped: SIGSEGV en mysql_stmt_prepare (demo-restaurante,
2026-09-20).

Qué verifica (contra tests/regress/ws_http_lock.te):
  1. Varios GET /slow (handler síncrono que retiene el lock ~700 ms y luego lee
     un local declarado ANTES de dormir) mientras se abren handshakes WS que
     ejecutan un handler .te. Todos los /slow deben responder 200 con su marker
     intacto; cada WS debe recibir el frame "hello:" del handler.
  2. Un handler WS con error fatal de runtime (/ws/boom) NO mata el proceso:
     el server sigue vivo y /ok responde 200.

Uso:  python3 tests/regress/run_ws_http_lock.py --bin ./src/typeeasy [--port P]
Imprime "WS_HTTP_LOCK_RESULT: PASS" / "FAIL".
"""
from __future__ import annotations
import argparse, functools, os, socket, subprocess, sys, threading, time, urllib.request

print = functools.partial(print, flush=True)  # sobrevive a `timeout` con stdout en pipe

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "ws_http_lock.te")


def free_port() -> int:
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def http_get(port: int, path: str, timeout: float = 60.0):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # conexión rechazada / reset = server caído
        return 0, f"{type(e).__name__}: {e}"


def ws_handshake(port: int, path: str, timeout: float = 60.0) -> tuple[bool, bytes]:
    """Handshake WS crudo. Devuelve (101 recibido, primeros bytes tras el 101)."""
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.settimeout(timeout)
    req = (f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
           "Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").encode()
    s.sendall(req)
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    ok = buf.startswith(b"HTTP/1.1 101")
    head, _, rest = buf.partition(b"\r\n\r\n")
    # Con el lock compartido el handler WS espera su turno FIFO detrás de los
    # /slow en cola (hasta slow*700 ms): el frame puede tardar varios segundos.
    deadline = time.time() + min(timeout, 20.0)
    while ok and b"hello" not in rest and time.time() < deadline:
        s.settimeout(max(0.5, deadline - time.time()))
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            rest += chunk
        except (socket.timeout, OSError):
            break
    try:
        s.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    s.close()
    return ok, rest


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "..", "src", "typeeasy"))
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--slow", type=int, default=6, help="GET /slow concurrentes")
    ap.add_argument("--ws", type=int, default=8, help="handshakes WS durante los /slow")
    a = ap.parse_args()
    port = a.port or free_port()
    log = open("/tmp/ws_http_lock_srv.log", "wb") if os.name != "nt" else open(os.path.join(os.environ.get("TEMP", "."), "ws_http_lock_srv.log"), "wb")
    srv = subprocess.Popen([a.bin, "--api", "-p", str(port), "--host", "127.0.0.1", FIXTURE], stdout=log, stderr=subprocess.STDOUT)
    fails: list[str] = []
    try:
        for _ in range(80):
            if srv.poll() is not None:
                break
            if http_get(port, "/ok", 2.0)[0] == 200:
                break
            time.sleep(0.25)
        else:
            fails.append("server never became ready")
        if srv.poll() is not None:
            fails.append(f"server died at startup rc={srv.returncode}")

        # --- Fase 1: HTTP lentos + WS entrelazados -----------------------------
        results: dict[int, tuple[int, str]] = {}
        def slow(k: int) -> None:
            results[k] = http_get(port, f"/slow?id={k}", 120.0)
        threads = [threading.Thread(target=slow, args=(k,), daemon=True) for k in range(a.slow)]
        for t in threads:
            t.start()
            time.sleep(0.05)
        time.sleep(0.2)   # los /slow ya tomaron/esperan el lock: ahora entran los WS
        ws_ok = 0
        for _ in range(a.ws):
            if srv.poll() is not None:
                break
            ok, rest = ws_handshake(port, "/ws/test")
            if ok and b"hello" in rest:
                ws_ok += 1
            else:
                fails.append(f"ws handshake/frame failed: ok={ok} rest={rest[:60]!r}")
        for t in threads:
            t.join(150.0)
        for k in range(a.slow):
            code, body = results.get(k, (0, "<no result>"))
            if code != 200 or f'"marker":"ALIVE-{k}"' not in body:
                fails.append(f"/slow?id={k} -> {code} {body[:120]}")
        print(f"phase1: slow_ok={sum(1 for k in range(a.slow) if results.get(k, (0,''))[0] == 200)}/{a.slow} ws_ok={ws_ok}/{a.ws}")
        if srv.poll() is not None:
            fails.append(f"server died during phase 1 rc={srv.returncode}")

        # --- Fase 2: fatal dentro de un handler WS no mata el proceso ----------
        if srv.poll() is None:
            ok, _ = ws_handshake(port, "/ws/boom", 30.0)
            time.sleep(0.5)
            alive = srv.poll() is None
            code, body = http_get(port, "/ok", 10.0)
            print(f"phase2: boom_handshake={ok} alive={alive} /ok={code}")
            if not ok:
                fails.append("ws /ws/boom handshake did not upgrade")
            if not alive:
                fails.append(f"server died after a runtime error inside a WS handler rc={srv.returncode}")
            if code != 200:
                fails.append(f"/ok after ws fatal -> {code} {body[:120]}")
            code, body = http_get(port, "/slow?id=final", 30.0)
            if code != 200 or '"marker":"ALIVE-final"' not in body:
                fails.append(f"/slow after ws fatal -> {code} {body[:120]}")
    finally:
        if srv.poll() is None:
            srv.terminate()
            try:
                srv.wait(10)
            except subprocess.TimeoutExpired:
                srv.kill()
        log.close()
    for f in fails:
        print("FAIL:", f)
    print("WS_HTTP_LOCK_RESULT:", "PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
