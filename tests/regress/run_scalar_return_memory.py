#!/usr/bin/env python3
"""Focused RSS regression for scalar AST nodes returned by named functions.

Starts a private, synthetic TypeEasy API process, compares a small control route
with a route that returns two fresh 256 KiB strings, and exits non-zero when the
post-warmup request batch grows RSS by more than the configured budget.
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
FIXTURE = Path(__file__).with_name("scalar_return_memory.te")
MIB = 1024 * 1024
FILL_BYTES = 128 * 1024
DEFAULT_REQUESTS = 200
DEFAULT_GROWTH_LIMIT = 16 * MIB


def free_port() -> int:
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        return int(server.getsockname()[1])


def rss_bytes(pid: int) -> int:
    with open(f"/proc/{pid}/status", encoding="ascii") as status:
        for line in status:
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    raise RuntimeError("VmRSS no disponible para el proceso de prueba")


def get_json(port: int, route: str, timeout: float = 5.0) -> dict:
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
            if get_json(port, "/control") == {"ok": 1}:
                return
        except (OSError, RuntimeError, json.JSONDecodeError) as error:
            last_error = type(error).__name__
        time.sleep(0.1)
    raise RuntimeError(f"API TypeEasy no inició dentro del plazo ({last_error or 'sin respuesta'})")


def run_phase(process: subprocess.Popen, port: int, route: str, count: int, expected: dict,
              sample_every: int = 1) -> dict:
    start = rss_bytes(process.pid)
    peak = start
    samples = []
    for index in range(1, count + 1):
        if process.poll() is not None:
            raise RuntimeError(f"TypeEasy terminó durante {route} con código {process.returncode}")
        actual = get_json(port, route)
        if actual != expected:
            raise RuntimeError(f"La respuesta sintética de {route} no coincide con el contrato esperado")
        if index % sample_every == 0 or index == count:
            current = rss_bytes(process.pid)
            peak = max(peak, current)
            samples.append({"request": index, "rssBytes": current})
    time.sleep(0.1)
    end = rss_bytes(process.pid)
    return {"requests": count, "rssBeforeBytes": start, "rssAfterBytes": end,
            "rssDeltaBytes": end - start, "rssPeakBytes": peak, "samples": samples}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", default=str(ROOT / "src" / "typeeasy"), help="binario TypeEasy nativo")
    parser.add_argument("--control-requests", type=int, default=DEFAULT_REQUESTS)
    parser.add_argument("--scalar-requests", type=int, default=DEFAULT_REQUESTS)
    parser.add_argument("--growth-limit-mib", type=float, default=16)
    args = parser.parse_args()

    if not sys.platform.startswith("linux"):
        parser.error("esta regresión necesita /proc y solo corre en Linux")
    if args.control_requests < 1 or args.scalar_requests < 1 or args.growth_limit_mib <= 0:
        parser.error("requests y growth-limit deben ser positivos")
    binary = Path(args.bin).resolve()
    if not binary.is_file():
        parser.error(f"binario TypeEasy no encontrado: {binary}")

    template = FIXTURE.read_text(encoding="utf-8")
    if template.count("__SCALAR_RETURN_FILL__") != 1:
        parser.error("el fixture debe tener exactamente un marcador de relleno")
    fixture = template.replace("__SCALAR_RETURN_FILL__", "x" * FILL_BYTES)
    port = free_port()
    process = None
    summary = {"binary": binary.name, "port": port, "fillBytes": FILL_BYTES,
               "returnedStringBytes": FILL_BYTES * 2, "growthLimitBytes": int(args.growth_limit_mib * MIB)}
    try:
        with tempfile.TemporaryDirectory(prefix="te-scalar-return-") as directory:
            fixture_path = Path(directory) / "scalar_return_memory.te"
            fixture_path.write_text(fixture, encoding="utf-8")
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
            # The readiness request itself is the one small control warm-up.
            control = run_phase(process, port, "/control", args.control_requests, {"ok": 1})
            # One large return settles lazy allocations before the measured batch.
            if get_json(port, "/scalar") != {"first": FILL_BYTES * 2, "second": FILL_BYTES * 2}:
                raise RuntimeError("El endpoint escalar no devolvió las longitudes esperadas")
            scalar = run_phase(process, port, "/scalar", args.scalar_requests,
                               {"first": FILL_BYTES * 2, "second": FILL_BYTES * 2})
            summary.update({"control": control, "scalar": scalar})
    except Exception as error:
        summary["error"] = type(error).__name__
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

    limit = summary["growthLimitBytes"]
    control_delta = max(0, summary["control"]["rssDeltaBytes"])
    scalar_delta = max(0, summary["scalar"]["rssDeltaBytes"])
    summary["result"] = "PASS" if control_delta <= limit and scalar_delta <= limit else "FAIL"
    print(json.dumps(summary, separators=(",", ":")))
    return 0 if summary["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
