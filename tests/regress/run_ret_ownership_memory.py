#!/usr/bin/env python3
"""RSS regression for temporaries left behind by `__ret__` assignments (--api).

Starts a private, synthetic TypeEasy API process (ret_ownership_memory.te, which
uses an in-memory SQLite database and no external service) and, for each route,
drives a warm-up batch followed by a measured batch of requests. Every route
repeats one family of calls whose result is stored in `__ret__`:

  sql      SQL envelope ({success,data|error}) of sql_exec / sql_query
  lasterr  sql_last_error()
  env      env("K", default) / env("K") ?? default
  str      string methods (trim, upper, contains, substring, pad_*, repeat, ...)
  conv     to_int / to_float, null-safe `?.`, list/map length/contains
  loop     hot numeric loop (bytecode cache recompiled after every request)
  guard    endpoint protected by a `@guard` decorator

The run fails (exit 1) when RSS grows by more than the per-route budget during
the measured batch. Every response is checked against its expected JSON so a
broken route cannot pass by doing nothing. Linux only (reads /proc); the SQL
route needs the SQLite plugin installed in ~/.te/packages/sqlite.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("ret_ownership_memory.te")
KIB = 1024
GUARD_TOKEN = "__RET_GUARD_FILL__"
GUARD_FILL_BYTES = 512

# route -> expected JSON body
ROUTES = {
    "/control": {"ok": 1},
    "/sql": {"ok": 36},
    "/lasterr": {"ok": 60},
    "/env": {"ok": 120},
    "/str": {"ok": 30},
    "/conv": {"ok": 40},
    "/loop": {"ok": 1},
    "/guard": {"ok": 1},
}
DEFAULT_REQUESTS = 600
DEFAULT_WARMUP = 100
DEFAULT_BUDGET_KIB = 64


def free_port() -> int:
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        return int(server.getsockname()[1])


def rss_bytes(pid: int) -> int:
    with open(f"/proc/{pid}/status", encoding="ascii") as status:
        for line in status:
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * KIB
    raise RuntimeError("VmRSS no disponible para el proceso de prueba")


def get_json(port: int, route: str, timeout: float = 10.0) -> dict:
    request = urllib.request.Request(f"http://127.0.0.1:{port}{route}", method="GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
            if response.status != 200:
                raise RuntimeError(f"{route} devolvió HTTP {response.status}")
    except urllib.error.HTTPError as error:
        error.read()
        raise RuntimeError(f"{route} devolvió HTTP {error.code}") from None
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise RuntimeError(f"{route} no devolvió un objeto JSON")
    return value


def wait_ready(process: subprocess.Popen, port: int, timeout: float = 20.0) -> None:
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"El proceso TypeEasy terminó con código {process.returncode}")
        try:
            if get_json(port, "/control") == ROUTES["/control"]:
                return
        except (OSError, RuntimeError, json.JSONDecodeError) as error:
            last_error = type(error).__name__
        time.sleep(0.1)
    raise RuntimeError(f"API TypeEasy no inició dentro del plazo ({last_error or 'sin respuesta'})")


def hit(process: subprocess.Popen, port: int, route: str, count: int) -> None:
    expected = ROUTES[route]
    for _ in range(count):
        if process.poll() is not None:
            raise RuntimeError(f"TypeEasy terminó durante {route} con código {process.returncode}")
        if get_json(port, route) != expected:
            raise RuntimeError(f"La respuesta de {route} no coincide con el contrato esperado")


def measure(process: subprocess.Popen, port: int, route: str, warmup: int, count: int) -> dict:
    hit(process, port, route, warmup)          # asienta arenas/cachés perezosas
    time.sleep(0.05)
    before = rss_bytes(process.pid)
    hit(process, port, route, count)
    time.sleep(0.05)
    after = rss_bytes(process.pid)
    return {"requests": count, "rssBeforeBytes": before, "rssAfterBytes": after,
            "rssDeltaBytes": after - before}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", default=str(ROOT / "src" / "typeeasy"), help="binario TypeEasy nativo")
    parser.add_argument("--requests", type=int, default=DEFAULT_REQUESTS, help="requests medidos por ruta")
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help="requests de calentamiento por ruta")
    parser.add_argument("--budget-kib", type=float, default=DEFAULT_BUDGET_KIB,
                        help="crecimiento máximo de RSS permitido por ruta durante el lote medido")
    parser.add_argument("--route", action="append", choices=sorted(ROUTES), help="limitar a estas rutas")
    args = parser.parse_args()

    if not sys.platform.startswith("linux"):
        parser.error("esta regresión necesita /proc y solo corre en Linux")
    if args.requests < 1 or args.warmup < 0 or args.budget_kib <= 0:
        parser.error("requests, warmup y budget-kib deben ser positivos")
    binary = Path(args.bin).resolve()
    if not binary.is_file():
        parser.error(f"binario TypeEasy no encontrado: {binary}")

    routes = args.route or [r for r in ROUTES if r != "/control"]
    port = free_port()
    process = None
    budget = int(args.budget_kib * KIB)
    summary = {"binary": binary.name, "port": port, "requestsPerRoute": args.requests,
               "warmupPerRoute": args.warmup, "budgetBytesPerRoute": budget, "routes": {}}
    try:
        with tempfile.TemporaryDirectory(prefix="te-ret-ownership-") as directory:
            fixture_path = Path(directory) / FIXTURE.name
            template = FIXTURE.read_text(encoding="utf-8")
            if template.count(GUARD_TOKEN) != 1:
                parser.error("el fixture debe tener exactamente un marcador de relleno")
            fixture_path.write_text(template.replace(GUARD_TOKEN, "x" * GUARD_FILL_BYTES), encoding="utf-8")
            env = os.environ.copy()
            env["TYPEEASY_NUM_THREADS"] = "1"
            process = subprocess.Popen(
                [str(binary), "--api", "-p", str(port), "--host", "127.0.0.1", str(fixture_path)],
                cwd=ROOT,
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            wait_ready(process, port)
            for route in routes:
                summary["routes"][route] = measure(process, port, route, args.warmup, args.requests)
    except Exception as error:
        summary["error"] = f"{type(error).__name__}: {error}"
        print(json.dumps(summary, separators=(",", ":")), file=sys.stderr)
        return 1
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)

    failed = [r for r, m in summary["routes"].items() if max(0, m["rssDeltaBytes"]) > budget]
    summary["failedRoutes"] = failed
    summary["result"] = "PASS" if not failed else "FAIL"
    print(json.dumps(summary, separators=(",", ":")))
    return 0 if not failed else 1


if __name__ == "__main__":
    raise SystemExit(main())
