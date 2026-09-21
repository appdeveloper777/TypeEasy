# Changelog

Formato: por release, tres bloques. **Cambios de comportamiento** lista todo lo que
un script existente puede observar distinto (salida, errores, tipos), con el test
que fija la conducta nueva. Política: `docs/VERSIONING.md`.

## 0.1.8 — 2026-09-19 (re-tag 2026-09-20 con los fixes WebSocket)

> El tag `v0.1.8` se movió el 2026-09-20 cuatro veces (3fa67c6 → 01db86d → eeb4894 → 5bef7ac →
> re-tag #4 "gotchas") porque el build original crasheaba en producción con WebSocket +
> HTTP concurrentes y los siguientes aún permitían que WebSockets zombis colgaran el
> servidor. Los assets del release se regeneraron; si descargaste 0.1.8 antes de esa
> fecha, verificá el SHA.

### Cambios de comportamiento (re-tag #4, 2026-09-21 — "todos los gotchas abiertos")
- **`load_native` rechaza plugins sin handshake de ABI.** Un `libte_*.so`/`.dll` compilado
  contra otra disposición de `TEHostAPI` se registraba sin error y después TODAS sus builtins
  devolvían `[]`/`0` en silencio (JunX 2026-09-20: plugin sqlite del paquete 0.0.13 bajo un
  host 0.1.6 → `/api/register` y `/api/ranking` devolvían `[]` con la BD intacta). Ahora el
  plugin exporta `te_module_abi_version()`/`te_module_api_size()` (macro
  `TE_PLUGIN_EXPORT_ABI()`); si faltan o no coinciden con el host, `load_native` devuelve 0 y
  loguea `[load_native] REJECTED '<name>' (<path>): …`. Escape: `TYPEEASY_ALLOW_LEGACY_PLUGINS=1`.
  **Consecuencia:** al actualizar el binario hay que instalar el plugin del MISMO release
  (`plugins/sqlite/`); los instaladores/tarballs ya lo traen. Test
  `tests/regress/run_plugin_abi_handshake.sh`.
- **Los workers de `--workers N` mueren con el padre** (`PR_SET_PDEATHSIG(SIGTERM)`, Linux) y el
  padre escala a `SIGKILL` a los que no salen tras 5 s. Antes un `kill -9` al padre dejaba
  huérfanos compartiendo el puerto (`SO_REUSEPORT`) y el siguiente arranque convivía con
  binarios viejos (gotcha ERP B8 "intermitencia"). Test `tests/regress/run_workers_pdeathsig.sh`.
- **`throw <map|lista|objeto>`: el `catch (e)` recibe el valor** (`e["codigo"]`, `e.length`);
  antes recibía `0`. Strings y números siguen llegando como string (`[ERR-1]`); `Uncaught:`
  muestra el JSON. Nueva regla `[ERR-4]` (`tests/lang/08_errors/throw_map_value.te`).
- **`await_all(...)` con tareas `async fn`/`go()` devuelve los resultados reales** (antes una
  lista de vacíos: los dos runtimes async compartían el mismo espacio de ids enteros). Los
  handles de fibra ahora son `100000 + índice`; `await_all`/`await_task` despachan al runtime
  dueño. Nueva regla `[ASY-2]` (`tests/lang/14_async/asy02_await_all_results.te`).
- **`%` con operandos float usa `fmod`** (`7.5 % 2` → `1.5`, `7 % 2.5` → `2`); con enteros no
  cambia. `[NUM-4]` actualizada (`num04_modulo_truncated_sign.te`).
- **División/módulo por cero escriben el mensaje en stderr** (antes stdout, ensuciando la
  salida de scripts parseados por herramientas); el resultado sigue siendo `0` y el programa
  continúa. `[NUM-5]` actualizada.
- **Acceso anidado con punto sobre maps en cualquier contexto**: `o.a.b.c`, `o?.a?.b`,
  `p.u.n` (json_parse) funcionan en `let`, concat, comparaciones, `print`/`println` (antes
  solo en `let`; en concat daba `""`, en `print` era syntax error). `[MAP-1]`
  (`17_collections/map03_nested_dot_access.te`).
- **`.length` sobre el resultado de una llamada o atributo anidado**: `uuid_v4().length`,
  `concat(a,b).length`, `o.a.s.length`, `json_parse(s).length` (antes `Error: object
  'uuid_v4' not found` y `0`/`""`). `[STR-1]` (`16_strings/str06_length_on_call.te`).
- **`m["k"].push(x);`, `lista[i].pop();`, `fs[1](5)`, `h["dup"](3)`** dejan de ser syntax
  error: método sobre un elemento indexado (muta el contenedor real) y llamada de un lambda
  guardado en lista/map, como sentencia y como expresión. `[LST-2]`
  (`17_collections/lst02_indexed_method_call.te`).
- **Envelope `sql_exec(..., true)` con sqlite**: un exec fallido que devuelve `-1` ahora produce
  `{ success:false, error:<sql_last_error> }` en vez de `success:true, data:-1`.
- **`json_parse` conserva los booleanos** (`[JSN-2]`): `true`/`false` llegan como **bool** y
  `json_stringify(json_parse(x))` los re-serializa como `true`/`false` (antes `1`/`0`: sin round-trip,
  un JSON reenviado a un tercero —PAC, Yappy, Facturapi— o guardado en una columna JSON salía con
  `1/0`). **Nada cambia para el código existente**: en contexto string valen `"1"`/`"0"`
  (`("" + p["k"]) == "1"` y `("" + p.k)` siguen igual —este último antes daba `"true"` sobre maps
  literales; ahora también `"1"`—), comparan igual a `1`/`0`/`true`, se bindean a SQL como `1`/`0`
  (`TINYINT`), y el model binding los coerciona como antes (`string` → `"1"`, `int` → `1`, `bool` OK).
  `println(p["k"])` imprime `true`/`false` (antes `1`/`0`). Tests `17_collections/jsn02_parse_types.te`,
  `jsn03_bool_roundtrip.te`.

### Cambios de comportamiento
- **WebSockets ociosos o muertos ya no agotan el pool de workers.** Cada WebSocket
  abierto ocupa un hilo worker de civetweb durante toda su vida. Hasta ahora el servidor
  arrancaba con `num_threads=8` fijo y sin `websocket_timeout_ms` ni ping/pong: bastaban
  8 sockets medio muertos (el navegador desapareció detrás de un proxy con `read_timeout`
  de 24 h) para que el proceso siguiera vivo pero **no atendiera ninguna petición HTTP**
  (caída del ajedrez en JunX, 2026-09-20: 8 conexiones ESTABLISHED de 73–116 min y cero
  requests). Ahora: `TYPEEASY_NUM_THREADS` (default **64**, hilos perezosos), PING cada
  `TYPEEASY_WS_TIMEOUT_MS` (default 30000) de inactividad y cierre del socket tras 5 PINGs
  sin PONG (`TYPEEASY_WS_PING_PONG=0` lo desactiva). Los navegadores contestan PONG solos;
  un handler `.te` no ve los frames de control. Test `tests/regress/run_ws_idle_starvation.py`
  (0.1.8: 9.º handshake falla y `/ok` se cuelga; fix: 12 WS ociosos + `/ok` 200, sockets
  muertos cerrados en ~6×timeout, un WS vivo sobrevive).
- **WebSocket y HTTP comparten UN solo lock de intérprete.** Hasta el primer build de
  0.1.8 los callbacks WS (`connect/ready/data/close`) ejecutaban el handler `.te` bajo
  un mutex propio, en paralelo con un handler HTTP en curso: el reset de fin de request
  del hilo WS borraba las variables del handler HTTP y cerraba sus conexiones DB
  request-scoped (**SIGSEGV en `mysql_stmt_prepare`**, demo-restaurante 2026-09-20; el
  bug existía desde que hay WebSocket nativo, 0.0.2x). Ahora los callbacks WS toman el
  invoke lock del servidor (orden `invoke → g_lock`, sin deadlock con `ws_broadcast`).
  Consecuencia observable: un handler WS espera su turno FIFO detrás de los HTTP en
  curso (`tests/regress/run_ws_http_lock.py`).
- Un error fatal de runtime dentro de un handler WS ya **no termina el proceso**
  (`exit(1)`): se aborta solo ese handler, se limpia el estado por request y la
  conexión/proceso siguen vivos (mismo test, fase 2).
- `ws_send`, `ws_subscribe` y `ws_broadcast` aceptan **cualquier expresión string**
  (`concat(...)`, `"a" + b`, ternario, `obj.attr`). Antes solo resolvían un literal o
  un identificador y `ws_send(concat("echo: ", msg))` —el ejemplo de la propia
  documentación— no enviaba nada en silencio (`run_ws_http_lock.py`, fase 1: frame
  `hello: <who>` construido con `concat`, canal con `+`).

### Interno
- `te_interp_lock_enter/leave` (`src/ast.c`) para hilos no-HTTP que ejecutan `.te`;
  `ws_invoke_guarded` (`src/typeeasy_api.c`) instala el `setjmp` de recuperación en
  la ruta WS. Test bloqueante en `scripts/run_asan_tests.sh` y `scripts/_regress_full.sh`.
- Los builtins WS usan `get_node_string()` (camino único de evaluación) en vez de
  `te_arg_string()`; el resto de builtins HTTP (`request_query`, …) no cambia.

### Cambios de comportamiento (build original 2026-09-19)
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
