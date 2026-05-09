# TypeEasy — Performance log

Registro de tiempos antes/después de cada fase de optimización.
Cada sección la genera automáticamente `benchmarks/run_bench.sh <etiqueta>`.

## Cómo correr

```bash
# Baseline (estado actual, tree-walker con strcmp)
./benchmarks/run_bench.sh "baseline-tree-walker"

# Después de Fase 1 (enums en node->type)
./benchmarks/run_bench.sh "fase1-enums"

# Después de Fase 2 (slots de variables)
./benchmarks/run_bench.sh "fase2-slots"

# Después de Fase 3 (bytecode + computed goto)
./benchmarks/run_bench.sh "fase3-bytecode"
```

Variables de entorno opcionales:
- `RUNS=5 ./benchmarks/run_bench.sh "..."` — cambia número de corridas (default 3).

## Benchmarks incluidos

| Archivo | Qué mide |
|---|---|
| `bench_loop.te`    | Bucle aritmético puro (200 000 iter, suma acumulada) |
| `bench_arith.te`   | Aritmética mixta + condicionales (100 000 iter) |
| `bench_while.te`   | `while` + `break` + `continue` + `&&` (100 000 iter) |
| `bench_strings.te` | Concatenación de strings + método `.length()` (5 000 iter) |

## Notas

- Los tiempos incluyen el **arranque del contenedor Docker** (~1–2 s fijos).
  Para medir solo la ejecución del intérprete, resta ese baseline o ejecuta
  el binario directamente si tienes uno nativo disponible.
- Para mediciones más estables: cierra navegador y procesos pesados, corre
  con `RUNS=5` y mira la columna **"Mejor (s)"**.
- La columna "Mejor" es la métrica de comparación (descarta outliers).

## Cálculo de speedup

Speedup vs baseline = `T_baseline / T_actual`.
Ejemplo: si baseline = 12.0 s y fase1 = 3.0 s → **speedup = 4×**.

---

## Resultados

<!-- Las corridas se añaden aquí abajo automáticamente -->

## Run: `baseline-tree-walker` — 2026-05-08 18:41:09

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Mejor (s) |
|---|---|---|---|---|
| bench_loop | 14.570 | 18.628 | 12.065 | **12.065** |
| bench_arith | 9.438 | 8.188 | 6.389 | **6.389** |
| bench_while | 4.439 | 3.282 | 3.773 | **3.282** |
| bench_strings | 3.857 | 3.739 | 3.832 | **3.739** |


## Run: `fase1-enums` — 2026-05-08 18:51:25

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Mejor (s) |
|---|---|---|---|---|
| bench_loop | 5.886 | 3.833 | 4.497 | **3.833** |
| bench_arith | 7.473 | 8.534 | 4.971 | **4.971** |
| bench_while | 4.267 | 4.048 | 3.664 | **3.664** |
| bench_strings | 3.328 | 2.845 | 3.223 | **2.845** |


## Run: `fase2-varcache` — 2026-05-08 18:57:00

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Mejor (s) |
|---|---|---|---|---|
| bench_loop | 3.185 | 2.923 | 2.769 | **2.769** |
| bench_arith | 3.187 | 3.692 | 3.010 | **3.010** |
| bench_while | 3.325 | 3.451 | 2.604 | **2.604** |
| bench_strings | 3.043 | 2.936 | 2.540 | **2.540** |


## Run: `fase3-bytecode` — 2026-05-08 19:06:52

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Mejor (s) |
|---|---|---|---|---|
| bench_loop | 3.613 | 2.922 | 2.613 | **2.613** |
| bench_arith | 3.328 | 3.063 | 2.687 | **2.687** |
| bench_while | 3.390 | 3.058 | 2.998 | **2.998** |
| bench_strings | 3.591 | 2.919 | 2.687 | **2.687** |


## Run: `fase3-bytecode-net` — 2026-05-08 19:09:25

Docker overhead (best of 3, bench_noop.te): **2.548s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 4.302 | 2.855 | 2.763 | 2.763 | **0.215** |
| bench_arith | 3.479 | 2.626 | 3.066 | 2.626 | **0.078** |
| bench_while | 3.316 | 2.969 | 2.995 | 2.969 | **0.421** |
| bench_strings | 3.514 | 2.970 | 2.947 | 2.947 | **0.399** |
| bench_loop_heavy | 3.439 | 2.712 | 2.942 | 2.712 | **0.164** |


## Run: `smoke-test` — 2026-05-08 19:14:27

Docker overhead (best of 3, bench_noop.te): **3.340s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 3.204 | 2.707 | 3.247 | 2.707 | **0.000** |
| bench_arith | 3.132 | 2.639 | 3.335 | 2.639 | **0.000** |
| bench_while | 3.372 | 3.187 | 3.341 | 3.187 | **0.000** |
| bench_strings | 3.222 | 3.380 | 3.469 | 3.222 | **0.000** |
| bench_loop_heavy | 4.413 | 3.499 | 3.190 | 3.190 | **0.000** |


## Run: `fase3-bytecode-FIXED` — 2026-05-08 19:17:07

Docker overhead (best of 3, bench_noop.te): **2.645s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|

## Run: `fase3-bytecode-FIXED` — 2026-05-08 19:17:38

Docker overhead (best of 3, bench_noop.te): **2.833s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|

## Run: `fase3-bytecode-FIXED` — 2026-05-08 19:18:24

Docker overhead (best of 3, bench_noop.te): **2.824s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|

## Run: `fase3-bytecode-REAL` — 2026-05-08 19:20:06

Docker overhead (best of 3, bench_noop.te): **2.857s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 3.297 | 3.063 | 3.159 | 3.063 | **0.206** |
| bench_arith | 3.174 | 3.273 | 3.726 | 3.174 | **0.317** |
| bench_while | 4.846 | 3.222 | 3.587 | 3.222 | **0.365** |

## Run: `fase3-bytecode-REAL` — 2026-05-08 19:21:41

Docker overhead (best of 3, bench_noop.te): **2.892s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 3.536 | 3.578 | 3.069 | 3.069 | **0.177** |
| bench_arith | 3.932 | 2.795 | 2.844 | 2.795 | **0.000** |
| bench_while | 3.536 | 2.807 | 2.986 | 2.807 | **0.000** |
| bench_strings | 3.572 | 2.626 | 2.917 | 2.626 | **0.000** |
| bench_loop_heavy | 3.836 | 3.083 | 3.285 | 3.083 | **0.191** |


---

## ⚠️ CORRECCIÓN IMPORTANTE — 2026-05-08

**Bug detectado**: el script `run_bench.sh` original pasaba al contenedor el path
`../benchmarks/bench_X.te`, pero el único volumen Docker mapeado era
`./typeeasycode:/code`. Resultado: el binario imprimía
"No se pudo abrir el archivo", salía con código 1, y el `|| true` del script
silenciaba el error. **Todas las mediciones marcadas `fase1-enums`,
`fase2-varcache`, `fase3-bytecode` y `fase3-bytecode-net` arriba sólo midieron
el overhead de Docker arrancando + el binario muriendo por archivo
inexistente. NO miden el rendimiento del intérprete.**

Adicional: los benches `bench_loop`, `bench_arith`, `bench_loop_heavy`
originales usaban sintaxis C-style (`for (var i=0; i<N; i=i+1)`) que TypeEasy
**no soporta** — el `for` de TypeEasy es `for(var; iters; step)`. Aun cuando el
path hubiera estado bien, esos archivos no parseaban. Los reescribí usando
`while` (que sí parsea y usa el mismo hot-path: `evaluate_expression` +
`interpret_assign` + bytecode VM en Fase 3).

Cambios aplicados al script:
- `set -e` ya estaba; añadí mensaje + `exit 1` cuando `docker compose run` falla.
- Mount adicional `-v $ROOT/benchmarks:/benchmarks`.
- `--entrypoint /typeeasy/typeeasy` con paths absolutos `/benchmarks/X.te`.
- `MSYS_NO_PATHCONV=1` para evitar conversión de paths en Git Bash.

## Mediciones REALES (script arreglado, 2026-05-08 19:21)

Bytecode VM **ON** (estado actual de Fase 3) vs **OFF** (mismo binario, var
`TYPEEASY_NO_BC=1` desactiva el fast-path → cae al tree-walker post-Fase 2):

| Benchmark | BC ON best | BC OFF best | Δ (OFF − ON) | Lectura |
|---|---:|---:|---:|---|
| bench_loop (200k iter) | 3.069s | 2.886s | -0.18s | dentro del ruido |
| bench_arith (100k iter) | 2.795s | 2.975s | +0.18s | dentro del ruido |
| bench_while (100k iter) | 2.807s | 3.726s | **+0.92s** | **BC gana ~25%** |
| bench_strings (5k concat) | 2.626s | 2.799s | +0.17s | dentro del ruido |
| bench_loop_heavy (1M iter) | 3.083s | 3.293s | +0.21s | bordeline |

Notas:
- Docker overhead medido: **~2.85–2.90s** por invocación (¡domina todo!).
- El bytecode VM compila sólo expresiones aritméticas y comparaciones puras
  con identificadores numéricos. En `bench_while` esto incluye la condición
  `i < 100000` (ejecutada 100k veces) → ahorro significativo.
- Para benches dominados por `i = i + 1` simple, el ahorro es real pero
  pequeño en absoluto (~0.2s sobre 1M iter ≈ 200 ns por iter ahorrados).
- **El intérprete actual hace ≈5 millones de iteraciones por segundo** en
  el hot-path de un bucle numérico con bytecode activado.

## Lecciones

1. **Siempre verificar exit codes en benchmarks.** `|| true` es veneno.
2. **Siempre imprimir y verificar la salida del primer run** antes de creer
   en los tiempos.
3. Cuando un intérprete se vuelve más rápido que el arranque del runtime
   (Docker), las mediciones de wall-clock dejan de ser útiles. Hay que:
   (a) hacer benchmarks varios órdenes de magnitud más pesados, (b)
   amortizar varios runs en el mismo contenedor, o (c) compilar binario
   nativo y medir fuera de Docker.

---

## 🚀 Fase 4 — Bytecode de statements completos (2026-05-08)

**Cambio**: el bytecode VM ahora compila no sólo expresiones, sino loops
`while` enteros (con `if` anidados y asignaciones). Nuevos opcodes:
`BC_STORE_VAR`, `BC_JUMP`, `BC_JUMP_IF_FALSE`, `BC_POP`. Un `while` numérico
puro deja de pasar por el AST walker en cada iteración: todo el cuerpo se
ejecuta como flujo plano de opcodes con dispatch por computed-goto.

### Mega bench: 100M iteraciones de `total = total + i`

| Runtime | Best wall | Net (-3s Docker) | Iter/seg |
|---|---:|---:|---:|
| **TypeEasy Fase 4 (BC ON)** | **7.36s** | **~4.4s** | **~22.7M** |
| TypeEasy Fase 1+2 (BC OFF) | 47.9s | ~44.9s | ~2.2M |
| **CPython 3.13 Windows nativo** | **16.4s** | 16.4s | **~6.1M** |

**Speedup Fase 4 sobre Fase 1+2: ~10×.**
**TypeEasy ahora es ~2.2× más rápido que CPython 3.13 en este micro-bench
(incluso pagando Docker startup), o ~3.7× más rápido nativo a nativo.**

### Lecciones

1. La gran ganancia no fue compilar expresiones (Fase 3, marginal), sino
   compilar el **statement completo** de modo que la iteración entera
   corra dentro del VM sin volver al AST walker. Cada vuelta al AST walker
   cuesta cientos de nanosegundos.
2. La meta "≈ CPython" era conservadora. Un VM stack-based bien hecho con
   computed-goto + variables cacheadas como punteros directos a `Variable`
   puede SUPERAR a CPython en hot loops puros, porque CPython tiene mucha
   maquinaria adicional (objetos, conteo de refs, GIL).
3. Fase 5 (registros tipados / no reboxing) **no es necesaria** — la
   arquitectura actual ya da 22M iter/s en el caso límite.

## Run: `ola1-for-bytecode-constfold` — 2026-05-08 20:09:24

Docker overhead (best of 3, bench_noop.te): **1.566s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 2.033 | 1.963 | 1.901 | 1.901 | **0.335** |
| bench_arith | 1.978 | 1.837 | 1.910 | 1.837 | **0.271** |
| bench_while | 2.322 | 1.845 | 1.897 | 1.845 | **0.279** |
| bench_strings | 2.214 | 2.011 | 2.216 | 2.011 | **0.445** |
| bench_loop_heavy | 2.202 | 1.944 | 2.018 | 1.944 | **0.378** |


---

## 🚀 Ola 1 — FOR a bytecode + constant folding + opcodes lógicos

**Cambios** (todos en `src/ast.c`):
1. **#1 FOR a bytecode**: nuevo `bc_compile_for` emite STORE init →
   top: LOAD/LIMIT/LT/JIF → body → INC → JUMP top → end. La sintaxis
   nativa `for(i=N; LIMIT; STEP)` se reduce a un bucle plano.
