# Backend Wasm para TypeEasy

TypeEasy puede generar WebAssembly Text Format (`.wat`) como una salida adicional del compilador. Esta ruta no cambia la ejecucion normal del interprete, los endpoints, ORM ni el servidor embebido.

## Uso

Desde `src/`:

```bash
make
./typeeasy --emit-wat ../typeeasycode/wasm/demo_suma.te -o demo_suma.wat
./typeeasy --emit-wasm ../typeeasycode/wasm/demo_suma.te -o demo_suma.wasm
```

Para `--emit-wasm`, instala WABT para tener disponible `wat2wasm`. Tambien puedes convertir manualmente el `.wat`:

```bash
wat2wasm demo_suma.wat -o demo_suma.wasm
node ../tools/wasm_runner/run_wasm.js demo_suma.wasm
```

Tambien se acepta el formato legacy con el archivo primero:

```bash
./typeeasy ../typeeasycode/wasm/demo_suma.te --emit-wat -o demo_suma.wat
./typeeasy ../typeeasycode/wasm/demo_suma.te --emit-wasm -o demo_suma.wasm
```

## Alcance inicial

Soportado en esta primera version:

- enteros `int`
- variables simples con `let`, `var` o `int`
- asignaciones
- suma, resta, multiplicacion y division entera
- comparaciones `>`, `<`, `==`, `>=`, `<=`, `!=`
- `if` / `else`
- `print()` y `println()` de enteros mediante imports del host

No soportado todavia:

- strings en Wasm
- clases y objetos
- listas
- endpoints HTTP
- ORM/MySQL
- `json()` / `xml()`
- llamadas nativas generales
- `for` numerico, hasta normalizar la estructura AST usada por el interprete actual

Si el backend encuentra una sentencia no soportada, falla con un mensaje `[WASM] Error` y no genera salida parcial.

## Imports esperados

El modulo generado importa estas funciones desde el host:

```wat
(import "env" "print_i32" (func $print_i32 (param i32)))
(import "env" "print_i32_ln" (func $print_i32_ln (param i32)))
```

El runner de Node en `tools/wasm_runner/run_wasm.js` ya provee esos imports.