"""Polars equivalent of bench_linq_1M.te — 1M rows columnar.

Run:
    cd typeeasycode
    time python3 examples/12_linq_csv_10M/bench_linq_1M_polars.py
"""
import polars as pl

df = pl.read_csv("productos_1000000.csv")
print("filas=" + str(df.height))

total = df["precio"].sum()
print("sumBy.precio=" + str(total))
print("avgBy.precio=" + str(total / df.height))
print("countWhere>500=" + str(df.filter(pl.col("precio") > 500).height))
print("countWhere<10=" + str(df.filter(pl.col("precio") < 10).height))
print("all(precio>=0)=" + str(int(df["precio"].min() >= 0)))
print("none(precio<0)=" + str(int(df["precio"].min() >= 0)))

baratos = df.filter(pl.col("precio") < 10)
print("where<10.length=" + str(baratos.height))

mb_idx = df["precio"].arg_min()
mb = df.row(mb_idx)
print("minBy=" + mb[0] + "," + str(mb[1]))

mx_idx = df["precio"].arg_max()
mx = df.row(mx_idx)
print("maxBy=" + mx[0] + "," + str(mx[1]))

top5 = df.head(5)
print("take5.length=" + str(top5.height))

print("OK")
