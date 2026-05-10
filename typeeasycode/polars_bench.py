import polars as pl, time
fn = "/code/productos_big.csv"
ts=[]
for _ in range(20):
    t=time.perf_counter_ns()
    df = pl.read_csv(fn)
    ts.append((time.perf_counter_ns()-t)//1000)
ts.sort()
for t in ts[:5]: print(f"  {t} us")