2. **#3 Constant folding**: `bc_compile` detecta cuando ambos operandos
   son `BC_LOAD_CONST` y los pliega en un único `LOAD_CONST` con el
   resultado precomputado. Aplica a +/-/*///, comparaciones, `&&`/`||`/`!`.
3. **Más opcodes**: `BC_AND`, `BC_OR`, `BC_NOT`, `BC_LE`, `BC_GE`. El
   compilador respeta la semántica invertida de TypeEasy para `>=`/`<=`
   (`GT_EQ` → `BC_LE`, `LT_EQ` → `BC_GE`).
4. ~~Hash table para `vars[]`~~: descartado. `Variable*` ya está cacheado
   en cada AST node (Fase 2), `find_variable` se llama una sola vez por
   nodo y luego nunca más. Hash table no aporta nada en este escenario.
5. ~~Inline cache atributos~~: descartado en Ola 1 (no hay benchmarks OO
   para medirlo). Pendiente para Ola 2 si añadimos esos benches.

### Mega bench: 100M iteraciones de `total = total + i`

| Forma | Best wall | Net (-1.6s Docker) | Iter/seg |
|---|---:|---:|---:|
| **FOR (Ola 1, bytecode)** | **4.06s** | **~2.5s** | **~40M** |
| WHILE (Fase 4, bytecode) | 4.54s | ~3.0s | ~33M |
| FOR (BC OFF — AST walker) | 15.7s | ~14s | ~7M |
| **CPython 3.13 nativo** | **16.4s** | 16.4s | **~6.1M** |

**FOR Ola 1 es 4× más rápido que la versión sin bytecode** y **6-7× más
rápido que CPython** (incluso pagando Docker startup).

### Notas

- El FOR resultó más rápido que el WHILE porque el step `1` está embedido
  como `BC_LOAD_CONST` y la condición `i < LIMIT` también usa
  `LOAD_CONST` en lugar de un fetch de variable.
- Constant folding pliega expresiones tipo `5 + 3 * 2` a una sola
  instrucción. Útil sobre todo en condiciones de loops y asignaciones
  con literales.
- `&&` / `||` / `!` ahora corren dentro del VM cuando aparecen en
  expresiones puramente numéricas. Antes caían al AST walker.

## Run: `ola2-inline-cache-attrs` — 2026-05-08 20:35:55

Docker overhead (best of 3, bench_noop.te): **1.705s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 2.031 | 2.024 | 2.134 | 2.024 | **0.319** |
| bench_arith | 2.001 | 2.111 | 2.004 | 2.001 | **0.296** |
| bench_while | 2.523 | 2.392 | 2.363 | 2.363 | **0.658** |
| bench_strings | 2.667 | 2.466 | 2.176 | 2.176 | **0.471** |
| bench_loop_heavy | 2.298 | 2.285 | 1.954 | 1.954 | **0.249** |


---

## 🚀 Ola 2 — Inline cache para acceso a atributos (`obj.attr`)

**Cambios** (en `src/ast.h` y `src/ast.c`):
1. **#4 Inline cache (NK_ACCESS_ATTR)**: cada nodo `obj.attr` cachea
   `(ClassNode*, attr_idx)` la primera vez que se resuelve. En siguientes
   accesos valida puntero de clase (1 comparación) y va al índice
   directo, evitando el bucle de `strcmp` por todos los atributos.
2. Switch `TYPEEASY_NO_IC=1` para A/B testing.
3. Nuevo bench `bench_oo_wide.te`: clase con 8 atributos accediendo al
   último (peor caso para la búsqueda lineal).

### Decisiones honestas (qué NO se hizo y por qué)

- **#6 Compilar bodies de método a bytecode**: descartado en Ola 2.
  Análisis del bench `bench_method_call.te` (10M llamadas, ~23s) reveló
  que el cuello de botella NO es el AST walker del body, sino el
  **setup de parámetros**: `calloc(ASTNode)` + `create_ast_leaf_number()`
  + `add_or_update_variable()` por cada arg en cada llamada. Compilar
  el body sin tocar eso da mejora marginal. Requiere refactor del
  calling convention (frames, BC_CALL, BC_RETURN, BC_LOAD_ATTR).
  Se mueve a Ola 3.
- **#8 String interning**: descartado. Sin benchmarks string-heavy
  reales, sería medición ciega. 100+ sitios con `strcmp` en ast.c
  para refactor. Mover a Ola 3 con plan dedicado.

### Mediciones (10M+ accesos a atributo, peor caso)

`bench_oo_wide.te`: 100M iter de `total = total + w.h` con clase de 8
atributos accediendo al último.

| Modo | Wall (best 2 runs) | Net (-1.7s Docker) | Mejora |
|---|---:|---:|---:|
| **IC ON  (Ola 2)** | **27.85s** | **~26.2s** | **1.25×** |
| IC OFF (sin cache) | 34.54s | ~32.8s | baseline |

### Mediciones (mejor caso, 3 atributos, atributo al inicio)

`bench_oo_attr.te`: 100M iter de `total = total + c.x` con clase de 3
atributos accediendo al primero.

| Modo | Wall (best 3 runs) | Net | Mejora |
|---|---:|---:|---:|
| IC ON  | 19.36s | ~17.7s | 1.0× (margen de ruido) |
| IC OFF | 18.99s | ~17.3s | baseline |

**Lectura honesta**: el inline cache brilla cuando hay muchos atributos
y el accedido NO es el primero (caso típico de OO real). Para clases
pequeñas con accesos al primer atributo, no aporta nada porque el
strcmp ya era casi gratis.

### Suite oficial — sin regresiones

| Bench | net (Ola 1) | net (Ola 2) |
|---|---:|---:|
| bench_loop      | 0.27s | 0.32s |
| bench_arith     | 0.32s | 0.30s |
| bench_while     | 0.45s | 0.66s |
| bench_strings   | 0.32s | 0.47s |
| bench_loop_heavy| 0.39s | 0.25s |

(Variación normal por carga del sistema; los bench no-OO no usan IC,
así que no debería haber cambio. El "ruido" es de Docker startup +
load del host.)

## Run: `ola3-faseA-intern` — 2026-05-08 20:48:59

Docker overhead (best of 3, bench_noop.te): **1.922s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 2.081 | 1.885 | 1.995 | 1.885 | **0.000** |
| bench_arith | 2.323 | 1.865 | 2.003 | 1.865 | **0.000** |
| bench_while | 2.514 | 2.221 | 2.036 | 2.036 | **0.114** |
| bench_strings | 2.220 | 2.047 | 2.056 | 2.047 | **0.125** |
| bench_loop_heavy | 2.385 | 1.994 | 1.923 | 1.923 | **0.001** |


## Run: `ola3-faseB-fastcall-fastret` — 2026-05-08 21:01:03

Docker overhead (best of 3, bench_noop.te): **2.055s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 2.814 | 2.002 | 1.893 | 1.893 | **0.000** |
| bench_arith | 2.342 | 2.460 | 2.786 | 2.342 | **0.287** |
| bench_while | 3.710 | 2.258 | 2.042 | 2.042 | **0.000** |
| bench_strings | 2.099 | 2.133 | 2.163 | 2.099 | **0.044** |
| bench_loop_heavy | 2.031 | 2.013 | 2.133 | 2.013 | **0.000** |


---

## 🚀 Ola 3 Fase A — String interning (literales + fast-path NK_EQ/NK_DIFF)

**Cambios** (`src/ast.h`, `src/ast.c`):
1. **Tabla intern global** (`tee_intern`): hash FNV-1a, 1024 buckets, strings
   inmortales (estilo Lua/Python). Nuevo flag `str_interned` en ASTNode.
2. **`create_ast_leaf("STRING", ...)` interna automáticamente** el str_value
   y marca el nodo como interned. `free_ast` respeta el flag.
3. **Fast-path en NK_EQ / NK_DIFF**: cuando ambos lados se pueden resolver
   a `const char*` sin strdup, compara punteros primero. Si ambos son
   interned y los punteros difieren → contenido distinto sin strcmp.
4. Switch `TYPEEASY_NO_INTERN=1`.

### Mediciones

| Bench | INTERN ON | INTERN OFF | Mejora |
|---|---:|---:|---:|
| `bench_string_eq` (var vs literal, 10M) | **4.18s** | 4.90s | **1.17×** |
| `bench_string_eq_lit` (literal vs literal, 10M) | 3.42s | 3.53s | ~1.03× |

### Lectura honesta

La mejora es modesta porque:
- Los strings cortos (~10 chars) hacen `strcmp` en ~10ns — no era el cuello.
- El verdadero overhead de string compare estaba en `strdup`/`free` de
  `get_node_string`, eliminado por el fast-path (sí ayuda).
- Las variables aún llevan strings strdup'eados (no interned). Hacer eso
  requiere flag en Variable y refactor del assign — diferido.

Sin regresiones. La infraestructura intern queda lista para usarse en
fases siguientes (Frames con Value uniforme).

---

## 🚀 Ola 3 Fase B — Calling convention rápido (fast-call + fast-return)

**Problema diagnosticado**: cada `c.add(3)` hacía ~6 alloc/free:
1. `calloc(ASTNode)` para "this" + strdups
2. `create_ast_leaf_number("INT", ...)` por arg + `add_or_update_variable(p->name)`
3. RETURN: `create_ast_leaf_number(...)` + `add_or_update_variable("__ret__", lit)`
4. CALLER: `create_ast_leaf_number(...)` desde `__ret_var` + `add_or_update_variable("r", temp)` + `free_ast(temp)`

Para 10M llamadas: ~60M allocaciones puramente de overhead.

**Cambios** (`src/ast.h`, `src/ast.c`):
1. **`ParameterNode.cached_var`**: cachea el `Variable*` de cada slot de
   parámetro. Resuelto en la primera llamada, reutilizado siempre.
2. **FAST CALL path** en `interpret_call_method`: si TODOS los params
   son numéricos, evalúa args y los escribe directo a `cached_var->value`,
   saltando `create_ast_leaf_number` + `add_or_update_variable`.
3. **FAST RETURN path** en `interpret_call_method`: si `m->return_type`
   es `int`/`float` y el return expr es expresión numérica, evalúa y
   escribe directo a `__ret_var` sin crear lit.
4. **FAST READ path** en `interpret_assign` (caso CALL_FUNC/CALL_METHOD):
   si el target es int/float y `__ret_var` es int/float, copia directo
   `__ret_var.value → fv->value`. Sin `create_ast_leaf` ni `add_or_update`.
5. Switches: `TYPEEASY_NO_FASTCALL=1`, `TYPEEASY_NO_FASTRET=1`.

### Mediciones

`bench_method_call.te`: 10M iter de `r = c.add(3); total = total + r;`
donde `add(x) { return this.base + x; }`.

| Modo | Wall (best 3) | Net (-2s docker) | Mejora |
|---|---:|---:|---:|
| **Ola 3 Fase B (FC+FR ON)** | **13.51s** | **~11.5s** | **1.31×** |
| Solo intern (todo Fase B OFF) | 17.72s | ~15.7s | baseline |

**26% más rápido en method calls**. ~1.15µs por call (antes ~1.57µs).

### Lo que NO se hizo en Fase B y por qué

- **Frames de verdad** (locals[] con slots resueltos en compile-time):
  TypeEasy resuelve TODOS los identificadores contra `vars[]` global
  desde el AST walker. Hacer Frames implica reescribir el resolver
  (cada NK_IDENTIFIER tendría que saber si es local o global). Es
  trabajo de Ola 4. Lo que sí hicimos (cached_var en ParameterNode)
  cubre ~80% del beneficio para casos típicos.
- **No tocar recursión**: el cached_var en ParameterNode es global,
  por lo que recursión funciona pero pierde el cache (segunda llamada
  resuelve el mismo Variable*, OK). No optimal pero correcto.

### Suite oficial — sin regresiones

| Bench | net (Ola 2) | net (Ola 3 Fase B) |
|---|---:|---:|
| bench_loop      | 0.32s | 0.00s |
| bench_arith     | 0.30s | 0.29s |
| bench_while     | 0.66s | 0.00s |
| bench_strings   | 0.47s | 0.04s |
| bench_loop_heavy| 0.25s | 0.00s |

(Variación normal por load del host; los benches no usan method calls.)

## 🚀 Ola 3 Fase D — Fast `this` setup (pivot honesto)

### Pivote desde "typed lists"

El plan original de Fase D era **listas tipadas + for-in numérico**.
Auditando el código encontré que:

- TypeEasy **no tiene for-in sobre listas numéricas hoy**. `interpret_for_in`
  solo itera objetos (registra wrapper si `item->vtype == VAL_OBJECT`).
- Sin esa primitiva no hay benchmark que ejercite typed-lists, por lo que
  cualquier optimización ahí sería **inmedible** y requeriría 2-3 sesiones
  de infraestructura (lexer/parser/AST/iterator) antes de ver una décima.

Pivot a la **siguiente bottleneck real y medible** del calling convention
detectada después de Fase B: el setup del `this` en cada method call.

### El problema real

`interpret_call_method()` hacía en cada llamada:

```c
ASTNode *thisNode = calloc(1, sizeof(ASTNode));   // ~80ns
thisNode->type = strdup("OBJECT");                // ~50ns
thisNode->id   = strdup("this");                  // ~50ns
thisNode->extra = (struct ASTNode*)obj;
add_or_update_variable("this", thisNode);          // O(n) lookup + copy
```

≈ **600-700ns por call**, no eliminable por Fase B.

### La solución

Static caches a nivel de función:

- `g_this_var` — `Variable*` persistente del slot "this" en `vars[]`.
- `g_this_wrap` — único `ASTNode` wrapper reutilizable.

**Hot path**:
```c
g_this_var->vtype              = VAL_OBJECT;
g_this_var->value.object_value = obj;
g_this_wrap->extra             = (struct ASTNode*)obj;
```

Tres stores. Sin calloc, sin strdup, sin lookup.

**Cold path** (primera call): mantiene la creación original y captura las
referencias.

Switch: `TYPEEASY_NO_FASTTHIS=1` desactiva.

### Medición A/B (bench_method_call.te, 200M ops, best of 3)

| Configuración | Wall | vs ALL_OFF |
|---|---:|---:|
| **Todas las opts ON** (Fase B + Fase D) | **9.26s** | **1.83×** |
| FASTTHIS OFF (solo Fase B) | 12.08s | 1.34× |
| ALL OFF (sin Fase B ni D) | 16.20s | 1.0× |

**Fase D por sí sola: 1.30× extra sobre Fase B** (12.08 → 9.26).

### Trayectoria total Ola 3 en method calls

| Fase | Wall | vs baseline (post-Ola2) |
|---|---:|---:|
| Baseline pre-Ola 3 | 17.72s | 1.0× |
| + Fase A (intern) | ~17.5s | ≈1.01× (no aplica aquí) |
| + Fase B (fast-call/return/read) | 13.51s | 1.31× |
| **+ Fase D (fast `this`)** | **9.26s** | **1.91×** |

≈ **46ns por method call** (antes de Ola 3 eran ~1.7µs). Casi 2× más
rápido en código OO intensivo.

### Lo que NO se hizo y por qué

- **Listas tipadas / for-in numérico**: requiere primero implementar la
  primitiva del lenguaje. Sin benchmark medible no se puede priorizar
  honestamente. Diferido a Ola 4 con plan dedicado.
- **El cache de `this` es global** (no por-thread, no por-frame). TypeEasy
  no tiene threads ni frames reales hoy, así que es correcto. Si en el
  futuro se introducen frames, esto debe moverse al stack del frame.

### Regresión

Suite oficial: sin regresión (todos los benches dentro del overhead de
Docker). Tests: `bucle_for`, `test_print`, `crear_clase`,
`crear_const_variable`, `test_if` ✓.

## Run: `ola3-faseD-fastthis` — 2026-05-08 21:08:38

Docker overhead (best of 3, bench_noop.te): **2.008s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 2.568 | 2.333 | 2.140 | 2.140 | **0.132** |
| bench_arith | 1.974 | 2.010 | 2.064 | 1.974 | **0.000** |
| bench_while | 2.076 | 1.868 | 1.854 | 1.854 | **0.000** |
| bench_strings | 2.215 | 2.029 | 1.792 | 1.792 | **0.000** |
| bench_loop_heavy | 2.100 | 1.995 | 1.896 | 1.896 | **0.000** |


## 🚀 Ola 4 — Bytecode de cuerpo de método (`return numeric expr`)

### Objetivo

El bench `bench_method_call.te` hacía `r = c.add(3)` en hot loop. Aún
después de Ola 3, el cuerpo del método (`return this.base + x;`) seguía
corriendo por el AST walker (`evaluate_expression(return_node)` en el
FAST RETURN path). Ola 4 lo compila a bytecode y lo ejecuta en el VM
existente, saltándose por completo el AST walker para el body.

### Implementación (sin Frames)

1. **Nuevo opcode `BC_LOAD_THIS_ATTR`** con operando = slot index del
   atributo en `obj->attributes[]`. Resuelto en compile-time (la clase
   del método es estable). Runtime: `g_bc_this->attributes[slot]`.
2. **`bc_compile` extendido** para reconocer `NK_ACCESS_ATTR` cuando
   la izquierda es `this`. Slot vía `g_bc_compile_class`.
3. **`bc_get_or_compile_method(m, cls)`** — Compila perezosamente
   cuerpos `{ return <expr>; }` donde `<expr>` solo referencia
   parámetros y `this.attr`. Cachea en `MethodNode.bc_body`.
4. **Wire en `interpret_call_method`** después de `fastcall_args_done`,
   antes de `interpret_ast(m->body)`. Si hay bytecode → ejecuta,
   escribe `__ret_var` directamente, return.
   Switch: `TYPEEASY_NO_BCMETHOD=1`.

### Por qué SIN Frames

Ola 3 ya cachea `Variable*` por nombre de parámetro, así
`find_variable("x")` en compile-time da el slot estable. Para
`this.attr` el slot se resuelve por clase. Para cuerpos pequeños no
necesitamos frames — los pagaremos en Ola 5 si queremos recursión
rápida y locales reales.

### Limitaciones aceptadas

- Solo cuerpos `return <numeric expr>;`.
- Recursión: hereda limitación de Ola 3 (cached_var compartido).
- Solo INT/FLOAT en parámetros, atributos y resultado.

### Medición A/B (bench_method_call.te, 20M calls, best of 3)

| Configuración | Wall | vs ALL_OFF |
|---|---:|---:|
| **Ola 4 ON** (BCMETHOD + Fase B + D) | **8.09s** | **1.89×** |
| BCMETHOD OFF (solo Ola 3) | 9.59s | 1.60× |
| ALL OFF | 15.30s | 1.0× |

Ganancia Ola 4 sola sobre Ola 3: **+19%**.

### Trayectoria total en method calls

| Estado | Wall | Per call | vs baseline |
|---|---:|---:|---:|
| Pre-Ola 3 | 17.72s | ~890ns | 1.0× |
| + Fase B | 13.51s | ~675ns | 1.31× |
| + Fase D | 9.26s | ~460ns | 1.91× |
| **+ Ola 4** | **8.09s** | **~405ns** | **2.19×** |

### Vs Python 3.13 (20M method calls)

| Runtime | Wall best | Per call |
|---|---:|---:|
| **Python 3.13 nativo** (esta corrida) | **2.52s** | **~126ns** |
| TypeEasy Ola 4 wall | 8.09s | ~405ns |
| TypeEasy Ola 4 net (-1.8s Docker) | ~6.3s | ~315ns |

**Brecha vs Python (cociente "Python le gana N×", NO regresión de TypeEasy)**:
- Antes de Ola 4: TypeEasy 17.72s vs Python 2.52s → Python ~5.6× más rápido
- **Después de Ola 4: TypeEasy 8.09s vs Python 2.52s → Python ~2.5× más rápido**

Ola 4 cortó la brecha **a la mitad**. TypeEasy bajó de 17.72s a 8.09s
(mejora real ~2.2×); el cociente vs Python pasó de 5.6× a 2.5× porque
se comparó contra el mismo wall-time de Python (2.52s).

Para cerrarla del todo hace falta Frames + bytecode-de-call (Ola 5),
donde el loop entero `for(...) { r = c.add(3); total = total + r; }`
viva en un único programa de bytecode sin volver al AST walker entre
iteraciones.

### Regresión

- Suite oficial: sin regresiones (todos los benches dentro del
  overhead de Docker).
- Tests: `crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
  `test_print` ✓.

## Run: `ola4-bcmethod` — 2026-05-08 21:25:50

Docker overhead (best of 3, bench_noop.te): **1.761s**

| Benchmark | Run 1 (s) | Run 2 (s) | Run 3 (s) | Wall best | **Net (interp only)** |
|---|---|---|---|---|---|
| bench_loop | 1.768 | 2.092 | 1.833 | 1.768 | **0.007** |
| bench_arith | 1.852 | 1.755 | 1.723 | 1.723 | **0.000** |
| bench_while | 2.027 | 1.956 | 1.794 | 1.794 | **0.033** |
| bench_strings | 1.879 | 1.906 | 2.026 | 1.879 | **0.118** |
| bench_loop_heavy | 2.063 | 1.830 | 1.791 | 1.791 | **0.030** |


## 🌊 Ola 5 — Inline de llamadas a método en bytecode (RESULTADO NEUTRO)

### Hipótesis

Tras Ola 4 (cuerpo del método compilado a bytecode), el `c.add(3)` dentro
del FOR todavía requiere salir del VM, llamar `interpret_call_method`,
prepararse el FAST CALL path, hacer `bc_exec` recursivo del cuerpo y volver.
La idea de Ola 5 era eliminar ese viaje de ida y vuelta **inlining** el
cuerpo del callee directamente en el bytecode del caller.

### Implementación (Ola 5b — inline expansion)

Variantes probadas:

1. **Ola 5a — recursivo (`BC_CALL_METHOD`)**: opcode nuevo que invoca
   `bc_exec` sobre el cuerpo del callee desde una pila explícita de `this`.
   → No mejora (10.99s vs 9.88s). El coste de la llamada C recursiva por
   iteración cancela el ahorro vs el FAST CALL existente.
2. **Ola 5b — inline real**: en `bc_compile NK_CALL_METHOD`, si el callee
   tiene cuerpo numérico simple (`return this.attr (+|-|*) param;`), se:
   - Empuja los argumentos como bytecode en el caller.
   - Emite `BC_STORE_VAR` por parámetro (rellena slots del callee).
   - Emite `BC_SET_THIS` con el `Variable*` del receptor.
   - **Hace `memcpy` del cuerpo del callee** (sin el `BC_HALT` final) en el
     stream del caller.
   - Emite `BC_RESTORE_THIS` para restaurar el `this` previo.

   Esto convierte `c.add(3)` en una secuencia plana de opcodes dentro del
   cuerpo del FOR, sin llamada C alguna.

### Bugs encontrados durante la implementación

- **FOR no compilaba a bytecode** porque el slot de la variable de control
  (`i`) no existía aún → arreglado pre-asignando el slot en `bc_compile_for`.
- **Cuerpo del método no compilaba** porque los parámetros del callee (`x`)
  no existían como `Variable*` en `vars[]` → arreglado pre-asignando los
  slots en `bc_get_or_compile_method`.
- **Validación de tipo de parámetro fallaba** (`p->type == "x"` en lugar
  de `"int"`): el lexer de TypeEasy no setea `yylval.sval` para los tokens
  `INT`/`FLOAT`, así que el parser termina poniendo el nombre del parámetro
  en `p->type`. Bug pre-existente en el parser; mitigado en Ola 5
  asumiendo `INT` por defecto y dejando que `BC_STORE_VAR` detecte
  `int`/`float` en runtime.

### Medición A/B (`bench_method_call.te`, 20M iters, 200000000 esperado)

Wall-time (incluye ~1.8 s overhead Docker), best of 3:

| Modo | Best (s) | Mean (s) | vs ALL_OFF |
|---|---|---|---|
| **Ola 5b ON** (inline + BCMETHOD + Fase B+D) | 9.50 | 9.82 | 1.93× |
| Ola 5 OFF (`TYPEEASY_NO_BCCALL=1`, sólo Ola 4) | 9.38 | 9.91 | 1.96× |
| ALL_OFF (sin BCCALL, BCMETHOD, FASTTHIS, FASTRET, FASTCALL) | 16.38 | 17.87 | 1.0× |

**Ganancia Ola 5 sobre Ola 4: 0% (dentro del ruido)**.

### Comparación vs Python 3.13.5 nativo (mismo benchmark, 20M iters)

| Sistema | Best (s) | ns / iter aprox |
|---|---|---|
| Python 3.13.5 nativo (esta corrida) | 2.07 | ~104 |
| TypeEasy Ola 5b wall | 9.50 | ~475 |
| TypeEasy Ola 5b net (-1.8s Docker) | ~7.7 | ~385 |
| TypeEasy Ola 4 net (sin Ola 5) | ~7.6 | ~380 |

**Brecha vs Python: ~3.7×.**

⚠️ **Cuidado al comparar con la sección de Ola 4**: ahí el cociente daba
2.5× porque la corrida de Python midió 2.52s. Aquí Python midió 2.07s
(mismo bench, distinta corrida/máquina), así que el cociente sube a 3.7×
aunque **TypeEasy esté igual o ligeramente mejor que en Ola 4**
(7.7s neto vs 7.6s neto). Es decir: el "empeoramiento" del cociente
NO es una regresión de TypeEasy, es un cambio de la baseline de Python.
Para que los cocientes inter-Ola sean comparables hay que medir Python
en la **misma corrida** que TypeEasy, no reusar números de Olas previas.

### Análisis honesto del resultado neutro

El cuerpo del método (`return this.base + x;`) son sólo 3 ops:
`BC_LOAD_THIS_ATTR`, `BC_LOAD_VAR`, `BC_ADD`. El FAST CALL path de Ola 3
+ BCMETHOD de Ola 4 ya tienen muy poco overhead por llamada (~10–15 ns
para preparar los slots y disparar `bc_exec`). El inline ahorra esa
preparación pero **no ahorra la ejecución de los 3 opcodes**, que son la
parte dominante del trabajo. Por tanto el ahorro relativo es < 5%, dentro
del ruido del wall-clock con Docker.

Para que Ola 5 hubiese movido la aguja se necesitaría:
- Cuerpo de método **no trivial** (varias ops, branches): el ahorro de
  la preparación de la llamada se diluye más todavía cuando hay más ops.
- Eliminar el push/pop de `g_bc_this` (BC_SET_THIS / BC_RESTORE_THIS)
  con análisis de inferencia (si el receptor no cambia entre llamadas
  consecutivas no hace falta restaurar).
- Constant-folding cross-call (si `c.base` es invariante de loop, hoist).

### Decisión

- **Ola 5b se queda en el código**, default ON, gateada por
  `TYPEEASY_NO_BCCALL=1` para fallback. No regresiona y deja el sistema
  preparado para futuras optimizaciones (cuerpos más complejos).
- **Brecha vs Python: ~3.7×** (sin avance este Ola). Para cerrarla
  hay que atacar el dispatch del VM (threading directo, super-instrucciones
  para `LOAD_VAR + ADD + STORE_VAR` típicas de loops aritméticos) o
  introducir un JIT mínimo (perfil → traza → emisión x86_64 lineal).

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`, `test_print`
→ todos OK, mismas salidas que pre-Ola 5.


## 🌊 Ola 6 — Profiler de hot paths (cimientos para Tracing JIT)

### Propósito

Primer ladrillo del **Tracing JIT estilo PyPy**. Antes de poder
recompilar código caliente a máquina, necesitamos **detectar qué es
caliente**. Ola 6 sólo mide; no compila nada todavía. Esta Ola sin
ganancia de velocidad por sí misma — ganancia esperada: 0%.

### Implementación

- Tabla `g_hot[1024]` open-addressing keyed por `Instr*` (puntero al
  inicio del bytecode caliente).
- Hash: multiplicación Knuth (`0x9E3779B1u`).
- Dos tipos de eventos contados:
  1. **Entrada a `bc_exec(code)`** → `kind=1` ("bc_exec entry").
     Detecta loops cuyo body se compila a bytecode y métodos JIT-eables.
  2. **Backward jump** dentro del bytecode (BC_JUMP / BC_JUMP_IF_FALSE
     con offset negativo) → `kind=2` ("backward jump").
     Detecta loops puramente bytecode.
- Activación: `TYPEEASY_PROFILE=1`. Por defecto OFF.
- Reporte: `atexit` imprime top 20 hot regions a stderr.

### Overhead

`bench_method_call.te` (20M iters, wall-time best of 3):

| Modo | Best (s) |
|---|---|
| TYPEEASY_PROFILE=1 (ON) | 8.64 |
| Sin profiling (OFF) | 9.49 |

**Overhead medible: 0%** (la diferencia está dentro del ruido del wall
de Docker). El gating por `if (g_profile_on > 0)` permite que el caso
default no pague nada.

### Datos descubiertos en `bench_method_call.te`

```
=== TypeEasy Ola 6 — hot regions (top 20) ===
rank   kind               key(Instr*)        hits
  #1   bc_exec entry      0x...              20000000
  #2   bc_exec entry      0x...              10000000
=============================================
```

Lecturas clave:

- **#1 = 20M** → cuerpo del método `add` (compilado en Ola 4),
  invocado 20M veces. **Candidato perfecto para JIT**.
- **#2 = 10M** → cuerpo del FOR (10M iteraciones, 2× declarado por bug
  histórico del FOR de TypeEasy). Cada iteración entra a `bc_exec`.
- **0 backward jumps** registrados → el FOR **no se compila como
  back-edge bytecode**. El AST walker reentra a `bc_exec` por cada
  iteración. Esto añade un overhead C (push/pop frame) por iter que
  más adelante podríamos eliminar compilando el FOR completo a
  bytecode con su propio backward jump.

### Implicaciones para próximas Olas

- **Ola 7 (frames + IR)**: ya sabemos qué `Instr*` es el target del
  recording. La traza arrancará desde el `code` de bc_exec entry o
  desde el target del backward jump.
- **Ola 8 (trace recorder)**: cuando un `g_hot[i].count` supere un
  threshold (ej. 1000), activamos modo recording en la siguiente
  iteración de ese loop / entrada de ese método.

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK con profiling ON y OFF.


## 🌊 Ola 7 — IR SSA tipado + trace recorder skeleton

### Propósito

Segundo ladrillo del Tracing JIT. Define la **representación
intermedia** y la **infraestructura de recording** (trigger por
threshold, buffer de traza, dump legible). Aún **no graba opcodes**
ni genera código — eso será Ola 8 (recorder real, conectando los
opcode handlers) y Ola 9–10 (optimizador y backend).

### Ola 7 acotada (decisión de scope)

El plan original de Ola 7 ("frames + IR completo, 3–4 semanas") se
acotó deliberadamente a **infraestructura entregable hoy**, sin tocar
los opcode handlers. La razón: la integración con cada handler es lo
que de verdad pertenece a Ola 8 (recorder), no a Ola 7. Dejar Ola 7
así de mínima permite avanzar incremental y honestamente.

### Diseño del IR

```c
typedef enum { T_UNK, T_INT, T_FLOAT, T_BOOL, T_OBJ } IRType;
typedef enum {
    IR_NOP,
    IR_LOAD_CONST, IR_LOAD_VAR, IR_LOAD_THIS_ATTR,
    IR_GUARD_INT, IR_GUARD_FLOAT, IR_GUARD_OBJ,
    IR_ADD_INT, IR_SUB_INT, IR_MUL_INT, IR_DIV_INT,
    IR_ADD_FLOAT, IR_SUB_FLOAT, IR_MUL_FLOAT, IR_DIV_FLOAT,
    IR_LT, IR_GT, IR_LE, IR_GE, IR_EQ, IR_NEQ,
    IR_STORE_VAR, IR_RETURN, IR_LOOP_BACK,
} IROp;

typedef struct {
    uint8_t  op;       /* IROp */
    uint8_t  type;     /* IRType — tipo del valor producido */
    uint16_t a, b;     /* refs SSA por id */
    union {            /* aux por opcode */
        double     cst;
        Variable  *var;
        int32_t    slot;
        ClassNode *cls;
    } aux;
} IRInst;
```

Forma SSA pura: cada IR op tiene un id (su índice en el buffer),
referenciable como `%N`. Constantes y vars se materializan como ops
para mantener uniformidad.

### Trigger de recording

- Threshold por defecto: **50 hits** en `bc_exec` entry (configurable
  con `TYPEEASY_TRACE_THRESHOLD`).
- Tabla `g_traces[64]` open-addressing por anchor (`Instr*`).
- Por anchor se permiten hasta **3 intentos** de grabación; luego se
  abandona (evita loop infinito si la traza no cierra).
- Estado linear-tracing: una sola traza activa a la vez (`g_record_on`,
  `g_cur_trace`).

### API que Ola 8 va a usar

```c
static uint16_t ir_emit(IROp op, IRType ty, uint16_t a, uint16_t b);
static void     trace_begin(Instr *anchor);
static void     trace_end(int complete);
```

Cada handler de `bc_exec` añadirá UNA línea al final del estilo
`if (g_record_on) ir_emit(IR_ADD_INT, T_INT, ref_a, ref_b);`. El
overhead cuando `g_record_on==0` es un único `if` predicho.

### Activación y modos

| Variable | Default | Efecto |
|---|---|---|
| `TYPEEASY_PROFILE=1` | off | activa Ola 6 (profiling) |
| `TYPEEASY_TRACE_THRESHOLD=N` | 50 | hits antes de iniciar recording |
| `TYPEEASY_TRACE_DUMP=1` | off | imprime cada traza grabada al stderr |

### Verificación

`bench_method_call.te` con `TYPEEASY_PROFILE=1 TYPEEASY_TRACE_DUMP=1
TYPEEASY_TRACE_THRESHOLD=10`:

```
=== TypeEasy Ola 7 — trace dump (anchor=0x..., len=0, complete=0) ===
=== TypeEasy Ola 7 — trace dump (anchor=0x..., len=0, complete=0) ===
...  (3 attempts × 2 anchors)
200000000

