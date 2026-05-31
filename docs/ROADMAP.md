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
| 2.2 | Hot-reload sólido en `--dev` | ⏳ pendiente |
| 2.3 | Errores HTTP no tumban el server | ✅ setjmp/longjmp → 500 (revisar `file:line` en dev) |
| 2.4 | Documentar los ~15 builtins de API | ✅ `docs/API_BUILTINS.md` |
| 3.1 | Reposicionar CSV/LINQ como soporte | ✅ narrativa (hero + READMEs de ejemplos) |
| 3.2 | README con promesa única | ✅ hero reescrito |
| 3.3 | Installer empaqueta el CLI wrapper | ✅ `.iss` + `package_windows_release.sh` |
| 4 | Crecer en el carril (migraciones, middleware, ORM liviano, WS) | 🔜 post v0.1.0 |

Leyenda: ✅ hecho · ⏳ en curso · 🔜 planificado

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
- [ ] **Hot-reload en `--dev`**: al cambiar el `.te`, recargar sin reiniciar a
  mano; invalidar AST y workers de forma segura.
- [ ] **Errores HTTP con `file:line` en modo dev**: hoy un error de runtime se
  convierte en 500 vía setjmp/longjmp; exponer la ubicación en el cuerpo cuando
  `--dev` está activo (nunca en producción).

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
- [ ] **WebSockets de primera clase**: handler `[Ws("/canal")]` con
  `on_open/on_message/on_close`, reusando `te_websocket`.

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
