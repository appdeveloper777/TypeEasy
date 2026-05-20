# Refactor de `src/ast.c` — Plan incremental

`src/ast.c` tiene **16,812 líneas** y mezcla intérprete, builtins, JIT
bytecode, runtime CSV/DataFrame, hooks del debugger, JSON, HTTP, threading,
y wrappers POSIX→Windows. Romperlo en una sola sesión es ruleta rusa: cada
cambio requiere validación en **Docker Linux + Windows MSYS2** (regla #1
del repo), y muchas piezas comparten estado vía globals + forward decls
que no están en `ast.h`.

Este documento define una **partición lógica del archivo** y un **orden
de extracción** con criterios objetivos de seguridad. La regla cardinal:

> Cada extracción debe poder commitearse aislada y validarse con
> `docker compose build typeeasy && bash typeeasycode/_regression.sh`
> (59 PASS / 0 FAIL / 3 XFAIL como baseline) **y** con el smoke test del
> workflow `release-windows.yml`. Si alguno falla, se revierte.

---

## 1. Mapa lógico del monolito

Bloques identificados (líneas aproximadas; usar `grep -n` para exactas):

| Bloque | Responsabilidad | Tamaño aprox. | Estado de aislamiento |
|---|---|---|---|
| **A. Plataforma / wrappers** (`te_pread`, `te_nprocs_online`, macros `TE_TIMEGM`, `#ifdef _WIN32`) | Compatibilidad POSIX↔Win | ~150 líneas | Aislado — sin deps de runtime |
| **B. Symbol table** (`vars[]`, `find_variable`, `declare_variable`, `add_or_update_variable`) | Tabla global de variables | ~600 líneas | Acoplado a `Variable` |
| **C. JSON helpers** (`te_json_emit_node`, `te_json_parse_value`) | Serialización JSON | ~400 líneas | Aislado — entra/sale por `ASTNode*`/`char*` |
| **D. HTTP cliente** (`te_http_request`, `http_get`, `http_post`) | TCP HTTP/1.0 plano | ~250 líneas | Aislado |
| **E. Time/UUID builtins** (`now`, `now_epoch`, `date_*`, `uuid_v4`, `uuid_valid`) | Stdlib temporal | ~400 líneas | Aislado |
| **F. String/Math builtins** (`Math.*`, `string.*`) | Stdlib | ~300 líneas | Aislado |
| **G. CSV reader + DataFrame** (`from_csv_to_list`, `csv_*`, `df_*`, AVX2, pthread workers) | Pipeline columnar | ~3500 líneas | Aislado en la mayoría; deps en builtin dispatch |
| **H. Bytecode VM** (`BCOp`, `bc_compile`, `bc_exec`, registros `g_bc_reg_*`) | JIT opt-in | ~2500 líneas | Acoplado a `ASTNode`, `MethodNode` |
| **I. Debugger hooks** (`debugger_on_statement`, `dbg_printf`, `cmd_vars`) | DAP | ~600 líneas | Acoplado a `Variable`, AST, vía hooks |
| **J. LINQ / higher-order** (`.where`, `.select`, `.map`, `.filter`, `.reduce`, etc.) | Métodos de colecciones | ~1500 líneas | Acoplado pesado a AST/Variable/dispatch |
| **K. Class/Object interpreter** (`new`, dispatch de métodos, herencia, `inherit_from`) | OOP core | ~1500 líneas | Acoplado al walker entero |
| **L. Statement walker** (`interpret_ast`, `interpret_var_decl`, `interpret_if`, `interpret_for`, `interpret_for_in`, `interpret_while`, `interpret_call_*`) | Tree-walker semántico | ~3500 líneas | **Núcleo** — última extracción posible |
| **M. Expression evaluator** (`evaluate_expression`, `is_string_type`, `get_node_string`, `te_resolve_arg`) | Lectura de valores desde AST | ~1500 líneas | Acoplado a todo |
| **N. Builtin dispatch** (`te_builtin_dispatch`, registry calls) | Tabla de saltos | ~800 líneas | Acoplado a A-F + J + tokens DB |

