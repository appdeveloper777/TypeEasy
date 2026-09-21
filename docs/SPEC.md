# TypeEasy — Especificación mínima del lenguaje (v1.0.0)

Este documento define la **superficie estable del lenguaje** que el intérprete
soporta hoy y contra la cual `tests/lang/` valida en CI. La fuente normativa
de la sintaxis es `src/parser.l` (lexer) y `src/parser.y` (gramática Bison).
Si esta especificación diverge del parser, **el parser manda** y este archivo
debe corregirse.

**Reglas con ID y tests 1:1.** Las reglas normativas llevan un identificador
entre corchetes (p. ej. `[NUM-3]`). Cada una debe tener al menos un test en
`tests/lang/` que declare `// spec: NUM-3` en su cabecera; `python
tools/te-test/spec_coverage.py` lista las reglas sin test y los tests que citan
IDs inexistentes (corre en CI). Una regla sin ID es descriptiva, no normativa.

Para semántica detallada de declaraciones y tipos, ver
[LANGUAGE_SEMANTICS.md](LANGUAGE_SEMANTICS.md). Para el modelo de ejecución
(interpretador árbol vs. bytecode JIT), ver
[EXECUTION_MODEL.md](EXECUTION_MODEL.md).

---

## 1. Convenciones léxicas

### Codificación
- `[LEX-1]` Los archivos `.te` son **UTF-8**: los bytes no ASCII se aceptan en
  strings, comentarios e identificadores (`let configuración = 5;` es válido).
- Strings literales pueden contener cualquier byte (se almacenan opacos).

### Comentarios
```ebnf
LineComment  = "//" { any-char-but-newline }
BlockComment = "/*" { any-char } "*/"   (* NO anidados *)
```

### Identificadores
```ebnf
Identifier = letter { letter | digit | "_" }
letter     = "A" … "Z" | "a" … "z" | "_"
digit      = "0" … "9"
```

### Literales numéricos
`[LEX-3]` Hex (`0xFF` → 255), binario (`0b1010` → 10) y separador `_` (`1_000_000`) son enteros.
```ebnf
IntLit     = DecInt | HexInt | BinInt
DecInt     = digit { digit | "_" }                   (* "1_000_000" válido *)
HexInt     = "0x" hex { hex | "_" }                  (* "0xDEAD_BEEF" *)
BinInt     = "0b" ("0"|"1") { "0" | "1" | "_" }
FloatLit   = digit { digit } "." digit { digit }     (* sin exponente *)
DecimalLit = ( DecInt | FloatLit ) "m"                (* 1.10m, 7m: tipo decimal *)
```

### Literales string e interpolación
```ebnf
StringLit       = '"' { char | escape } '"'
InterpStringLit = '$"' { char | "{" Expr "}" | "\{" | "\}" } '"'
```

### Palabras reservadas
`[LEX-2]` Ninguna de estas puede usarse como identificador ni como clave de map
sin comillas (`{ from: 1 }` es error; `{ "from": 1 }` es válido). El
`--syntax-check` lo reporta con la pista *"'from' is a reserved word"*.
```
let var const class extends new this return
if else while for in break continue
true false null
try catch finally throw
fn import dynamic async await
int string float bool datetime uuid decimal void
print println fprint fprintln json xml
endpoint on_open on_message on_close
from as                          (LINQ)
dataset model train predict layer plot   (ML DSL)
agent listener bridge state match case node
```

### Operadores y puntuación
```
+ - * / %                          aritméticos
== != < <= > >=                    relacionales
&& || !                            lógicos (short-circuit)
& | ^ ~ << >>                      bitwise
?? ?.                              null-aware
+= -= *= /= ++ --                  compuestos / unarios
= ,                                asignación / separador
( ) { } [ ]                        agrupación
; :                                terminator / type-annotation
. ?                                acceso atributo / tipo nullable
=>                                 cuerpo de lambda
```

### Precedencia (mayor → menor binding)
1. `UMINUS`, `~`, `!`
2. `*` `/` `%`
3. `+` `-`
4. `<<` `>>`
5. `&`
6. `^`
7. `|`
8. `in`
9. `<` `<=` `>` `>=`
10. `==` `!=`
11. `&&`
12. `||`
13. `??`
14. `=` (asignación, asociativa a derecha)

