# TypeEasy — Modelo de ejecución

> **Decisión declarada**: TypeEasy es un **tree-walking interpreter** con
> un **bytecode VM opt-in para hot loops** y **caché JIT por nodo y por
> método**. No es ni busca ser un AOT/JIT general (LLVM, Cranelift). El
> objetivo es productividad full-stack con performance "suficientemente
> buena" — competir con CPython en compute y ganarle en pipelines de
> datos cuando el modelo objeto-por-fila importa.

Última actualización: mayo 2026.

---

## 1. Pipeline

```
.te source
    │
    │  flex (src/parser.l)        ← lexer; tokens en parser.tab.h
    ▼
token stream
    │
    │  bison (src/parser.y)       ← parser LALR(1); construye ASTNode
    ▼
AST (ASTNode tree, src/ast.h)
    │
    │  interpret_ast / interpret_statement_list
    ▼
walker (src/ast.c — case por NK_*)        ◄── (1) ruta primaria
    │
    │  bc_get_or_compile / bc_get_or_compile_stmt / bc_get_or_compile_method
    ▼
bytecode (BCOp enum, lineal)               ◄── (2) ruta opt-in
    │
    │  bc_exec (dispatch table)
    ▼
resultado o TRACE_ABORT → fallback walker
```

### Etapa 1 — Lexer (flex)
`src/parser.l` produce tokens C tipados (`<sval>`, `<intval>`, etc.).
Notas críticas:
- Toda regla cuyo token sea `%token <sval>` **debe** ejecutar
  `yylval.sval = strdup(yytext);`. Olvidarlo es SIGSEGV silencioso en
  Windows LLP64 (bug histórico v0.0.1).
- Soporta `#include` léxico via `yy_switch_to_buffer` (mecanismo de
  imports — ver SPEC §7).

### Etapa 2 — Parser (bison)
`src/parser.y` define ~120 reglas. Genera `ASTNode` enlazados por
`left/right/next/extra`. Cada nodo guarda `node->line = yylineno` —
**obligatorio en toda `create_*_node` que emita una sentencia**, sino
los breakpoints del debugger no enganchan (regla aprendida con varios
bugs históricos del DAP).

### Etapa 3 — Interpreter (tree-walker)
**Ruta primaria.** `interpret_ast` hace `switch (node->kind)` sobre
`NK_*` enum. Todos los features funcionan aquí; el bytecode es una
optimización oportunista, no un requisito.

### Etapa 4 — Bytecode VM (opt-in por nodo)
Estructura: `BCOp` enum + array de `Instr` por nodo/method/loop body
compilado. Dispatch table en `bc_exec`. Soporta:
- Aritmética (incluye `%`, bitwise, shifts) — Phase G.
- Comparaciones y branches.
- Lectura/escritura de variables locales y atributos.
- Llamadas a métodos simples.
- Loops `for in` / `while` (con cache JIT del body completo).

**NO soporta**: lambdas, `try`/`catch`, `throw`, `import`, dispatch a
LIST/MAP/OBJECT pesado. Si el compilador encuentra algo no
representable, **devuelve `BC_NOT_COMPILABLE` y se cae al walker** —
nunca rompe semántica.

#### Invalidación del caché
La caché `node->bc` y `method->bc_body` se invalida en cada
`runtime_reset_vars_to_initial_state` (entre requests del API server)
porque almacena punteros a `Variable*` que se moverían tras truncar
`var_count`. Mecanismo: registros `g_bc_reg_nodes` /
`g_bc_reg_methods` poblados al compilar, recorridos en
`bc_invalidate_all`.

#### Cuándo se gatilla la compilación
- Loops y métodos: tras N ejecuciones (warmup contador implícito).
- Expresiones: solo si todas las hojas son representables.
- Forzado off cuando el debugger está activo
  (`g_debug_enabled` gatea `bc_get_or_compile` y bloques BC en NK_WHILE/NK_FOR)
  para que los hooks `debugger_on_statement` siempre disparen.

### Etapa 5 — Builtin dispatch
`te_builtin_dispatch` (en `ast.c`) consulta primero el **registry**
(`src/te_builtins.{c,h}`, Fase 1 mayo 2026). Si no hay match, cae al
if-chain legacy. Builtins nuevos se registran via
`te_builtin_register("name", adapter)`. Plugins externos (`.so`) se
cargan con `load_native("name")` y exportan `te_module_register`.

---

## 2. Modelo de datos en runtime

### `Variable`
Slot global en `vars[]` (append-only, hashed por `te_sym_*`). Campos:
- `id`, `type` (string descriptiva: `"INT"`, `"STRING"`, `"BOOL"`,
  `"DATETIME"`, `"OBJECT"`, `"LIST"`, `"MAP"`, …)
- `vtype` (`VAL_INT` | `VAL_FLOAT` | `VAL_STRING` | `VAL_OBJECT`)
- `value` (union)
- `is_const` flag

### `ASTNode`
Nodo del árbol. Vive durante toda la ejecución (no se libera entre
sentencias; sólo al exit con `free_ast`). Para CSV reader hay arenas
custom (`g_csv_arena`, `t_ast_pool`) que evitan el `calloc` per-row.

### Objetos
`ObjectNode { ClassNode *class; Variable *attributes; int attr_count; }`.
Almacenado en `Variable.value.object_value`. **No confundir** con
`"OBJECT_LITERAL"` que es un AST de KV_PAIR (cabeza encadenada),
sin `ObjectNode` detrás — castear ese puntero a `ObjectNode*` segfaultea.

