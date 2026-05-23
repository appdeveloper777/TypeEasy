"""Polars equivalent of bench_linq_10M.te — 10M rows columnar.

Misma carga (read_csv) y mismas tres agregaciones LINQ-equivalentes:
  sumBy(precio), countWhere(precio>50), countWhere(precio<10).

Run:
    cd typeeasycode
    time python3 examples/12_linq_csv_10M/bench_linq_10M_polars.py
"""
import polars as pl

df = pl.read_csv("productos_10000000.csv")
print("filas=" + str(df.height))

total = df["precio"].sum()
print("sumBy.precio=" + str(total))
print("countWhere>50=" + str(df.filter(pl.col("precio") > 50).height))
print("countWhere<10=" + str(df.filter(pl.col("precio") < 10).height))

print("OK")
