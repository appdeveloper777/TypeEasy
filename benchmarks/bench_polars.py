import polars as pl, time

t0 = time.time()
df = pl.read_csv("benchmarks/bench_10m.csv")
t_load = time.time() - t0

t1 = time.time()
dulces = df.filter(pl.col("categoria") == "dulce")
cnt = dulces.height
suma = dulces["precio"].sum()
prom = dulces["precio"].mean()
t_query = time.time() - t1

print(f"count={cnt} sum={suma} avg={prom:.6f}")
print(f"[polars] load={t_load:.3f}s query={t_query:.3f}s total={t_load+t_query:.3f}s")