Total: ~17k líneas (consistente con `wc -l`).

---

## 2. Orden de extracción (de menor a mayor riesgo)

### Fase 1 — **Helpers puros** (RIESGO BAJO)
Self-contained, entradas/salidas claras, sin globals compartidos.

1. **`src/te_json.{c,h}`** — bloque C.
   - Exportar: `char *te_json_stringify(ASTNode *root)`,
     `ASTNode *te_json_parse(const char *src)`,
     `void te_json_emit_node(ASTNode *node, char **buf, size_t *cap, size_t *len)`.
   - Internas (`static`): el resto de helpers de emit/parse.
   - **Dependencias**: solo `ast.h` (para `ASTNode`).
   - **Riesgo de regresión**: bajo. La interfaz pública es estable.

2. **`src/te_http.{c,h}`** — bloque D.
   - Exportar: `char *te_http_request(const char *method, const char *url, const char *body)`.
   - **Dependencias**: sockets POSIX/Winsock vía wrappers del bloque A.

3. **`src/te_time.{c,h}`** — bloque E (fecha + UUID).
   - Exportar las 8 funciones del registry.
   - **Dependencias**: `time.h`/`windows.h`. Mantener `TE_TIMEGM` macro en
     el `.h`.

4. **`src/te_stdlib_math.{c,h}`** y **`src/te_stdlib_string.{c,h}`** — bloque F.
   - Self-contained tras pequeño desacoplo del dispatch.

**Después de Fase 1**: `ast.c` baja a ~14,500 líneas. Smoke tests +
`_regression.sh` deben seguir verdes. Cada extracción es un commit
separado.

### Fase 2 — **Subsistemas grandes pero cohesivos** (RIESGO MEDIO)

5. **`src/te_csv.{c,h}` y `src/te_dataframe.{c,h}`** — bloque G.
   Mover la totalidad del CSV reader + DataFrame, incluyendo arenas
   (`g_csv_arena`, `t_csv_arena`, `t_ast_pool`) y AVX2 helpers.
   La superficie pública: `from_csv_to_list(...)` y entradas
   `te_df_dispatch_method` para `.sum/.min/.max/.group_sum/.length`.
   **Cuidado**: el `from_pool` flag en `ASTNode` cruza módulos; ya está
   en `ast.h`, dejar ahí.

6. **`src/te_bytecode.{c,h}`** — bloque H.
   Mover BCOp, `bc_compile`, `bc_exec`, registros e invalidación.
   La superficie pública: `bc_get_or_compile(...)`,
   `bc_get_or_compile_stmt(...)`, `bc_get_or_compile_method(...)`,
   `bc_exec(...)`, `bc_invalidate_all(...)`.
   **Cuidado crítico**: el `g_debug_enabled` gate debe replicarse;
   los callsites en `interpret_while`/`interpret_for` quedan en
   `ast.c`. Mantener el contrato de "fallback transparente al walker".

7. **`src/te_debugger_hooks.{c,h}`** — bloque I (la parte que no está
   en `src/debugger.c`).

### Fase 3 — **Núcleo del intérprete** (RIESGO ALTO)
Solo cuando Fase 1-2 lleven 1-2 meses estables.

8. **`src/te_linq.{c,h}`** — bloque J.
   Mover `.map/.filter/.reduce/.where/.select/.any/.all/.first/...` con
   su lookup helper. Probablemente la extracción más espinosa: hay
   ramas tempranas en `interpret_call_method` que deben quedar
   despachando al nuevo módulo.

9. **`src/te_object.{c,h}`** — bloque K.
   Class/object lifecycle, `inherit_from`, construct/destroy, dispatch
   por método.

10. **`src/te_expr.{c,h}`** — bloque M.
    `evaluate_expression` + helpers de resolución. Toca a casi todo.

