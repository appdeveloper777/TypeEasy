# Changelog

Formato: por release, tres bloques. **Cambios de comportamiento** lista todo lo que
un script existente puede observar distinto (salida, errores, tipos), con el test
que fija la conducta nueva. Política: `docs/VERSIONING.md`.

## 0.1.8 — 2026-09-19

### Cambios de comportamiento
- `println([1, 2])` (lista **literal**) imprime `[1, 2]` en vez de `0` (la variable ya
  funcionaba); `null` dentro de listas se imprime `null` (`tests/lang/17_collections/lst01*`, `nul01*`).
- `println` de un map (variable) imprime JSON en vez de fallar con segfault
  (`17_collections/map01*`).
- `println` de una expresión `decimal` imprime el texto canónico (`3.30`) en vez de
  16 dígitos flotantes (`15_numeric/num06*`, `num13*`).
- `println(m["clave_inexistente"])` imprime `null`; antes cortaba con
  `Error: key not found` (`17_collections/map02*`).
- `println(x?.attr)` con `x == null` imprime `null` (antes segfault) (`17_collections/nul01*`).
- `.length` funciona sobre cualquier expresión string dentro de `print`/`println`
  (antes solo sobre literal) (`16_strings/str02*`).
- `--syntax-check` devuelve `{"ok","errors","warnings"}`; se agregó la clave
  `warnings` (antes solo `ok`/`errors`) (`09_diagnostics/syntax_check_clean_json.te`).
- `--syntax-check` ahora **rechaza** `super` (S5, error) y **analiza los cuerpos de
  métodos de clase** (antes solo top-level): scripts que pasaban el check con un
  error dentro de un método ahora fallan (`09_diagnostics/lint_super.te`,
  `syntax_check_semantics.te`).
- Mensaje de error de parseo al usar una palabra reservada como identificador/clave
  (`from`, `as`, `xml`, …) incluye la sugerencia de renombrar/entrecomillar
  (`09_diagnostics/lint_reserved_word_hint.te`).

### Nuevo
- `docs/SPEC.md` normativa con **64 reglas con ID** (`[LEX-1]` … `[LNQ-4]`), cada una
  con test 1:1 `// spec: ID`; `tools/te-test/spec_coverage.py --strict` en CI.
- Lint en `--syntax-check` (canal `warnings`): S6 `"json"` como 4.º argumento en
  `mysql_*`/`db_*` de escritura; S7 operando string en `&&`/`||`/`!`.
- `te --fmt <archivo> [--write|--check]`: formateador conservador (solo espacios e
  indentación; aborta con rc 3 si el stream de tokens cambiaría). Idempotente sobre
  la suite y sobre los 123 `.te` del ERP.
- Directiva `// args:` en `tests/lang` para pasar flags al binario.
- LSP: las `warnings` del check se muestran como diagnósticos de severidad Warning.
- `deploy/te-ab-bytecode.sh` (ERP): A/B bytecode vs walker sobre 35 endpoints reales.

### Interno
- Refactor mecánico `strcmp(node->type, TE_T_X)` → `nk_of(node) == NK_X` en 266
  sitios (`tools/refactor/nk_convert.cjs`); `te_csv` invalida `kind` al reasignar `type`.
- `audit_core_debt.sh`: 0 magic strings de tipo (te_fmt usa `TE_DT_*`).

### Limitaciones conocidas (documentadas en SPEC, sin corregir)
- `await_all` devuelve resultados vacíos; `throw <map>` se captura como `0`;
  `json_parse` convierte booleanos a `1/0`; `%` trunca operandos; división por cero
  imprime a stdout y continúa; `m["k"].push(x)` y `fs[1](5)` son error de sintaxis.

## 0.1.7 — 2026-09-18

### Cambios de comportamiento
- `Math.floor/ceil/round/abs/trunc` devuelven **INT** cuando el resultado es entero
  y cabe en 64 bits (antes se casteaba a 32 bits y caía a FLOAT con `.0`, p. ej.
  `Math.floor(4294967296.5)` → `4294967296` en vez de `4294967296.0`)
  (`07_stdlib/math_int64_result.te`).

### Corregido
- Pool MySQL: al soltar/adquirir un slot se hace `ROLLBACK` + `autocommit=1`; una
  transacción abierta por early-return ya no se hereda al siguiente request
  (`tests/dbreal/mysql_pool_rollback`).

## 0.1.6 — 2026-09-1x

### Cambios de comportamiento
- `import "archivo"` inexistente se **reporta** siempre; con `--strict-imports` o
  `TYPEEASY_STRICT_IMPORTS=1` es **fatal** (antes se ignoraba en silencio).
- El launcher (`typeeasy` → `typeeasy-bin`) pasa todos los argumentos y reporta la
  versión real del binario.

### Nuevo
- `sql_last_error()`.
- Diagnóstico de headroom de `MAX_VARS`.

## 0.1.5

### Corregido
- Cadenas de métodos re-entrantes y métodos de string sobre atributo/índice
  (`obj.attr.trim()`, `lst[i].upper()`).

## 0.1.2 – 0.1.4 (resumen)
- 0.1.2: scoping real (frames en métodos/ctor/lambdas, `this` restaurado, recursión
  correcta, un local ya no pisa el global homónimo), closures léxicas,
  `this.metodo(args)`, `dynamic`.
- 0.1.1: tipo `decimal`.
