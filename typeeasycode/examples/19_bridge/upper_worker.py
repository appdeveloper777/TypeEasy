#!/usr/bin/env python3
# Worker minimo en Python: lee una linea, responde la misma linea en mayusculas.
# Mismo protocolo que perl_worker.pl, para el demo universal cross-plataforma.
import sys
for line in sys.stdin:
    sys.stdout.write(line.rstrip("\n").upper() + "\n")
    sys.stdout.flush()