---

## 2. Gramática (subset normativo)

EBNF con `{X}` = cero o más, `[X]` = opcional, `(A|B)` = alternativa.

### Programa
```ebnf
Program       = { TopLevel }
TopLevel      = Import | ClassDecl | FuncDecl | VarDecl | Statement
Import        = "import" StringLit ";"
```

### Declaraciones
```ebnf
VarDecl       = ("let" | "var" | "const") Identifier [ ":" Type ] "=" Expr ";"
Type          = "int" | "string" | "float" | "bool" | "datetime" | "uuid"
              | "dynamic" | Identifier
              | Type "?"                                  (* nullable *)

ClassDecl     = "class" Identifier [ "extends" Identifier ] "{" ClassBody "}"
ClassBody     = { AttrDecl | MethodDecl | ConstructorDecl }
AttrDecl      = Identifier ":" Type ";"
ConstructorDecl
              = "__constructor" "(" [ ParamList ] ")" Block
MethodDecl    = Identifier "(" [ ParamList ] ")" ":" Type Block
ParamList     = Param { "," Param }
Param         = Identifier ":" Type

FuncDecl      = (* equivalente a VarDecl asignando una lambda *)
Lambda        = "fn" "(" [ LambdaParams ] ")" "=>" (Expr | Block)
LambdaParams  = Identifier { "," Identifier }
```

### Sentencias
```ebnf
Statement     = VarDecl | Assignment | If | While | For
              | Break | Continue | Return | Throw | TryCatch
              | ExprStmt | Block

Block         = "{" { Statement } "}"
Assignment    = LValue ("=" | "+=" | "-=" | "*=" | "/=") Expr ";"
              | LValue ("++" | "--") ";"
LValue        = Identifier | ChainedAccess

If            = "if" "(" Expr ")" Block [ "else" (If | Block) ]
While         = "while" "(" Expr ")" Block
For           = "for" "(" "let" Identifier "in" Expr ")" Block              (* for-in *)
              | "for" "(" Identifier "=" IntLit ";" Expr ";" Expr ")" Block  (* clásico: LÍMITE exclusivo; PASO *)
              | "for" "(" Expr (";"|",") Expr (";"|",") Expr ")" Block        (* range: start; stop; step *)
              | "for" "(" ("var"|"let") Identifier "=" Expr ";" Expr ";" Update ")" Block   (* Java *)
Update        = LValue ("++" | "--") | LValue ("+=" | "-=" | "*=" | "/=" | "=") Expr
```
Formas del `for`: `[CTL-1]` clásico `for (i = 0; LÍMITE; PASO)`: el 2º campo es un **límite
exclusivo** y el 3º el **paso** (una condición ahí es error S2/S3 del `--syntax-check`); el init
debe ser un literal entero. `[CTL-2]` estilo Java `for (var i = a; i < n; i++)`: la variable es
local del bucle y `continue` ejecuta el update. `[CTL-3]` `for (let x in lista)` itera listas;
sobre un literal escalar es error (S4).
```ebnf
Break         = "break" ";"
Continue      = "continue" ";"
Return        = "return" [ Expr ] ";"
Throw         = "throw" Expr ";"
TryCatch      = "try" Block "catch" "(" Identifier ")" Block [ "finally" Block ]
ExprStmt      = Expr ";"
```

