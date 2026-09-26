#!/usr/bin/env python3
"""te_sqlcheck.py — valida contra una base REAL el SQL embebido en archivos .te.

Extrae las consultas que se pasan a db_query/db_exec/mysql_*/sql_*/sqlite_* (literales,
constantes `let X = "..."` y concatenaciones con `+`/concat() de literales), las prepara
en la base SIN ejecutarlas y reporta archivo:línea de cada error (tabla o columna que no
existe, sintaxis inválida, etc.).

Backends (ninguno ejecuta la consulta):
  --sqlite archivo.db          EXPLAIN <sql>            (módulo sqlite3 de Python)
  --mysql-cmd "mysql mi_db"    PREPARE s FROM '<sql>'   (cliente mysql/mariadb por CLI)

Solo se validan SELECT/INSERT/UPDATE/DELETE/REPLACE/WITH. Los parámetros `@nombre` y `?`
se preparan como placeholders. Las consultas armadas con valores de runtime se cuentan
como "dinámicas" y se omiten (con --loose se reemplazan por `?` y sus errores salen como
warning).

Uso:
  python tools/te-sqlcheck/te_sqlcheck.py --sqlite app.db src/*.te
  python tools/te-sqlcheck/te_sqlcheck.py --mysql-cmd "mysql -uroot mi_db" modules/**/*.te
  ... --json            salida JSON
Exit: 0 sin errores, 1 si hay errores, 2 uso inválido.
"""
from __future__ import annotations

import argparse
import glob
import json
import re
import shlex
import sqlite3
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

SQL_FUNCS = {
    "db_query", "db_exec", "db_query_one", "db_scalar",
    "mysql_query", "mysql_exec", "mysql_query_one",
    "sql_query", "sql_exec",
    "sqlite_query", "sqlite_exec",
    "pg_query", "pg_exec",
}
DML_RE = re.compile(r"^\s*\(?\s*(SELECT|INSERT|UPDATE|DELETE|REPLACE|WITH)\b", re.I)
PARAM_RE = re.compile(r"(?<![@\w])@([A-Za-z_]\w*)")
DYN = object()  # marcador: valor desconocido en tiempo de build


@dataclass
class Tok:
    kind: str   # str | id | op
    val: str
    line: int


def tokenize(src: str) -> list[Tok]:
    toks: list[Tok] = []
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += src.count("\n", i, j)
            i = j
        elif c == '"' or (c == "$" and i + 1 < n and src[i + 1] == '"'):
            interp = c == "$"
            if interp:
                i += 1
            start_line = line
            i += 1
            buf = []
            while i < n and src[i] != '"':
                ch = src[i]
                if ch == "\\" and i + 1 < n:
                    nx = src[i + 1]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(nx, "\\" + nx))
                    i += 2
                    continue
                if ch == "\n":
                    line += 1
                buf.append(ch)
                i += 1
            i += 1
            # $"...{x}..." interpola en runtime: si tiene {expr} es dinámico
            val = "".join(buf)
            toks.append(Tok("dyn" if interp and "{" in val else "str", val, start_line))
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            toks.append(Tok("id", src[i:j], line))
            i = j
        else:
            toks.append(Tok("op", c, line))
            i += 1
    return toks


def split_args(toks: list[Tok], i: int) -> tuple[list[list[Tok]], int]:
    """toks[i] es '('. Devuelve los argumentos top-level y el índice tras ')'."""
    depth, args, cur = 0, [], []
    j = i
    while j < len(toks):
        t = toks[j]
        if t.kind == "op" and t.val in "([{":
            depth += 1
            if depth > 1:
                cur.append(t)
        elif t.kind == "op" and t.val in ")]}":
            depth -= 1
            if depth == 0:
                if cur:
                    args.append(cur)
                return args, j + 1
            cur.append(t)
        elif t.kind == "op" and t.val == "," and depth == 1:
            args.append(cur)
            cur = []
        else:
            cur.append(t)
        j += 1
    return args, j


