# TypeEasy — Semántica del lenguaje

Documento de referencia con las decisiones de diseño del lenguaje TypeEasy.
Sirve como contexto para futuras sesiones de desarrollo (humanas o con IA).

---

## 1. Declaración de variables

TypeEasy distingue tres palabras clave para declarar variables, con semántica
inspirada en TypeScript / Swift / Dart:

| Keyword | Reasignable | Valor permitido | Análogo |
|---------|-------------|-----------------|---------|
| `const` | ❌ No | Solo literales en compile-time (ej. `3.1416`, `"hola"`, `42`) | `constexpr` (C++), `#define` |
| `let`   | ❌ No | Cualquier expresión runtime (`new Class(...)`, llamadas a métodos, etc.) | `const` (JS), `final` (Dart), `let` (Swift) |
| `var`   | ✅ Sí | Cualquier expresión runtime | `let` (JS), `var` (Dart) |

### Ejemplos

```dart
const PI = 3.1416;            // ✅ literal
// const p = new Person(...); // ❌ no permitido (no es literal)

let nombre = "Fernando";      // ✅
// nombre = "Otro";           // ❌ Error: No se puede asignar a la variable constante 'nombre'.

let obj = new Person(36, "Fernando", "Roussecloud");
obj.edad = 99;                // ✅ mutar atributo del objeto SÍ se permite
// obj = new Person(...);     // ❌ reasignar la VARIABLE no se permite

var contador = 0;
contador = contador + 1;      // ✅
```

### Regla clave para `let` con objetos

`let` congela **la referencia**, no el contenido del objeto:

- `obj.atributo = ...` → permitido (mutación de atributo).
- `obj = nuevoObjeto` → prohibido (reasignación de la variable).

Misma semántica que `const` en JavaScript o `final` en Dart.

---

## 2. Métodos de clase con tipo de retorno obligatorio

Todo método debe declarar explícitamente su tipo de retorno con `: <tipo>`
entre los paréntesis de parámetros y la llave `{` del cuerpo.

### Tipos de retorno válidos

| Tipo      | Significado |
|-----------|-------------|
| `int`     | Debe retornar un entero. |
| `string`  | Debe retornar un string. |
| `float`   | Debe retornar un float. Acepta widening de `int` → `float`. |
| `void`    | NO debe retornar valor. Si retorna algo, error en runtime. |
| `dynamic` | Acepta cualquier tipo. Estilo C# `dynamic`. La validación de tipo se difiere al sitio de llamada. |

### Sintaxis

```dart
class Person {
    edad : int;
    nombre : string;

    __constructor(_edad : int, _nombre : string) {
        this.edad = _edad;
        this.nombre = _nombre;
    }

    Saludar() : string {
        return "hola";
    }

    EdadDoble() : int {
        return this.edad * 2;
    }

    Imprimir() : void {
        println(this.nombre);
        // return 5;  // ❌ error: void no debe retornar
    }

    Cualquiera() : dynamic {
        return 42;        // o "string", o 3.14 — acepta cualquier tipo
    }
}
```

### Validación en runtime

El intérprete (`interpret_call_method` en `src/ast.c`) valida:

1. Si el método es `void` y retorna valor → error.
2. Si el método NO es `void` ni `dynamic` y no retorna valor → error.
3. Si retorna valor, su tipo (`INT` / `STRING` / `FLOAT`) debe coincidir con el
   tipo declarado. Excepción: `int` → `float` se permite (widening).
4. `dynamic` salta la validación de tipo del retorno.

### Validación en el sitio de llamada

El tipo declarado en la variable receptora se valida **independientemente** del
tipo de retorno del método:

```dart
let obj = new Person(36, "Fernando");

string s = obj.EdadDoble();   // ❌ EdadDoble retorna int, no string
int n = obj.EdadDoble();      // ✅
var x = obj.EdadDoble();      // ✅ var infiere
```

Incluso si el método es `dynamic`, el tipo de la variable receptora se sigue
validando contra el valor concreto retornado.

---

## 3. Atributos de clase tipados

Todos los atributos deben declarar tipo:

```dart
class Producto {
    id     : int;
    nombre : string;
    precio : float;
}
```

---

## 4. Build & ejecución

⚠️ El host Windows **no** tiene `gcc` / `flex` / `bison` locales. Cualquier
cambio en `src/parser.l`, `src/parser.y`, `src/ast.c`, `src/ast.h` requiere
**rebuild en Docker**:

```bash
docker compose build typeeasy
docker compose run --rm typeeasy <archivo>.te
```

El binario `src/typeeasy.exe` puede estar desactualizado y causar segfaults si
se ejecuta directamente desde Windows.

---

## 5. Archivos clave del intérprete

| Archivo | Responsabilidad |
|---------|-----------------|
| `src/parser.l` | Lexer Flex. Tokens `LET`, `VAR`, `CONST`, `VOID`, `DYNAMIC`, `INT`, `STRING`, `FLOAT`, etc. |
| `src/parser.y` | Gramática Bison. Reglas `var_decl`, `method_decl`, `method_return_type`. Las reglas `LET` setean `d->value = 1` (flag `is_const`). |
| `src/ast.h`    | `struct MethodNode` con campo `return_type`. `struct Variable` con campo `is_const`. |
| `src/ast.c`    | `declare_variable(id, value, is_const)`, `interpret_assign` (chequea `is_const`), `interpret_call_method` (valida `return_type`). |

---

## 6. Historial de decisiones de diseño

- **`let` = inmutable, `var` = mutable, `const` = literal**: unificar con
  TypeScript/Swift/Dart. Antes `let` y `var` eran sinónimos.
- **`: <tipo>` obligatorio en métodos**: prevenir bugs silenciosos con métodos
  que olvidaban retornar. Migración automática agregó `: dynamic` a todos los
  `.te` existentes para no romper código legado.
- **`dynamic`**: escape hatch al estilo C# para cuando el desarrollador
  quiere flexibilidad pero sin caer en "ningún tipo".
