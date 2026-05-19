"""Polars read of 10M-row CSV."""
import os, polars as pl
fn = os.path.join(os.path.dirname(__file__), "productos_10000000.csv")
df = pl.read_csv(fn)
print(len(df))
