"""Python list-of-objects equivalent: groupBy + agg múltiples sobre 1M filas."""
import csv

class Producto:
    __slots__ = ("cat","precio","stock")

productos = []
with open("productos_cat_1M.csv", newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        p = Producto()
        p.cat = row[0]; p.precio = int(row[1]); p.stock = int(row[2])
        productos.append(p)

print("filas=" + str(len(productos)))

cats = ["A","B","C","D","E","F","G","H","I","J"]
totalSum = 0
for c in cats:
    cnt = sum(1 for p in productos if p.cat == c)
    sp  = sum(p.precio for p in productos if p.cat == c)
    print(f"cat={c} count={cnt} sumPrecio={sp}")
    totalSum += sp
print("totalSum=" + str(totalSum))

mb = min(productos, key=lambda p: p.precio)
mx = max(productos, key=lambda p: p.precio)
print(f"min={mb.precio} max={mx.precio}")

cnt2 = sum(1 for p in productos if p.precio > 500 and p.stock < 100)
print("countAhi=" + str(cnt2))

print("OK")
