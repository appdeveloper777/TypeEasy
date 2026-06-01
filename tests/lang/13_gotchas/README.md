# `13_gotchas/` — Inventario vivo de bugs semánticos del core

Cada `.te` aquí ancla un **gotcha real y reproducible** (o su arreglo). El runner
distingue dos mecanismos según cómo se manifieste el bug:

### A) Bugs que terminan con error (exit ≠ 0) → `// xfail:`
El runner trata `xfail` **por código de salida**. Mientras el caso termine con
error, queda `XFAIL` (verde). Cuando se arregle (exit 0), el runner lo marca
`XPASS` y **sale con código ≠ 0**, forzando a cerrar la deuda: quitar `xfail` y
agregar el `// expect:` correcto.

### B) Bugs silenciosos (exit 0, salida incorrecta) → TRIPWIRE
`xfail` no sirve acá (el proceso sale 0). Se ancla la **salida BUGGY actual** con
`// expect:` para que el test pase hoy, y se documenta la **salida DESEADA** en
comentarios. Cuando alguien arregle el bug, la salida cambia → el test **falla
ruidosamente** → eso avisa que hay que actualizar el `expect:` al valor correcto.

### C) Regresión positiva (bug ya resuelto)
Algunos gotchas históricos **ya funcionan**. Se dejan como test positivo
(`// expect:` con la salida correcta, sin `xfail`) para **bloquear regresiones**.

> Regla de Nivel 1 (estabilidad del core): **ningún gotcha nuevo entra sin su
> test aquí**. Esta carpeta es el inventario vivo de la deuda de fiabilidad.

## Inventario actual

| Test | Tipo | Gotcha / estado |
|------|------|-----------------|
| `string_split.te` | regresión✓ | `s.split(sep)` — RESUELTO (`expect: 3`) |
| `inline_call_index.te` | regresión✓ | indexar llamada inline `f(x)[i]` — RESUELTO (`expect: 20`) |
| `inline_call_of_call.te` | regresión✓ | currying inline `make(10)(5)` — RESUELTO (`expect: 15`) |
| `nested_closure_return.te` | regresión✓ | retornar función desde función preserva la captura — RESUELTO (`expect: 15`) |
| `map_closure_capture.te` | regresión✓ | closure capturando var externa dentro de `.map()` — RESUELTO |
| `string_plus_map_index.te` | regresión✓ | `"texto" + mapa["clave"]` — RESUELTO |
| `out_of_range_index.te` | TRIPWIRE | índice fuera de rango `xs[99]` colapsa a 0 silencioso (deseado: null/vacío) |

Al arreglar un TRIPWIRE: actualizá su `// expect:` al valor deseado.
Al arreglar un `xfail`: quitá la directiva y agregá el `// expect:` correcto.
Nunca borres un test: queda como regresión permanente.
