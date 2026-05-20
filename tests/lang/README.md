# `tests/lang/` — Conformance test suite

Cada `.te` aquí es un **test del lenguaje** (no un ejemplo ni un benchmark).
Las expectativas se declaran por archivo y el runner las verifica
deterministicamente.

## Ejecutar

Desde la raíz del repo:

```bash
# Vía Docker (recomendado, no requiere binario local).
python tools/te-test/run_tests.py tests/lang --docker

# Vía binario nativo:
python tools/te-test/run_tests.py tests/lang --bin bin/typeeasy.exe

# Subset:
python tools/te-test/run_tests.py tests/lang --filter types_
```

El runner sale con código distinto de cero si hay tests `FAIL` o `XPASS`.

## Declarar expectativas

Tres mecanismos, el primero que matche gana:

1. **Sibling `.expected`** — match exacto byte a byte tras `rstrip()`.
2. **Directivas en cabecera** del `.te` (líneas `// ...` al inicio del archivo):
   ```te
   // expect: hello world
   // expect-exit: 0
   // expect-contains: foo
   // expect-stderr-contains: deprecated
   // xfail: feature not implemented yet
   // skip: documenting a bug
   // skip-on: windows
   // timeout: 10
   ```
3. **Sin expectativas** → **smoke test**: pasa si el exit code es 0.

## Convenciones

- Un test = una sola feature aislada. Mantenelo bajo 30 líneas.
- Categorías sugeridas:
  - `00_lexer/` — tokens, literales raros (hex/bin/underscore separators, `?.`, `??`).
  - `01_types/` — `int`, `float`, `string`, `bool`, `datetime`, `uuid`, `T?`.
  - `02_declarations/` — `let` / `var` / `const` semantics.
  - `03_control_flow/` — `if/else`, `while`, `for in`, `break`, `continue`.
  - `04_functions/` — funciones, lambdas, recursión, return types.
  - `05_oop/` — clases, atributos, métodos, `extends`.
  - `06_collections/` — listas, maps, métodos higher-order.
  - `07_stdlib/` — `Math.*`, `string.*`, `json_*`, `env`.
  - `08_errors/` — `throw` / `try` / `catch` / `finally`.
- Tests que **deben** fallar (validar diagnostics) → directiva `// xfail:`.

`typeeasycode/examples/` y `typeeasycode/_regression.sh` siguen siendo válidos
(suite legacy/extendida). La nueva suite es la fuente de verdad para CI.
