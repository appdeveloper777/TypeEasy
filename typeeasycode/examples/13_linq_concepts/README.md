# 13 — LINQ por concepto

Un archivo `.te` por operador LINQ. Cada uno se ejecuta de forma independiente:

```bash
docker compose run --rm typeeasy examples/13_linq_concepts/linq_where.te
```

## Índice

| Archivo | Concepto |
|---|---|
| `linq_where.te`         | Filtrado por predicado (`where` / `filter`) |
| `linq_select.te`        | Proyección (`select` / `map`) |
| `linq_orderby.te`       | Ordenamiento ascendente y descendente |
| `linq_groupby.te`       | Agrupar por clave (string hash) |
| `linq_distinct.te`      | Únicos (`distinct` / `distinctBy`) |
| `linq_reduce.te`        | Acumulación (`reduce` / `aggregate` / `fold`) |
| `linq_tomap.te`         | Indexar por clave (`toMap` / `toDictionary`) |
| `linq_zip.te`           | Pareo posicional |
| `linq_aggregates.te`    | `sum`/`sumBy`/`count`/`countWhere`/`avg`/`min`/`max` |
| `linq_first_any_all.te` | `first`/`firstWhere`/`last`/`any`/`all`/`none` |
| `linq_take_skip.te`     | Paginación (`take`/`skip`) |
| `linq_join.te`          | Unir strings (`join`) |
| `linq_fusion.te`        | Fusión `where → select` en una pasada |
| `linq_lazy.te`          | Iteradores diferidos (`.lazy()`) |
