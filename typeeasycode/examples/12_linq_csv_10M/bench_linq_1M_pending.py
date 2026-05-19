"""Python equivalent of bench_linq_1M_pending.te — operators added in Phase 2:
find/firstWhere, lastWhere, takeWhile, skipWhile, orderBy, orderByDescending, groupBy.

Run:
    cd typeeasycode
    time python3 examples/12_linq_csv_10M/bench_linq_1M_pending.py
"""
import csv
from itertools import takewhile, dropwhile
from collections import defaultdict

class Producto:
    __slots__ = ("nombre", "precio")

productos = []
with open("productos_1000000.csv", newline="") as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        p = Producto()
        p.nombre = row[0]
        p.precio = int(row[1])
        productos.append(p)

print("filas=" + str(len(productos)))

f1 = next(p for p in productos if p.precio > 999)
print("firstWhere>999=" + str(f1.precio))

l1 = None
for p in productos:
    if p.precio < 2:
        l1 = p
print("lastWhere<2=" + str(l1.precio))

tw = list(takewhile(lambda p: p.precio < 500, productos))
print("takeWhile<500.length=" + str(len(tw)))

sw = list(dropwhile(lambda p: p.precio < 500, productos))
print("skipWhile<500.length=" + str(len(sw)))

ob = sorted(productos, key=lambda p: p.precio)
print("orderBy[0].precio=" + str(ob[0].precio))

obd = sorted(productos, key=lambda p: p.precio, reverse=True)
print("orderByDesc[0].precio=" + str(obd[0].precio))

gb = defaultdict(list)
for p in productos:
    gb[p.precio % 10].append(p)
print("groupBy.keys=" + str(len(gb)))

print("OK")
