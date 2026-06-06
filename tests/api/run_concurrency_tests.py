#!/usr/bin/env python3
"""Concurrency safety net for `typeeasy --api`.

Fires many SIMULTANEOUS requests, each carrying a DISTINCT value, and asserts
that every response carries back its OWN value — never another request's.

This is the regression detector for the thread-local / GIL-removal work:
  * It PASSES today, because the global interpreter lock serializes handlers.
  * It MUST keep passing once handlers run in parallel. If per-request state
    (vars[], request params, __ret__, ...) ever leaks across threads, a
    response for id=A will come back carrying id=B and this test goes red.

It also reports the wall-clock for the whole batch so the parallel speed-up
is visible once the GIL is lifted (today the number is the serialized cost).

Pure stdlib (urllib + concurrent.futures). Native and docker modes, mirroring
run_api_tests.py — it reuses that module's server lifecycle helpers.

Usage:
    python tests/api/run_concurrency_tests.py --mode native
    python tests/api/run_concurrency_tests.py --mode docker
    python tests/api/run_concurrency_tests.py --mode native -n 400 -w 32
"""

from __future__ import annotations

import argparse
import json as jsonlib
import platform
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# Reuse the proven server lifecycle helpers from the smoke suite.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_api_tests import (  # noqa: E402
    find_free_port,
    start_docker_server,
    start_native_server,
    stop_docker_server,
    wait_for_server,
)


def http_get(port: int, path: str, timeout: float = 15.0) -> tuple[int, str]:
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")
    except Exception as e:  # noqa: BLE001 — surface as a failed sample
        return -1, f"{type(e).__name__}: {e}"


def _drain_stdout(proc) -> threading.Thread | None:
    """Continuously drain the server's stdout pipe.

    start_native_server() launches the server with stdout=PIPE. Under a heavy
    request load the server logs fill the ~64KB OS pipe buffer and the server
    BLOCKS on write if nobody reads it — a classic pipe deadlock the 20-request
    smoke suite never hits. Drain it on a daemon thread so the server runs free.
    """
    if not proc or not proc.stdout:
        return None

    def _pump():
        try:
            for _ in iter(proc.stdout.readline, b""):
                pass
        except Exception:
            pass

    th = threading.Thread(target=_pump, daemon=True)
    th.start()
    return th


# ---------------------------------------------------------------------------
# Contamination probes
# ---------------------------------------------------------------------------


def probe_user_ids(port: int, n: int, workers: int) -> tuple[int, list[str]]:
    """GET /users/{i} for i in 0..n-1 concurrently.

    The handler reads request_param("id") into a local var and echoes it back
    as JSON {"id":"<i>","name":"Demo"}. Each response MUST carry its own id.
    Returns (elapsed_ms, errors).
    """
    errors: list[str] = []

    def one(i: int):
        s, b = http_get(port, f"/users/{i}")
        return i, s, b

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(one, i) for i in range(n)]
        for f in as_completed(futs):
            i, s, b = f.result()
            if s != 200:
                errors.append(f"id={i}: status {s} body={b[:120]!r}")
                continue
            try:
                data = jsonlib.loads(b)
            except Exception:
                errors.append(f"id={i}: non-JSON body {b[:120]!r}")
                continue
            got = str(data.get("id"))
            if got != str(i):
                # The smoking gun: this request got another request's value.
                errors.append(f"CONTAMINATION id={i} but response carried id={got}")
    elapsed_ms = int((time.time() - t0) * 1000)
    return elapsed_ms, errors


def probe_hola_names(port: int, n: int, workers: int) -> tuple[int, list[str]]:
    """GET /api/hola/{token} with unique tokens concurrently.

    The handler builds {"mensaje":"¡Hola <token> desde TypeEasy!"}. Each
    response must contain its OWN token and no other request's token.
    Returns (elapsed_ms, errors).
    """
    errors: list[str] = []
    tokens = [f"tok{idx:05d}" for idx in range(n)]
    token_set = set(tokens)

    def one(tok: str):
        s, b = http_get(port, f"/api/hola/{tok}")
        return tok, s, b

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(one, tok) for tok in tokens]
        for f in as_completed(futs):
            tok, s, b = f.result()
            if s != 200:
                errors.append(f"{tok}: status {s} body={b[:120]!r}")
                continue
            if tok not in b:
                errors.append(f"{tok}: response missing own token: {b[:120]!r}")
                continue
            # Ensure no DIFFERENT request's token bled into this response.
            for other in token_set:
                if other != tok and other in b:
                    errors.append(
                        f"CONTAMINATION {tok} response also carried {other}: {b[:160]!r}"
                    )
                    break
    elapsed_ms = int((time.time() - t0) * 1000)
    return elapsed_ms, errors


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description="TypeEasy --api concurrency safety net")
    ap.add_argument("--mode", choices=["docker", "native"], default="native")
    ap.add_argument("--image", default="typeeasy-typeeasy:latest",
                    help="docker image (for --mode docker)")
    ap.add_argument("--port", type=int, default=0, help="host port (0 = auto)")
    ap.add_argument("-n", "--num", type=int, default=200,
                    help="number of distinct concurrent requests per probe")
    ap.add_argument("-w", "--workers", type=int, default=24,
                    help="client-side thread pool size (concurrency level)")
    args = ap.parse_args()

    port = args.port or find_free_port()
    print(f"[concurrency] mode={args.mode} port={port} os={platform.system()} "
          f"n={args.num} workers={args.workers}")

    cid = None
    proc = None
    try:
        if args.mode == "docker":
            cid = start_docker_server(args.image, port)
            print(f"[concurrency] container={cid[:12]}")
        else:
            proc = start_native_server(port)
            print(f"[concurrency] pid={proc.pid}")
            _drain_stdout(proc)

        if not wait_for_server(port):
            print("FAIL: server did not respond to /ping within timeout")
            if proc and proc.poll() is not None and proc.stdout:
                print(proc.stdout.read().decode("utf-8", errors="replace"))
            return 2

        total_errors: list[str] = []

        ms1, e1 = probe_user_ids(port, args.num, args.workers)
        status1 = "PASS" if not e1 else "FAIL"
        print(f"{status1}  /users/{{id}} x{args.num}  ({ms1} ms)  errors={len(e1)}")
        total_errors += e1

        ms2, e2 = probe_hola_names(port, args.num, args.workers)
        status2 = "PASS" if not e2 else "FAIL"
        print(f"{status2}  /api/hola/{{name}} x{args.num}  ({ms2} ms)  errors={len(e2)}")
        total_errors += e2

        if total_errors:
            print("\n--- first failures ---")
            for line in total_errors[:15]:
                print("  " + line)

        failed = len(total_errors)
        print(f"\n=== CONCURRENCY: requests={2 * args.num}  "
              f"FAIL={failed} ===")
        return 0 if failed == 0 else 1
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