### Expresiones
```ebnf
Expr          = OrExpr
OrExpr        = AndExpr { "||" AndExpr }
AndExpr       = NullCoalesce { "&&" NullCoalesce }
NullCoalesce  = EqExpr { "??" EqExpr }
EqExpr        = RelExpr { ("==" | "!=") RelExpr }
RelExpr       = InExpr { ("<" | "<=" | ">" | ">=") InExpr }
InExpr        = BitOrExpr [ "in" BitOrExpr ]
BitOrExpr     = BitXorExpr { "|" BitXorExpr }
BitXorExpr    = BitAndExpr { "^" BitAndExpr }
BitAndExpr    = ShiftExpr { "&" ShiftExpr }
ShiftExpr     = AddExpr { ("<<" | ">>") AddExpr }
AddExpr       = MulExpr { ("+" | "-") MulExpr }
MulExpr       = UnaryExpr { ("*" | "/" | "%") UnaryExpr }
UnaryExpr     = ("-" | "~" | "!") UnaryExpr | Postfix
Postfix       = Primary { Postfixed }
Postfixed     = "." Identifier                            (* attribute access *)
              | "?." Identifier                           (* null-safe access *)
              | "[" Expr "]"                              (* index *)
              | "(" [ ArgList ] ")"                       (* call *)
ArgList       = Expr { "," Expr }
Primary       = IntLit | FloatLit | StringLit | InterpStringLit
              | "true" | "false" | "null"
              | Identifier
              | ListLit | MapLit
              | Lambda
              | "new" Identifier "(" [ ArgList ] ")"
              | "(" Expr ")"

ListLit       = "[" [ Expr { "," Expr } ] "]"
MapLit        = "{" [ MapEntry { "," MapEntry } ] "}"
MapEntry      = StringLit ":" Expr
```

---

## 3. Sistema de tipos

### Tipos primitivos

| Tipo | Storage runtime | Notas |
|---|---|---|
| `int` | `int64` (representado internamente `VAL_INT`) | Aritmética exacta de 64 bits entre enteros (ver §3b). |
| `float` | `double` (`VAL_FLOAT`) | Sin literales con exponente. |
| `decimal` | mantisa `__int128` + escala (`VAL_DECIMAL`) | Dinero exacto: literal `1.10m`, `decimal(x)`; ver §3b y LANGUAGE_SEMANTICS §3. |
| `string` | `char*` UTF-8 opaco (`VAL_STRING`) | Concatenación con `+`. |
| `bool` | `int` 0/1 + tag `BOOL` (`VAL_INT`) | Literales `true` / `false`. |
| `datetime` | string ISO `YYYY-MM-DDTHH:MM:SSZ` (`VAL_STRING` + tag `DATETIME`) | Solo formato ISO. |
| `uuid` | string canónico 36 chars (`VAL_STRING` + tag `UUID`) | Validación con `uuid_valid`. |
| `void` | — | Solo válido como tipo de retorno de método. |
| `dynamic` | cualquiera | Escape hatch; salta validación de retorno. |

### Tipos nullable
`T?` indica que el valor puede ser `null`. La gramática lo permite en
declaraciones de atributo, tipo de retorno y parámetros.

### Compatibilidad de tipos en declaraciones
`[DECL-4]` La anotación `: Type` se **valida en runtime**: `let n : int = "x"` aborta con
`TypeError: cannot assign a value of type 'STRING' to a variable of type 'INT'`.
- `int` ↔ `int` (estricto).
- `float` acepta `int` (widening).
- `bool` acepta `int`/`bool` (alias por el storage compartido).
- `datetime` acepta `string` (alias — `now()` retorna string ISO).
- `uuid` acepta `string` (alias — `uuid_v4()` retorna string).
- `T?` acepta `T` y `null`.

### Inferencia
Cuando se omite `: Type` en una declaración, el tipo se infiere del valor
inicial. `var x = 1;` ⇒ `int`; `var s = "hi";` ⇒ `string`.

### Inmutabilidad
`[DECL-1]` `const` solo admite un literal. `[DECL-2]` `let` no se reasigna (runtime:
`cannot assign to constant variable`; `--syntax-check` lo reporta estáticamente, regla S1)
pero sí permite mutar atributos del objeto referido. `[DECL-3]` `var` se reasigna libremente.
| Keyword | Reasignable | Mutación de atributo | Valor permitido |
|---|---|---|---|
| `const` | ❌ | n/a (solo literales) | Literal compile-time |
| `let`   | ❌ | ✅ (sólo congela la referencia) | Cualquier expresión runtime |
| `var`   | ✅ | ✅ | Cualquier expresión runtime |

---

## 3b. Semántica numérica (normativa)

Una sola implementación define la aritmética (`src/te_num.h` + `te_eval_i64` en
`src/te_value.c`); el acelerador bytecode la comparte y `tests/regress/run_bc_diff.py`
verifica que ambos caminos producen la misma salida.

