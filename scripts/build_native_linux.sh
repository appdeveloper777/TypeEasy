#!/usr/bin/env bash
# Build nativo de `typeeasy` en Linux (Ubuntu/Debian).
# Equivalente a scripts/build_native_windows.sh pero para Linux.
#
# Prerrequisitos (una sola vez):
#   sudo apt-get update
#   sudo apt-get install -y build-essential flex bison \
#                           libmariadb-dev libpq-dev \
#                           freetds-dev pkg-config
#
# Uso:
#   bash scripts/build_native_linux.sh
#
# Resultado: src/typeeasy (ELF nativo Linux).

set -euo pipefail

cd "$(dirname "$0")/../src"

for tool in gcc flex bison make pkg-config; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "ERROR: '$tool' no encontrado. Instala dependencias (ver cabecera)." >&2
    exit 1
  fi
done

make clean >/dev/null 2>&1 || true
make typeeasy

echo ""
echo "=== Listo: src/typeeasy ==="
ls -lh typeeasy