### Listas y maps
Linked-list de `ASTNode` por `->next` (LIST) o KV_PAIR (MAP). La cabeza
guarda `extra = TEListIdx { len }` para que `.length` sea O(1). Para
pipelines de datos masivos (`from "*.csv", Class`) existe un fast-path
`DataFrame` columnar opt-in (`TE_CSV_DATAFRAME=1`) que rompe la
iteración OO — solo agregados.

---

## 3. Concurrencia

- Intérprete **no thread-safe**. El API server embebido toma un mutex
  global por request.
- Threads existen **solo dentro de operaciones internas**: CSV reader
  paralelo (`pthread`, cap 8-12 hilos, `TE_CSV_THREADS` override) y
  `df_group_sum` paralelo (thread-local hashmaps fijos).
- En Windows usa `winpthreads` (mingw-w64). El macro `TE_HAS_PTHREAD`
  vale 1 en Linux y `_WIN32`.

---

## 4. ¿Por qué este modelo?

### Alternativas consideradas y descartadas

| Modelo | Por qué no |
|---|---|
| **Pure tree-walker** | Loops de 10⁸ iteraciones son ~10× más lentos que el bytecode actual. La fase G de bytecode dio aceleración real medida. |
| **Bytecode VM única (sin walker)** | Cubrir el 100% de la superficie (lambdas, try/catch, imports, dispatcher OO complejo, DataFrame) en bytecode duplica la complejidad del intérprete. El walker ya funciona; reemplazarlo no era ROI positivo. |
| **AOT (LLVM)** | Mata el "no install, no runtime" del producto. El binario crecería 10-100×, y el tiempo de compilación de scripts cortos arruina UX. |
| **JIT general (Cranelift, MIR)** | Mismas razones; complejidad alta para un equipo pequeño. |

### Ventajas de la mezcla actual
1. **Cobertura semántica completa** — el walker es la red de seguridad.
   Cualquier feature nueva funciona sin tocar el bytecode.
2. **Aceleración donde duele** — hot loops y métodos numéricos pequeños
   se compilan a bytecode con dispatch table compacto.
3. **Fallback transparente** — si el bytecode no puede, el walker corre.
   Cero compromiso semántico.
4. **Debuggable** — basta apagar el bytecode (`g_debug_enabled=1`) para
   que cada sentencia pase por el hook DAP.

### Costo asumido
- Doble implementación de aritmética (walker + bytecode).
- Invalidación del caché bytecode entre requests del API server (resuelto
  con `bc_invalidate_all`).
- Performance "buena, no SOTA": para data-pipelines vectorizables
  (group-by, sum, min/max) seguimos detrás de Polars wall-clock (~7-10×
  a 10M filas), aunque ganamos a Python OO con `__slots__` ~3×.

---

## 5. Métricas de salud actuales

| Componente | Estado |
|---|---|
| Cobertura `NK_*` en walker | 100% — definición canónica de la semántica |
| Cobertura en bytecode VM | ~60% — aritmética, comparaciones, control flow básico, loops, métodos planos |
| Tests `tests/lang/` | Suite inicial (este commit). Crecer hasta cubrir cada feature de SPEC §2 con al menos un test. |
| `_regression.sh` legacy | 59 PASS / 0 FAIL / 3 XFAIL (v1.0.0) |
| Cross-platform | Linux Docker + Windows MSYS2 — ambos verdes; macOS pendiente |

---

## 6. Reglas de oro al modificar el motor

1. **El walker es la spec.** Cualquier divergencia entre walker y
   bytecode VM se resuelve corrigiendo el bytecode, **nunca** el walker.
2. **`BC_NOT_COMPILABLE` es bienvenido.** Es preferible saltar al
   walker que arriesgar una semántica errónea en bytecode.
3. **Toda `create_*_node` que emita sentencia debe asignar `node->line`.**
   Sin esto, breakpoints y stack traces fallan.
4. **Todo builtin nuevo va en el registry** (`te_builtin_register`),
   nunca en parser.l/parser.y. La gramática no debe conocer builtins.
5. **No mutar el AST en lambdas.** `evaluate_native_args` mutaba
   IDENTIFIER→STRING en sitio, lo que congelaba bindings dentro de
   `.where(...)` LINQ. Builtins llamables desde lambda deben resolver
   identifiers en cada invocación.
6. **Cross-platform o no entra.** Toda función POSIX usada en `src/`
   requiere wrapper `#ifdef _WIN32`. Ver
   `/memories/repo/typeeasy.md` para la lista canónica.

---

## 7. Roadmap razonable para el motor

Sin promesas de fecha. Ordenado por ROI estimado:

1. **Modularizar `src/ast.c` (16k líneas)** — ver
   [REFACTOR_AST_C.md](REFACTOR_AST_C.md).
2. **Subir cobertura del bytecode VM**: agregar `BC_IN` (LIST/MAP
   membership), strings cortas (interning + concat), lambdas como
   bytecode con upvalues simples.
3. **Optimización de IO en CSV reader**: el cuello de botella a 10M
   filas no es compute, es la materialización serial de
   `ObjectNode + Variable[]`. Fast-path "POD" desde workers paralelos.
4. **Closures verdaderos** (capturas por referencia) — requiere extender
   `call_lambda` con frame de variables capturadas. Hoy: solo params.
5. **`super` en herencia**.
6. **`async/await` cooperativo** (long shot — coroutine state machine).
