# 16 · WebSocket nativo

Ejemplo minimo de la sintaxis para exponer un endpoint WebSocket usando el
runtime nativo de TypeEasy (`api_server/te_websocket.c`).

## Sintaxis

```te
endpoint {
    [WebSocket("/ws/echo")]
    ws_echo() {
        // Se ejecuta una vez por mensaje recibido del cliente.
        let msg = request_body();         // payload del frame (texto)
        let topic = "echo";
        ws_subscribe(topic);              // une la conexion al canal "echo"
        ws_send(concat("echo: ", msg));   // responde SOLO al emisor
        ws_broadcast(topic, msg);         // reenvia a todos los suscriptos
    }
}
```

| Builtin                       | Proposito                                       |
|-------------------------------|-------------------------------------------------|
| `ws_subscribe(canal)`         | Une la conexion actual al canal indicado.       |
| `ws_send(texto)`              | Envia un frame al cliente actual.               |
| `ws_broadcast(canal, texto)`  | Envia un frame a todos los suscriptos al canal. |
| `request_ws_id()`             | Id estable de la conexion actual.               |
| `request_param("id")`         | Captura segmentos de la ruta (`/ws/game/{id}`). |

## Como correrlo

```bash
docker compose run --rm -p 8080:8080 typeeasy --api examples/16_websocket/ws_echo.te --port 8080
```

Cliente rapido (Python):

```python
import asyncio, websockets
async def main():
    async with websockets.connect("ws://localhost:8080/ws/echo") as ws:
        await ws.send("hola")
        print(await ws.recv())
asyncio.run(main())
```

## Regression

Ver `chess/tests/regression_ws.py` para una prueba automatica de handshake
contra una `chess.te` en ejecucion.