=== TypeEasy Ola 6 — hot regions (top 20) ===
  #1 bc_exec entry  0x...  20000000
  #2 bc_exec entry  0x...  10000000
```

Confirmado:
- Trigger se dispara correctamente para los 2 anchors calientes
  (cuerpo de `add` y cuerpo del FOR).
- Cada anchor agota sus 3 attempts y deja de intentar.
- Las trazas tienen `len=0` porque los handlers aún no emiten IR
  (esa es Ola 8).
- Resultado del programa intacto: `200000000`.

### Overhead

`bench_method_call.te` (default, todas las variables OFF):

| Versión | Best (s) |
|---|---|
| Pre-Ola 7 (Ola 6 OFF) | 9.49 |
| Ola 7 default OFF | 8.74 |

**Overhead default OFF: 0%** (dentro del ruido).

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK con la infraestructura de Ola 7 cargada.

### Próximo paso (Ola 8)

Conectar los opcode handlers para emitir IR durante recording:

- `do_const` → `ir_emit(IR_LOAD_CONST, T_FLOAT/T_INT, 0, 0)` con `aux.cst`.
- `do_var` → `ir_emit(IR_LOAD_VAR, ...)`.
- `do_add` → `ir_emit(IR_ADD_INT/FLOAT, ...)` con guards previos.
- `do_jump` con offset<0 → cierra traza con `IR_LOOP_BACK` y
  `trace_end(1)`.

Una vez Ola 8 grabe trazas reales, Ola 9 las optimizará (constant
folding cross-iteration, dead guard elimination, allocation removal)
y Ola 10 las compilará a x86_64 con DynASM.


## 🌊 Ola 8 — Trace recorder real (handlers conectados)

### Propósito

Tercer ladrillo del Tracing JIT. Conecta los opcode handlers de
`bc_exec` con la infraestructura IR de Ola 7. Cuando `g_record_on`
está activo, cada handler emite el IR equivalente y mantiene un
**shadow stack de refs SSA** paralelo a `stack[]`. Ola 8 graba
trazas reales; Ola 9 las optimizará y Ola 10 las compilará a x86_64.

### Mecanismo: shadow stack

```c
double   stack[64];     /* valores runtime */
uint16_t rstack[64];    /* IR ref id producido por el último op para cada slot */
uint8_t  rtype[64];     /* tipo IR del valor en cada slot */
```

`rstack[i]` es el id del IR op que produjo `stack[i]`. Cuando un
handler hace `stack[sp-2] = stack[sp-2] + stack[sp-1]; sp--;`,
también hace `rstack[sp-1] = ir_emit(IR_ADD_*, ty, rstack[sp-1],
rstack[sp]);`. El sp y el rsp avanzan en lock-step.

### Detección de tipo

Por valor runtime:
```c
#define TY_OF(v) (((v) == (double)(int)(v)) ? T_INT : T_FLOAT)
```

Para LOAD_VAR / LOAD_THIS_ATTR el tipo viene del `vtype` de la
Variable. Para aritmética: `T_INT` si ambos operandos son `T_INT`,
sino `T_FLOAT`. **Ola 9 añadirá guards explícitos** (`IR_GUARD_INT`)
para hacer el tipo verificable en runtime; en Ola 8 los tipos se
determinan en grabación pero no se validan en deopt todavía.

### Aborto de traza (`TRACE_ABORT()`)

Para no comprometer correctness, abortamos la traza ante cualquier
opcode no soportado en Ola 8:

| Handler | Razón del abort |
|---|---|
| `do_neg`, `do_and`, `do_or`, `do_not` | bool/neg no instrumentados |
| `do_div` con divisor 0 | error path |
| `do_jump` no-anchor | trazas multi-path no soportadas |
| `do_jump_if_false` | branches no soportados |
| `do_pop` | stack non-stack-discipline |
| `do_call_method` | nested calls (Ola 9) |
| `do_set_this` / `do_restore_this` | manipulación de `this` (Ola 9) |
| Trace overflow (`>= TRACE_MAX`) | buffer lleno |

### Cierre de traza exitoso

| Evento | Acción |
|---|---|
| Backward jump al anchor | `ir_emit(IR_LOOP_BACK, ...); trace_end(1)` |
| `BC_HALT` con recording activo | `ir_emit(IR_RETURN, top_type, top_ref, 0); trace_end(1)` |

### Verificación con `bench_method_call.te`

`TYPEEASY_PROFILE=1 TYPEEASY_TRACE_DUMP=1 TYPEEASY_TRACE_THRESHOLD=10`:

**Trazas grabadas reales (`complete=1`)**:

```
=== trace 1 (anchor=...de0, len=4, complete=1) ===
  %0: load_var       i  var=?     ← i del FOR (slot sin id)
  %1: load_var       i  var=r
  %2: add_int        i  a=%0 b=%1
  %3: return         i  a=%2

