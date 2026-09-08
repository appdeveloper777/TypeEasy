#!/usr/bin/env python3
"""TypeEasy --api endpoint test runner (stdlib only).

Each suite is a JSON file `tests/api/<name>.api.json`:

    {
      "entry": "frames_api.te",          # .te with endpoint{} blocks (relative to the json)
      "health": "/health",               # GET polled until 200 (default "/health")
      "env": { "TYPEEASY_DEV": "1" },    # extra env for the server (optional)
      "env_file": "../../ERP/.env",      # optional dotenv loaded into the server env
      "cases": [
        { "name": "id sobrevive al callee",
          "method": "POST", "path": "/api/ped/4/facturar",
          "headers": { "X-Nick": "demo1" },
          "body": { "forma_pago_id": "1" },          # object -> JSON; string -> raw
          "expect": { "status": 200,
                      "json": { "id": "4", "nick": "demo1" },   # subset match (deep)
                      "contains": "\"chk\":1",                 # substring of body
                      "header": { "Content-Type": "application/json" } } }
      ]
    }

Usage:
    python tools/te-test/run_api_tests.py tests/api [--bin ./src/typeeasy.exe] [--port 8181] [-k substr]

Exit code 0 when every case of every suite passes. The server's stderr/stdout is
saved next to the suite as `<name>.server.log` and shown on failure.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def free_port(preferred: int) -> int:
    with socket.socket() as s:
        try:
            s.bind(("127.0.0.1", preferred))
            return preferred
        except OSError:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]


def load_env_file(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip().strip('"').strip("'")
    return out


def http(method: str, url: str, headers: dict, body) -> tuple[int, dict, str]:
    data = None
    if body is not None:
        if isinstance(body, (dict, list)):
            data = json.dumps(body).encode("utf-8")
            headers = {"Content-Type": "application/json", **headers}
        else:
            data = str(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, dict(r.headers), r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read().decode("utf-8", "replace")


def subset(expected, actual) -> bool:
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(k in actual and subset(v, actual[k]) for k, v in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(expected) == len(actual) and all(subset(e, a) for e, a in zip(expected, actual))
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        try:
            return float(expected) == float(actual)
        except (TypeError, ValueError):
            return False
    return expected == actual


def run_suite(spec_path: Path, binary: str, port: int, only: str | None) -> tuple[int, int]:
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    entry = (spec_path.parent / spec["entry"]).resolve()
    env = dict(os.environ)
    if spec.get("env_file"):
        env.update(load_env_file((spec_path.parent / spec["env_file"]).resolve()))
    env.update({k: str(v) for k, v in spec.get("env", {}).items()})
    port = free_port(port)
    base = f"http://127.0.0.1:{port}"
    log_path = spec_path.with_suffix("").with_suffix(".server.log")
    log = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen([binary, "--api", str(entry), "--port", str(port), "--host", "127.0.0.1"],
                            cwd=str(entry.parent), env=env, stdout=log, stderr=subprocess.STDOUT)
    passed = failed = 0
    try:
        health = spec.get("health", "/health")
        deadline = time.time() + float(spec.get("startup_timeout", 60))
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            try:
                if http("GET", base + health, {}, None)[0] == 200:
                    break
            except Exception:
                time.sleep(0.25)
        else:
            print(f"FAIL  {spec_path.name}: server did not answer {health} in time")
            return 0, 1
        if proc.poll() is not None:
            print(f"FAIL  {spec_path.name}: server exited early (see {log_path.name})")
            return 0, 1
        for case in spec["cases"]:
            name = case.get("name", case.get("path"))
            if only and only not in name:
                continue
            t0 = time.time()
            status, headers, body = http(case.get("method", "GET"), base + case["path"], case.get("headers", {}), case.get("body"))
            exp = case.get("expect", {})
            problems = []
            if "status" in exp and status != exp["status"]:
                problems.append(f"status {status} != {exp['status']}")
            if "contains" in exp:
                for needle in ([exp["contains"]] if isinstance(exp["contains"], str) else exp["contains"]):
                    if needle not in body:
                        problems.append(f"body missing {needle!r}")
            if "json" in exp:
                try:
                    parsed = json.loads(body)
                except ValueError:
                    parsed = None
                    problems.append("body is not JSON")
                if parsed is not None and not subset(exp["json"], parsed):
                    problems.append(f"json mismatch: expected subset {json.dumps(exp['json'])}")
            for hk, hv in exp.get("header", {}).items():
                got = next((v for k, v in headers.items() if k.lower() == hk.lower()), None)
                if got is None or hv not in got:
                    problems.append(f"header {hk}={got!r} !~ {hv!r}")
            ms = int((time.time() - t0) * 1000)
            if problems:
                failed += 1
                print(f"FAIL  {spec_path.stem} :: {name} ({ms} ms)")
                for p in problems:
                    print(f"      - {p}")
                print(f"      body: {body[:400]}")
            else:
                passed += 1
                print(f"PASS  {spec_path.stem} :: {name} ({ms} ms)")
    finally:
        proc.kill()
        proc.wait(timeout=10)
        log.close()
        if failed:
            tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-12:]
            print(f"      --- server log tail ({log_path.name}) ---")
            for line in tail:
                print(f"      {line}")
    return passed, failed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target", help="suite .api.json or a directory of them")
    ap.add_argument("--bin", default=os.environ.get("TYPEEASY_BIN", "./src/typeeasy.exe" if os.name == "nt" else "./src/typeeasy"))
    ap.add_argument("--port", type=int, default=8181)
    ap.add_argument("-k", dest="only", default=None, help="only cases whose name contains this")
    args = ap.parse_args()
    args.bin = str(Path(args.bin).resolve())   # server runs with cwd = suite dir
    target = Path(args.target)
    suites = sorted(target.rglob("*.api.json")) if target.is_dir() else [target]
    if not suites:
        print(f"[runner] no *.api.json under {target}")
        return 1
    total_p = total_f = 0
    for i, suite in enumerate(suites):
        p, f = run_suite(suite, args.bin, args.port + i, args.only)
        total_p += p
        total_f += f
    print(f"\n=== API TOTAL: PASS={total_p}  FAIL={total_f} ===")
    return 1 if total_f else 0


if __name__ == "__main__":
    sys.exit(main())
