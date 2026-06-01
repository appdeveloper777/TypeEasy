#!/usr/bin/env python3
"""Concurrency / load smoke test for `typeeasy --api`.

Production-ready bloqueante #4. The API server runs many civetweb worker
threads but the AST interpreter is serialized behind a global lock (see
api_server/te_websocket.c). This test does NOT assert throughput; it asserts
*correctness and survival under concurrency*:

  - Fire N concurrent requests across a small thread pool against a mix of
    endpoints (GET /ping, GET /users/{id}, POST /echo, POST /api/login).
  - Every response must be correct (right status, body echoes the input).
  - Zero 5xx, zero dropped connections, server still answering /ping at the end.

If the global serialization or the per-request state reset
(runtime_reset_vars_to_initial_state) has a race, a use-after-free, or leaks
state between requests, this surfaces it as wrong bodies, 5xx, or a crash.

Reuses the server lifecycle helpers from run_api_tests.py so there is a single
source of truth for starting native/docker servers.

Usage:
  python tests/api/run_load_test.py --mode native   [--requests 500] [--workers 16]
  python tests/api/run_load_test.py --mode docker

Exit code 0 = green, non-zero = failures detected.
"""

from __future__ import annotations

import argparse
import json as jsonlib
import platform
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# Reuse the exact same server lifecycle as the smoke suite.
from run_api_tests import (
    TEST_JWT_SECRET,
    find_free_port,
    start_docker_server,
    start_native_server,
    stop_docker_server,
    wait_for_server,
)


def _get(port: int, path: str) -> tuple[int, str]:
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")


def _post(port: int, path: str, body: str,
          ctype: str = "application/json") -> tuple[int, str]:
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(
        url, data=body.encode("utf-8"), method="POST",
        headers={"Content-Type": ctype})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")


def _one_request(port: int, i: int) -> tuple[bool, str]:
    """Run one request; return (ok, detail). Rotates over a few endpoints so
    distinct interpreter paths are exercised concurrently."""
    kind = i % 4
    try:
        if kind == 0:
            s, b = _get(port, "/ping")
            if s != 200:
                return False, f"/ping status {s}"
            return True, ""
        if kind == 1:
            uid = i  # unique per request: must be echoed back verbatim
            s, b = _get(port, f"/users/{uid}")
            if s != 200:
                return False, f"/users status {s}"
            if str(uid) not in b:
                return False, f"/users body did not echo id {uid}: {b!r}"
            return True, ""
        if kind == 2:
            payload = f"load-{i}"
            s, b = _post(port, "/echo", payload, "text/plain")
            if s >= 500:
                return False, f"/echo 5xx {s}"
            if payload not in b:
                return False, f"/echo body did not echo {payload!r}: {b!r}"
            return True, ""
        # kind == 3: JWT issue (heaviest path: HMAC + JSON)
        s, b = _post(port, "/api/login", '{"name":"ana","age":30}')
        if s != 200:
            return False, f"/api/login status {s}: {b!r}"
        token = jsonlib.loads(b).get("token", "")
        if token.count(".") != 2:
            return False, f"/api/login bad token: {token!r}"
        return True, ""
    except Exception as e:  # dropped connection / timeout / reset = failure
        return False, f"request {i} ({kind}) raised: {e!r}"


def run_load(port: int, total: int, workers: int) -> int:
    print(f"[load] firing {total} requests over {workers} workers")
    t0 = time.time()
    ok = 0
    failures: list[str] = []
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(_one_request, port, i) for i in range(total)]
        for f in as_completed(futs):
            success, detail = f.result()
            if success:
                ok += 1
            else:
                failures.append(detail)
    elapsed = time.time() - t0
    rps = total / elapsed if elapsed > 0 else 0.0
    print(f"[load] {ok}/{total} ok in {elapsed:.2f}s (~{rps:.0f} req/s)")

    # Server must still be alive and answering after the storm.
    alive = wait_for_server(port, timeout=5)
    if not alive:
        failures.append("server stopped answering /ping after load")

    if failures:
        print(f"\n=== LOAD: FAIL  ({len(failures)} problems) ===")
        for d in failures[:20]:
            print(f"  - {d}")
        if len(failures) > 20:
            print(f"  ... and {len(failures) - 20} more")
        return 1
    print(f"\n=== LOAD: PASS  ({ok}/{total}, server alive) ===")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="TypeEasy API concurrency/load test")
    ap.add_argument("--mode", choices=["docker", "native"], default="docker")
    ap.add_argument("--image", default="typeeasy-typeeasy:latest")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--requests", type=int, default=400)
    ap.add_argument("--workers", type=int, default=16)
    args = ap.parse_args()

    port = args.port or find_free_port()
    print(f"[load] mode={args.mode} port={port} os={platform.system()}")

    cid = None
    proc = None
    try:
        if args.mode == "docker":
            cid = start_docker_server(args.image, port)
            print(f"[load] container={cid[:12]}")
        else:
            proc = start_native_server(port)
            print(f"[load] pid={proc.pid}")

        if not wait_for_server(port):
            print("FAIL: server did not respond to /ping within timeout")
            return 2

        return run_load(port, args.requests, args.workers)
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
