#!/usr/bin/env python3
"""Permanent multi-user OVERLAP test for `typeeasy --api` (Option-1a).

Proves that concurrent requests to an ASYNC endpoint interleave instead of
serialising: while a handler is parked in `await_all` (waiting on a spawned
worker subprocess) the global invoke lock is released so OTHER requests run.

How it works
------------
The fixture app `fixtures/async_overlap_app.te` exposes GET /slow/{tok}. Each
call spawns a worker that sleeps 500 ms and echoes {tok}, then awaits it. So:

  * Serialised (lock held during await): K requests take ~K * 500 ms.
  * Interleaved (Option-1a):              K requests take ~1 * 500 ms (+ the
                                          K serialised spawns, which are cheap).

The test measures a single-request baseline and the K-concurrent batch and
asserts the batch is far below K * baseline. It ALSO asserts every response
carries its OWN {tok} — the contamination guard that must survive interleaving.

Pure stdlib. Native and docker modes, mirroring run_api_tests.py.

Usage:
    python tests/api/run_async_overlap_test.py --mode native
    python tests/api/run_async_overlap_test.py --mode native -k 5
    python tests/api/run_async_overlap_test.py --mode docker
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_api_tests import find_free_port, wait_for_server  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures"
APP_TE = FIXTURE_DIR / "async_overlap_app.te"
WORKER_PY = FIXTURE_DIR / "sleep_worker.py"

# Must match the ms value baked into the fixture's /slow handler.
SLEEP_MS = 500


def _quote(p: str) -> str:
    """Quote a path so it survives CreateProcessA / `sh -c` arg splitting
    (the repo path may contain spaces)."""
    return p if (p.startswith('"') or " " not in p) else f'"{p}"'


def http_get(port: int, path: str, timeout: float = 30.0) -> tuple[int, str]:
    url = f"http://127.0.0.1:{port}{path}"
    try:
        with urllib.request.urlopen(
            urllib.request.Request(url, method="GET"), timeout=timeout
        ) as r:
            return r.status, r.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")
    except Exception as e:  # noqa: BLE001
        return -1, f"{type(e).__name__}: {e}"


def _drain(proc: subprocess.Popen) -> None:
    if not proc or not proc.stdout:
        return

    def pump():
        try:
            for _ in iter(proc.stdout.readline, b""):
                pass
        except Exception:
            pass

    threading.Thread(target=pump, daemon=True).start()


# ---------------------------------------------------------------------------
# Server lifecycle (custom: serves the overlap fixture, not endpoint.te)
# ---------------------------------------------------------------------------


def start_native(port: int) -> subprocess.Popen:
    bin_name = "typeeasy.exe" if os.name == "nt" else "typeeasy"
    binary = REPO_ROOT / "src" / bin_name
    if not binary.exists():
        sys.exit(f"native binary not found at {binary}")
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = "C:\\msys64\\mingw64\\bin;C:\\msys64\\usr\\bin;" + env.get("PATH", "")
    worker_cmd = f"{_quote(sys.executable)} {_quote(str(WORKER_PY))}"
    env["TE_ASYNC_WORKER"] = worker_cmd
    return subprocess.Popen(
        [str(binary), "--api", str(APP_TE), "--port", str(port), "--host", "127.0.0.1"],
        cwd=str(REPO_ROOT), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )


def start_docker(image: str, port: int) -> str:
    # Mount the test tree so the fixture app + worker are visible in-container.
    mount = str(REPO_ROOT)
    if os.name == "nt":
        os.environ.setdefault("MSYS_NO_PATHCONV", "1")
        mount = mount.replace("\\", "/")
    app_in = "/repo/tests/api/fixtures/async_overlap_app.te"
    worker_in = "/repo/tests/api/fixtures/sleep_worker.sh"
    cmd = [
        "docker", "run", "--rm", "-d",
        "-p", f"{port}:9000",
        "-v", f"{mount}:/repo",
        "-w", "/repo",
        "--entrypoint", "/typeeasy/typeeasy",
        # POSIX sh worker (the slim image has sh + sleep but no python3).
        "-e", f"TE_ASYNC_WORKER=sh {worker_in}",
        image,
        "--api", app_in, "--port", "9000", "--host", "0.0.0.0",
    ]
    return subprocess.check_output(cmd, text=True).strip()


def stop_docker(cid: str) -> None:
    subprocess.run(["docker", "rm", "-f", cid],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------------------
# Probes
# ---------------------------------------------------------------------------


def one_slow(port: int, tok: str) -> tuple[str, int, str]:
    s, b = http_get(port, f"/slow/{tok}")
    return tok, s, b


def one_slowsync(port: int, tok: str) -> tuple[str, int, str]:
    s, b = http_get(port, f"/slowsync/{tok}")
    return tok, s, b


def measure_single(port: int, tok: str) -> tuple[int, int, str]:
    t0 = time.time()
    _, s, b = one_slow(port, tok)
    return int((time.time() - t0) * 1000), s, b


def measure_batch(port: int, k: int) -> tuple[int, list[tuple[str, int, str]]]:
    toks = [f"ovl{i:04d}" for i in range(k)]
    t0 = time.time()
    out: list[tuple[str, int, str]] = []
    with ThreadPoolExecutor(max_workers=k) as ex:
        futs = [ex.submit(one_slow, port, t) for t in toks]
        for f in as_completed(futs):
            out.append(f.result())
    return int((time.time() - t0) * 1000), out


def measure_batch_sync(port: int, k: int) -> tuple[int, list[tuple[str, int, str]]]:
    """Same as measure_batch but hits the PLAIN /slowsync handler, which must
    serialise (no `async` => keeps the invoke lock across the await)."""
    toks = [f"syn{i:04d}" for i in range(k)]
    t0 = time.time()
    out: list[tuple[str, int, str]] = []
    with ThreadPoolExecutor(max_workers=k) as ex:
        futs = [ex.submit(one_slowsync, port, t) for t in toks]
        for f in as_completed(futs):
            out.append(f.result())
    return int((time.time() - t0) * 1000), out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description="TypeEasy --api async overlap test")
    ap.add_argument("--mode", choices=["docker", "native"], default="native")
    ap.add_argument("--image", default="typeeasy-typeeasy:latest")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("-k", "--concurrency", type=int, default=6,
                    help="number of simultaneous /slow requests")
    ap.add_argument("--factor", type=float, default=0.70,
                    help="batch must be < factor * (k * single) to prove overlap")
    args = ap.parse_args()

    k = max(2, args.concurrency)
    port = args.port or find_free_port()
    print(f"[overlap] mode={args.mode} port={port} os={platform.system()} "
          f"k={k} sleep_ms={SLEEP_MS} factor={args.factor}")

    cid = None
    proc = None
    failures: list[str] = []
    try:
        if args.mode == "docker":
            cid = start_docker(args.image, port)
            print(f"[overlap] container={cid[:12]}")
        else:
            proc = start_native(port)
            print(f"[overlap] pid={proc.pid}")
            _drain(proc)

        if not wait_for_server(port):
            print("FAIL: server did not respond to /ping within timeout")
            return 2

        # Warm up (pay one-time worker cold-start cost outside the timings).
        _, ws, wb = one_slow(port, "warmup")
        if ws != 200:
            print(f"FAIL: warmup request failed: status={ws} body={wb[:160]!r}")
            return 1

        # Baseline: one request in isolation.
        single_ms, ss, sb = measure_single(port, "single")
        if ss != 200 or "single" not in sb:
            failures.append(f"single request bad: status={ss} body={sb[:160]!r}")
        print(f"  single  /slow x1            ({single_ms} ms)")

        # The interleaving claim: k requests overlap their awaits.
        batch_ms, results = measure_batch(port, k)
        print(f"  batch   /slow x{k:<3}          ({batch_ms} ms)")

        # 1) Correctness: every response carries its OWN token, no contamination.
        for tok, s, b in results:
            if s != 200:
                failures.append(f"{tok}: status {s} body={b[:120]!r}")
            elif tok not in b:
                failures.append(f"{tok}: response missing own token: {b[:120]!r}")
            else:
                bled = [o for (o, _s, _b) in results if o != tok and o in b]
                if bled:
                    failures.append(f"CONTAMINATION {tok} also carried {bled[0]}: {b[:160]!r}")

        # 2) Overlap: batch must be far below the serialised cost (k * single).
        serial_ms = k * single_ms
        threshold = int(serial_ms * args.factor)
        overlapped = batch_ms < threshold
        speedup = (serial_ms / batch_ms) if batch_ms > 0 else 0.0
        verdict = "PASS" if overlapped else "FAIL"
        print(f"{verdict}  overlap  batch={batch_ms}ms < {threshold}ms "
              f"(= {args.factor} x {k} x {single_ms}ms)  speedup~{speedup:.2f}x")
        if not overlapped:
            failures.append(
                f"NO OVERLAP: batch={batch_ms}ms not < {threshold}ms "
                f"(serialised would be ~{serial_ms}ms) — requests did not interleave"
            )

        # 3) The OTHER direction: the PLAIN /slowsync handler (no `async`) must
        #    SERIALISE — proving the `async` modifier actually changes behaviour
        #    (C#/.NET semantics), not just syntax. A serialised batch costs ~Kx;
        #    we require it well above the overlap threshold. This direction is
        #    robust to system load (load only makes a serialised batch slower).
        sync_batch_ms, sync_results = measure_batch_sync(port, k)
        print(f"  batch   /slowsync x{k:<3}      ({sync_batch_ms} ms)")
        for tok, s, b in sync_results:
            if s != 200:
                failures.append(f"{tok}: status {s} body={b[:120]!r}")
            elif tok not in b:
                failures.append(f"{tok}: response missing own token: {b[:120]!r}")
        sync_threshold = threshold  # same bar the async batch had to beat
        serialised = sync_batch_ms >= sync_threshold
        sync_verdict = "PASS" if serialised else "FAIL"
        sync_speedup = (serial_ms / sync_batch_ms) if sync_batch_ms > 0 else 0.0
        print(f"{sync_verdict}  no-overlap  batch={sync_batch_ms}ms >= {sync_threshold}ms "
              f"(plain handler serialises)  speedup~{sync_speedup:.2f}x")
        if not serialised:
            failures.append(
                f"PLAIN NOT SERIALISED: /slowsync batch={sync_batch_ms}ms < {sync_threshold}ms "
                f"— a non-async handler should keep the lock and serialise"
            )

        status = "PASS" if not failures else "FAIL"
        print(f"{status}  correctness  {len(results)} responses, "
              f"errors={len([f for f in failures if 'OVERLAP' not in f])}")

        if failures:
            print("\n--- failures ---")
            for line in failures[:15]:
                print("  " + line)

        print(f"\n=== ASYNC OVERLAP: k={k}  single={single_ms}ms  batch={batch_ms}ms  "
              f"FAIL={len(failures)} ===")
        return 0 if not failures else 1
    finally:
        if cid:
            stop_docker(cid)
        if proc:
            try:
                proc.terminate()
                proc.wait(timeout=5)
            except Exception:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
