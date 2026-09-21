# Changelog

Formato: por release, tres bloques. **Cambios de comportamiento** lista todo lo que
un script existente puede observar distinto (salida, errores, tipos), con el test
que fija la conducta nueva. Política: `docs/VERSIONING.md`.

## 0.1.10 — 2026-09-21 (seguridad y robustez; audita e incorpora el PR #8 de @atheneox)

### Seguridad
- **`response_header(nombre, valor)` sanea CR/LF y caracteres de control** en nombre y valor
  (CWE-113, *HTTP response splitting*): un valor que venga de la petición ya no puede inyectar
  cabeceras ni partir la respuesta. Suite `tests/api/security_api.api.json`.
- **Launchers Windows `cli/typeeasy.cmd` e `installer/windows/typeeasy.cmd`**: los argumentos van
  como `argv` separados a `bash -c '… "$@"'` en vez de pegarse al texto del script; un nombre con
  `$(...)`/backticks ya no se ejecuta como comando. Verificado: `version`, `new "mi app"`, passthrough.
- **`te_xlsx.c` (extractor ZIP/XLSX)**: una entrada *stored* con `comp_sz ≠ uncomp_sz` ya no lee
  fuera del buffer (heap OOB read con un archivo malicioso).
- **Plugin Mongo `mongo_query`**: el filtro es *whitelist* (solo `campo: escalar` planos); una
  clave `$op`/con `.` o un valor documento/array/regex rechaza la query entera (`[]` + stderr) en
  vez de correr un filtro debilitado (inyección NoSQL tipo `{"password":{"$ne":null}}`). Escape
  hatch para scripts que arman sus propios operadores: `TE_MONGO_ALLOW_OPERATORS=1`.
- **Ejemplo WhatsApp/Gemini (`tools/whatsapp_adapter`, `docker-compose.yml`)**: `/waha_webhook`
  exige `WAHA_WEBHOOK_SECRET`, sin `WAHA_API_KEY` por defecto hardcodeada, y los servicios internos
  (`gemini` :5003, `agent` :8081/:8082, sin auth propia) dejan de publicarse al host. `web/dev_server.py`
  (editor local) exige mismo origen + `Content-Type` en los endpoints que escriben.

### Robustez / corrección
- **Atributos de clase `int`: `o.v = 9007199254740993` es exacto** (pasaba por `double` y quedaba
  `…992`); `println(o.v)` imprime 64 bits (usaba `%d`). Test `01_types/int64_exact.te`.
- `list.contains(x)` con `x` lista/map liberaba el nodo compartido (use-after-free) → solo libera
  escalares frescos. Test `17_collections/lst07_contains_container_arg.te`.
- `l[i] = v` / `m["k"] = v` liberan el escalar reemplazado (un leak por asignación en `--api`);
  los contenedores reemplazados siguen siendo alias válidos. Test `lst08_index_reassign_many.te`.
- `clone_object` (push de objeto a lista, `where`) no copiaba los atributos objeto → puntero sin
  inicializar para un `Q?` nulo. Test `05_oop/object_optional_attr_clone.te`.
- `xml(obj)` / `xml(lista)`: el buffer crece con el texto (antes `malloc(4096)` + `strcat` →
  heap overflow con un atributo de texto ordinario > 4 KB). Test `05_oop/xml_long_attribute.te`.
- `print/println(o.attr)` con `o` objeto nulo → `null` en vez de NULL-deref.
- `request_cookie()` ya no trunca valores > 1 KB (JWT/SSO). `request_headers()`,
  `request_queries()`, `request_params()` devuelven **siempre JSON válido**: un par que no entra en
  el buffer se descarta entero en vez de cortarse a medias. Suite `security_api`.
- `for (x in listaDeObjetos)` liberaba el wrapper de cada iteración (leak). `fprintln` de enteros
  usa `%lld`. `plot()` verifica `fopen`/`popen`. `/api/discover` (`typeeasy_embedded_discover`)
  crece el buffer en vez de desbordar los 64 KB fijos con muchas rutas (el ERP tiene ~1000).
- Windows sin Postgres: `postgres_connect()` devuelve `-1` y `postgres_query()` `{"error":…}` en vez
  de dejar `__ret__` viejo. SQL Server: se borra el `.conf` temporal de FreeTDS por conexión.
