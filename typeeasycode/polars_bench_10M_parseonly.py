"""Polars 10M — parse-only timing (excluye startup de Python)."""
import os, time, polars as pl
fn = os.path.join(os.path.dirname(__file__), "productos_10000000.csv")
# warmup
pl.read_csv(fn)
ts = []
for _ in range(5):
    t0 = time.perf_counter_ns()
    df = pl.read_csv(fn)
    ts.append((time.perf_counter_ns() - t0) // 1_000_000)
ts.sort()
print(f"rows={len(df)}  runs_ms={ts}  median={ts[len(ts)//2]} ms  min={ts[0]} ms")
