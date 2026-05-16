"""Python equivalent of bench_csv_10M_noctor.te
Reads 10M-row CSV into Producto objects (no explicit __init__ args, like the .te version)
and prints the count + first product. Used for TypeEasy vs Python comparison.
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
p0 = productos[0]
print("primer producto: " + p0.nombre + "," + str(p0.precio))