- `api_server/te_websocket.c`: `realloc` chequeados. `api_server/servidor_api.c` (servidor de la
  imagen Docker, no el binario `--api`): lock de invocación compartido con WS + recuperación
  `setjmp` (un fatal en un handler → 500, no cae el proceso), reset de estado por intento de
  match, reloj monotónico; `src/Dockerfile` vuelve a compilar (`bytecode.c`/`strvars.c` no
  existían; faltaban `te_evloop.c`/`te_decimal.c`).

## 0.1.9 — 2026-09-21

### Cambios de comportamiento
- **División y módulo por cero lanzan** (`[NUM-5]`): `a / 0` y `a % 0` con `int`, `float` o
  `decimal` producen un error de runtime catcheable —`ArithmeticError: division by zero.` /
  `ArithmeticError: modulo by zero.` llega como string al `catch (e)`—. El resto de la
  expresión y del bucle en curso se abortan (también en el acelerador bytecode) y una variable
  ya existente **conserva su valor** (`q = 4 / 0` no la pisa con `0`). Sin `catch`, el
  programa termina con `Uncaught: ArithmeticError: …` y exit 1. Hasta 0.1.8 devolvía `0`,
  avisaba por stderr y seguía: un `0` plausible se colaba en costos/promedios sin rastro.
  Tests `15_numeric/num05_div_zero_throws.te`, `num05_div_zero_uncaught.te`,
  `08_errors/runtime_div_zero.te`.
- **`--api`: un `throw` no capturado dentro de un handler responde 500**
  `{"error":"internal_error"}` y loguea `Uncaught in handler <nombre>: <mensaje>` en stderr.
  Hasta 0.1.8 respondía **200 con body vacío y sin rastro en el log** (cualquier `throw` sin
  `try`, incluido el nuevo ArithmeticError, era invisible). En handlers WebSocket solo se
  loguea (no se envía frame). Test `tests/api` caso `uncaught_throw_500`.
- **Método inexistente sobre lista/map lanza** (`[LST-5]`): `nums.max()` o `m.contains("a")`
  producen `TypeError: unknown method 'max' on list value.` catcheable (sin `catch`: `Uncaught:` y
  exit 1; en `--api`, 500). Hasta 0.1.9 avisaba en stderr y devolvía `null`, que como falsy
  escondía el typo (`if (res.contains("error"))` sobre un envelope seguía como si no hubiera error).
  Los métodos sobre instancias de clase y strings ya eran fatales; no cambian. Tests
  `17_collections/lst05_unknown_method.te`, `lst05_unknown_method_uncaught.te`.
- **`print`/`println` no emiten nada si el argumento lanza** (`[ERR-6]`): `println(f(x))` con `f`
  que hace `throw` propaga la excepción con stdout intacto (antes imprimía una línea vacía). Cubre
  fn, método, concatenación, literal de lista y expresión numérica (`println(10 / 0)`). Test
  `19_errors/err06_println_throw_no_output.te`.

### Empaquetado (.deb) — cierra issue #7
- **`typeeasy-api@.service` arranca out-of-the-box.** La unit traía `WorkingDirectory=${TYPEEASY_APIS_DIR}`
  y systemd **no expande variables** ahí: quedaba "bad unit file setting" y nunca arrancaba (5.º bug
  encima de los 4 del issue, que ya estaban corregidos: `ExecStart` al wrapper real, unit en
  `/lib/systemd/system`, `/etc/default/typeeasy-api` como conffile, `ProtectHome` comentado). Ahora el
  `cd` lo hace `/bin/sh -c` con `TYPEEASY_APIS_DIR` (raíz del proyecto, default `/opt/typeeasy`;
  configurable por `/etc/default/typeeasy-api[@puerto]`). Verificado con systemd real: `active`,
  health 200 como `www-data`, restart/stop limpios.
- **`Depends` completos:** faltaban `libgomp1` (OpenMP) y `zlib1g` → en un Ubuntu 24.04 limpio el
  binario no cargaba (`libgomp.so.1: cannot open shared object file`). `libssl3 | libssl3t64`,
  `libcurl4 | libcurl4t64`.
- Tests `let_reassign_fails` y `syntax_error` dejan de ser `xfail`: afirman exit 1 + mensaje en
  stderr (`[ERR-5]` nueva). Suite lang sin XFAIL.

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
