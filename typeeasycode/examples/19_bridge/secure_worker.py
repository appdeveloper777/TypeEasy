#!/usr/bin/env python3
# Worker del ejemplo "API segura + bridge".
# La API de TypeEasy (con JWT + CORS) delega aqui el calculo pesado.
# Protocolo: una linea JSON de entrada -> una linea JSON de salida.
#   Peticion : {"op": "factorial", "n": 20}
#   Respuesta: {"ok": true, "result": 2432902008176640000}
import sys, json, math


def handle(req):
    op = req.get("op", "echo")
    if op == "factorial":
        return {"ok": True, "result": math.factorial(int(req.get("n", 0)))}
    if op == "fib":
        n = int(req.get("n", 0))
        a, b = 0, 1
        for _ in range(n):
            a, b = b, a + b
        return {"ok": True, "result": a}
    if op == "reverse":
        return {"ok": True, "result": str(req.get("text", ""))[::-1]}
    return {"ok": False, "error": f"op desconocida: {op}"}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            resp = handle(json.loads(line))
        except Exception as e:  # noqa: BLE001
            resp = {"ok": False, "error": str(e)}
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