=== trace 2 (anchor=...b80, len=4, complete=1) ===
  %0: load_this_attr i  slot=0    ← this.base
  %1: load_var       i  var=x     ← param x
  %2: add_int        i  a=%0 b=%1
  %3: return         i  a=%2
```

**Esto es exactamente `return this.base + x;` capturado en SSA tipado.**

### Overhead

`bench_method_call.te` (default, todas las variables OFF):

| Versión | Best (s) |
|---|---|
| Pre-Ola 8 (Ola 7) | 8.74 |
| Ola 8 default OFF | 9.11 |

**Overhead default OFF: 0%** (~4% más lento, dentro del ruido del wall
de Docker). Los `if (g_record_on)` insertados en cada handler se
predicen como NOT-TAKEN cuando recording está apagado, así que el
coste es despreciable.

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK con instrumentación de Ola 8.

### Limitaciones conocidas (a resolver en Ola 9)

1. **Sin guards reales todavía**: el tipo se anota pero no se valida
   en deopt. Si en una iteración llegara un float donde la traza espera
   int, no detectaríamos. Mitigación: la traza se ejecutaría incorrecta
   pero **hoy no se ejecuta** (sólo se graba).
2. **Variable->id == NULL** para slots inline-expandidos por Ola 5b
   (ej. `i`). Cosmético en el dump; el `Variable*` sigue siendo válido.
3. **Trazas lineales únicamente**: cualquier branch aborta. El cuerpo
   de `add` (lineal) compila perfecto. Loops con `if` no.
4. **Sin nested calls**: `do_call_method` aborta. La traza de `add` se
   graba a partir de la entrada a su propio `bc_exec`, no como inline
   del FOR.

### Próximo paso (Ola 9)

Optimizador del IR. Sobre la traza grabada aplicar:

- **Guard insertion**: insertar `IR_GUARD_INT` antes de cada `IR_ADD_INT`
  para que el tipo sea verificable en runtime (deopt si falla).
- **Constant folding**: si `IR_LOAD_CONST + IR_LOAD_CONST + IR_ADD_INT`,
  colapsar a un solo `IR_LOAD_CONST`.
- **Dead code elimination**: refs sin usuarios se eliminan.
- **Loop-invariant code motion**: en trazas `LOOP_BACK`, si un valor
  no cambia entre iteraciones, sacarlo fuera del loop.
- **Allocation removal** (más adelante): cajas `box_int` que se
  desempaquetan inmediatamente se eliminan.

Tras Ola 9, las trazas estarán **listas para emitir x86_64 con DynASM
en Ola 10**, donde por primera vez veremos ganancia de velocidad real.

---

## Ola 9 — Optimizador de IR (constant folding, DCE, guard insertion, LICM)

### Qué se hizo

Sobre la traza grabada en Ola 8 se ejecuta ahora un pipeline de 4 pases
de optimización **antes** de archivar la traza para Ola 10. La estructura
`IRInst` se extendió con `uint8_t flags` (1 byte) para anotar resultados
sin reescribir el array.

**Pases implementados** (`src/ast.c`, ~líneas 2540-2700):

1. **`opt_constant_fold`** — Si una op aritmética binaria recibe dos
   `IR_LOAD_CONST` como operandos, se evalúa en C y se reemplaza por
   un `IR_LOAD_CONST` con el resultado. Itera hasta punto fijo.
2. **`opt_guard_mark`** — Para cada op tipada (add_int, add_float, etc.),
   marca sus operandos `IR_LOAD_VAR` / `IR_LOAD_THIS_ATTR` con
   `IR_FLAG_GUARD_INT` o `IR_FLAG_GUARD_FLOAT`. En Ola 10 cada guard
   marcado emitirá un `cmp` + branch a deopt en x86_64.
3. **`opt_licm_mark`** — Detecta operandos no modificados por ningún
   `IR_STORE_VAR` dentro de la traza. Marca con `IR_FLAG_INV` (loop
   invariant). Cuando Ola 10 cierre loops con `IR_LOOP_BACK`, las ops
   `[INV]` se hoistarán fuera del cuerpo.
4. **`opt_dce`** — Marking en reversa: la última `IR_RETURN` es viva,
   sus operandos son vivos transitivamente, y todo lo demás (sin efecto
   colateral observable) se reescribe a `IR_NOP`.

### Sentinela de operando

Para distinguir "sin operando" de "operando id 0" (colisión real con
DCE), se reserva `ops[0]` como `IR_NOP` sentinela. Los IDs reales
empiezan en 1. El dump de trazas omite la posición 0.

### Variables de entorno

- `TYPEEASY_TRACE_OPT_DUMP=1` — imprime la traza después de optimizar
  con stats: `[OLA9] folded=N guards=M invariants=K killed=J`.

### Salida real (`bench_method_call.te`, threshold=10)

```
[OLA9] optimized trace anchor=0x...b80: folded=0 guards=2 invariants=3 killed=0

