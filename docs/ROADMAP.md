# TypeEasy — Roadmap de estabilización (v0.0.19 → v0.1.0)

> **Promesa única:** *La API más rápida de escribir y desplegar — un binario,
> cero runtime, cero Docker, cero `npm install`.*
>
> Regla de gobierno (**Nivel 0**): **nada entra al core que no sirva
> directamente a esa promesa.** Cerramos deuda de fiabilidad antes de agregar
> features. Sin closures generales, async, metaprogramación ni scripting de
> propósito general.

Este roadmap traduce las 5 prioridades estratégicas en releases concretos.

---

## Estado de las prioridades

| Nivel | Tema | Estado |
|-------|------|--------|
| 0 | Congelar features hasta cerrar deuda | ✅ regla activa |
| 1.1 | Erradicar gotchas semánticos | ✅ suite `tests/lang/13_gotchas/` |
| 1.2 | Blindar LLP64 con guard de CI | ✅ `scripts/audit_llp64_prototypes.sh` + workflow |
| 1.3 | Errores en inglés `file:line:` | ✅ `yyerror` + test de regresión |
| 2.1 | DB sin fallos silenciosos | ✅ ABI guard `struct_size` (falla ruidoso) |
| 2.2 | Hot-reload sólido en `--dev` | ✅ servidor de producción (`typeeasy_api`, `--hotreload`/`TYPEEASY_HOTRELOAD`) |
| 2.3 | Errores HTTP no tumban el server | ✅ setjmp/longjmp → 500; `file:line` en el cuerpo con `TYPEEASY_DEV=1` |
| 2.4 | Documentar los ~15 builtins de API | ✅ `docs/API_BUILTINS.md` |
| 3.1 | Reposicionar CSV/LINQ como soporte | ✅ narrativa (hero + READMEs de ejemplos) |
| 3.2 | README con promesa única | ✅ hero reescrito |
| 3.3 | Installer empaqueta el CLI wrapper | ✅ `.iss` + `package_windows_release.sh` |
| 4 | Crecer en el carril (migraciones, middleware, ORM liviano, WS) | 🔜 post v0.1.0 |

Leyenda: ✅ hecho · ⏳ en curso · 🔜 planificado

---

## Plan de pago de deuda del núcleo (0.0.35 → 0.1.0)

Auditoría 2026-09-08 (código + 3 meses de producción): la gestión es profesional,
el núcleo es artesanal. `src/ast.c` = 12k líneas, 9 funciones > 300 líneas
(`interpret_call_method_impl` 1.119), 75 globales `g_*`, aritmética por `double`
con casts `(int)` (enteros efectivos de 32 bits), MySQL/Postgres sin prepared
statements. Se paga **sin reescribir**, en fases con la suite en verde y release
por fase.

| Fase | Qué | Estado |
|------|-----|--------|
| 0 | **Cerrar la puerta:** `scripts/audit_core_debt.sh` en CI (global nuevo o función > 300 líneas = build rojo; la baseline solo achica), fuzz nightly 20 min. **Congelamiento de sintaxis**: ninguna regla nueva en `parser.y` hasta terminar la Fase 2; las ideas van abajo en "Features en espera". | ✅ |
| 1 | **Modelo de valores `TeValue`** (`te_value.c`): enteros `long long` end-to-end, `evaluate_expression` deja de ser el único camino (retorna `double`); migrar los 19 `(int)` / 13 `(long long)` casts; tests `int64_*`. Prerequisito de todo lo demás. | ✅ **1a** (0.0.35): lexer/AST/casts en `long long`, `db_params` y filas MySQL sin truncar, test `int64_roundtrip` (14 casos) + `int64_param`. Exacto hasta 2^53 (la evaluación sigue en `double`). ⏳ **1b** `TeValue`/i64 pleno. |
| 2 | **Partir `ast.c`** por dominio, movimiento puro sin cambio semántico (un módulo por PR): `te_interp_call.c` (despacho por receptor), `te_interp_decl.c`, `te_interp_flow.c`, `te_print.c`, `te_frames.c`. Orden: de menos a más globales tocados. | ⏳ **cut 1-2a** (0.0.35): `te_print.c`, `te_interp_flow.c`, `ast_internal.h`; `interpret_call_method_impl` 1.119→687 vía `te_cm_*` (patrón dispatch); `interpret_call_method_alone` (400 líneas muertas) eliminada. `ast.c` 12.061→10.851. Siguen: `te_interp_call.c`, `te_interp_decl.c`, resto de `impl`. |
| 3 | **Estado explícito `TeVM*`**: agrupar los globales en una struct, luego pasarla como parámetro por módulo → intérprete reentrante, tests unitarios en C, base para hilos. | ⏳ **paso A** (0.0.35): `te_vm.h` + `TeVM g_vm` con vars/var_count/return_flag/throw_flag/break_flag/continue_flag/return_node (466 sitios, 11 archivos; 14 `extern` eliminados). 🔜 paso B (`TeVM*` por parámetro). |
| 4 | Derivados: prepared statements MySQL (`mysql_stmt_*`) y Postgres (`PQexecParams`) en `db_params.c`; reemplazar `setjmp/longjmp` por propagación de error; unificar AST-walker y bytecode; tipo `decimal`. | ⏳ (0.0.35) tests de inyección (`tests/db` 11/11, `12_db/sqlite_params_injection.te`) confirman que `db_substitute_params` es *emulated prepared statements* (escape + comillas, números tipados): server-side prepares pasan a mejora, no bloqueante. `longjmp`→error depende de 3B. |

Reglas: un PR = un movimiento (los fixes van aparte con su test); suite Win+Linux
+ ASAN + `--syntax-check` del `main.te` del ERP (120 archivos) antes y después;
bench de referencia (clínica, `--workers 2`, ~1.010 rps) no puede bajar.

