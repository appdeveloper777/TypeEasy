"""Polars groupBy 1M — parse + agg, parse-only y agg-only."""
import os, time, polars as pl
fn = os.path.join(os.path.dirname(__file__), "..", "..", "productos_cat_1M.csv")

# Warmup
df_w = pl.read_csv(fn)
_ = (df_w.group_by("cat").agg(pl.len(), pl.col("precio").sum()))

# Parse + groupby end-to-end (sin startup Python)
ts_full, ts_parse, ts_agg = [], [], []
for _ in range(5):
    t0 = time.perf_counter_ns()
    df = pl.read_csv(fn)
    t1 = time.perf_counter_ns()
    g = (df.group_by("cat")
           .agg(pl.len().alias("count"), pl.col("precio").sum().alias("sum"))
           .sort("cat"))
    mn = df["precio"].min(); mx = df["precio"].max()
    cnt2 = df.filter((pl.col("precio") > 500) & (pl.col("stock") < 100)).height
    t2 = time.perf_counter_ns()
    ts_parse.append((t1 - t0) // 1_000_000)
    ts_agg.append((t2 - t1) // 1_000_000)
    ts_full.append((t2 - t0) // 1_000_000)

def stat(ts):
    ts2 = sorted(ts); return ts2[len(ts2)//2], ts2[0]

mp, np = stat(ts_parse); ma, na = stat(ts_agg); mf, nf = stat(ts_full)
print(f"parse-only       : runs={sorted(ts_parse)}  median={mp} ms  min={np} ms")
print(f"groupby+min/max+filter: runs={sorted(ts_agg)}  median={ma} ms  min={na} ms")
print(f"full (parse+ops) : runs={sorted(ts_full)}  median={mf} ms  min={nf} ms")