class Folder:
    """Evalúa en build una expresión de strings: "a" + X + concat("b", Y)."""

    def __init__(self, consts: dict, loose: bool):
        self.consts = consts
        self.loose = loose
        self.dynamic = False

    def fold(self, toks: list[Tok]):
        self.dynamic = False
        parts = self._sum(toks)
        if parts is DYN:
            return DYN
        return parts

    def _piece(self, v):
        if v is DYN:
            self.dynamic = True
            return "?" if self.loose else DYN
        return v

    def _sum(self, toks: list[Tok]):
        # separa por '+' top-level
        depth, terms, cur = 0, [], []
        for t in toks:
            if t.kind == "op" and t.val in "([{":
                depth += 1
            elif t.kind == "op" and t.val in ")]}":
                depth -= 1
            if depth == 0 and t.kind == "op" and t.val == "+":
                terms.append(cur)
                cur = []
            else:
                cur.append(t)
        terms.append(cur)
        out = []
        for term in terms:
            v = self._piece(self._term(term))
            if v is DYN:
                return DYN
            out.append(v)
        return "".join(out)

    def _term(self, t: list[Tok]):
        if len(t) == 1 and t[0].kind == "str":
            return t[0].val
        if len(t) == 1 and t[0].kind == "id" and t[0].val in self.consts:
            return self.consts[t[0].val]
        if len(t) >= 3 and t[0].kind == "op" and t[0].val == "(" and t[-1].val == ")":
            return self._sum(t[1:-1])
        if len(t) >= 3 and t[0].kind == "id" and t[0].val == "concat" and t[1].val == "(":
            args, end = split_args(t, 1)
            if end != len(t):
                return DYN
            out = []
            for a in args:
                v = self._piece(self._sum(a))
                if v is DYN:
                    return DYN
                out.append(v)
            return "".join(out)
        return DYN


@dataclass
class Query:
    file: str
    line: int
    func: str
    sql: str
    loose: bool = False
    error: str | None = None


@dataclass
class Report:
    queries: list[Query] = field(default_factory=list)
    dynamic: list[tuple[str, int, str]] = field(default_factory=list)


def collect_consts(toks: list[Tok], folder: Folder) -> dict:
    consts: dict = {}
    reassigned: set[str] = set()
    for k in range(len(toks) - 1):
        a, b = toks[k], toks[k + 1]
        prev = toks[k - 1] if k else None
        if a.kind == "id" and b.kind == "op" and b.val == "=" and not (
                k + 2 < len(toks) and toks[k + 2].val == "="):
            if prev is None or not (prev.kind == "id" and prev.val in ("let", "var", "const")):
                if not (prev is not None and prev.kind == "op" and prev.val in ("!", "<", ">", "=")):
                    reassigned.add(a.val)
        if a.kind == "op" and a.val == "+" and b.val == "=" and prev is not None and prev.kind == "id":
            reassigned.add(prev.val)
    k = 0
    while k < len(toks):
        t = toks[k]
        if t.kind == "id" and t.val in ("let", "var", "const") and k + 2 < len(toks) \
                and toks[k + 1].kind == "id" and toks[k + 2].val == "=":
            name = toks[k + 1].val
            j, depth, expr = k + 3, 0, []
            while j < len(toks):
                x = toks[j]
                if x.kind == "op" and x.val in "([{":
                    depth += 1
                elif x.kind == "op" and x.val in ")]}":
                    depth -= 1
                if depth == 0 and x.kind == "op" and x.val == ";":
                    break
                if depth < 0:
                    break
                expr.append(x)
                j += 1
            if name in reassigned:
                consts[name] = DYN
            else:
                folder.consts = consts
                saved = folder.loose
                folder.loose = False
                v = folder.fold(expr)
                folder.loose = saved
                consts[name] = v
            k = j
        k += 1
    return consts


def extract(path: Path, loose: bool, rep: Report) -> None:
    src = path.read_text(encoding="utf-8", errors="replace")
    toks = tokenize(src)
    folder = Folder({}, loose)
    folder.consts = collect_consts(toks, folder)
    for k in range(len(toks) - 1):
        t = toks[k]
        if t.kind != "id" or t.val not in SQL_FUNCS or toks[k + 1].val != "(":
            continue
        if k and toks[k - 1].kind == "op" and toks[k - 1].val == ".":
            continue
        # declaración de la función (fn db_query(...) / método) -> no es llamada
        if k and toks[k - 1].kind == "id" and toks[k - 1].val in ("fn", "function"):
            continue
        args, _ = split_args(toks, k + 1)
        found = False
        dyn_hint = False
        for a in args:
            strs = [x for x in a if x.kind == "str"]
            v = folder.fold(a)
            if v is not DYN and isinstance(v, str) and DML_RE.match(v):
                rep.queries.append(Query(str(path), t.line, t.val, v, loose=folder.dynamic))
                found = True
                break
            if v is DYN and (any(DML_RE.match(s.val) for s in strs) or
                             any(x.kind == "id" and x.val in folder.consts for x in a)):
                dyn_hint = True
        if not found and dyn_hint:
            rep.dynamic.append((str(path), t.line, t.val))