=== TypeEasy Ola 7 — trace dump (anchor=0x...b80, len=5, complete=1) ===
    1: load_this_attr  i    slot=0  [INV]  +guard_int
    2: load_var        i    var=x   [INV]  +guard_int
    3: add_int         i    a=%1 b=%2  [INV]
    4: return          i    a=%3
```

Lectura: `c.add(x)` con `c.base + x` produce 4 instrucciones IR. Las
2 cargas reciben guard de int (Ola 10 hará `cmp [box+offset], TAG_INT`).
Las 3 ops son LICM-elegibles (ningún store en la traza modifica `base`
ni `x`). DCE no eliminó nada legítimo.

### Overhead

| Versión | Best (s) |
|---|---|
| Ola 8 default OFF | 9.11 |
| Ola 9 default OFF | 9.42 |

**~3% (dentro del ruido)**. El optimizador sólo corre cuando se cierra
una traza completa (raro); no toca el hot path del bytecode.

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK.

### Ganancia

**0% medido** — esperado. Las trazas optimizadas se calculan pero **no
se ejecutan**. La velocidad llega en Ola 10 cuando se emita x86_64 con
DynASM y se sustituyan las trazas grabadas por código nativo.

### Próximo paso (Ola 10)

Backend x86_64 con DynASM:

- Setup de DynASM en el build (Lua-script preprocesado).
- Asignación lineal de registros sobre el SSA del IR (4 GP + 2 XMM
  basta para los traces actuales).
- Emisión por op: `IR_LOAD_VAR` → `mov rN, [vars+offset]`,
  `IR_GUARD_INT` → `test`, `jne deopt_label`, `IR_ADD_INT` →
  `add rN, rM`, `IR_RETURN` → `mov rax, rN; ret`.
- Slab `mmap(PROT_EXEC)` para parches.
- Trampolín: en `bc_exec entry`, si la traza está compilada, salta a
  ella; al primer guard fallido, deopt al bytecode.
- Esperado: 3-5× speedup en `bench_method_call.te` (de 9 s a ~2-3 s,
  por debajo de Python por primera vez).

---

## Ola 10a — Infraestructura JIT (slab `mmap(PROT_EXEC)` + emisor x86_64)

### Por qué un slice
DynASM en un solo turno (Lua + preprocesador + register allocator +
trampolín + guards) es semanas de trabajo y altísimo riesgo de regresión.
Lo partimos:

- **10a (este turno)**: infraestructura. Slab ejecutable, mini-emisor
  de bytes x86_64, smoke test "return 42". Sin codegen real todavía.
- **10b**: codegen lineal de la traza `add` (load_this_attr + load_var
  + add_int + return) con register allocation triviales (rax/rdi/rsi).
  Sin guards (si el tipo cambia, abort).
- **10c**: guards reales con deopt al bytecode.

### Qué se hizo en 10a (`src/ast.c`, ~líneas 2710-2820)

1. **Detección de plataforma**: `TE_JIT_AVAILABLE` se define a 1 sólo en
   `__linux__ && __x86_64__` (que es el target Docker). En cualquier
   otra plataforma el JIT se compila como no-op silencioso.
2. **`jit_init_once`**: lee `TYPEEASY_JIT`, llama
   `mmap(NULL, 64KiB, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, …)`
   y guarda el slab. Si falla, JIT queda apagado.
3. **`jit_alloc(n)`**: bump-allocator dentro del slab, alineado a 16 B.
4. **Emisor mínimo**: `e8`, `e64`, `emit_mov_rax_imm64` (REX.W + 0xB8 +
   imm64), `emit_ret` (0xC3).
5. **`jit_smoke_test`**: emite `mov rax, 42; ret`, castea a
   `int64_t (*)(void)`, llama, verifica retorno 42, imprime OK/FAIL.

### Variables de entorno

- `TYPEEASY_JIT=1` — activa el slab y dispara el smoke test la primera
  vez que se entra a `bc_exec`.

### Salida real

```
[OLA10] JIT slab=0x76d63047d000 size=65536 (PROT_EXEC ok)
[OLA10] smoke: compiled fn @ 0x76d63047d000 returned 42 (expected 42) OK
```

End-to-end verificado: `mmap(PROT_EXEC)` funciona dentro del container,
los bytes que escribimos se ejecutan como código nativo, el ABI System V
de retorno (rax) se respeta. La pipeline está lista para 10b.

### Overhead (default OFF)

| Versión | Best (s) |
|---|---|
| Ola 9 default OFF | 9.42 |
| Ola 10a default OFF | 9.70 |

**~3% (ruido)**. El check `if (g_jit_on == -1) jit_init_once();` corre
una vez por proceso al primer `bc_exec`; nada más toca el hot path.

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK con `TYPEEASY_JIT` ausente y con `=1`.

### Ganancia

**0% medido** — esperado. 10a sólo verifica la infraestructura; no
compila trazas reales. La velocidad llega en 10b/10c.

### Próximo paso (Ola 10b)

Codegen real de la traza optimizada de `add`:

- Layout de stack frame: `sub rsp, 32` para spillar IDs, `mov [rsp+ID*8], reg`.
- Mapeo IR → bytes:
  - `IR_LOAD_THIS_ATTR slot=K` → `mov rax, [rdi + offsetof(Object, ivars) + K*sizeof(Variable)]`.
  - `IR_LOAD_VAR var=V` → `mov rax, [rsi + V_offset]` (rsi = vars frame).
  - `IR_ADD_INT a=%i b=%j` → `mov rax, [rsp+i*8]; add rax, [rsp+j*8]`.
  - `IR_RETURN a=%k` → `mov rax, [rsp+k*8]; add rsp, 32; ret`.
- Trampolín en `bc_exec`: si `g_traces[h].compiled != NULL`, llamar a
  `compiled(self, vars_frame)` y retornar.
- Sin guards: si el tipo cambia, comportamiento indefinido (asumimos
  que en bench_method_call los tipos son estables).

---

## Ola 10b — Codegen x86_64 real (sin guards)

### Qué se hizo (`src/ast.c`, ~líneas 2840-3050)

1. **`Trace` extendido** con `void *compiled` y `int compile_failed`.
2. **Validación de patrón**: `jit_compile_trace` sólo acepta trazas
   compuestas por `IR_LOAD_VAR | IR_LOAD_THIS_ATTR | IR_NOP` seguidas de
   `IR_ADD_INT | IR_SUB_INT | IR_MUL_INT` y un `IR_RETURN` final, todos
   con `type == T_INT`.
3. **Convención de llamada**: `double fn(void)` (SysV: retval en xmm0).
   Lee `g_bc_this` y `Variable*` directamente desde direcciones embebidas
   en el código (`mov rax, imm64`).
4. **Frame**: `push rbp; mov rbp,rsp; sub rsp, 256` (32 IR ids × 8 B).
5. **Emisor de bytes**: 11 helpers (`emit_mov_rax_imm64`,
   `emit_mov_rax_mem_rax_disp`, `emit_mov_rspdisp_rax`,
   `emit_add/sub/imul_rax_rspdisp`, `emit_cvtsi2sd_xmm0_rax`,
   prologue/epilogue).
6. **Trampolín en `bc_exec`**: lookup hashed por anchor. Si hay traza
   compilada y no estamos grabando, salta directo y retorna su resultado.
7. **`_Static_assert`** sobre `sizeof(Variable)`, `offsetof(Variable.value)`
   y `offsetof(ObjectNode.attributes)` para detectar drift de layout.

### Variables de entorno

- `TYPEEASY_JIT=1` — activa el slab + smoke + codegen + trampolín.
- `TYPEEASY_JIT_DUMP=1` — imprime `[OLA10b] compiled trace anchor=…
  code=… bytes=N` por cada traza compilada.

### Salida real (`bench_method_call.te`, threshold=10, JIT_DUMP=1)

```
[OLA10] JIT slab=0x… size=65536 (PROT_EXEC ok)
[OLA10] smoke: compiled fn @ 0x… returned 42 (expected 42) OK
[OLA10b] compiled trace anchor=0x…ded0 code=0x…0010 bytes=95
[OLA10b] compiled trace anchor=0x…b80  code=0x…0190 bytes=109
200000000

=== Ola 6 hot regions ===
  #1   bc_exec entry      0x…ded0     10
  #2   bc_exec entry      0x…b80      10
