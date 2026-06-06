#!/usr/bin/env python3
"""Before/after performance harness for the `--api` parallelism work.

Measures, against a running `typeeasy --api bench_endpoint.te` server:

  1. single-request latency of /bench/heavy   (the per-request CPU cost)
  2. concurrent wall-clock + throughput of N /bench/heavy requests
        - with the GIL (today) this is ~serialized: wall ~= N * cost
        - once the GIL is lifted it should drop toward (N / cores) * cost
  3. the same for /bench/light (I/O-bound; connection throughput ceiling)

Prints a compact table you can capture as the "before" and "after" snapshots.
Pure stdlib. Starts/stops the native server itself (no dependency on the
smoke-suite's hardcoded endpoint file).

Usage:
    python tests/api/bench_perf.py                 # native, defaults
    python tests/api/bench_perf.py -n 64 -w 16
    python tests/api/bench_perf.py --label before
"""

from __future__ import annotations

import argparse
import os
import platform
import socket
import statistics
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_TE = REPO_ROOT / "benchmarks" / "bench_endpoint.te"


def find_free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def start_server(port: int, server_workers: int = 1) -> subprocess.Popen:
    bin_name = "typeeasy.exe" if os.name == "nt" else "typeeasy"
    binary = REPO_ROOT / "src" / bin_name
    if not binary.exists():
        sys.exit(f"native binary not found at {binary}")
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = "C:\\msys64\\mingw64\\bin;C:\\msys64\\usr\\bin;" + env.get("PATH", "")
    cmd = [str(binary), "--api", str(BENCH_TE), "--port", str(port), "--host", "127.0.0.1"]
    if server_workers and server_workers > 1:
        cmd += ["--workers", str(server_workers)]
    proc = subprocess.Popen(
        cmd,
        cwd=str(REPO_ROOT), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )

    # Drain stdout so server log volume can't deadlock the pipe under load.
    def _pump():
        try:
            for _ in iter(proc.stdout.readline, b""):
                pass
        except Exception:
            pass

    threading.Thread(target=_pump, daemon=True).start()
    return proc


def wait_for_server(port: int, timeout: float = 20.0) -> bool:
    deadline = time.time() + timeout
    url = f"http://127.0.0.1:{port}/bench/light"
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as r:
                if r.status == 200:
                    return True
        except (urllib.error.URLError, ConnectionError, OSError):
            time.sleep(0.2)
    return False


def get(port: int, path: str, timeout: float = 30.0) -> tuple[int, float]:
    url = f"http://127.0.0.1:{port}{path}"
    t0 = time.time()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            r.read()
            return r.status, (time.time() - t0) * 1000.0
    except Exception:
        return -1, (time.time() - t0) * 1000.0


def single_latency(port: int, path: str, samples: int = 15) -> float:
    """Median single-request latency (ms), server warmed first."""
    get(port, path)  # warm
    lats = []
    for _ in range(samples):
        s, ms = get(port, path)
        if s == 200:
            lats.append(ms)
    return statistics.median(lats) if lats else float("nan")


def concurrent_run(port: int, path: str, n: int, workers: int) -> dict:
    """Fire n requests at `workers` concurrency. Returns timing summary."""
    lats: list[float] = []
    errors = 0
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(get, port, path) for _ in range(n)]
        for f in as_completed(futs):
            s, ms = f.result()
            if s == 200:
                lats.append(ms)
            else:
                errors += 1
    wall = (time.time() - t0) * 1000.0
    lats.sort()
    p95 = lats[int(len(lats) * 0.95) - 1] if lats else float("nan")
    return {
        "wall_ms": wall,
        "rps": (len(lats) / (wall / 1000.0)) if wall > 0 else 0.0,
        "mean_ms": statistics.mean(lats) if lats else float("nan"),
        "p95_ms": p95,
        "errors": errors,
        "ok": len(lats),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="TypeEasy --api perf before/after harness")
    ap.add_argument("-n", "--num", type=int, default=64,
                    help="requests per concurrent batch")
    ap.add_argument("-w", "--workers", type=int, default=16,
                    help="client concurrency level")
    ap.add_argument("--label", default="", help="snapshot label, e.g. before/after")
    ap.add_argument("--server-workers", type=int, default=1,
                    help="prefork worker processes for the server (--workers N)")
    ap.add_argument("--port", type=int, default=0)
    args = ap.parse_args()

    cores = os.cpu_count() or 1
    port = args.port or find_free_port()
    label = f" [{args.label}]" if args.label else ""
    print(f"[bench]{label} os={platform.system()} cores={cores} "
          f"n={args.num} workers={args.workers} server_workers={args.server_workers} port={port}")

    proc = start_server(port, args.server_workers)
    try:
        if not wait_for_server(port):
            print("FAIL: server did not come up")
            if proc.stdout:
                print(proc.stdout.read().decode("utf-8", "replace"))
            return 2

        heavy_single = single_latency(port, "/bench/heavy")
        light_single = single_latency(port, "/bench/light")
        print(f"  single-request  heavy={heavy_single:7.2f} ms   "
              f"light={light_single:7.2f} ms")

        for name, path in (("heavy", "/bench/heavy"), ("light", "/bench/light")):
            r = concurrent_run(port, path, args.num, args.workers)
            # Ideal parallel wall if perfectly scaled across cores:
            single = heavy_single if name == "heavy" else light_single
            ideal = (args.num / min(cores, args.workers)) * single
            print(f"  {name:5s} x{args.num:<4d} wall={r['wall_ms']:8.1f} ms  "
                  f"rps={r['rps']:7.1f}  mean={r['mean_ms']:7.2f}  "
                  f"p95={r['p95_ms']:7.2f}  err={r['errors']}  "
                  f"(ideal~{ideal:6.0f} ms)")
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
