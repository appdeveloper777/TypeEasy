#!/usr/bin/env python3
# Worker de ejemplo para el subprocess bridge de TypeEasy.
# Protocolo: lee una linea JSON de stdin, responde una linea JSON por stdout.
# Peticion:  {"op": "upper", "text": "hola"}
# Respuesta: {"ok": true, "result": "HOLA"}
import sys, json

def handle(req):
    op = req.get("op", "echo")
    if op == "upper":
        return {"ok": True, "result": str(req.get("text", "")).upper()}
    if op == "add":
        return {"ok": True, "result": req.get("a", 0) + req.get("b", 0)}
    if op == "echo":
        return {"ok": True, "result": req.get("text", "")}
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
