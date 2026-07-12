# TypeEasy `v0.0.30` (retag) — Fix del `SIGSEGV`: stack overflow en handlers grandes

**De:** equipo runtime TypeEasy
**Para:** equipo ERP (reportantes)
**Fecha:** 2026-07-12
**Ref:** reporte *"`SIGSEGV` / empty reply intermitente en endpoints pesados; recursión sin guardia en el evaluador (frame `0x41bd4a`)"* — el que dejamos abierto como **#5** ("único bloqueante real").

---

## TL;DR

- **#5 → RESUELTO.** No era una recursión "de datos" ni un `NULL-deref` de `json_parse`: era **stack overflow del worker**. El intérprete recorría el **cuerpo de cada handler de forma recursiva** (un nivel de pila de C por cada *statement* del bloque, y por cada rama `else if`). Un handler grande (el de **cierre de caja**, ~34–42 columnas + HTML de ~7 KB) pasaba el límite de pila del hilo de civetweb → **SIGSEGV silencioso** (empty reply, sin log; el worker moría y systemd reiniciaba).
- **El símbolo `0x41bd4a` es `interpret_ast`** (el evaluador), no `interpret_dataset`. Ese nombre era una **atribución errónea de `-O3`** (funciones chicas inlineadas dentro del rango de símbolo de otra). Lo confirmamos con `nm -n` sobre el binario de producción y con un backtrace **exacto** bajo AddressSanitizer.
- **Fix:** los dos recorridos ahora son **iterativos** (misma técnica que ya usaba `free_ast`). Se elimina la recursión no acotada **por construcción** — sin límites artificiales que rompan recursión legítima.
- **Entrega:** **retag de `v0.0.30`** (mismo número de versión, binario nuevo con el fix). Ver *Entrega y verificación*.

---

## Qué era realmente (causa raíz)

El backtrace de producción mostraba `~34` frames del mismo símbolo y luego caía. Con el binario **no-stripped** de producción y `nm -n` vimos que `0x41bd4a` cae **dentro de `interpret_ast`** (que arranca en `0x41bb40`); `interpret_dataset`/`interpret_match` estaban **inlineados** en su `switch` por `-O3`. El backtrace real (ASan `-O1`, símbolos exactos) es una escalera:

```
#0 interpret_ast              ast.c
#1 interpret_statement_list    ast.c   <- interpret_ast(node->left)
#2 interpret_ast              ast.c   <- case NK_STATEMENT_LIST
#3 interpret_statement_list    ast.c
...  1 nivel de pila por cada statement del bloque
```

La gramática es *left-recursive* (`statement_list: statement_list statement`), así que un bloque de **N** statements queda anidado por `->left` y `interpret_statement_list` lo bajaba **recursivamente** → **la profundidad de `interpret_ast` == número de statements del handler**. Lo mismo con `if / else if / … / else`: cada `else if` es otro nodo `IF` encadenado por `->next`, y `interpret_if` recursaba una vez por rama.

Esto explica **todo** lo que reportaron:

| Observación de ERP | Explicación |
|---|---|
| El handler de **cierre** crashea; el de **test** no | El de cierre tiene **muchos más statements** → más profundidad → cruza el límite de pila del worker. |
| "Crashea al 2º/3er envío" | Bajo carga, el worker que atiende el handler grande es el que revienta; no es acumulación de heap. |
| El workaround del dev (un `concat` final en vez de ~30 `html = html + …`) **ayudaba** | ~30 statements **menos** en el bloque = ~30 niveles de pila menos → se quedaba por debajo del límite. |
| "`~34` frames" en el dump | El crash handler **trunca** el backtrace; la recursión real era mucho más profunda. |

Irónicamente, `free_ast` (justo arriba en el mismo archivo) ya documentaba y evitaba esta misma clase de bug ("iterar… no recursión… para evitar stack overflow"); a `interpret_statement_list` e `interpret_if` les faltaba el mismo tratamiento.

---

## El fix

`src/ast.c` — commit **`2ee1027`** (incluido en el retag de `v0.0.30`).

