"""Python equivalent of bench_linq_1M.te — full LINQ coverage on 1M rows.

Run:
    cd typeeasycode
    time python3 examples/12_linq_csv_10M/bench_linq_1M.py
"""
import csv

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

total = sum(p.precio for p in productos)
print("sumBy.precio=" + str(total))
print("avgBy.precio=" + str(total / len(productos)))
print("countWhere>500=" + str(sum(1 for p in productos if p.precio > 500)))
print("countWhere<10=" + str(sum(1 for p in productos if p.precio < 10)))
print("all(precio>=0)=" + str(int(all(p.precio >= 0 for p in productos))))
print("none(precio<0)=" + str(int(not any(p.precio < 0 for p in productos))))

baratos = [p for p in productos if p.precio < 10]
print("where<10.length=" + str(len(baratos)))

mb = min(productos, key=lambda p: p.precio)
print("minBy=" + mb.nombre + "," + str(mb.precio))

mx = max(productos, key=lambda p: p.precio)
print("maxBy=" + mx.nombre + "," + str(mx.precio))

top5 = productos[:5]
print("take5.length=" + str(len(top5)))

print("OK")
