#!/usr/bin/env python3
"""
TypeEasy language test runner.

Discovers .te files under a root directory and executes each with the
TypeEasy interpreter, comparing observed stdout (and exit code) against
expectations declared per test.

Expectation sources (first match wins):
  1. Sibling file `<name>.expected` (full stdout, byte-exact after rstrip).
  2. Header directives inside the `.te` file (one per line, near top):
        // expect: <single-line expected stdout>
        // expect-exit: <int>     (default 0)
        // expect-contains: <substring>
        // expect-stderr-contains: <substring>
        // xfail: <reason>        (test is expected to fail / non-zero exit)
        // skip: <reason>         (skip unconditionally)
        // skip-on: windows|linux (skip on a given OS)
        // timeout: <seconds>     (per-test wall clock, default 30)

If neither a `.expected` file nor any `// expect*` directive is present,
the test is treated as a SMOKE TEST: pass iff exit code == 0.

Interpreter resolution (in order):
  1. --bin <path>
  2. $TYPEEASY_BIN
  3. ./bin/typeeasy[.exe]
  4. Docker fallback: `docker compose run --rm typeeasy <path-inside-container>`
     (only when --docker is passed; the tests must live under a path that the
      container mounts — typically typeeasycode/ → /code).

Outputs:
  - Human summary to stdout.
  - Optional JUnit XML via --junit <path> for CI ingestion.

Exit code = number of FAIL tests (0 = green).
"""
from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional
from xml.etree import ElementTree as ET


# ---------- expectations ----------

DIRECTIVE_RE = re.compile(r"^\s*//\s*(expect|expect-exit|expect-contains|"
                          r"expect-stderr-contains|xfail|skip|skip-on|timeout)\s*:\s*(.*)$")


@dataclass
class Expect:
    expected_stdout: Optional[str] = None       # exact match (rstrip applied)
    expected_exit: int = 0
    contains: list[str] = field(default_factory=list)
    stderr_contains: list[str] = field(default_factory=list)
    xfail: Optional[str] = None
    skip: Optional[str] = None
    skip_on: list[str] = field(default_factory=list)
    timeout_s: float = 30.0
    has_any_assertion: bool = False             # False ⇒ smoke (exit 0 only)


def parse_expect(te_path: Path) -> Expect:
    e = Expect()
    sibling = te_path.with_suffix(".expected")
    if sibling.is_file():
        e.expected_stdout = sibling.read_text(encoding="utf-8").rstrip("\r\n")
        e.has_any_assertion = True

    # Scan first 80 lines for directives. Stop scanning at the first
    # non-comment, non-blank line to avoid surprises mid-file.
    with te_path.open("r", encoding="utf-8", errors="replace") as f:
        for i, raw in enumerate(f):
            if i >= 80:
                break
            line = raw.rstrip()
            if not line:
                continue
            if not line.lstrip().startswith("//"):
                break
            m = DIRECTIVE_RE.match(line)
            if not m:
                continue
            key, val = m.group(1), m.group(2).strip()
            if key == "expect":
                e.expected_stdout = val
                e.has_any_assertion = True
            elif key == "expect-exit":
                try:
                    e.expected_exit = int(val)
                except ValueError:
                    pass
                e.has_any_assertion = True
            elif key == "expect-contains":
                e.contains.append(val)
                e.has_any_assertion = True
            elif key == "expect-stderr-contains":
                e.stderr_contains.append(val)
                e.has_any_assertion = True
            elif key == "xfail":
                e.xfail = val or "expected to fail"
                e.has_any_assertion = True
            elif key == "skip":
                e.skip = val or "skipped"
            elif key == "skip-on":
                e.skip_on.append(val.lower())
            elif key == "timeout":
                try:
                    e.timeout_s = float(val)
                except ValueError:
                    pass
    return e


# ---------- runner ----------

@dataclass
class Result:
    path: Path
    status: str          # PASS | FAIL | XFAIL | XPASS | SKIP
    duration_s: float
    detail: str = ""
    stdout: str = ""
    stderr: str = ""
    exit_code: int = 0


def host_os() -> str:
    s = platform.system().lower()
    if "windows" in s:
        return "windows"
    if "linux" in s:
        return "linux"
    if "darwin" in s:
        return "macos"
    return s


def resolve_native_bin(repo_root: Path, override: Optional[str]) -> Optional[Path]:
    if override:
        p = Path(override)
        return p if p.is_file() else None
    env = os.environ.get("TYPEEASY_BIN")
    if env and Path(env).is_file():
        return Path(env)
    for name in ("typeeasy.exe", "typeeasy"):
        p = repo_root / "bin" / name
        if p.is_file():
            return p
    return None