def normalize(sql: str) -> str:
    return PARAM_RE.sub("?", sql)


# ---------- backends ----------

def check_sqlite(db: str, queries: list[Query]) -> None:
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    for q in queries:
        sql = normalize(q.sql)
        nbind = 0
        for _ in range(3):
            try:
                conn.execute("EXPLAIN " + sql, [None] * nbind)
                q.error = None
                break
            except sqlite3.ProgrammingError as e:
                m = re.search(r"uses (\d+)", str(e))
                if not m:
                    q.error = str(e)
                    break
                nbind = int(m.group(1))
            except sqlite3.Error as e:
                q.error = str(e)
                break
    conn.close()


def sql_quote(s: str) -> str:
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def check_mysql(cmd: str, queries: list[Query]) -> None:
    lines = []
    for q in queries:
        sql = " ".join(normalize(q.sql).split())  # una línea por sentencia
        lines.append("PREPARE te_sqlcheck FROM " + sql_quote(sql) + ";")
    script = "\n".join(lines) + "\n"
    argv = shlex.split(cmd) + ["--force", "--batch", "--skip-column-names"]
    proc = subprocess.run(argv, input=script, capture_output=True, text=True, encoding="utf-8")
    err_re = re.compile(r"^ERROR (\d+) \([^)]*\) at line (\d+)[^:]*: (.*)$")
    hits = 0
    for raw in proc.stderr.splitlines():
        m = err_re.match(raw.strip())
        if m:
            idx = int(m.group(2)) - 1
            if 0 <= idx < len(queries):
                queries[idx].error = f"{m.group(1)}: {m.group(3)}"
                hits += 1
    if proc.returncode != 0 and hits == 0:
        raise SystemExit(f"te_sqlcheck: el cliente mysql falló: {proc.stderr.strip()[:400]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="archivos .te o globs (**/*.te)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--sqlite", help="archivo SQLite contra el que validar")
    g.add_argument("--mysql-cmd", help='comando del cliente, p.ej. "mysql -uroot erp"')
    ap.add_argument("--loose", action="store_true",
                    help="valida también consultas con partes dinámicas (reemplazadas por ?) como warning")
    ap.add_argument("--list", action="store_true", help="solo lista el SQL extraído (sin base)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    files: list[Path] = []
    for pat in a.files:
        hits = glob.glob(pat, recursive=True)
        files.extend(Path(h) for h in (hits or [pat]) if h.endswith(".te"))
    if not files:
        print("te_sqlcheck: no hay archivos .te", file=sys.stderr)
        return 2
    rep = Report()
    for f in files:
        extract(f, a.loose, rep)

    if not a.list:
        if a.sqlite:
            check_sqlite(a.sqlite, rep.queries)
        elif a.mysql_cmd:
            check_mysql(a.mysql_cmd, rep.queries)
        else:
            print("te_sqlcheck: indicá --sqlite, --mysql-cmd o --list", file=sys.stderr)
            return 2

    errors = [q for q in rep.queries if q.error and not q.loose]
    warns = [q for q in rep.queries if q.error and q.loose]
    if a.json:
        print(json.dumps({
            "checked": len(rep.queries), "errors": len(errors), "warnings": len(warns),
            "dynamic_skipped": len(rep.dynamic),
            "items": [{"file": q.file, "line": q.line, "func": q.func, "loose": q.loose,
                       "error": q.error, "sql": q.sql} for q in rep.queries if q.error or a.list],
        }, ensure_ascii=False, indent=2))
    else:
        if a.list:
            for q in rep.queries:
                print(f"{q.file}:{q.line}: {q.func}: {' '.join(q.sql.split())[:200]}")
        for q in errors + warns:
            kind = "warning" if q.loose else "error"
            print(f"{q.file}:{q.line}: {kind}: {q.error}")
            print(f"    {q.func}: {' '.join(q.sql.split())[:200]}")
        print(f"te_sqlcheck: {len(rep.queries)} consultas validadas, {len(errors)} errores, "
              f"{len(warns)} warnings, {len(rep.dynamic)} dinámicas omitidas")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
