#!/usr/bin/env python3
"""Memory-leak audit for `typeeasy --api` (production-ready bloqueante #6).

The API server is a long-lived process: it parses the .te once and then serves
requests forever, resetting per-request interpreter state via
runtime_reset_vars_to_initial_state(). A leak that happens *per request* is the
real production risk: RSS grows monotonically until the OOM killer reaps the
worker (and, under prefork, workers fall like dominoes).

This audit does NOT use LeakSanitizer-at-exit: the interpreter is short-lived by
design and intentionally does not free every one-time/startup allocation, so
LSan-at-exit is dominated by harmless noise. Instead we measure the thing that
actually matters: *steady-state RSS growth as a function of requests served*.

Method:
  1. Start the server (single worker for a deterministic measurement).
  2. Warm up (fire WARMUP requests) so first-touch / lazy caches settle.
  3. Sample RSS, then fire REQUESTS in BATCHES, sampling RSS after each batch.
  4. Fit a line RSS = a + b*requests over the post-warmup samples. `b` is the
     per-request growth (bytes/req). Report it plus total growth.
  5. FAIL if sustained growth exceeds --threshold-bytes-per-req (default 256 B).

A clean server shows RSS flattening after warmup: `b` ~ 0 (allocator noise
only). A per-request leak shows `b` clearly > 0 and total growth scaling with
request count.

Reuses the server lifecycle helpers from run_api_tests.py (single source of
truth for native/docker start/stop).

Usage:
  python tests/api/run_leak_audit.py --mode native [--requests 20000] [--batches 20]
  python tests/api/run_leak_audit.py --mode docker

Exit code 0 = green (no significant per-request growth), non-zero = leak/failure.
"""

from __future__ import annotations

import argparse
import ctypes
import json as jsonlib
import os
import platform
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# Reuse the exact same server lifecycle as the smoke / load suites.
from run_api_tests import (
    find_free_port,
    start_docker_server,
    start_native_server,
    stop_docker_server,
    wait_for_server,
)


# ---------------------------------------------------------------------------
# RSS sampling (cross-platform, dependency-free)
# ---------------------------------------------------------------------------

def _rss_kb_linux_pid(pid: int) -> int | None:
    try:
        with open(f"/proc/{pid}/status", "r") as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])  # already in kB
    except OSError:
        return None
    return None


def _rss_kb_windows_pid(pid: int) -> int | None:
    # Use psapi GetProcessMemoryInfo: no external dependency.
    PROCESS_QUERY_INFORMATION = 0x0400
    PROCESS_VM_READ = 0x0010

    class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_uint32),
            ("PageFaultCount", ctypes.c_uint32),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    kernel32 = ctypes.windll.kernel32
    psapi = ctypes.windll.psapi
    h = kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        return None
    try:
        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        ok = psapi.GetProcessMemoryInfo(
            h, ctypes.byref(counters), counters.cb)
        if not ok:
            return None
        return int(counters.WorkingSetSize) // 1024
    finally:
        kernel32.CloseHandle(h)


def _rss_kb_docker(cid: str) -> int | None:
    # docker stats reports working set, e.g. "12.5MiB / 7.65GiB".
    try:
        out = subprocess.check_output(
            ["docker", "stats", "--no-stream", "--format", "{{.MemUsage}}", cid],
            text=True, stderr=subprocess.DEVNULL).strip()
    except (subprocess.CalledProcessError, OSError):
        return None
    m = re.match(r"\s*([0-9.]+)\s*([KMGT]?i?B)", out)
    if not m:
        return None
    val = float(m.group(1))
    unit = m.group(2).lower()
    mult = {
        "b": 1 / 1024,
        "kib": 1, "kb": 1,
        "mib": 1024, "mb": 1024,
        "gib": 1024 * 1024, "gb": 1024 * 1024,
        "tib": 1024 * 1024 * 1024, "tb": 1024 * 1024 * 1024,
    }.get(unit, None)
    if mult is None:
        return None
    return int(val * mult)


class RssSampler:
    """Resolves the right RSS source once and samples it on demand (in kB)."""

    def __init__(self, mode: str, proc, cid: str | None):
        self.mode = mode
        self.cid = cid
        self.pid = proc.pid if proc is not None else None

    def sample_kb(self) -> int | None:
        if self.mode == "docker":
            return _rss_kb_docker(self.cid)
        # native
        if os.name == "nt":
            return _rss_kb_windows_pid(self.pid)
        return _rss_kb_linux_pid(self.pid)


# ---------------------------------------------------------------------------
# Request mix (same endpoints exercised by the load test)
# ---------------------------------------------------------------------------

def _get(port: int, path: str) -> int:
    url = f"http://127.0.0.1:{port}{path}"
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            r.read()
            return r.status
    except urllib.error.HTTPError as e:
        e.read()
        return e.code


def _post(port: int, path: str, body: str, ctype: str) -> int:
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(
        url, data=body.encode("utf-8"), method="POST",
        headers={"Content-Type": ctype})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            r.read()
            return r.status
    except urllib.error.HTTPError as e:
        e.read()
        return e.code


# When set (0..3) the audit hammers a single endpoint to isolate a leak.
_ONLY_KIND: int | None = None


def _one_request(port: int, i: int) -> bool:
    """Exercise a rotating mix so multiple interpreter allocation paths run."""
    kind = _ONLY_KIND if _ONLY_KIND is not None else (i % 4)
    try:
        if kind == 0:
            return _get(port, "/ping") == 200
        if kind == 1:
            return _get(port, f"/users/{i}") == 200
        if kind == 2:
            return _post(port, "/echo", f"leak-{i}", "text/plain") < 500
        return _post(port, "/api/login", '{"name":"ana","age":30}',
                     "application/json") == 200
    except Exception:
        return False


