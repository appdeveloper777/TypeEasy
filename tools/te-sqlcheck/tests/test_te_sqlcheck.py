#!/usr/bin/env python3
"""Self-test de te_sqlcheck contra una base SQLite temporal (sin dependencias)."""
import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE.parent / "te_sqlcheck.py"
FIXTURE = HERE / "fixture.te"


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "t.db"
        c = sqlite3.connect(db)
        c.execute("CREATE TABLE clientes (id INTEGER PRIMARY KEY, nombre TEXT, email TEXT)")
        c.commit()
        c.close()
        p = subprocess.run([sys.executable, str(TOOL), "--sqlite", str(db), "--json", str(FIXTURE)],
                           capture_output=True, text=True)
    rep = json.loads(p.stdout)
    lines = FIXTURE.read_text(encoding="utf-8").splitlines()
    want_err = {i + 1 for i, l in enumerate(lines) if "// ERR" in l}
    want_ok = sum(1 for l in lines if "// OK" in l)
    got_err = {it["line"] for it in rep["items"] if it["error"]}
    ok = True
    if got_err != want_err:
        print(f"FAIL errores en lineas {sorted(got_err)}, esperado {sorted(want_err)}")
        ok = False
    if rep["checked"] != want_ok + len(want_err):
        print(f"FAIL checked={rep['checked']}, esperado {want_ok + len(want_err)}")
        ok = False
    if rep["dynamic_skipped"] != 1:
        print(f"FAIL dynamic_skipped={rep['dynamic_skipped']}, esperado 1")
        ok = False
    if p.returncode != 1:
        print(f"FAIL exit={p.returncode}, esperado 1")
        ok = False
    print("te_sqlcheck self-test:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
