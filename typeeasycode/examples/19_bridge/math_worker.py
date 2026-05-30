#!/usr/bin/env python3
# Worker de ejemplo: expone varias funciones Python reales a TypeEasy.
# Protocolo: lee una linea JSON de stdin, responde una linea JSON por stdout.
# Peticion:  {"op": "factorial", "n": 5}
# Respuesta: {"ok": true, "result": 120}
import sys, json, math


def factorial(n):
    return math.factorial(int(n))


def sqrt(x):
    return math.sqrt(float(x))


def reverse(text):
    return str(text)[::-1]


def is_prime(n):
    n = int(n)
    if n < 2:
        return False
    for i in range(2, int(math.isqrt(n)) + 1):
        if n % i == 0:
            return False
    return True


def handle(req):
    op = req.get("op", "echo")
    if op == "factorial":
        return {"ok": True, "result": factorial(req.get("n", 0))}
    if op == "sqrt":
        return {"ok": True, "result": sqrt(req.get("x", 0))}
    if op == "reverse":
        return {"ok": True, "result": reverse(req.get("text", ""))}
    if op == "is_prime":
        return {"ok": True, "result": is_prime(req.get("n", 0))}
    return {"ok": False, "error": f"op desconocida: {op}"}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            resp = handle(req)
        except Exception as e:  # noqa: BLE001
            resp = {"ok": False, "error": str(e)}
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
