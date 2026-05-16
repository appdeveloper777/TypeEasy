# 11 — LINQ sobre listas de objetos

Operadores LINQ de v0.0.11 aplicados a colecciones de instancias de clases
definidas por el usuario.

## Cómo correr

```bash
docker compose run --rm typeeasy examples/11_linq_objects/linq_objetos.te
```

## Qué demuestra

- Agregaciones con lambda accesor: `sumBy`, `avgBy`, `countWhere`.
- Operadores que devuelven el **objeto completo**: `minBy`, `maxBy`,
  `first`, `last`, `firstWhere`.
- Filtrado y proyección: `where`, `select`.
- Ordenamiento por campo: `orderBy`, `orderByDescending`.
- Predicados: `all`, `none`.
- Encadenamiento (`where → orderByDescending → first`).