### Tipos de resultado y promoción
- `[NUM-1]` `int op int` (`+ - * %`) es **exacto en 64 bits** y da `int`, incluso más
  allá de 2^53 (`9007199254740993 + 2` → `9007199254740995`). El desborde **envuelve**
  en complemento a dos sin error (`9223372036854775807 + 1` → `-9223372036854775808`).
- `[NUM-2]` Si algún operando es `float`, la operación se hace en `double` y el resultado
  es `float` salvo que sea integral: **un `double` integral se almacena como `int`**
  (`1.5 + 1.5` → `3` de tipo `int`; `2.0 * 3` → `6`). Los literales conservan su tipo
  léxico: `3.0` es `float` aunque sea integral (`let b : int = 3.0` → TypeError).
- `[NUM-3]` `int / int` es **división real**: da `int` solo si es exacta (`10 / 5` → `2`,
  `9007199254740993 / 3` → `3002399751580331` exacto) y `float` si hay resto
  (`35 / 20` → `1.75`, `-7 / 2` → `-3.5`). Para división entera usar `Math.floor`,
  `Math.ceil`, `Math.trunc` o `to_int`.
- `[NUM-4]` `%` entre enteros sigue el signo del dividendo (C): `-7 % 3` → `-1`, `7 % -3` → `1`.
  Si algún operando es float el resultado es el **resto real** (`fmod`): `7.5 % 2` → `1.5`,
  `7 % 2.5` → `2` (hasta 0.1.8 se truncaban ambos operandos: `7.5 % 2` → `1`).
- `[NUM-5]` División o módulo por cero (`int`, `float` y `decimal`) **lanzan** un error de runtime
  catcheable: `ArithmeticError: division by zero.` / `ArithmeticError: modulo by zero.` llega como
  string al `catch (e)`; el resto de la expresión y del bucle se abortan y una variable ya existente
  **conserva su valor** (`q = 4 / 0` no la pisa con `0`). Sin `catch`: `Uncaught: ArithmeticError: …`
  y exit 1 (`[ERR-2]`); en `--api`, un throw no capturado en un handler responde **500**
  `{"error":"internal_error"}` y loguea `Uncaught in handler <nombre>: …` en stderr.
  (Hasta 0.1.8 devolvía `0`, avisaba por stderr y el programa seguía: un `0` plausible se colaba en
  costos/promedios sin rastro.)
- `[NUM-6]` `decimal` es exacto: `0.1m + 0.2m == 0.3m` es `true`; `decimal op int|float`
  da `decimal` (el `float` entra por su texto: `2.5m + 0.1` → `2.6`); la división conserva
  hasta 18 decimales (`"" + (1m / 3m)` → `0.333333333333333333`). El texto canónico
  (`"" + d`, `json`, `.to_string()`) conserva la escala (`2.50m` → `"2.50"`).

### Comparación
- `[NUM-7]` `int` vs `int` compara exacto en 64 bits (`9007199254740993 > 9007199254740992`
  → `true`). `int` vs `float` compara en `double` (`1 == 1.0` → `true`, `2 > 1.5` → `true`).
- `[NUM-8]` `bool` es `int` 0/1: `true == 1` → `true`, `true + 1` → `2`, `!0` → `true`.
  `null == 0` → `false`. Un string numérico se compara numéricamente con `==`
  (`"3" == 3` → `true`); `<`/`>` entre string y número **no está definido** (hoy `false`).

### Conversiones explícitas
- `[NUM-9]` `to_int(x)`: `float` **trunca hacia cero** (`1.75` → `1`, `-1.75` → `-1`);
  string numérico se parsea (`"42"` → `42`, `"4.9"` → `4`); string no numérico o vacío → `0`.
  `to_float("1.5")` → `1.5`, `to_float(2)` → `2`, no numérico → `0`.
- `[NUM-10]` `Math.*` recibe y calcula en `double` (precisión exacta hasta 2^53) y devuelve
  `int` cuando el resultado es integral y cabe en 64 bits (`Math.ceil(5000000000 / 2)` →
  `2500000000` de tipo `int`), si no `float`. `Math.round` redondea **half-up hacia +∞**
  (`Math.round(2.5)` → `3`, `Math.round(-2.5)` → `-2`); `decimal.round(n)` es half-away-from-zero.

