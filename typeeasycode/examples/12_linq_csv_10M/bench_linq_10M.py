"""Python equivalent of bench_linq_10M.te
Same aggregations, same input, for wall-clock comparison.

Run:
    cd typeeasycode
    time python3 examples/12_linq_csv_10M/bench_linq_10M.py
"""
import csv

class Producto:
    __slots__ = ("nombre", "precio")

productos = []
with open("productos_10000000.csv", newline="") as f:
    reader = csv.reader(f)
    next(reader)  # header
    for row in reader:
        p = Producto()
        p.nombre = row[0]
        p.precio = int(row[1])
        productos.append(p)

print("filas=" + str(len(productos)))

total = sum(p.precio for p in productos)
print("sumBy.precio=" + str(total))
print("countWhere>50=" + str(sum(1 for p in productos if p.precio > 50)))
print("countWhere<10=" + str(sum(1 for p in productos if p.precio < 10)))

print("OK")
