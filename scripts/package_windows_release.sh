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
  # ldd en MSYS2 imprime: "libfoo.dll => /mingw64/bin/libfoo.dll (0xADDR)"
  # Solo copiamos DLLs que vivan en el arbol de MSYS2/MINGW (no las de WINDOWS\System32).
  mapfile -t DLLS < <(
    ldd "$BIN_PATH" \
      | awk '/=> \// { print $3 }' \
      | grep -iE '\.dll$' \
      | grep -iE '/(mingw64|msys64|usr|clang64|ucrt64)/' \
      | sort -u
  )

  echo "DLLs detectadas para bundlear:"
  for dll in "${DLLS[@]}"; do
    if [[ -f "$dll" ]]; then
      echo "  $dll"
      cp -f "$dll" "$PKG_DIR/bin/"
    fi
  done
fi

# Fallback / refuerzo: copiar explicitamente las DLLs criticas que sabemos que typeeasy.exe necesita.
# Si ldd fallo o no las detecto, esto evita "no se encontro libXXX.dll" en la maquina del usuario.
MSYS_BIN_CANDIDATES=(
  "/mingw64/bin"
  "/c/msys64/mingw64/bin"
  "/d/a/_temp/msys64/mingw64/bin"
)
MSYS_BIN=""
for cand in "${MSYS_BIN_CANDIDATES[@]}"; do
  if [[ -d "$cand" ]]; then MSYS_BIN="$cand"; break; fi
done

if [[ -n "$MSYS_BIN" ]]; then
  echo "Copiando DLLs criticas desde $MSYS_BIN"
  for dll in libwinpthread-1.dll libmariadb.dll libgcc_s_seh-1.dll libstdc++-6.dll \
             libssl-3-x64.dll libcrypto-3-x64.dll zlib1.dll libiconv-2.dll; do
    if [[ -f "$MSYS_BIN/$dll" ]]; then
      cp -f "$MSYS_BIN/$dll" "$PKG_DIR/bin/"
    fi
  done
fi

echo "Contenido final de bin/:"
ls -lh "$PKG_DIR/bin/"

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
