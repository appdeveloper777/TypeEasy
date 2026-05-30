# Ejemplo 20: Async cooperativo real (`spawn` / `await` / `await_all`)

TypeEasy incluye un **runtime asíncrono cooperativo de verdad**: un event loop
de un solo hilo que solapa esperas de E/S en vez de serializarlas. No es azúcar
sintáctico; el bucle hace *polling* no bloqueante de cada subproceso hijo y
reanuda cada tarea cuando su respuesta está lista.

## Builtins

| Builtin | Devuelve | Descripción |
|---|---|---|
| `spawn(lambda)` | `int` (handle) | Difiere un lambda; se ejecuta cuando el scheduler lo alcanza. |
| `lang_call_async(slot, request)` | `int` (handle) | Escribe la petición al subproceso **ya** y deja la respuesta para después. |
| `await_task(task)` / `await(task)` | resultado | Corre el event loop hasta que la tarea termina. |
| `await_all(t1, t2, ... )` o `await_all([t1, t2])` | lista | Corre todas las tareas **concurrentemente** y devuelve sus resultados en orden. |

Los handles de tarea son enteros (como los *slots* del bridge), así que se
guardan en variables como cualquier número.

## Por qué es concurrencia real

`worker_slow.py` "duerme" 500 ms antes de responder (simula una consulta lenta,
una llamada HTTP, cómputo en otro runtime...). Tres llamadas:

- **Secuencial** con `lang_call` (bloqueante): ~1500 ms.
- **Concurrente** con `lang_call_async` + `await_all`: ~600 ms.

Medido en esta máquina: **1600 ms secuencial vs 608 ms async** (~2.6×). La
diferencia es la prueba de que el event loop solapa las esperas.

## Ejecutar

```sh
bin/typeeasy.exe typeeasycode/examples/20_async/async_demo.te
```

Salida esperada:

```
r0 -> uno
r1 -> dos
r2 -> tres
Tiempo total (ms): ~600
```

## Cómo funciona

`lang_call_async` usa las primitivas no bloqueantes del bridge
(`te_bridge_write_line` / `te_bridge_poll_line`, ver `src/te_bridge.c`): la
petición se entrega de inmediato y la respuesta se cosecha byte a byte sin
bloquear, vía `poll()` en POSIX y `PeekNamedPipe()` en Windows. El scheduler
(`src/te_async.c`) recorre las tareas vivas en cada pasada, completa las que ya
tienen una línea disponible y duerme ~1 ms solo cuando nadie progresó (para no
quemar CPU).

## Notas / limitaciones conocidas

- `spawn` ejecuta su lambda hasta completarse cuando el scheduler lo alcanza;
  el solapamiento real proviene de las tareas de E/S (`lang_call_async`).
- Indexar el resultado de una llamada en línea (`json_parse(rs[0])`) o pasar
  `rs[0]` directo como argumento no está soportado por el intérprete: extrae
  primero a una variable (`let s = rs[0]; let m = json_parse(s);`). El ejemplo
  ya sigue ese patrón.
