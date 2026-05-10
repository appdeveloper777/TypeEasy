import polars as pl, time, sys

fn = "/code/productos_1000000.csv"
runs = int(sys.argv[1]) if len(sys.argv) > 1 else 10

times = []
for i in range(runs):
    t = time.perf_counter_ns()
    df = pl.read_csv(fn)
    elapsed = (time.perf_counter_ns() - t) // 1000
    times.append(elapsed)
    print(f"  run{i+1}: {elapsed} us  rows={len(df)}", flush=True)

times.sort()
print(f"--- best: {times[0]} us  worst: {times[-1]} us  avg: {sum(times)//len(times)} us ---")
