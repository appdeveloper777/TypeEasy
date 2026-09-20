#!/usr/bin/env python3
"""spec_coverage.py — Cobertura spec ↔ tests 1:1.

Cada regla normativa de docs/SPEC.md lleva un ID entre corchetes ("[NUM-3]").
Cada test de tests/lang/ puede declarar en su cabecera `// spec: NUM-3` (varios IDs
separados por coma o espacio). Este script cruza ambos lados:

  * IDs citados por tests que NO existen en la spec  -> error (exit 1): la spec cambió
    o el test tiene un typo.
  * Reglas de la spec sin ningún test                -> se listan; exit 1 solo con --strict.

Uso:
  python tools/te-test/spec_coverage.py            # reporte + exit 1 si hay IDs desconocidos
  python tools/te-test/spec_coverage.py --strict   # además exit 1 si hay reglas sin test
  python tools/te-test/spec_coverage.py --json     # salida JSON (para CI / badges)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

RULE_RE = re.compile(r"`\[([A-Z]{2,6}-\d+)\]`")
TEST_RE = re.compile(r"^\s*//\s*spec\s*:\s*(.+)$")
ID_RE = re.compile(r"[A-Z]{2,6}-\d+")


def spec_rules(spec: Path) -> dict[str, int]:
    rules: dict[str, int] = {}
    for i, line in enumerate(spec.read_text(encoding="utf-8").splitlines(), 1):
        for m in RULE_RE.finditer(line):
            rules.setdefault(m.group(1), i)
    return rules


def test_refs(root: Path) -> dict[str, list[Path]]:
    refs: dict[str, list[Path]] = {}
    for te in sorted(root.rglob("*.te")):
        with te.open(encoding="utf-8", errors="replace") as f:
            for i, raw in enumerate(f):
                if i >= 80:
                    break
                line = raw.rstrip()
                if not line:
                    continue
                if not line.lstrip().startswith("//"):
                    break
                m = TEST_RE.match(line)
                if not m:
                    continue
                for rid in ID_RE.findall(m.group(1)):
                    refs.setdefault(rid, []).append(te)
    return refs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", default="docs/SPEC.md")
    ap.add_argument("--tests", default="tests/lang")
    ap.add_argument("--strict", action="store_true", help="exit 1 tambien si hay reglas sin test")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    repo = Path(__file__).resolve().parents[2]
    rules = spec_rules(repo / a.spec)
    refs = test_refs(repo / a.tests)
    unknown = sorted(r for r in refs if r not in rules)
    uncovered = sorted(r for r in rules if r not in refs)
    covered = sorted(r for r in rules if r in refs)
    report = {
        "rules": len(rules), "covered": len(covered), "uncovered": uncovered, "unknown": unknown,
        "tests_per_rule": {r: [str(p.relative_to(repo)).replace("\\", "/") for p in refs[r]] for r in covered},
    }
    if a.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        pct = (100.0 * len(covered) / len(rules)) if rules else 0.0
        print(f"spec rules: {len(rules)}  covered: {len(covered)} ({pct:.0f}%)  uncovered: {len(uncovered)}  unknown ids in tests: {len(unknown)}")
        for r in covered:
            print(f"  OK    {r:<10} {len(refs[r])} test(s): {', '.join(p.name for p in refs[r])}")
        for r in uncovered:
            print(f"  MISS  {r:<10} (SPEC.md:{rules[r]}) sin test")
        for r in unknown:
            print(f"  ???   {r:<10} citado en {', '.join(str(p.relative_to(repo)) for p in refs[r])} pero no existe en la spec")
    if unknown:
        return 1
    if a.strict and uncovered:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