def run_test(te_path: Path, expect: Expect, bin_path: Optional[Path],
             use_docker: bool, docker_image: str, repo_root: Path) -> Result:
    # Skip handling
    if expect.skip:
        return Result(te_path, "SKIP", 0.0, expect.skip)
    if host_os() in expect.skip_on:
        return Result(te_path, "SKIP", 0.0, f"skip-on {host_os()}")

    if use_docker:
        # Mount repo root at /work and invoke the interpreter inside the image.
        try:
            rel = te_path.resolve().relative_to(repo_root.resolve())
        except ValueError:
            return Result(te_path, "FAIL", 0.0,
                          "Docker mode requires test inside repo root")
        cmd = [
            "docker", "run", "--rm",
            "-v", f"{repo_root.as_posix()}:/work",
            "-w", "/work",
            "--entrypoint", "/typeeasy/typeeasy",
            docker_image,
            f"/work/{rel.as_posix()}",
        ]
    else:
        if not bin_path:
            return Result(te_path, "FAIL", 0.0, "No TypeEasy binary found (use --docker or set TYPEEASY_BIN)")
        cmd = [str(bin_path), str(te_path)]

    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=expect.timeout_s,
            cwd=repo_root,
        )
        elapsed = time.time() - t0
        rc = proc.returncode
        out = (proc.stdout or "").rstrip("\r\n")
        err = (proc.stderr or "").rstrip("\r\n")
    except subprocess.TimeoutExpired:
        return Result(te_path, "FAIL", expect.timeout_s,
                      f"TIMEOUT after {expect.timeout_s}s")
    except FileNotFoundError as exc:
        return Result(te_path, "FAIL", 0.0, f"Cannot launch interpreter: {exc}")

    # XFAIL: expected to fail (non-zero exit) — invert pass/fail logic
    if expect.xfail:
        if rc != 0:
            return Result(te_path, "XFAIL", elapsed, expect.xfail,
                          out, err, rc)
        return Result(te_path, "XPASS", elapsed,
                      f"xfail expected non-zero exit, got 0", out, err, rc)

    failures: list[str] = []
    if rc != expect.expected_exit:
        failures.append(f"exit={rc} (expected {expect.expected_exit})")
    if expect.expected_stdout is not None and out != expect.expected_stdout:
        failures.append("stdout mismatch")
    for needle in expect.contains:
        if needle not in out:
            failures.append(f"stdout missing: {needle!r}")
    for needle in expect.stderr_contains:
        if needle not in err:
            failures.append(f"stderr missing: {needle!r}")

    if not expect.has_any_assertion and rc == 0:
        return Result(te_path, "PASS", elapsed, "smoke OK", out, err, rc)

    if failures:
        return Result(te_path, "FAIL", elapsed, "; ".join(failures), out, err, rc)
    return Result(te_path, "PASS", elapsed, "", out, err, rc)


# ---------- discovery + reporting ----------

def discover(root: Path) -> list[Path]:
    files = sorted(p for p in root.rglob("*.te") if p.is_file())
    return files


def render_junit(results: list[Result], path: Path) -> None:
    suite = ET.Element("testsuite", {
        "name": "typeeasy.lang",
        "tests": str(len(results)),
        "failures": str(sum(1 for r in results if r.status in ("FAIL", "XPASS"))),
        "skipped": str(sum(1 for r in results if r.status == "SKIP")),
        "time": f"{sum(r.duration_s for r in results):.3f}",
    })
    for r in results:
        case = ET.SubElement(suite, "testcase", {
            "name": r.path.name,
            "classname": str(r.path.parent.as_posix()),
            "time": f"{r.duration_s:.3f}",
        })
        if r.status in ("FAIL", "XPASS"):
            fail = ET.SubElement(case, "failure", {"message": r.detail})
            fail.text = f"--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}\n"
        elif r.status == "SKIP":
            ET.SubElement(case, "skipped", {"message": r.detail})
        elif r.status == "XFAIL":
            so = ET.SubElement(case, "system-out")
            so.text = f"[XFAIL] {r.detail}"
    path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="TypeEasy language test runner")
    ap.add_argument("root", nargs="?", default="tests/lang",
                    help="Directory to discover .te tests (default: tests/lang)")
    ap.add_argument("--bin", default=None, help="Path to typeeasy binary")
    ap.add_argument("--docker", action="store_true",
                    help="Run tests via `docker run` against an interpreter image")
    ap.add_argument("--docker-image", default="typeeasy-typeeasy:latest",
                    help="Image to invoke in --docker mode (default: typeeasy-typeeasy:latest)")
    ap.add_argument("--junit", default=None, help="Write JUnit XML report")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--filter", default=None,
                    help="Substring filter applied to test path")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    root = (repo_root / args.root).resolve() if not Path(args.root).is_absolute() else Path(args.root)
    if not root.exists():
        print(f"[runner] root does not exist: {root}", file=sys.stderr)
        return 2

    bin_path = None if args.docker else resolve_native_bin(repo_root, args.bin)
    if not args.docker and bin_path is None:
        print("[runner] no native binary; try --docker or set TYPEEASY_BIN", file=sys.stderr)
        return 2

    tests = discover(root)
    if args.filter:
        tests = [t for t in tests if args.filter in str(t)]
    if not tests:
        print(f"[runner] no .te tests under {root}")
        return 0

    print(f"[runner] {len(tests)} tests under {root.relative_to(repo_root)}"
          f"  mode={'docker' if args.docker else f'native:{bin_path}'}")

    results: list[Result] = []
    for t in tests: 
        e = parse_expect(t)
        r = run_test(t, e, bin_path, args.docker, args.docker_image, repo_root)
        results.append(r)
        rel = t.relative_to(repo_root)
        line = f"{r.status:5s} {rel} ({r.duration_s*1000:.0f} ms)"
        if r.detail and r.status != "PASS":
            line += f"  — {r.detail}"
        print(line)
        if args.verbose and r.status in ("FAIL", "XPASS"):
            if r.stdout:
                print("    stdout:", r.stdout.splitlines()[:5])
            if r.stderr:
                print("    stderr:", r.stderr.splitlines()[:5])

    if args.junit:
        render_junit(results, Path(args.junit))

    counts = {k: sum(1 for r in results if r.status == k)
              for k in ("PASS", "FAIL", "XFAIL", "XPASS", "SKIP")}
    print("\n=== TOTAL: " + "  ".join(f"{k}={v}" for k, v in counts.items()) + " ===")
    return counts["FAIL"] + counts["XPASS"]


if __name__ == "__main__":
    sys.exit(main())