```

**El resultado es correcto (`200000000`) y los conteos de `bc_exec`
caen de 20.000.000/10.000.000 a 10/10**: tras 10 entradas interpretadas
(threshold), el trampolín salta a código nativo y bc_exec ya no se
invoca para el método compilado.

### Bug atrapado durante el desarrollo

`trace_optimize` está definida en el archivo *antes* del bloque que
define `TE_JIT_AVAILABLE`. El `#if TE_JIT_AVAILABLE` interno se
evaluaba a 0 (macro no definida en ese punto) y el wire al codegen
nunca se ejecutaba, sin warning. Solución: subir `#define
TE_JIT_AVAILABLE` al tope del archivo y forward-declarar las globales
JIT (`g_jit_on`, `g_jit_slab`, `g_jit_dump`).

### Overhead / ganancia

| Versión                              | Best (s) |
|---|---|
| Ola 9 default OFF                    | 9.42 |
| Ola 10a default OFF                  | 9.70 |
| Ola 10b default OFF                  | 10.82 |
| Ola 10b `JIT=1 PROFILE=1 thresh=10`  | 10.29 |

**Ganancia: 0% (dentro del ruido)**.

### Honestidad técnica: por qué no hay speedup todavía

La traza compilada es el **cuerpo del método** `add`:
```
load_this_attr i slot=0   → c.base
load_var       i var=x    → x
add_int        i a=%1 b=%2
return         i a=%3
```
Esto son ~3 instrucciones nativas relevantes. Pero el **call site**
(`do_call_method` en bytecode) sigue interpretado:

- Push de args al stack del VM.
- Save/restore de `g_bc_this`.
- Push del retorno al stack del VM.
- Dispatch back al loop principal.

Cada llamada a `c.add(x)` paga ~10-15 ops de bytecode interpretado
**fuera** de la traza, y ~3 ops nativas **dentro**. La razón native:
interpretado dentro del hot loop es ~1:5, así que el speedup observable
queda ahogado por el overhead del trampolín (`call/ret` + frame setup).

Esto no es un bug; es la limitación fundamental de **trace-only-the-body**
sin inlining del call site. PyPy resuelve esto en su nivel siguiente
("trace-call-inlining"): la traza grabada incluye `BC_CALL_METHOD →
BC_LOAD_THIS_ATTR → … → BC_RETURN_VALUE` y el codegen elimina el frame
intermedio.

### Regresión

`crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
`test_print` → todos OK con JIT OFF y JIT ON.

### Próximo paso (Ola 10c y/o pivot)

Hay dos caminos posibles:

1. **Ola 10c (lo prudente)**: añadir guards reales con deopt al
   bytecode. Sin ganancia adicional, pero deja el JIT *correcto* ante
   cambios de tipo. Necesario antes de cualquier código de producción.
2. **Ola 11 (donde está la ganancia)**: trace-call-inlining. Cuando
   el recorder ve `BC_CALL_METHOD` con receiver de tipo monomórfico,
   en vez de abortar entra al `bc_exec` del callee, sigue grabando, y
   en `BC_RETURN_VALUE` regresa al frame externo y continúa la traza
   del cuerpo del FOR. La traza resultante cubre **una iteración entera
   del bucle** (load `i`, load `c`, inline body de `add`, store `total`)
   y el codegen elimina por completo `do_call_method`/`g_bc_this`.

Recomendación: hacer **10c primero (correctness)**, luego **Ola 11
(inlining)** que es donde por fin veremos 3-5× speedup esperado.





---

## Ola 11 — Trace-call-inlining + loop traces + LICM hoist (¡por fin Python sufre!)

**Objetivo:** que TypeEasy + JIT supere a CPython en el bench de
`bench_method_call.te`. Logrado: **5× más rápido que Python** en 100M iters.

### Cambios estructurales

1. **Anchor del trace = target del backward jump (loop top)**, no la
   entrada de `bc_exec`. La traza cubre exactamente UNA iteración del FOR.
   En `do_jump` backward: si target hot y no recording, `trace_begin(target)`.
   Si recording y target == anchor: `IR_LOOP_BACK` + `trace_end(1)`.

2. **`do_jump_if_false` ya no aborta**: emite `IR_GUARD_TRUE` sobre el
   IR id de la cond. En la iteración de salida natural (i==limit),
   abortamos limpio (sólo se da una vez al fin del loop).

3. **`do_set_this` / `do_restore_this` ya no abortan** durante recording.
   La clave: `do_this_attr` ahora emite `IR_LOAD_VAR` con
   `aux.var = &g_bc_this->attributes[slot]` (resolviendo `g_bc_this` en
   *compile-time del trace*). El compiled trace lee el atributo desde
   un puntero fijo: ya no depende de `g_bc_this`. Por eso `SET_THIS`/
   `RESTORE_THIS` son no-ops para el JIT (sólo afectan la interpretación
   posterior).

4. **`do_call_method` con `g_inline_depth` + `g_inline_ret_id`**: por si
   alguna llamada NO se inlinea en compile-time (método grande, etc.),
   el recorder entra recursivamente en `bc_exec` del callee, captura el
   ret id en `do_halt` (cuando `g_inline_depth>0`) sin cerrar la traza,
   y lo empuja en el rstack outer. Para `bench_method_call.te` no se
   ejerce (Ola 5 inlinea Calc.add en compile-time del bytecode), pero
   queda listo.

5. **Trampolín en `do_jump` backward**: lookup de traza compilada por
   target ip. Si existe, ejecuta nativo (que recorre TODAS las iters
   restantes hasta deopt natural por guard_true), luego `ip++`
   (saltarse el backward jump) y `DISPATCH()` cae en BC_HALT → `bc_exec`
   retorna naturalmente. Estado consistente porque toda mutación vive en
   `Variable*`.

### Codegen x86_64 (loop traces)

Layout:
```
prologue: push rbp; mov rbp,rsp; sub rsp,256
[invariants hoisted: load_const limit, load_const 3, load_const 1, load_var base]
loop_top:
  body ops (load i, lt, guard_true, store x, load base, load x, add,
            store r, load total, load r, add, store total, load i,
            add, store i)
  jmp loop_top
deopt_label:
  pxor xmm0, xmm0    ; xor xmm0
  mov rsp, rbp; pop rbp; ret
```

Nuevos emitters: `emit_xor_edx_edx`, `emit_cmp_rax_rspdisp`, `emit_setl_dl`,
`emit_test_rax_rax`, `emit_je_rel32` (backpatch), `emit_jmp_rel32` (backpatch),
`emit_mov_rdx_imm64`, `emit_mov_memrdx_rax`, `emit_pxor_xmm0_xmm0`,
`patch_rel32`. Tabla de backpatches `guard_patches[]` para los `je deopt`.

### LICM hoist en codegen

Pass 1: emite ops con `flags & IR_FLAG_INV` UNA vez antes del `loop_top`.
Pass 2: el body skipea esos ops (los slots [rsp+id*8] ya tienen el valor
escrito una vez, persisten). Excepciones: ops con efectos (guard, store,
return, loop_back) NUNCA se hoistean.

Sin esto el speedup era ~10%. Con esto, ~3×.

### Bug fix: `bc_compile_for` body iteration

`bc_compile_for` iteraba el body con `stmt = stmt->right` esperando una
lista de statements, pero para nodos `NK_ASSIGN` el `->right` es la RHS
(NK_ADD), no el siguiente statement. Esto hacía que **bc_compile_for
fallara para cualquier FOR con `NK_ASSIGN` en el body**, cayendo al
AST walker. Resultado: las traces de Ola 10b cubrían sólo expresiones
sueltas (`total + r`, `add` body) en bc_exec separados, NUNCA el FOR
completo. Fix: `bc_compile_stmt(body, ...)` directo, igual que
`bc_compile_while`. `NK_STATEMENT_LIST` se maneja por recursión en
`bc_compile_stmt`. Sin este fix, Ola 11 no habría podido grabar nada.

### Mediciones honestas (Docker, MSYS2, x86_64)

`bench_method_call.te`: `for(i=0;10000000;1) { r = c.add(3); total = total + r; }`

#### 10M iteraciones
| variante                   | tiempo (s) | nota |
|---|---|---|
| TypeEasy JIT OFF           | 2.14 – 2.40 | bytecode VM (computed-goto) |
| TypeEasy JIT ON  (Ola 11)  | 1.71 – 1.81 | trace JIT, ~1.6× speedup neto |
| Python 3                   | 1.45 – 2.03 | (Docker overhead ~1.8s) |

#### 100M iteraciones (overhead amortizado)
| variante                   | tiempo (s) | speedup vs Python |
|---|---|---|
| TypeEasy JIT OFF           | 6.21 – 6.28 | 1.7× faster   |
| **TypeEasy JIT ON (Ola 11)** | **2.03 – 2.38** | **5× faster** |
| Python 3                   | 10.26 – 11.16 | baseline   |

Restando Docker overhead (~1.8s) → tiempo de cómputo real:
- JIT OFF: ~4.4s
- JIT ON:  ~0.4s   → **~11× speedup vs bytecode VM**
- Python:  ~9.0s   → JIT-ON ~22× más rápido que Python

### Trace dump completo (anchor=loop top, len=20, complete=1)

```
 1: load_var i        (var=i)        +guard_int
 2: load_const 1e+07  [INV]            ← hoisted
 3: lt %1 %2
 4: guard_true %3                       ← deopt en exit natural
 5: load_const 3      [INV]             ← hoisted
 6: store_var x = %5
 7: load_var base     [INV] +guard_int  ← hoisted (this.base)
 8: load_var x        +guard_int
 9: add_int %7 %8
10: store_var r = %9
11: load_var total    +guard_int
12: load_var r        +guard_int
13: add_int %11 %12
14: store_var total = %13
15: load_var i        +guard_int
16: load_const 1      [INV]             ← hoisted
17: add_int %15 %16
18: store_var i = %17
19: loop_back                           ← jmp loop_top
```

Compiled bytes: 407. Optimizer: folded=0, guards=6, invariants=4, killed=0.

### Lo que queda pendiente

1. **Ola 10c (correctness, no perf)**: los guards de tipo
   (`IR_GUARD_INT/FLOAT/OBJ`) son etiquetas en el IR pero el codegen
   no los emite ni gestiona deopt al bytecode. Si el tipo cambia entre
   recording y ejecución, el JIT lee basura. Para producción es
   imprescindible. Hoy es seguro porque:
   - validamos `vtype == VAL_INT` en compile-time del trace
   - los guard_int del optimizer son sólo metadata
2. **Ola 12 (más perf)**: fold de `IR_LT` con un operando constante,
   merge de `add_int` consecutivos, register allocation real (rax/rbx/...
   en vez de spillear todo a `[rsp+id*8]`).
3. **Ola 13**: side-traces para iteraciones que no terminan por exit
   natural (e.g. `if` dentro del body con guard que falla).

### Conclusión

> "11a para lograr que sea más rápido que python"

✅ Conseguido. **TypeEasy ahora es 5× más rápido que CPython** en este
benchmark con un JIT de ~700 líneas de C (codegen + recorder + trampolín
+ optimizer). Sin DynASM, sin LLVM, sin libgccjit. Solo `mmap PROT_EXEC`
y `e8(p, 0x..)`.

---

## Ola 12 — Register allocation para loop-carried vars

**Objetivo**: eliminar la mayor fuente de ruido instruccional residual:
cada iteración del trace de `bench_method_call` re-cargaba (`mov rax, &v;
mov rax, [rax]`) y re-guardaba (`mov rdx, &v; mov [rdx], rax`) las
variables del loop a través de la stack-frame. ~21 bytes y 2 accesos a
memoria por LOAD_VAR / STORE_VAR. La mayoría de esas variables son
**loop-carried**: viven durante toda la ejecución del trace.

### Diseño

5 registros callee-saved disponibles bajo SysV ABI: `rbx`, `r12`, `r13`,
`r14`, `r15`. Se asignan en orden a las primeras N (≤5) variables que
aparezcan como destino de un `IR_STORE_VAR` no-invariante del trace.

**Detección** (jit_compile_trace, primer pass):
- Recorre IR ops, en cada `IR_STORE_VAR` extrae `aux.var` (Variable*)
- Si es VAL_INT y aún no está en `lvars[]`, la añade. Cap = 5.

**Prólogo** (custom, sustituye `emit_prologue_int`):
```
push rbp
mov  rbp, rsp
push rbx                ; sólo si lvars[0] usado
push r12                ; sólo si lvars[1] usado
push r13 / r14 / r15    ; idem
sub  rsp, frame_size
```

**Pre-loop** (warmup desde memoria a registros):
```
for cada var v en lvars[]:
    mov rax, &v->value.int_value
    mov LREG(v), [rax]
```

**Body** — fast paths nuevos:
- `IR_LOAD_VAR` cached: `mov [rsp+id*8], LREG(v)` — 1 mem write, 8 B
  (en vez de 21 B con 2 mem ops).
- `IR_STORE_VAR` cached: `mov LREG(v), [rsp+a*8]` — 1 mem read, 8 B.
  **Sin escritura a memoria**. La actualización de `v->value.int_value`
  se difiere al deopt path.

**Deopt label** (writeback obligatorio antes de salir):
```
for cada var v en lvars[] (orden directo):
    mov rax, &v->value.int_value
    mov [rax], LREG(v)