- **`interpret_statement_list`**: recorre el espinazo `->left` de forma **iterativa** con un stack en **heap** (buffer inline de 256 para el caso común, crece solo en bloques enormes). Orden de ejecución y chequeos de `return/break/continue/throw` **idénticos** al original.
- **`interpret_if`**: recorre la cadena `else if` en un **bucle** en vez de `interpret_ast(node->next)` recursivo.

Diff: **+61 / −9** líneas, acotado a esas dos funciones. Sin límites de profundidad artificiales.

> **Sobre el pedido de "límite de profundidad + HTTP 5xx en vez de SIGSEGV":** optamos por la solución de raíz (eliminar la recursión) en lugar de un tope numérico, porque un límite fijo puede cortar recursión legítima (funciones recursivas de usuario con cuerpos anidados). Resultado equivalente y más seguro: **ya no hay SIGSEGV** por tamaño de handler. Nota: cadenas `else if` de **>~1250 ramas** ni siquiera llegan al runtime — el parser (bison) las rechaza antes con `memory exhausted`.

---

## Verificación (AddressSanitizer, en la VM; producción intacta)

| Prueba | Antes | Después |
|---|---|---|
| Bloque de 2000 / 12000 / 60000 statements (CLI) | `stack-overflow` | OK (`60000`) |
| Cadena `else if` de 1000 / 1200 ramas | overflow en runtime | OK (`999`) |
| **Handler de 4000 statements por el worker HTTP** (civetweb, ruta real de prod) | SIGSEGV | `{"total":4000}` **`[200] × 8`**, sin crash |
| Suite `tests/lang` completa | — | **PASS=82, FAIL=0** |
| Control-flow (`return`/`break`/`continue`/`throw` a mitad de bloque + anidados) | — | correcto (salida `A702TC1234`) |

Todo en puertos/DB no-productivos; el `erp-backend` (`:8090`) no se tocó.

---

## Entrega y verificación

Es un **retag de `v0.0.30`** (mismo número, binario nuevo). Bájenlo de **GitHub Releases `v0.0.30`** cuando el CI termine de reconstruir.

> **Los checksums cambian** respecto al `v0.0.30` anterior (binario distinto). Nuestros builds no son byte-reproducibles, así que **no se fíen del hash**: usen el nuevo `SHA256SUMS-0.0.30.txt` del release (se regenera solo por CI) **y** el check funcional de abajo.

**Check funcional de #5** (confirma que es el build con el fix — antes tumbaba el proceso, ahora imprime el total):

```bash
# genera un script con un bloque grande y lo corre; con el fix imprime 8000, antes: crash
python3 - <<'PY' > big.te
n=8000
print("var x = 0;")
for _ in range(n): print("x = x + 1;")
print("print(x);")
PY
typeeasy big.te        # fix OK => imprime 8000 ; buggy => "Segmentation fault"
```

Y el check funcional de **#1** (comparación en asignación) sigue valiendo como sello del `0.0.30`:

```typeeasy
var a=0; a=(5==5);
var b=0; b=(7>3);
print("check a=" + ("" + a) + " b=" + ("" + b));   // => "a=1 b=1"
```

---

## Estado final de todos los reportes

| # | Manifestación | Estado |
|---|---|---|
| 1 | `==` / índice / ternario false en asignación | ✅ Arreglado y liberado en `v0.0.30` (`7337eb3`) |
| 2 | `msg = r` raw en `for-in` | ⛔ No reproduce en `0.0.30` |
| 3 | `TIMESTAMP` @param −6h | ⚠️ No es del runtime; `session time_zone` cross-máquina |
| 4 | aliasing tras `db_query` | ⛔ No reproduce; ASan limpio |
| **5** | **`SIGSEGV` / empty reply en handlers pesados** | ✅ **Arreglado** (`2ee1027`) — stack overflow por recorrido recursivo; ahora iterativo. Incluido en el **retag de `v0.0.30`**. |

**El único bloqueante (#5) queda cerrado.** Actualicen al binario `v0.0.30` recién reconstruido y confirmen con el check funcional de #5 en su handler de cierre de caja.
