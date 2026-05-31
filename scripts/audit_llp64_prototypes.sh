#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# audit_llp64_prototypes.sh — Guard contra el bug LLP64 #1 de TypeEasy.
#
# CONTEXTO
#   En Windows (modelo de datos LLP64) `long` es 32 bits pero los punteros son
#   64 bits. Si una función constructora que DEVUELVE UN PUNTERO (p. ej.
#   `ASTNode *create_ast_node(...)`) se llama desde el parser/lexer SIN un
#   prototipo visible, GCC asume implícitamente `int (*)()` y trunca el puntero
#   devuelto a 32 bits -> SIGSEGV silencioso que SOLO aparece en Windows.
#
#   En Linux (LP64) el mismo código suele "funcionar de casualidad" porque
#   int/long/puntero coinciden mejor, así que el bug pasa los CI de Linux y
#   explota recién en el binario de Windows. Este guard lo detecta en cualquier
#   plataforma, en segundos, sin compilar.
#
# QUÉ HACE
#   Junta todos los símbolos con forma de constructor (create_/append_/build_/
#   make_/new_*) que se INVOCAN en el parser/lexer y verifica que cada uno tenga
#   un prototipo declarado en algún src/*.h. Si falta alguno -> sale con código 1.
#
# USO
#   bash scripts/audit_llp64_prototypes.sh
#   (exit 0 = ok ; exit 1 = hay símbolos sin prototipo)
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Fuentes a auditar: la gramática/lexer tracked + los generados si existen.
CANDIDATES=(
  src/parser.y
  src/parser.l
  src/parser.tab.c
  src/lex.yy.c
)
SOURCES=()
for f in "${CANDIDATES[@]}"; do
  [ -f "$f" ] && SOURCES+=("$f")
done

if [ "${#SOURCES[@]}" -eq 0 ]; then
  echo "audit-llp64: no parser/lexer sources found; nothing to audit." >&2
  exit 0
fi

if [ ! -d src ] || ! ls src/*.h >/dev/null 2>&1; then
  echo "audit-llp64: no src/*.h headers found; cannot verify prototypes." >&2
  exit 1
fi

# Símbolos constructores INVOCADOS en el parser/lexer (nombre seguido de '(').
called="$(grep -hoE '\b(create|append|build|make|new)_[a-z0-9_]+[[:space:]]*\(' "${SOURCES[@]}" \
          | sed -E 's/[[:space:]]*\($//' \
          | sort -u)"

if [ -z "$called" ]; then
  echo "audit-llp64: no constructor-style symbols invoked; OK."
  exit 0
fi

missing=0
checked=0
while IFS= read -r sym; do
  [ -z "$sym" ] && continue
  checked=$((checked + 1))
  # Un prototipo es cualquier declaración del símbolo seguida de '(' en un .h.
  if ! grep -hqE "\b${sym}[[:space:]]*\(" src/*.h; then
    echo "MISSING PROTOTYPE: ${sym}  -> invoked in parser/lexer, not declared in src/*.h"
    missing=$((missing + 1))
  fi
done <<< "$called"

echo "audit-llp64: checked ${checked} constructor-style symbol(s)."

if [ "$missing" -gt 0 ]; then
  cat >&2 <<EOF

FAIL: ${missing} símbolo(s) constructores se invocan en el parser/lexer sin
prototipo en src/*.h. En Windows (LLP64) esto trunca el puntero devuelto a 32
bits y produce un SIGSEGV que NO aparece en Linux.

Arreglo: declará el prototipo correcto en el header adecuado (normalmente
src/ast.h), por ejemplo:

    ASTNode *<symbol>(/* args */);

EOF
  exit 1
fi

echo "OK: todos los constructores del parser/lexer tienen prototipo en src/*.h."
exit 0