def _fire(port: int, count: int, workers: int) -> int:
    ok = 0
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(_one_request, port, i) for i in range(count)]
        for f in as_completed(futs):
            if f.result():
                ok += 1
    return ok


# ---------------------------------------------------------------------------
# Linear fit (least squares slope) — no numpy dependency
# ---------------------------------------------------------------------------

def _slope_bytes_per_req(samples: list[tuple[int, int]]) -> float:
    """samples = [(requests_served, rss_kb), ...]; returns bytes/request."""
    n = len(samples)
    if n < 2:
        return 0.0
    sx = sum(x for x, _ in samples)
    sy = sum(y for _, y in samples)
    sxx = sum(x * x for x, _ in samples)
    sxy = sum(x * y for x, y in samples)
    denom = n * sxx - sx * sx
    if denom == 0:
        return 0.0
    slope_kb_per_req = (n * sxy - sx * sy) / denom
    return slope_kb_per_req * 1024.0


# ---------------------------------------------------------------------------
# Audit driver
# ---------------------------------------------------------------------------

def run_audit(sampler: RssSampler, port: int, total: int, batches: int,
              warmup: int, workers: int, threshold_bpr: float) -> int:
    print(f"[leak] warmup {warmup} requests to settle lazy/first-touch allocs")
    _fire(port, warmup, workers)
    # Let allocator/OS settle the working set briefly.
    time.sleep(0.5)

    base = sampler.sample_kb()
    if base is None:
        print("FAIL: could not read RSS of the server process")
        return 3
    print(f"[leak] baseline RSS after warmup: {base/1024:.1f} MiB")

    per_batch = max(1, total // batches)
    served = 0
    samples: list[tuple[int, int]] = []
    for b in range(batches):
        ok = _fire(port, per_batch, workers)
        served += per_batch
        if ok < per_batch:
            print(f"  ! batch {b}: {ok}/{per_batch} ok (some requests failed)")
        time.sleep(0.2)
        rss = sampler.sample_kb()
        if rss is None:
            print(f"  ! batch {b}: RSS sample failed")
            continue
        samples.append((served, rss))
        delta = (rss - base) / 1024.0
        print(f"  batch {b+1}/{batches}: served={served:>7} "
              f"RSS={rss/1024:.1f} MiB  (delta {delta:+.2f} MiB)")

    if len(samples) < 2:
        print("FAIL: not enough RSS samples to assess growth")
        return 3

    final_rss = samples[-1][1]
    total_growth_mib = (final_rss - base) / 1024.0
    bpr = _slope_bytes_per_req(samples)
    proj_mib_per_million = bpr * 1_000_000 / (1024 * 1024)

    print()
    print("=== LEAK AUDIT SUMMARY ===")
    print(f"  requests served (post-warmup): {served}")
    print(f"  baseline RSS:  {base/1024:.1f} MiB")
    print(f"  final RSS:     {final_rss/1024:.1f} MiB")
    print(f"  total growth:  {total_growth_mib:+.2f} MiB")
    print(f"  growth rate:   {bpr:+.1f} bytes/request "
          f"(~{proj_mib_per_million:+.1f} MiB per 1M requests)")
    print(f"  threshold:     {threshold_bpr:.1f} bytes/request")

    if bpr > threshold_bpr:
        print(f"\n=== LEAK AUDIT: FAIL  "
              f"({bpr:.1f} > {threshold_bpr:.1f} bytes/req sustained) ===")
        return 1
    print(f"\n=== LEAK AUDIT: PASS  "
          f"(steady-state, {bpr:.1f} <= {threshold_bpr:.1f} bytes/req) ===")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="TypeEasy API memory-leak audit (RSS growth under load)")
    ap.add_argument("--mode", choices=["docker", "native"], default="docker")
    ap.add_argument("--image", default="typeeasy-typeeasy:latest")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--requests", type=int, default=20000,
                    help="total requests after warmup")
    ap.add_argument("--batches", type=int, default=20,
                    help="RSS sample points across the run")
    ap.add_argument("--warmup", type=int, default=2000,
                    help="requests fired before baseline RSS is taken")
    ap.add_argument("--workers", type=int, default=8,
                    help="concurrent client threads")
    ap.add_argument("--threshold-bytes-per-req", type=float, default=256.0,
                    help="max tolerated sustained RSS growth per request")
    ap.add_argument("--only", type=int, choices=[0, 1, 2, 3], default=None,
                    help="isolate a single endpoint: 0=ping 1=users 2=echo 3=login")
    args = ap.parse_args()

    if args.only is not None:
        global _ONLY_KIND
        _ONLY_KIND = args.only
        print(f"[leak] isolating endpoint kind={args.only}")

    port = args.port or find_free_port()
    print(f"[leak] mode={args.mode} port={port} os={platform.system()}")

    cid = None
    proc = None
    try:
        if args.mode == "docker":
            cid = start_docker_server(args.image, port)
            print(f"[leak] container={cid[:12]}")
        else:
            proc = start_native_server(port)
            print(f"[leak] pid={proc.pid}")

        if not wait_for_server(port):
            print("FAIL: server did not respond to /ping within timeout")
            return 2

        sampler = RssSampler(args.mode, proc, cid)
        return run_audit(
            sampler, port, args.requests, args.batches,
            args.warmup, args.workers, args.threshold_bytes_per_req)
    finally:
        if cid:
            stop_docker_server(cid)
        if proc:
            try:
                proc.terminate()
                proc.wait(timeout=5)
            except Exception:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