### Formato a texto
- `[NUM-11]` Un `float` **calculado** se imprime con el **menor texto que hace round-trip**
  (`0.1 + 0.2` → `0.30000000000000004`, `1/3` → `0.3333333333333333`); si es integral se
  imprime sin decimales (`"" + 1.0` → `1`, `2.0 * 3` → `6`); magnitudes extremas usan
  exponente (`1e+20`, `1e-06`). Es el mismo formato en `print`, `$"{...}"`, `+` string,
  listas y `json()`. Excepción: un **literal** float dentro de `json()` conserva su texto
  fuente (`json({ c: 1.50 })` → `{"c":1.50}`); para formato monetario usar `decimal`.

### Bitwise
- `[NUM-12]` `& | ^ ~ << >>` operan sobre el `int64` truncado de sus operandos; `>>` es
  aritmético (`-8 >> 1` → `-4`); `1 << 40` → `1099511627776`; `1 << 63` envuelve a
  `-9223372036854775808`.

---

## 4. Semántica de evaluación

### Orden
Estricto, izquierda a derecha. Los operadores `&&` `||` `??` hacen
short-circuit (no evalúan la rama derecha si la izquierda decide el
resultado).

### Igualdad
- `==` y `!=` comparan por valor entre tipos compatibles.
- Comparación entre tipos distintos sin coerción definida → `false` para `==`.
- `null == null` ⇒ `true`. `null == X` ⇒ `false` para cualquier no-null.

### Conversión implícita
- `int → float` en operaciones aritméticas mixtas.
- `int → string` solo dentro de `print`/`println`/`$"..."`/concatenación con `+`.
- Toda otra conversión requiere builtin explícito.

### Concatenación string
El operador `+` produce string si **algún** operando es string. Una sub-expresión
aritmética se evalúa como número antes de concatenar (`"s=" + (10 + 20)` → `"s=30"`)
y se formatea según `[NUM-11]`.

### Operador `in`
- Sobre `List`: pertenencia de elemento.
- Sobre `Map`: pertenencia de **clave** (`key in m`).

### Verdad (truthiness) y lógicos
- `[CTL-4]` La condición de `if`/`while`/ternario se evalúa **como número**: `0`, `null`, cualquier
  lista o map (incluso `[1]`) y **cualquier string** (incluso `"x"`; una variable string además
  emite `Error: variable ... cannot be evaluated as a number`) son falsos; solo un número ≠ 0 es
  verdadero. Para strings comparar `s != ""`, para contenedores `.length > 0`.
- `[CTL-5]` `&&`, `||` y `!` evalúan sus operandos **como números**: un string no numérico vale 0
  (`true && "x"` → `0`, `!"x"` → `1`), a diferencia de `if ("x")`. Usar comparaciones explícitas
  (`s != ""`). El `--syntax-check` avisa (S7) cuando un literal string es operando de un lógico.
- `[CTL-6]` `while` con `break`/`continue`; ternario `c ? a : b` asociativo a la derecha (anidable).

### Ámbitos
- `[SCP-1]` Cada `fn`/método/constructor abre un frame: sus `let`/`var` y parámetros mueren al salir
  y **nunca pisan** una variable exterior homónima (la sombrean). Una fn sí puede leer y asignar
  variables globales (`var`). Leer un nombre inexistente en contexto string da `""`.
- `[SCP-2]` Un bloque `{ ... }` suelto **no** es una sentencia (error de sintaxis): los bloques solo
  existen como cuerpo de `if`/`while`/`for`/`fn`/`try`.

---

## 5. Stdlib normativa

Builtins mínimos garantizados en este nivel de la spec:

### Numéricos / matemáticos
`Math.abs(x)`, `Math.floor(x)`, `Math.ceil(x)`, `Math.round(x)`,
`Math.sqrt(x)`, `Math.pow(b, e)`, `Math.min(a, b)`, `Math.max(a, b)`.

### Strings
- `[STR-1]` `.length` es la longitud en bytes UTF-8 y funciona sobre variables y expresiones
  (`"abc".length`, `s.trim().length`, `uuid_v4().length`, `o.a.s.length`, también en comparaciones
  y en contexto string).
