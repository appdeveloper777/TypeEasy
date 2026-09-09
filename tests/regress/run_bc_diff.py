#!/usr/bin/env python3
"""Differential test: bytecode accelerator vs. AST walker.

Runs every tests/lang/**/*.te twice — default (bytecode ON) and with
TYPEEASY_NO_BC=1 (walker only) — and requires stdout + exit code to be
byte-identical. The bytecode layer is an *accelerator* of the walker, not a
second engine: it must never change a program's observable behaviour.

Usage: run_bc_diff.py [--bin PATH] [--root tests/lang] [--timeout 30]
Exit code = number of differing tests.
"""
from __future__ import annotations
import argparse, os, re, subprocess, sys
from pathlib import Path

DIRECTIVE_RE = re.compile(r"^\s*//\s*(skip|skip-on|env|timeout|nondeterministic)\s*:\s*(.*)$")


def directives(p: Path):
    out = {"skip": None, "skip-on": None, "env": [], "timeout": None, "nondeterministic": None}
    try:
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines()):
            if i > 40:
                break
            m = DIRECTIVE_RE.match(line)
            if not m:
                continue
            k, v = m.group(1), m.group(2).strip()
            if k == "env":
                out["env"].append(v)
            else:
                out[k] = v
    except OSError:
        pass
    return out


def run(bin_path, te, env, timeout, cwd):
    try:
        r = subprocess.run([bin_path, te.name], cwd=cwd, env=env, capture_output=True,
                           timeout=timeout)
        return r.returncode, r.stdout
    except subprocess.TimeoutExpired:
        return "timeout", b""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.environ.get("TYPEEASY_BIN", "./src/typeeasy"))
    ap.add_argument("--root", default="tests/lang")
    ap.add_argument("--timeout", type=int, default=30)
    a = ap.parse_args()
    bin_path = str(Path(a.bin).resolve())
    files = sorted(Path(a.root).rglob("*.te"))
    is_win = sys.platform.startswith("win")
    diffs, ran, skipped = [], 0, 0
    for te in files:
        d = directives(te)
        if d["skip"] is not None or d["nondeterministic"] is not None or (d["skip-on"] == ("windows" if is_win else "linux")):
            skipped += 1
            continue
        env = dict(os.environ)
        for kv in d["env"]:
            if "=" in kv:
                k, v = kv.split("=", 1)
                env[k.strip()] = v.strip()
        timeout = int(d["timeout"]) if d["timeout"] else a.timeout
        env_bc = dict(env); env_bc.pop("TYPEEASY_NO_BC", None)
        env_nobc = dict(env); env_nobc["TYPEEASY_NO_BC"] = "1"
        rc1, out1 = run(bin_path, te, env_bc, timeout, str(te.parent))
        rc2, out2 = run(bin_path, te, env_nobc, timeout, str(te.parent))
        ran += 1
        if rc1 != rc2 or out1 != out2:
            diffs.append((te, rc1, rc2, out1, out2))
            print(f"DIFF  {te}  (bc rc={rc1}, walker rc={rc2})")
            if out1 != out2:
                l1 = out1.decode("utf-8", "replace").splitlines()
                l2 = out2.decode("utf-8", "replace").splitlines()
                for i in range(max(len(l1), len(l2))):
                    x = l1[i] if i < len(l1) else "<EOF>"
                    y = l2[i] if i < len(l2) else "<EOF>"
                    if x != y:
                        print(f"      line {i+1}: bc={x!r}  walker={y!r}")
                        break
    print(f"\nbc-diff: {ran} tests compared, {skipped} skipped, {len(diffs)} differ.")
    return len(diffs)


if __name__ == "__main__":
    sys.exit(main())
