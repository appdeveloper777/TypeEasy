#!/usr/bin/env python3
"""Genera CSV de 5M filas con IDs grandes (12-15 dígitos) para ejercitar
el SIMD int parser del polish #7.

Cada fila: nombre,id_grande
id_grande = 100_000_000_000 + i  (12 digitos)
"""
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 5_000_000
out = sys.argv[2] if len(sys.argv) > 2 else "typeeasycode/bigint_5m.csv"

with open(out, "w") as f:
    f.write("nombre,id_grande\n")
    BASE = 100_000_000_000  # 12-digit base → SIMD path will fire
    for i in range(N):
        f.write(f"Producto,{BASE + i}\n")

print(f"OK: {N} filas, base={BASE}, escrito en {out}")