- `[STR-2]` Métodos: `.upper()` `.lower()` `.trim()` `.contains(sub)` `.starts_with(p)` `.ends_with(s)`
  `.index_of(sub)` (−1 si no está) `.replace(a, b)` (**todas** las ocurrencias) `.split(sep)`
  (`split("")` devuelve el string entero como único elemento) `.substring(ini, fin)` (fin exclusivo)
  `.substring(ini)` `.substr(ini, len)` `.pad_left(n, c)` `.pad_right(n, c)`. Se encadenan.
- `[STR-3]` Los strings **no se indexan** con `[]` (`s[0]` → `Error: not a list.`); usar `substr`.
- `[STR-4]` `+` con un string coerciona el otro operando: `int` dígitos, `float` según `[NUM-11]`,
  `bool` → `1`/`0`, `null` → `null`, lista → `[1, 2]`, map → JSON.
- `[STR-5]` `$"...{expr}..."` interpola variables y aritmética; `\{` `\}` son llaves literales;
  `{null}` → vacío. **No** admite llamadas a método dentro de `{}` (usar variable intermedia).
- `[STR-6]` Escapes: `\t` `\n` `\"` `\\`. `\uXXXX` **no** se interpreta (queda literal).
- `[STR-7]` `==`/`!=` comparan por valor exacto (`"10" == "10.0"` es falso); `<` `>` entre strings
  **no ordenan** (siempre falso).

### Colecciones (List)
- `[LST-1]` Índice 0-based; fuera de rango (incl. negativo) → `null`; `.length`; anidamiento
  `a[i][j]`; tipos mixtos permitidos.
- `[LST-2]` `.push(x)` y `.pop()` mutan la lista; `l[i] = v` asigna. Asignar una lista a otra
  variable crea un **alias** (misma lista); `.map`/`.filter` devuelven listas nuevas.
- `[LST-3]` Texto de una lista (`println`, `"" + l`): `[1, a, 2.5, 1, null, [1], {"k":1}]` — strings sin
  comillas, `bool` como `1`/`0`, `null`, anidados en JSON. `json(l)` es JSON estricto.
- `[LST-4]` Operadores: `.map .filter .reduce(fn, init) .where .select .sum .count .first .last
  .any .all .none` (bool) `.contains(x)` (1/0) `x in l` `.join(sep) .orderBy(fn) .thenBy(fn)
  .take(n) .skip(n) .avg .distinct .countWhere .sumBy`.
- `[LST-5]` Un método inexistente **no lanza**: `[method] unknown method 'x' on LIST value` en
  stderr y resultado `null` (no existen `max/min/indexOf`: usar `orderBy`/`reduce`/`filter`).
- `[LST-6]` `for (let x in l)` itera los elementos en orden (también sobre arrays de `json_parse`).

### Colecciones (Map)
- `[MAP-1]` Claves string (`"k"` o identificador). Acceso `m["k"]` y `m.k`, anidado con corchetes
  (`m["a"]["b"]`) o con punto (`m.a.b.c`, también `m?.a?.b` y en contexto string/numérico).
  Clave ausente → `null` (`== null` es verdadero) pero en contexto string
  da `""` (un `null` explícito da `"null"`).
- `[MAP-2]` `m["k"] = v` agrega o reemplaza; `.length`, `.keys()`, `.values()`, `.has(k)`, `"k" in m`.
- `[MAP-3]` `{}` es un map vacío (`.length` 0). Su texto (`println`, `"" + m`) es el mismo JSON de
  `json(m)`.

### JSON
- `[JSN-1]` `json(x)` / `json_stringify(x)` serializan map, lista y objeto: `true`/`false`/`null`
  literales, strings entre comillas, floats según `[NUM-11]`.
- `[JSN-2]` `json_parse(s)`: `true`/`false` llegan como **bool** y se re-serializan como `true`/`false`
  (round-trip); en contexto string valen `"1"`/`"0"` y comparan igual a `1`/`0` (`p["e"] == 1`,
  `("" + p["e"]) == "1"`), en `println` imprimen `true`/`false`. (Hasta 0.1.8 llegaban como `int` 1/0 y
  se re-serializaban `1`/`0`.) `null` → `null`; un número con decimales conserva su texto al
  re-serializar (`1.50`); los strings se conservan (`"007"`). Acceso `p["k"]`, `p["l"][i]`, `p["a"]["b"]`.
