# Ejemplo 19 — Bridge a otros lenguajes (subproceso)

TypeEasy puede delegar trabajo a programas escritos en **otros lenguajes**
(Java, C#, Python, Node, Rust, Go...) lanzandolos como subproceso y hablando
con ellos por un protocolo simple de **lineas JSON** sobre stdin/stdout.

No necesitas un servidor HTTP ni microservicios: el proceso vive mientras lo
necesites y la comunicacion es directa por tuberias del sistema operativo.

## Builtins

| Builtin | Descripcion |
|---|---|
| `lang_spawn(cmd)` | Lanza el comando. Devuelve un *slot* (int >= 0) o `-1` si falla. |
| `lang_call(slot, linea)` | Envia una linea y **espera una linea** de respuesta. |
| `lang_send(slot, linea)` | Envia una linea (sin esperar respuesta). Devuelve 1/0. |
| `lang_recv(slot)` | Lee una linea de respuesta. |
| `lang_close(slot)` | Cierra el proceso y libera el slot. |

> Tambien existen los alias `proc_spawn` / `proc_call` / `proc_send` /
> `proc_recv` / `proc_close` para quienes prefieran nombres genericos.

## Protocolo

- **Una peticion = una linea** terminada en `\n` escrita al *stdin* del hijo.
- **Una respuesta = una linea** terminada en `\n` leida de su *stdout*.
- El contenido es convencionalmente JSON, pero el bridge solo se ocupa del
  *framing* por saltos de linea: puedes usar el formato que quieras.

## Ejecutar

```bash
te run typeeasycode/examples/19_bridge/bridge_demo.te
```

Salida esperada:

```
=== Bridge TypeEasy <-> Python ===
upper('hola mundo') -> HOLA MUNDO
add(7, 35) -> 42
echo('ping') -> ping
Proceso cerrado. Bridge OK.
```

## El worker

[`worker.py`](worker.py) es un ejemplo minimo: lee JSON de stdin, responde
JSON por stdout. Para usar Java/C#/Rust basta con que tu programa siga el
mismo contrato (leer una linea, escribir una linea). Por ejemplo, en Java:

```java
import java.util.Scanner;
public class Worker {
    public static void main(String[] a) {
        Scanner in = new Scanner(System.in);
        while (in.hasNextLine()) {
            String line = in.nextLine();
            // ... procesar JSON ...
            System.out.println("{\"ok\": true, \"result\": \"...\"}");
            System.out.flush();
        }
    }
}
```

Luego: `lang_spawn("java -jar worker.jar")`.

> **Nota:** recuerda hacer `flush()` tras cada respuesta en el worker; de lo
> contrario `lang_call` se quedara esperando una linea que sigue en el buffer
> del proceso hijo.
