"""Polars idiomatic: groupBy + agg múltiples sobre 1M filas."""
import os, polars as pl

fn = os.path.join(os.path.dirname(__file__), "..", "..", "productos_cat_1M.csv")
df = pl.read_csv(fn)
print("filas=" + str(df.height))

# 1) groupBy nativo (esto es donde Polars debería brillar)
grp = (df.group_by("cat")
         .agg(pl.len().alias("count"), pl.col("precio").sum().alias("sumPrecio"))
         .sort("cat"))
totalSum = 0
for row in grp.iter_rows():
    c, cnt, sp = row
    print(f"cat={c} count={cnt} sumPrecio={sp}")
    totalSum += sp
print("totalSum=" + str(totalSum))

# 2) min/max global
print(f"min={df['precio'].min()} max={df['precio'].max()}")

# 3) filter complejo
cnt2 = df.filter((pl.col("precio") > 500) & (pl.col("stock") < 100)).height
print("countAhi=" + str(cnt2))

print("OK")
