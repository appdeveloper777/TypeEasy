#!/usr/bin/env bash
# Empaqueta TypeEasy para distribucion Windows sin Docker.
# Uso:
#   bash scripts/package_windows_release.sh 0.0.1

set -euo pipefail

VERSION="${1:-0.0.1}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="$ROOT_DIR/src/typeeasy.exe"
OUT_DIR="$ROOT_DIR/dist/windows"
PKG_DIR="$OUT_DIR/TypeEasy-${VERSION}-win64"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "ERROR: no existe $BIN_PATH" >&2
  echo "Compila primero: bash scripts/build_native_windows.sh" >&2
  exit 1
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/examples"

cp "$BIN_PATH" "$PKG_DIR/bin/typeeasy.exe"

for doc in README.md BUILD.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    cp "$ROOT_DIR/$doc" "$PKG_DIR/"
  fi
done

if [[ -f "$ROOT_DIR/typeeasycode/crear_const_variable.te" ]]; then
  cp "$ROOT_DIR/typeeasycode/crear_const_variable.te" "$PKG_DIR/examples/"
fi

if command -v ldd >/dev/null 2>&1; then
  mapfile -t DLLS < <(
    ldd "$BIN_PATH" \
      | awk '/=> \// { print $3 }' \
      | awk 'tolower($0) ~ /\\.dll$/ { print }' \
      | sort -u
  )

  for dll in "${DLLS[@]}"; do
    if [[ -f "$dll" ]]; then
      cp -f "$dll" "$PKG_DIR/bin/"
    fi
  done
fi

cat > "$PKG_DIR/README-WINDOWS.txt" <<EOF
TypeEasy Windows Package v${VERSION}

Estructura:
- bin/typeeasy.exe
- examples/crear_const_variable.te

Uso rapido (PowerShell):
  .\\bin\\typeeasy.exe .\\examples\\crear_const_variable.te

Este paquete corresponde a una release inicial (0.0.1).
EOF

echo "Paquete generado en: $PKG_DIR"