11. **`src/te_stmt.{c,h}`** — bloque L.
    El walker. Lo que quede en `ast.c` ahí es solo "glue" + registry de
    builtins + globals históricas.

Tras Fase 3, `src/ast.c` queda como un **shim** con:
- Includes de los nuevos headers.
- Globals que no se pueden mover (vars[], classes[], flags).
- `te_register_ast_builtins()` (registry bootstrap).
- Posiblemente vacío al final, renombrable a algo más honesto.

---

## 3. Procedimiento por extracción

Cada commit que extrae un bloque debe:

1. **Copiar** las funciones del bloque a `src/te_<X>.c`/`.h`.
2. **Reemplazar** las definiciones en `ast.c` por `#include "te_<X>.h"`
   y/o `extern` declarations donde el .h no aplique.
3. Si quedan helpers `static` requeridos por más de un sitio, promoverlos
   a `static` dentro del nuevo `.c` (no externos).
4. **Actualizar TRES sitios de build** (regla del repo):
   - `src/Makefile` — añadir el `.c` a `MOTOR_C_FILES`.
   - `src/Dockerfile` — añadir el `.c` a la lista `gcc -c` de la etapa
     `api_builder` y a la `typeeasy` target.
   - `scripts/build_native_windows.sh` — añadir el `.c` al `gcc -o
     typeeasy.exe` final.
   - `scripts/dev_api_entrypoint.sh` — también, para el dev compose.
5. **Validar**:
   - `docker compose build typeeasy` (Linux).
   - `docker compose run --rm typeeasy /code/_smoke_let.te` (smoke).
   - `bash typeeasycode/_regression.sh` desde el container (baseline
     59 PASS / 0 FAIL / 3 XFAIL).
   - `python tools/te-test/run_tests.py tests/lang --docker --docker-image typeeasy-typeeasy:latest`.
   - Empujar a una branch de CI para que `release-windows.yml` corra
     el smoke (`let/var/const`, OBJECT_LITERAL multi-key, llamada
     multi-arg).
6. **Mantener `.h` consistente**: cualquier función que retorne puntero
   y se llame desde `parser.y`/`parser.l` **debe** tener prototipo en
   `src/ast.h` (o en el nuevo `.h` y referenciado desde ast.h). El
   patrón histórico de implicit-decl + LLP64 truncando a 32 bits ya
   causó SIGSEGVs (bug v0.0.1 y v0.0.2).

---

## 4. Anti-patrones a evitar

- **NO** crear "utility" headers que se incluyen circularmente. Mejor
  un `te_internal.h` plano con los typedefs comunes (`Variable`,
  `ASTNode`, `ObjectNode`, `MethodNode`, `ClassNode`) y que los
  módulos lo incluyan.
- **NO** cambiar la firma de funciones públicas en una extracción.
  Refactor de firmas va en commits separados, no mezclados con el
  movimiento físico.
- **NO** introducir `inline` en headers sin marcar el `.c` que tiene
  la copia "out-of-line" — diferentes compiladores difieren en
  emisión.
- **NO** depender del orden de evaluación de globals entre `.o` files
  (constructores). Mantener `te_register_ast_builtins()` como
  llamada explícita en `main`/bootstrap.

---

## 5. Cuándo NO hacer este refactor

Si el equipo se está moviendo en otro frente (DB drivers, plugins,
release), **posponer**. Un refactor a medias en `ast.c` deja inconsistencias
peores que el monolito. Las fases 1 y 2 son independientes y se pueden
hacer una por sprint sin bloquear features.

---

## 6. Métrica de éxito

Pre-refactor: `wc -l src/ast.c` = 16,812.
Objetivo post-Fase 1: ≤ 14,500.
Objetivo post-Fase 2: ≤ 9,000.
Objetivo post-Fase 3: `src/ast.c` ≤ 1,500 (solo bootstrap + globals).

Tests verdes en cada commit, sin excepciones.
