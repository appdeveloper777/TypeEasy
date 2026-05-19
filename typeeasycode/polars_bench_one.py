"""Single-shot polars read of the same CSV used by TypeEasy bench."""
import os, polars as pl
fn = os.path.join(os.path.dirname(__file__), "productos_1000000.csv")
df = pl.read_csv(fn)
print(len(df))