### Features en espera (congeladas hasta cerrar Fase 2)
- `switch`/`match`; `for (a, b in map)`; spread `...`; string multilinea.
- Registrar aquí cualquier pedido de sintaxis con el caso de uso que lo motiva.

---

## v0.0.19 — Fiabilidad del core (Nivel 1)

**Objetivo:** que el lenguaje no mienta. Lo que parece funcionar, funciona; lo
que falla, falla ruidosamente y en inglés.

- [x] **Suite de gotchas** (`tests/lang/13_gotchas/`): un test por bug conocido.
  - TRIPWIRE para bugs silenciosos (`split`, indexado inline de llamada).
  - `xfail` para los que terminan con error (closure devuelta).
  - Regresión positiva para gotchas ya resueltos (closure en `.map`, `+` con map).
- [x] **Guard LLP64 en CI**: rechaza constructores del parser/lexer sin prototipo
  en `src/*.h` (causa raíz del SIGSEGV-solo-Windows).
- [x] **Diagnósticos en inglés** formato `archivo:linea: syntax error: ...` por
  stderr, salteables desde el editor; test de regresión en `09_diagnostics/`.
- [x] **Barrido de mensajes residuales** en español de cara al usuario en
  `ast.c`/parser/bridges: traducidos a inglés y redirigidos a `stderr` (los
  `printf("Error: ...")` que llegaban a stdout). Trazas `dbg_*` quedan como
  están (son depuración, no de cara al usuario).

**Salida medible:** suite verde (45 PASS / 3 XFAIL), guard LLP64 en cada PR.

---

## v0.0.20 — Flujo de API impecable (Nivel 2)

**Objetivo:** el camino feliz "escribo endpoint → corro → funciona" sin
sorpresas, y el camino infeliz da un error claro en vez de un cuelgue o `[]`.

- [x] **DB sin silencio**: el host publica `struct_size`; un plugin obsoleto se
  rechaza ruidosamente al registrar (no más `sqlite_*` devolviendo `[]`).
- [x] **Referencia de builtins de API** (`docs/API_BUILTINS.md`).
- [x] **Hot-reload en `--dev`**: cubierto por el servidor de producción
  (`api_server/servidor_api.c`, binario `typeeasy_api`) con modelo supervisor +
  rolling reload (`--hotreload` / `TYPEEASY_HOTRELOAD`). El path `typeeasy --api`
  (`typeeasy_api_server.c`) aún no lo porta (queda como mejora opcional).
- [x] **Errores HTTP con `file:line` en modo dev**: un error de runtime se
  convierte en 500 vía setjmp/longjmp; con `TYPEEASY_DEV=1` el cuerpo incluye
  `message`, `file` y `line` (nunca en producción: por defecto sólo
  `{"error":"internal_error"}`).

**Salida medible:** demo de 4 líneas levanta una API con auth + SQLite sin
tocar Docker; un plugin viejo produce un mensaje accionable, no `[]`.

---

## v0.0.21 — Narrativa honesta (Nivel 3)

**Objetivo:** que el README prometa exactamente lo que el producto cumple.

- [x] **README con promesa única**: hero centrado en "API en un binario".
- [x] **Installer empaqueta el CLI**: `bin/typeeasy-bin.exe` + dispatchers
  `typeeasy.cmd`/`te.cmd` + árbol `cli/` y `cli/templates/`; `InstallDelete`
  limpia los `.exe` viejos que rompían `te new` (bug v0.0.10).
- [x] **Reposicionar CSV/LINQ** de "bandera principal" a "feature de soporte":
  el hero aclara que TypeEasy **no** compite con Polars/pandas, y los READMEs de
  ejemplos (`06_csv/`, `11_linq_objects/`) encuadran CSV/LINQ como soporte para
  mover datos *dentro de tus endpoints*.

**Salida medible:** un dev nuevo entiende en 30s qué es TypeEasy y para qué no.

---

## v0.1.0 — Crecer en el carril (Nivel 4)

**Objetivo:** profundizar SOLO en lo que sirve a la promesa de API. Cada ítem
debe poder explicarse como "esto hace tu API más rápida de escribir/desplegar".

- [ ] **Migraciones de DB**: `te migrate up` / `te migrate down` con archivos
  versionados; estado en una tabla `__migrations`.
- [ ] **Middleware por ruta vía decoradores**: `@cache`, `@rate_limit`,
  `@cors` declarativos por handler, en la línea de `@auth`.
- [ ] **Relaciones ligeras de ORM**: `has_many` / `belongs_to` sobre el bridge
  existente, sin convertirse en un ORM completo.
- [x] **WebSockets de primera clase**: handler `[WebSocket("/canal")]` con
  cláusulas `on_open/on_message(msg)/on_close`, reusando `te_websocket`.
  Los handlers planos legacy siguen funcionando (sin cláusulas de ciclo de vida).

**Fuera de alcance (explícito):** closures generales, async/await,
metaprogramación, scripting de propósito general. Si un pedido no acelera
"escribir y desplegar una API", queda fuera del core.

---

## Cómo se verifica cada release

```bash
# Suite de conformidad del lenguaje (fuente de verdad)
python tools/te-test/run_tests.py tests/lang --bin bin/typeeasy.exe   # nativo
python tools/te-test/run_tests.py tests/lang --docker                 # CI

# Guard estático LLP64 (segundos, sin compilar)
bash scripts/audit_llp64_prototypes.sh
```

Baseline al cierre de v0.0.19: **45 PASS / 0 FAIL / 3 XFAIL**, guard LLP64 OK.
