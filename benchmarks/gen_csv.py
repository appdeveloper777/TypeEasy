import csv, random, sys, time

n = int(sys.argv[1]) if len(sys.argv) > 1 else 10_000_000
out = sys.argv[2] if len(sys.argv) > 2 else "bench_10m.csv"

cats = ["dulce", "salado", "bebida", "snack", "otro"]
random.seed(42)
t0 = time.time()
with open(out, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["id", "nombre", "precio", "categoria"])
    for i in range(1, n + 1):
        w.writerow([i, f"prod{i}", random.randint(1, 100), cats[i % len(cats)]])
print(f"generado {n} filas en {out} ({time.time()-t0:.1f}s)")