- `[JSN-3]` `json_parse("")` → `null`; texto inválido → `0` (no lanza); escalares JSON se parsean
  (`"42"` → 42, `"\"s\""` → `s`).

### Null
- `[NUL-1]` `null == null` es verdadero; `null == 0`, `"" == null` son falsos. `x ?? d` devuelve `d`
  si `x` es null. `x?.attr` sobre null → `null` (un nivel). `"" + null` → `"null"`; `println(null)`
  imprime `null`.

### Tiempo / UUID
`now()`, `now_epoch()`, `date_parse(s)`, `date_format(t, fmt)`,
`date_add(t, n, unit)`, `date_diff(a, b, unit)`,
`uuid_v4()`, `uuid_valid(s)`.

### IO
`print(x)`, `println(x)`, `read_file(path)`, `write_file(path, body)`,
`file_exists(path)`, `env(key, default?)`, `env_required(key)`.

### Red (opcional, HTTP/1.0 plano)
`http_get(url)`, `http_post(url, body)`. HTTPS soportado desde 0.0.20.

### Funciones
- `[FN-1]` `fn(a, b) => expr` y `fn(a) => { ... return v; }`. Sin `return` (o `return;`) el
  resultado es `null`. Recursión por nombre soportada.
- `[FN-2]` Closures por referencia: una fn ve y puede mutar variables externas (`var`) y captura
  las de su definidor aunque el frame haya terminado (`mk(10)` → `fn(x) => x + k`).
- `[FN-3]` Llamar con otra cantidad de argumentos es **TypeError fatal** (`'add' expects 2 arguments
  but 1 was passed`, exit 1, no capturable). `--syntax-check` lo detecta estáticamente.
- `[FN-4]` Los parámetros son locales mutables que sombrean (no pisan) variables externas.
- `[FN-5]` Las funciones son valores: asignables, almacenables en listas/maps, pasables y devolvibles.
  Llamar directamente una expresión indexada (`fs[1](5)`, `m["op"](5)`) **no** está soportado:
  asignar a una variable primero.

### Clases
- `[CLS-1]` `class X { attr : T; __constructor(...) { } M() : T { } }`. Atributos sin asignar valen
  el cero de su tipo (`int` 0, `string` ""). Se leen/escriben con `obj.attr`.
- `[CLS-2]` `extends` hereda atributos y métodos; el hijo puede redefinir métodos y su constructor
  asigna los atributos heredados (no hay `super`, ver S5).
- `[CLS-3]` Los objetos son referencias: `let q = p` aliasa; una lista de objetos permite
  `.map(fn(o) => o.attr)`, `.where`, `ps[i].attr`. Un método puede mutar `this`.
- `[CLS-4]` El tipo de retorno declarado se valida en runtime: un valor de otro tipo lanza
  `TypeError: method 'M' is declared as 'int' but returns a value of type 'string'.` (capturable con
  try/catch; sin catch → `Uncaught: ...` y exit 1).

### Async
- `[ASY-1]` `var t = async fn() => { ... };` crea una tarea; `await t` la ejecuta hasta el final y
  devuelve su valor. `sleep_async(ms)` cede el control. Varias tareas se solapan en un hilo con orden
  determinado por los `await`.
- `[ASY-2]` `await_all(t1, t2, ...)` / `await_all([t1, t2])` corre las tareas concurrentemente y
  devuelve la lista de resultados **en orden** (tanto tareas `async fn`/`go` como `spawn`/
  `lang_call_async`). (Hasta 0.1.8 con `async fn` devolvía valores vacíos: los handles de los dos
  runtimes colisionaban.)

### LINQ (cadena de métodos)
- `[LNQ-1]` `.orderBy(fn)` / `.orderByDescending(fn)` + `.thenBy(fn)` / `.thenByDescending(fn)`
  ordenan de forma estable por claves sucesivas; `.select(fn)` proyecta; `.where(fn)` filtra.