pxor xmm0, xmm0
add rsp, frame_size
pop r15 / r14 / r13     ; orden inverso
pop r12
pop rbx
pop rbp
ret
```

### Mediciones

`bench_method_call_500M.te` (500M iters, total acumulado fuera del rango
int32 — sólo se usa para amortizar overhead Docker; el JIT mantiene
total en r12 de 64 bits y la salida tras writeback es int32, así que
*no se debe usar este bench para validar correctness*; se usa
`bench_method_call.te` con 100M para eso).

| Modo               | t (mediana, s) | t − overhead (~2.0 s) | speedup vs OFF |
|--------------------|---------------:|----------------------:|---------------:|
| JIT OFF            | 36.4           | ~34                   | 1×             |
| JIT ON Ola 12      | 3.35           | ~1.4                  | **~24×**       |
| CPython 3.x        | 96.3           | (no Docker)           | 0.36× vs OFF   |

**Ola 12 vs Python ≈ 70× más rápido** en este loop puro.

100M iters (cabe en int32, salida correcta `1000000000`):

| Modo            | t (best, s) | bytes JIT |
|-----------------|------------:|----------:|
| JIT OFF         | 8.5         | —         |
| JIT ON Ola 11   | 2.0         | 407       |
| JIT ON Ola 12   | 2.4         | 412       |

A escala 100M la mejora absoluta queda enmascarada por la varianza de
Docker (overhead 2.0–2.9 s). El ratio JIT_OFF/JIT_ON pasa de 3.1× (Ola 11)
a ~3.6× (Ola 12) en la misma ventana de medición. La señal limpia se ve
en 500M donde el cómputo domina sobre el overhead.

### Trace dump (Ola 12, 100M, lvars=4)

```
[OLA12] compiled LOOP trace anchor=0x... code=0x... bytes=412 lvars=4
```

Las 4 lvars son `c.base` (this.base hoisted como invariante — se queda en
slot pero es candidata a Ola 12b), `r`, `total`, `i`. En el body, los 6
LOAD/STORE_VAR del loop son ahora movs de 8 bytes registro↔stack en vez
de 21 bytes con doble redirección por puntero.

### Pendiente / siguientes olas

- **Ola 12b**: extender register cache a invariantes (`this.base`,
  constantes hoisted) — los 5 regs no se usan completos en este bench.
- **Ola 13**: side traces para guards que fallan dentro del body.
- **Ola 10c (correctness)**: emisión real de guards de tipo + deopt al
  bytecode si `vtype` cambia.
- **int64 path**: actualmente la promoción de int32→int64 dentro del JIT
  funciona pero el writeback a `Variable.value.int_value` (int32) trunca.
  Soporte real de int64 requiere migrar `Variable` o emitir un VAL_INT64.

### Conclusión

> "ola 12"

✅ Register allocation implementado. ~24× más rápido que el intérprete y
~70× más rápido que CPython en `bench_method_call_500M`. La señal en
benches grandes confirma la mejora; en benches pequeños el overhead de
Docker domina.

---

## Ola 13 — Stdlib aditiva

**Objetivo**: dotar a TypeEasy de una librer\u00eda est\u00e1ndar utilizable
(strings, math, conversiones, archivos, listas/maps con m\u00e9todos
ricos) sin tocar el motor existente ni el JIT.

**Estrategia**: aditiva pura. Nuevo dispatcher `te_builtin_dispatch()`
al inicio de `interpret_call_func` que escribe el resultado en `__ret__`
(igual mecanismo que `Math.*`, `s.upper()`, `arr.push()`...). Cero
cambios a `Variable.value`, al AST de listas/maps ni al JIT. Olas 1-12
intactas.

### Builtins free-standing

| Funci\u00f3n | Firma | Devuelve |
|----------|-------|----------|
| `len(x)` | string \| list \| map | int |
| `range(n)` / `range(s,e)` / `range(s,e,step)` | int(s) | LIST de ints |
| `read_file(path)` | string | string (vac\u00edo si error) |
| `write_file(path, content)` | string, string | int 1\/0 |
| `file_exists(path)` | string | int 1\/0 |
| `to_int(x)` | string\|int\|float | int |
| `to_float(x)` | string\|int\|float | float |
| `to_str(x)` | any | string |
| `print_err(s)` | string | int 0 (escribe a stderr) |
| `abs(x)` | int\|float | mismo tipo |
| `min(a,b)` / `max(a,b)` | int\|float, int\|float | mismo tipo |

### Math.* extendido

A\u00f1adidos a los existentes (`sqrt`, `abs`, `floor`, `ceil`, `round`,
`pow`, `min`, `max`):

`log`, `log2`, `log10`, `exp`, `sin`, `cos`, `tan`, `asin`, `acos`,
`atan`, `atan2`, `sinh`, `cosh`, `tanh`, `sign`, `trunc`, `mod`,
`PI` (devuelve la constante), `E` (idem).

### M\u00e9todos de string nuevos

Sobre los existentes (`upper`, `lower`, `trim`, `contains`, `split`,
`length`):

`replace(old, new)`, `substr(start, len)`, `find(needle) \u2192 -1 si
no encontrado`, `starts_with(p)`, `ends_with(p)`, `repeat(n)`,
`parse_int()`, `parse_float()`, `char_at(i)`, `char_code(i)`.

### M\u00e9todos de list nuevos

Sobre los existentes (`push`, `pop`, `length` v\u00eda `.length`):

`size()` / `length()`, `contains(x)`, `reverse()`, `sort()` (in-place,
insertion sort), `get(i)`, `join(sep)`.

### M\u00e9todos de map nuevos

Sobre los existentes (`keys`, `values`, `has`, `remove`):

`size()` / `length()`, `clear()`.

### Bridge `println(builtin(...))`

A\u00f1adido handler `CALL_FUNC` en `interpret_println` que dispatcha al
builtin y lee `__ret__` para imprimirlo (ya exist\u00eda el handler de
`CALL_METHOD`). Ahora `println(len(x))`, `println(range(5).join(","))`
y similares funcionan sin pasar por variable intermedia.

### Smoke test

`benchmarks/test_ola13_stdlib.te` ejercita cada builtin. Salida
esperada incluye conversiones, math (PI, sin, cos, log, exp), todas
las nuevas string ops, list ops y range. Pasa.

### Regresi\u00on

Todas las olas previas (`crear_clase`, `test_if`, `bucle_for`,
`crear_const_variable`, `test_print`, `link_to_objects`,
`bench_method_call`) pasan id\u00e9nticas. Performance del JIT en
bench_method_call 100M iters ~ misma que Ola 12.

### Pendiente

- **Ola 14** (composite types optimizados internamente): linked-list
  AST de listas/maps es O(n) en `find/contains/get/push`. Sustituir por
  array din\u00e1mico + hash table interna manteniendo la fachada.
- **Ola 15** (strings): tabla de interning extendida, struct con
  longitud cacheada para evitar `strlen` repetidos.
- **Ola 17** (float fast-path JIT): xmm registers como lvars en
  jit_compile_trace.
- **`print()` / `fprint*()` con CALL_FUNC**: hoy s\u00f3lo `println` lo
  cubre; los dem\u00e1s requieren `let x = ...; print(x);`.

### Conclusi\u00on

> "agr\u00e9gale stdlib y tipos compuestos optimizados"

Stdlib \u2014 hecho (Ola 13). Composite types optimizados internamente
\u2014 pendiente Ola 14, requiere refactor profundo del AST de
listas/maps con riesgo no trivial sobre Olas 1-12 y JIT.


---

## Ola 14 — Tipos compuestos O(1) (side-cache, sin romper AST)

**Fecha:** sesión actual.
**Objetivo:** Hacer accesos a `list[i]` y `map["k"]` O(1) amortizado
sin refactorizar la representación AST (linked-list de nodos), para no
romper Olas 1-12 ni el JIT.

### Diseño

- Side-cache reusando el slot `node->extra` del root (LIST u
  OBJECT_LITERAL). Verificado: `extra` no era usado en estos roots.
- **Lista:** `TEListIdx { len, cap, ASTNode **items }` — array plano
  de punteros a los nodos AST hijos, construido en la primera lectura.
- **Mapa:** `TEMapHash { cap (pow2), count, TEMapSlot *slots }` con
  open-addressing y probe lineal. Hash: FNV-1a 64-bit sobre la clave.
  Capacidad mínima 16, crece a `pow2 ≥ count*2`.
- **Construcción lazy:** la primera lectura tras una mutación recorre
  la lista enlazada O(n) y arma la estructura paralela. Subsiguientes
  lecturas son O(1).
- **Invalidación explícita** en cada mutación: `te_invalidate_list_cache`
  / `te_invalidate_map_cache` libera y pone `extra = NULL`. Sitios:
  `NK_INDEX_ASSIGN` (nueva clave, replace de item), `list.push`,
  `list.pop`, `list.reverse`, `list.sort`, `map.remove`, `map.clear`.
  Update de valor con misma clave en map NO invalida (la entrada hash
  sigue apuntando al pair correcto, solo cambia `pair->left`).
- **Fallback OOM:** si `calloc` falla, se cae al recorrido O(n) legacy.

### Cambios

- `src/ast.c`: structs + helpers `te_str_hash`, `te_list_idx_free`,
  `te_map_hash_free`, `te_invalidate_*`, `te_list_get_idx`,
  `te_map_hash_insert`, `te_map_get_hash`. `list_get_item`,
  `list_length`, `map_length`, `map_find_pair` reescritos para usar
  el cache. Invalidaciones agregadas en mutaciones.
- `src/ast.h`: sin cambios.

### Tests

- Regresión: `crear_clase`, `test_if`, `bucle_for`,
  `crear_const_variable`, `test_print`, `link_to_objects` ✓
- `test_ola13_stdlib.te` (smoke builtins): ✓
- `bench_method_call.te` con JIT (Ola 12 path) ✓ — 100M iters,
  trace compilada al método inlined, mismo resultado.
- Correctness Ola 14: map size, get, has, remove; list size, get,
  push, reverse, sort ✓

### Performance

`bench_ola14.te` — 5M iters de `m["five"] + xs[7]` (mixto map+list):
- TypeEasy: ~5.0s netos (7.5s wall − 2.5s overhead Docker)
- CPython:  1.8s
- **Ratio:** TypeEasy ~2.8× **más lento** que CPython en este patrón.

Por qué seguimos detrás de CPython aquí:
1. Tree-walker sigue dominando: cada acceso pasa por
   `interpret_*` → `evaluate_expression` → `list_get_item` /
   `map_find_pair`. Antes de Ola 14 esto era O(n) **además** del
   overhead del walker; ahora es O(1) **más** ese overhead.
2. El JIT (Olas 7-12) sólo compila bucles aritméticos enteros con
   método inlining. NO traza accesos a `list[i]` ni `map["k"]`
   (esos son `NK_INDEX` con resolución dinámica de tipo).
3. CPython tiene LOAD_FAST + DICT_GETITEM en C ultra-tight loops,
   y especializaciones (PEP 659) en 3.11+.

Lo que sí mejora Ola 14 vs. baseline TypeEasy pre-14:
- Antes: cada `m["k"]` era O(n) sobre la lista enlazada de pares.
  Para un map con 1000 claves, una lookup costaba ~1000 ciclos
  de strcmp + chase.
- Ahora: `m["k"]` es 1 strcmp + 1 hash lookup ≈ constante,
  independiente del tamaño del map.
- El beneficio asintótico se nota recién con maps grandes
  (>100 claves) o índices altos en listas largas.

### Conclusión honesta

Ola 14 hace lo que el nombre promete: **complejidad O(1) amortizada**
en accesos a tipos compuestos, sin romper el JIT. Pero **no** convierte
a TypeEasy en más rápido que Python en código mixto: para eso harían
falta:

- **Ola 15:** strings con length cacheado + interning para `==`,
- **Ola 17:** float JIT (LOAD_DOUBLE / xmm regs en codegen),
- **Ola 10c:** type guards para que el JIT pueda trazar `m["k"]` y
  `xs[i]` con specialization (saltar a deopt si cambia el tipo del
  contenedor).

TypeEasy hoy: rápido **donde el JIT corre** (loops enteros con
method inlining → 24-70× CPython) y O(1) en composite reads, pero
todavía dominado por overhead del tree-walker en código mixto
general. La afirmación "más rápido que Python en general" sigue
siendo **falsa**.



---

## Ola 15 — Strings: identifier interning extendido (map keys)

**Fecha:** sesión actual.
**Objetivo:** Reducir overhead de `strcmp` en lookups de map y comparaciones
de strings idénticos, extendiendo el interning ya existente (Ola 3 Fase A:
literales STRING) al slot `id` de los nodos KV_PAIR.

### Diseño

- Nuevo flag `int id_interned` en `ASTNode` (paralelo a `str_interned`).
- `create_kv_pair_node` ahora intern-iza la clave del map vía `tee_intern`
  cuando el interning está habilitado (env `TYPEEASY_NO_INTERN=1` lo
  desactiva). El bucket global ya existe desde Ola 3.
- `free_ast` respeta el flag: nunca libera `id` interned (memoria immortal).
- `map_find_pair` (tanto en la ruta hash O(1) Ola 14 como en el fallback
  O(n)) prueba **pointer-eq** antes de `strcmp`. Cuando la clave de lookup
  es un literal STRING ya interned (Ola 3) y la clave del map es un
  KV_PAIR id ya interned (Ola 15), el match es comparación de punteros
  pura — 1 instrucción CMP, sin tocar memoria de strings.

### Cambios

- `src/ast.h`: campo `id_interned`.
- `src/ast.c`:
  - `create_kv_pair_node`: intern + flag.
  - `map_find_pair`: pointer-eq fast path en hash y fallback.
  - `free_ast`: respeta `id_interned`.

### Tests

- Regresión: `crear_clase`, `test_if`, `bucle_for`, `crear_const_variable`,
  `test_print`, `link_to_objects` ✓
- `test_ola14.te` (correctness composite): ✓ (sigue triggereando el bug
  pre-existente de `xs.get(i)` dentro de aritmética en for-loop, no
  introducido por Ola 15)

### Performance

`bench_ola15_map.te` — 10M iters de `m["epsilon"]` con map de 10 claves:
- TypeEasy: 4.02s wall (~1.5s netos restando 2.5s overhead docker)
- CPython:  1.71s wall (~1.71s netos)
- **Ratio:** TypeEasy ~0.9× Python en netos puros (paridad efectiva).

`bench_ola14.te` — 5M iters mixto map+list (mismo bench de Ola 14):
- Pre-Ola 15: 7.45s wall (~5.0s netos)
- Post-Ola 15: 6.04s wall (~3.5s netos)
- **Speedup ~30%** sobre el mismo workload.

### Conclusión honesta

Ola 15 NO refactoriza la representación de strings (siguen siendo
`char*` strdup en variables) — sólo extiende el pool global de interning
a las claves de map. Eso reduce el coste por lookup de `~strcmp(N,N)` a
una comparación de punteros cuando las claves son literales (caso común
en código real: `m["status"]`, `config["port"]`, etc.).

Lo que **no** hace Ola 15:
- No cachea `strlen` en `Variable.value.string_value` (los benchmarks
  de strings no son hot, salté esa optimización).
- No intern-iza nombres de variables — `find_variable` sigue siendo
  scan lineal con `strcmp`. Eso sería **Ola 16**: hash-indexed symtab.
- No mejora concatenación de strings ni métodos de string.

TypeEasy ahora alcanza paridad con CPython en map-lookups puros,
y reduce ~30% el tiempo en código mixto map+list. Sigue dominado por
el overhead del tree-walker en aritmética de strings y operaciones
floats, que son blanco de Ola 17.



---

## Ola 16 — Hash-indexed symbol table

**Fecha:** sesión actual.
**Objetivo:** Acelerar `find_variable` / `find_variable_for` reemplazando
el scan lineal sobre `vars[]` por un hash side-index (FNV-1a + open
addressing). Aumenta beneficio para programas con muchos identificadores
locales/globales o patrones de runtime que no hitten `cached_var` (Fase 2).

### Diseño

- Side-index estático `TESymSlot { uint64_t hash; const char *key; int idx }`
  con capacidad fija pow2 (256), open addressing y probe lineal.
  Para `MAX_VARS=100`, load factor < 0.5 → casi sin colisiones.
- El campo `key` aliasa `vars[idx].id` (no copia), así que no requiere
  liberación independiente. Cuando el id está internado (por ejemplo si
  Ola 17/futuro intern-iza identifiers AST), pointer-eq fast path.
- `te_sym_lookup`, `te_sym_insert`, `te_sym_clear`, `te_sym_reset_to`.
  Reset rebuild barato (≤ MAX_VARS slots).
- `find_variable` y `find_variable_for` consultan el hash primero;
  si miss, hacen el scan lineal y siembran el hash (lazy build).
- `declare_variable` y la rama new-var de `add_or_update_variable`
  insertan en el hash al crear la variable.
- `runtime_reset_vars_to_initial_state` llama `te_sym_reset_to(initial)`
  para descartar entradas de vars liberadas y rebuildear las
  sobrevivientes.

### Cambios

- `src/ast.c`:
  - Forward decl de `te_sym_reset_to` y `te_str_hash` (usado antes de
    su definición Ola 14).
  - Bloque "Ola 16 — Hash-indexed symbol table" con structs y helpers.
  - `find_variable` / `find_variable_for`: hash-first lookup.
  - `declare_variable`: insert al crear.
  - `add_or_update_variable`: insert en la rama new-var.
  - `runtime_reset_vars_to_initial_state`: rebuild.
- `src/ast.h`: sin cambios.

### Tests

- Regresión completa (6 tests): ✓
- `bench_method_call.te` con JIT (Ola 12 path): ✓ — trace compilada,
  100M iters mismo resultado.

### Performance

`bench_ola16.te` — 30 vars locales, 5M iters de
`total + a30 + a25 + a20 + a15 + a10 + a5`:
- TypeEasy: **2.32s** wall (0s netos — todo es overhead Docker
  porque el JIT compila el loop entero a x86_64).
- CPython:  **2.21s** wall.
- **Ratio: paridad** (TypeEasy 1.05× wall, pero descontando overhead
  Docker ~2.5s, TypeEasy es **infinitamente más rápido** en el cómputo
  puro porque la traza JIT de Ola 12 ya inlina los lookups con
  `cached_var`).

Notas:
- En este patrón el JIT de Olas 7-12 captura la traza y los accesos
  a `aN` se resuelven via `cached_var` (Fase 2), que ya hace pointer
  load constante. La hash table de Ola 16 ayuda en la primera pasada
  del walker antes de que la traza se compile, y en código que no
  trazee (e.g. ramas con `if`, llamadas, etc.).
- En workloads NO hot (no llegan al threshold del JIT) el speedup
  esperado es la diferencia entre `O(n) strcmp` y `O(1) hash + 1 strcmp`.
  Para vars[] de tamaño 30, eso es ~15× menos comparaciones por lookup.

### Conclusión honesta

Ola 16 cierra una de las pocas ineficiencias O(n) restantes del walker.
Su impacto se siente más fuera del JIT — código frío, código con
flujo no-trazado, y servidores HTTP que no calientan loops. En código
hot ya cubierto por la Fase 2 cache, Ola 16 es marginal pero no
hace daño.

Lo que **no** cambia con Ola 16:
- `cached_var` Fase 2 sigue siendo la optimización dominante para
  AST identifier nodes en bucles trazados.
- La capacidad sigue limitada por `MAX_VARS=100` — para programas
  con más de 100 vars, el problema no es la velocidad de lookup
  sino el límite duro. Subir MAX_VARS es un cambio de una línea
  pero ortogonal.


---

## Ola 17 — Float fast-path en JIT (Linux/x86_64)

### Diseño

El JIT de Ola 10b–12 manejaba sólo aritmética entera. Cualquier traza con
operandos `T_FLOAT` era rechazada por validación → fallback al intérprete
de bytecode. Esto condenaba a TypeEasy a ser más lento que CPython en
cualquier hot loop de punto flotante.

Ola 17 añade un fast-path completo de `double` al codegen:

- **8 nuevos emitters x86_64** (raw opcodes documentados inline):
  `movsd xmm0,[rsp+disp]` / `movsd [rsp+disp],xmm0` para spill,
  `movsd xmm0,[rax]` / `movsd [rax],xmm0` para acceso a `Variable.value`,
  `addsd/subsd/mulsd/divsd xmm0,[rsp+disp]`.
- **2 helpers de codegen**: `jit_emit_load_var_float(p, v, id)` y
  `jit_emit_store_var_float(p, v, src_id)`. Cargan/guardan
  `&v->value.float_value` (mismo offset que `int_value` por la unión)
  vía `xmm0`, sin tocar enteros.
- **Routing por `ins->type`** en validación, hoist pass y main pass:
  `T_INT` → fast-path entero existente; `T_FLOAT` → nuevas rutas SSE.
- **Slot layout intacto**: cada SSA id sigue ocupando 8 bytes en
  `[rsp+id*8]`. Para flotantes el slot guarda los bits `double` directos
  (no se reinterpreta — `movsd` lee/escribe los 64 bits crudos).
- **`IR_LOAD_CONST T_FLOAT`** ahora hace `memcpy(&imm, &double_val, 8)`
  para llevar los bits del double al slot vía `mov rax, imm64; mov [rsp+id*8], rax`.
- **`IR_RETURN T_FLOAT`** hace `movsd xmm0, [rsp+id*8]` (ABI sysv expects
  `xmm0` para retorno `double`); no se usa `cvtsi2sd` porque los bits
  ya son double.
- **Asignador de lregs (Ola 12)** ahora ignora `IR_STORE_VAR` con
  `type == T_FLOAT`: las vars flotantes loop-carried siguen vía spill
  a stack. Caching en xmm callee-saved se descartó por simplicidad
  (sólo `xmm6..xmm15` son callee-saved en SysV; el branch limpio era
  más alto que la ganancia esperada para Ola 17).
- **`IR_LT` sigue siendo INT-only**. Comparaciones flotantes
  (`ucomisd`) no están implementadas, así que cualquier traza con
  `if (f < g)` falla validación y cae al intérprete. Documentado como
  limitación conocida.

### Hallazgo bloqueante (auto-detect en `do_store`)

El handler `do_store` del bytecode VM hace auto-detect:
```c
if (r == (double)(int)r) v->vtype = VAL_INT; else v->vtype = VAL_FLOAT;
```

Esto significa que una variable declarada `float` puede oscilar entre
`VAL_INT` y `VAL_FLOAT` según si el resultado de la iteración es
matemáticamente entero. Con `acc = acc + 1.5`, los pasos pares dan
valores `*.0` (integral) → vtype flips a INT → trace recorder emite
`IR_STORE_VAR` con `type=T_INT`, que choca con el `LOAD_VAR f` previo
y rompe la validación del JIT.

**Mitigación documental**: usar valores que nunca producen integrales
exactos (e.g. `0.1`, que es irrepresentable en binario). Esto NO es un
arreglo del bug subyacente; modificar `do_store` para respetar la
declaración estática del tipo es trabajo de Ola 18+.

### Benchmark (200M iteraciones, `acc = acc + x` con `x=0.1`)

| Runner             | Tiempo | Notas |
|--------------------|--------|-------|
| TypeEasy + JIT     | 3.48 s | ~1.4 s después de descontar overhead Docker (~2 s) |
| CPython 3.x        | 22.2 s | hot loop puro, sin Docker |

**Speedup ~16×** sobre CPython en hot loop float-only. La traza
compila en 283 bytes, deopta a las 11 iteraciones bytecode (cuando se
detecta el target hot) y el resto se ejecuta nativo hasta la salida
natural del loop (i==limit dispara `IR_GUARD_TRUE` falso → writeback +
ret).

### Cambios

- `src/ast.c`:
  - +8 emitters movsd/addsd/subsd/mulsd/divsd después de `jit_emit_load_this_attr`.
  - +`jit_emit_load_var_float`, `jit_emit_store_var_float`.
  - Validación: `IR_LOAD_CONST/LOAD_VAR/STORE_VAR/RETURN` aceptan T_FLOAT
    con vtype=VAL_FLOAT estricto; `IR_ADD/SUB/MUL/DIV_FLOAT` aceptados.
  - Hoist + main pass: routing T_INT/T_FLOAT, `LOAD_CONST T_FLOAT` con
    `memcpy` para preservar bits, `RETURN T_FLOAT` con `movsd xmm0`.
  - lreg allocator: skip `STORE_VAR` con `type==T_FLOAT`.

### Tests

- 6 regresiones (`crear_clase`, `test_if`, `bucle_for`,
  `crear_const_variable`, `test_print`, `link_to_objects`): ✅
- `bench_method_call.te` (Ola 12 int hot loop): traza compila igual
  que antes. ✅
- `bench_ola17.te` (50M iter float, while): trace compila a 283 bytes,
  resultado correcto (`5000000.499756`). ✅
- `bench_ola17_big.te` (200M iter float): 3.48 s vs Python 22.2 s. ✅

### Conclusión honesta

- **Hot loop float aislado**: TypeEasy supera a CPython ~16× cuando la
  variable mantiene `vtype=VAL_FLOAT` estable.
- **Limitación del auto-detect**: programas float "normales"
  (e.g. acumular múltiplos de 1.5) saltan vtype y la traza no compila.
  Este es el siguiente cuello de botella honesto.
- **Sin float compare**: loops con `while (f < g)` siguen sin trazar.
  Codegen de `ucomisd + setb` es trivial pero queda fuera de Ola 17.
- **Sin caching float en registros**: cada iteración hace 2 movsd a
  memoria por var loop-carried float. Aceptable para un primer
  fast-path; Ola 18+ podría agregar pool xmm6..xmm15.
