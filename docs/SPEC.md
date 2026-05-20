# TypeEasy — Especificación mínima del lenguaje (v1.0.0)

Este documento define la **superficie estable del lenguaje** que el intérprete
soporta hoy y contra la cual `tests/lang/` valida en CI. La fuente normativa
de la sintaxis es `src/parser.l` (lexer) y `src/parser.y` (gramática Bison).
Si esta especificación diverge del parser, **el parser manda** y este archivo
debe corregirse.

Para semántica detallada de declaraciones y tipos, ver
[LANGUAGE_SEMANTICS.md](LANGUAGE_SEMANTICS.md). Para el modelo de ejecución
(interpretador árbol vs. bytecode JIT), ver
[EXECUTION_MODEL.md](EXECUTION_MODEL.md).

---

## 1. Convenciones léxicas

### Codificación
- Los archivos `.te` deben ser **ASCII** fuera de strings literales.
  El lexer rechaza bytes UTF-8 incluso dentro de comentarios.
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
```ebnf
IntLit    = DecInt | HexInt | BinInt
DecInt    = digit { digit | "_" }                   (* "1_000_000" válido *)
HexInt    = "0x" hex { hex | "_" }                  (* "0xDEAD_BEEF" *)
BinInt    = "0b" ("0"|"1") { "0" | "1" | "_" }
FloatLit  = digit { digit } "." digit { digit }     (* sin exponente *)
```

### Literales string e interpolación
```ebnf
StringLit       = '"' { char | escape } '"'
InterpStringLit = '$"' { char | "{" Expr "}" | "\{" | "\}" } '"'
```

### Palabras reservadas
```
let var const class extends new this return
if else while for in break continue
true false null
try catch finally throw
fn import dynamic
int string float bool datetime uuid void
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
For           = "for" "(" "let" Identifier "in" Expr ")" Block
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
| `int` | `int64` (representado internamente `VAL_INT`) | Operaciones aritméticas widening a `float` si algún operando es float. |
| `float` | `double` (`VAL_FLOAT`) | Sin literales con exponente. |
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
| Keyword | Reasignable | Mutación de atributo | Valor permitido |
|---|---|---|---|
| `const` | ❌ | n/a (solo literales) | Literal compile-time |
| `let`   | ❌ | ✅ (sólo congela la referencia) | Cualquier expresión runtime |
| `var`   | ✅ | ✅ | Cualquier expresión runtime |

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
El operador `+` produce string si **algún** operando es string.
Sub-expresiones aritméticas se materializan via `evaluate_expression`
y se formatean como `%lld` (enteros) o `%g` (floats).

### Operador `in`
- Sobre `List`: pertenencia de elemento.
- Sobre `Map`: pertenencia de **clave** (`key in m`).

---

## 5. Stdlib normativa

Builtins mínimos garantizados en este nivel de la spec:

### Numéricos / matemáticos
`Math.abs(x)`, `Math.floor(x)`, `Math.ceil(x)`, `Math.round(x)`,
`Math.sqrt(x)`, `Math.pow(b, e)`, `Math.min(a, b)`, `Math.max(a, b)`.

### Strings
`"abc".upper()`, `.lower()`, `.trim()`, `.contains(sub)`, `.split(sep)`,
`.length`.

### Colecciones (List)
`.length`, `.push(x)`, `.pop()`, `.map(fn)`, `.filter(fn)`, `.reduce(fn, init)`,
`.forEach(fn)`, `.find(fn)`, `.any(fn)`, `.every(fn)`.

### Colecciones (Map)
`.keys()`, `.values()`, `.has(k)`, `.remove(k)`.

### JSON
`json_stringify(x)`, `json_parse(s)`.

### Tiempo / UUID
`now()`, `now_epoch()`, `date_parse(s)`, `date_format(t, fmt)`,
`date_add(t, n, unit)`, `date_diff(a, b, unit)`,
`uuid_v4()`, `uuid_valid(s)`.

### IO
`print(x)`, `println(x)`, `read_file(path)`, `write_file(path, body)`,
`file_exists(path)`, `env(key, default?)`, `env_required(key)`.

### Red (opcional, HTTP/1.0 plano)
`http_get(url)`, `http_post(url, body)`. No soporta HTTPS sin plugin externo.

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

- `throw expr;` levanta una excepción cuyo `expr` se coerce a string.
- El handler `catch (e)` recibe el mensaje en `e`.
- `finally` se ejecuta siempre (también tras `return`).

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

- **UTF-8 en `.te`**: rechazado fuera de strings literales.
- **`?.` profundo**: solo el primer nivel; encadenado no implementado.
- **`super`**: no implementado. El constructor del hijo debe re-asignar
  atributos heredados.
- **`expression_list` y BINOPs**: pasar `a + b` directo como argumento a
  ciertos builtins multi-arg puede romperse por colisión de `->right`
  con el AST del BINOP. Workaround: usar variable intermedia.
- **HTTPS** en `http_get`/`http_post`: no soportado (solo HTTP/1.0).

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