- `[LNQ-2]` Conjuntos: `.union(b)`, `.intersect(b)`, `.except(b)`, `.distinct()`.
- `[LNQ-3]` `.single()` (exactamente uno), `.first()`, `.firstOrDefault()` (null/vacío si no hay).
- `[LNQ-4]` `.any(fn)` `.all(fn)` `.none(fn)` devuelven **bool** (imprimen `true`/`false`);
  `.count()` sin argumento es la longitud; `.countWhere(fn)` y `.sumBy(fn)` agregan.
  No existe sintaxis de consulta `from x in ...`: `from` es para cargar CSV (`from "f.csv", Clase`).

---

## 6. Manejo de errores

```te
try {
    risky();
} catch (e) {
    println("caught: " + e);
} finally {
    cleanup();
}
```

- `[ERR-1]` `throw expr;` con `expr` string o número; `catch (e)` recibe el valor **como string**
  (`throw 42` → `e + 1` es `"421"`). `finally` se ejecuta siempre. Un `throw` dentro de una fn se
  propaga al `try` que la llamó. Los `try` anidan.
- `[ERR-2]` Un `throw` sin `catch` termina el programa: `Uncaught: <valor>` en stderr y exit 1.
- `[ERR-3]` Llamar una función inexistente es un **error fatal** (`Error: function 'x' not defined.`,
  exit 1) que `try/catch` no intercepta. La división por cero **sí** es una excepción (`[NUM-5]`).
- `[ERR-4]` `throw <map|lista|objeto>`: el `catch (e)` recibe el **valor** (`e["codigo"]`, `e.length`);
  si no se captura, `Uncaught:` muestra su JSON. Escalares siguen la regla `[ERR-1]` (string).
  (Hasta 0.1.8 el catch recibía `0`.)

---

## 7. Módulos

```te
import "mod_calc.te";
```

El intérprete inyecta los tokens del módulo en el flujo léxico actual
(mecanismo `yy_switch_to_buffer` de flex). No hay namespacing: todas las
clases, funciones y variables top-level pasan al scope global. La
recursión de imports está limitada a profundidad 10.

Resolución de paths:
1. Absoluto (empieza con `/`).
2. Relativo al CWD.
3. `/code/<name>` (mount Docker de `typeeasycode/`).
4. `/app/<name>` (fallback legacy).

---

## 8. Limitaciones conocidas (no normativas)

Estas son **divergencias documentadas** entre la spec ideal y la
implementación actual:

- **`println(f(x))` cuando `f` lanza**: imprime una línea vacía antes de propagar la excepción.
- **`super`**: no existe (`--syntax-check` lo reporta, regla S5). Los métodos del padre
  se heredan y se llaman sobre `this`; el constructor del hijo re-asigna los atributos.
- **HTTPS** en `http_get`/`http_post`: soportado desde 0.0.20 (Windows y Linux).

Resueltas en 0.1.8 (re-tag #4, 2026-09-20): `?.`/`.` profundo sobre maps (`[MAP-1]`), `throw` de
map/lista (`[ERR-4]`), `await_all` con `async fn` (`[ASY-2]`), `m["k"].push(x)` / `fs[1](5)`
(`[LST-2]`, `[FN-5]`), `%` con floats (`[NUM-4]`), división por cero a stderr (`[NUM-5]`; desde 0.1.9 **lanza**),
`.length` sobre el resultado de una llamada (`[STR-1]`).

---

## 9. Política de evolución

- **No se quita** sintaxis sin un major bump y un período de XFAIL en
  `tests/lang/`.
- Toda nueva keyword/operador debe acompañarse de:
  1. Token en `src/parser.l` con `yylval.sval = strdup(yytext);` si es `<sval>`.
  2. Regla en `src/parser.y` y entrada en la precedencia.
  3. Caso en `src/ast.c` `evaluate_expression` / `interpret_*`.
  4. Test en `tests/lang/<categoria>/`.
  5. Entrada en esta spec.

- **Cross-platform**: ver `/memories/repo/typeeasy.md` para la lista de
  wrappers POSIX→Windows y reglas LLP64. Todo cambio en `src/` debe
  validarse en Docker Linux **y** Windows MSYS2 antes de tag.
