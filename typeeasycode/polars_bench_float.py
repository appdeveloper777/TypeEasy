"""Polars equivalente a _bench_float_where.te:
read CSV (cat:str, precio:float, stock:int) y countWhere(precio > 500.0).
Imprime parse_ms y filter_ms + cnt para comparar fase-a-fase con TE."""
import os, sys, time, polars as pl

fn = sys.argv[1] if len(sys.argv) > 1 else "/tmp/g.csv"
t0 = time.perf_counter_ns()
df = pl.read_csv(fn, schema={"cat": pl.Utf8, "precio": pl.Float64, "stock": pl.Int64})
t1 = time.perf_counter_ns()
cnt = df.filter(pl.col("precio") > 500.0).height
t2 = time.perf_counter_ns()
print(f"filas={df.height}")
print(f"cnt={cnt}")
print(f"parse_ms={(t1 - t0) / 1e6:.1f}")
print(f"filter_ms={(t2 - t1) / 1e6:.1f}")
print(f"total_ms={(t2 - t0) / 1e6:.1f}")
