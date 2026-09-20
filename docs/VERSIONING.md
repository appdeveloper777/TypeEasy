# Política de versionado de TypeEasy

Versión vigente: **0.x** (pre-1.0). Este documento fija qué garantiza cada tipo
de release y qué tendría que cumplirse para publicar 1.0.

## 1. Esquema

`MAJOR.MINOR.PATCH` (SemVer adaptado a la etapa 0.x):

| Componente | En 0.x significa |
|---|---|
| PATCH (0.1.**7** → 0.1.**8**) | Correcciones y features aditivas. **Puede** cambiar comportamiento observable si el cambio está listado en `CHANGELOG.md` → "Cambios de comportamiento" y tiene un test en `tests/lang` que fija la conducta nueva. |
| MINOR (0.**1** → 0.**2**) | Cambios de sintaxis, del modelo de ejecución o de la API de builtins que rompen scripts existentes de forma deliberada. Requieren nota de migración en el CHANGELOG. |
| MAJOR (0 → 1) | Ver §3. |

No se publican builds con versión sin tag: el binario toma la versión del tag
(`CFLAGS_EXTRA=-DTYPEEASY_VERSION=...` en CI). Los builds de desarrollo se
identifican como `X.Y.Z-dev`.

## 2. Qué garantiza un release 0.x

Lo que **sí** está garantizado para cualquier `0.1.z`:

1. **Spec normativa = tests.** Toda regla con ID en `docs/SPEC.md` (`[NUM-3]`,
   `[CTL-4]`, …) tiene al menos un test `// spec: ID` en `tests/lang`, y
   `tools/te-test/spec_coverage.py --strict` es paso obligatorio de CI. Si un
   release cambia una regla, cambia la spec, el test y el CHANGELOG **en el mismo
   commit**.
2. **Walker y bytecode equivalentes.** `tests/regress/run_bc_diff.py` (toda la
   suite con y sin `TYPEEASY_NO_BC=1`) debe dar `0 differ`. Adicionalmente, antes
   de instalar en producción se corre el A/B del ERP real (35 endpoints,
   `deploy/te-ab-bytecode.sh`) y el A/B binario anterior vs nuevo, ambos con
   `distintos=0`.
3. **Batería de release** (todo en verde antes de taggear): `tests/lang`,
   `tests/api`, `tests/dbreal` (MySQL real), `tests/regress/*` (envelope SQL,
   lambdas, JSON, API bleed, fatal recovery, WebSocket, debugger), ASan sobre la
   suite completa, `scripts/audit_core_debt.sh` (0 globales nuevos, 0 funciones
   > 300 líneas, 0 magic strings de tipo) y `scripts/audit_llp64_prototypes.sh`.
4. **Compatibilidad hacia atrás de facto.** Un script que corre en `0.1.n` corre
   igual en `0.1.n+1`, salvo lo listado en "Cambios de comportamiento". Un
   comportamiento no documentado en la spec **no** está garantizado (puede ser un
   bug que se corrige).
5. **Diagnóstico estable en forma.** `--syntax-check` devuelve siempre
   `{"ok":bool,"errors":[...],"warnings":[...]}`. Pueden agregarse warnings
   nuevos en PATCH; los `errors` nuevos (algo que antes parseaba y ahora no) son
   cambio de comportamiento y van al CHANGELOG.

Lo que **no** está garantizado en 0.x:

- Estabilidad de la ABI de plugins (`plugins/*`) ni del formato del bytecode.
- Texto exacto de mensajes de error (sí se garantiza el *prefijo* `Error:` y el
  código de salida).
- Que un gotcha listado en `SINTAXIS_Y_GOTCHAS.md` (ERP) sección B persista: se
  corrigen cuando se puede y pasan a la sección C con la versión que lo resolvió.

## 3. Criterios para 1.0

Se publica 1.0 cuando se cumplan **todos**:

1. **Superficie completa bajo spec.** Cada builtin de `docs/API_BUILTINS.md` y
   cada construcción del parser tiene ID en `SPEC.md` y test 1:1 (hoy: núcleo del
   lenguaje, numérico, strings, colecciones, funciones/clases, errores, async y
   LINQ; falta: SQL/`db_*`, HTTP/`request_*`/`response_*`, filesystem, fechas,
   WebSocket, CSV/XML/JSON avanzado).
2. **Ciclo limpio de gotchas.** Un ciclo completo de releases (≥ 1 mes, ≥ 2
   releases) en el que ningún gotcha nuevo entra a `SINTAXIS_Y_GOTCHAS.md` sin
   haber tenido antes un test en `tests/lang` (regla "gotcha = test", ver §4).
   El conteo empieza el **2026-09-19** (release 0.1.8).
3. **Un solo camino de evaluación.** El walker AST deja de ser el intérprete de
   referencia: toda evaluación pasa por bytecode y el walker queda solo para
   `--syntax-check`/LSP, o bien se elimina la comparación de tipos por string
   (hecho en 0.1.8: `strcmp(type)` → `NodeKind`) **y** el A/B walker-vs-bytecode
   se sostiene en `0 differ` durante el ciclo de §3.2.
4. **Semántica numérica cerrada.** `SPEC.md` §3b (división, promoción
   int/float/decimal, rangos de `Math.*`) sin ítems marcados "limitación"
   pendientes de decisión.
5. **Modelo de concurrencia escrito.** `docs/EXECUTION_MODEL.md` cubre workers,
   estado por request, pool SQL y `await_all` con tests en `tests/api`.
6. **Deprecaciones cerradas.** Todo lo marcado `@deprecated` durante 0.x se
   elimina o se promueve; no hay dos formas de hacer lo mismo sin una preferida.

Tras 1.0: PATCH = solo correcciones sin cambio de comportamiento documentado;
MINOR = aditivo compatible; MAJOR = incompatibilidades, con `te --fmt`/tooling de
migración cuando aplique.

## 4. Regla "gotcha = test"

Cuando alguien descubre un comportamiento sorprendente:

1. Se escribe primero el test en `tests/lang/<área>/…te` (con `.expected` o
   `// expect-contains`), marcado `// xfail` si es un bug a corregir.
2. Si es regla del lenguaje (no bug), se agrega/ajusta el ID en `SPEC.md` y el
   test lleva `// spec: ID`.
3. Solo entonces se documenta en `SINTAXIS_Y_GOTCHAS.md` (ERP) **citando el
   archivo de test**. Un gotcha sin test no se acepta en las listas A/B/C.
4. Al corregirse, el `xfail` se quita, el gotcha pasa de B a C con la versión, y
   el CHANGELOG lo lista en "Cambios de comportamiento".

## 5. Checklist de release

```
[ ] CHANGELOG.md: sección de la versión con "Cambios de comportamiento"
[ ] spec_coverage.py --strict = 100 %
[ ] lang / bc-diff / api / dbreal / regress / ASan / audits en verde (VM)
[ ] A/B ERP: binario anterior vs nuevo = distintos 0; bytecode vs walker = distintos 0
[ ] ERP aislado (te-math-aislado.sh) sin 5xx ni errores runtime
[ ] git tag vX.Y.Z → CI publica win64/linux + SHA256SUMS
[ ] Instalación VM (te-install-XYZ.sh fase 1 → --install) y local; un solo md5 en todos los procesos
[ ] SINTAXIS_Y_GOTCHAS.md §versiones + memoria del repo actualizadas
```
