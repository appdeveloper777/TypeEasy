#!/usr/bin/env bash
# Empaqueta TypeEasy para distribucion Linux (tar.gz + .deb).
# Uso:
#   bash scripts/package_linux_release.sh 0.0.1

set -euo pipefail

VERSION="${1:-0.0.1}"
ARCH="${ARCH:-amd64}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="$ROOT_DIR/src/typeeasy"
OUT_DIR="$ROOT_DIR/dist/linux"
PKG_NAME="TypeEasy-${VERSION}-linux-${ARCH}"
PKG_DIR="$OUT_DIR/$PKG_NAME"
TARBALL="$OUT_DIR/${PKG_NAME}.tar.gz"
DEB_DIR="$OUT_DIR/deb/typeeasy_${VERSION}_${ARCH}"
DEB_FILE="$OUT_DIR/typeeasy_${VERSION}_${ARCH}.deb"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "ERROR: no existe $BIN_PATH" >&2
  echo "Compila primero: bash scripts/build_native_linux.sh" >&2
  exit 1
fi

# --- 1) Carpeta portable + tar.gz -------------------------------------------
rm -rf "$PKG_DIR" "$TARBALL"
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/examples"

cp "$BIN_PATH" "$PKG_DIR/bin/typeeasy"
chmod +x "$PKG_DIR/bin/typeeasy"

for doc in README.md BUILD.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    cp "$ROOT_DIR/$doc" "$PKG_DIR/"
  fi
done

if [[ -f "$ROOT_DIR/typeeasycode/crear_const_variable.te" ]]; then
  cp "$ROOT_DIR/typeeasycode/crear_const_variable.te" "$PKG_DIR/examples/"
fi

cat > "$PKG_DIR/README-LINUX.txt" <<EOF
TypeEasy Linux Package v${VERSION} (${ARCH})

Estructura:
- bin/typeeasy
- examples/crear_const_variable.te

Uso rapido:
  ./bin/typeeasy ./examples/crear_const_variable.te

Dependencias runtime (instalar en el host):
  sudo apt-get install -y libmariadb3 libpq5 libsybdb5

Este paquete corresponde a una release inicial (0.0.1).
EOF

tar -C "$OUT_DIR" -czf "$TARBALL" "$PKG_NAME"
echo "Tarball generado: $TARBALL"

# --- 2) Paquete .deb --------------------------------------------------------
rm -rf "$DEB_DIR" "$DEB_FILE"
mkdir -p "$DEB_DIR/DEBIAN" \
         "$DEB_DIR/usr/bin" \
         "$DEB_DIR/usr/share/typeeasy/examples" \
         "$DEB_DIR/usr/share/doc/typeeasy"

install -m 0755 "$BIN_PATH" "$DEB_DIR/usr/bin/typeeasy"

if [[ -f "$ROOT_DIR/typeeasycode/crear_const_variable.te" ]]; then
  install -m 0644 "$ROOT_DIR/typeeasycode/crear_const_variable.te" \
                  "$DEB_DIR/usr/share/typeeasy/examples/"
fi

for doc in README.md BUILD.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    install -m 0644 "$ROOT_DIR/$doc" "$DEB_DIR/usr/share/doc/typeeasy/"
  fi
done

# Tamano instalado en KB
INSTALLED_SIZE=$(du -sk "$DEB_DIR" | cut -f1)

cat > "$DEB_DIR/DEBIAN/control" <<EOF
Package: typeeasy
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Maintainer: TypeEasy <noreply@typeeasy.dev>
Installed-Size: ${INSTALLED_SIZE}
Depends: libc6, libmariadb3, libpq5, libsybdb5
Description: TypeEasy interpreter and framework
 Interprete y framework experimental escrito en C que permite crear
 sintaxis propias, scripts y endpoints REST sin depender de Docker.
EOF

if command -v dpkg-deb >/dev/null 2>&1; then
  dpkg-deb --build --root-owner-group "$DEB_DIR" "$DEB_FILE" >/dev/null
  echo "Paquete .deb generado: $DEB_FILE"
else
  echo "WARN: 'dpkg-deb' no encontrado; se omite la generacion de .deb." >&2
fi

# --- 3) SHA256 --------------------------------------------------------------
HASHES="$OUT_DIR/SHA256SUMS-${VERSION}.txt"
: > "$HASHES"
for f in "$TARBALL" "$DEB_FILE"; do
  if [[ -f "$f" ]]; then
    ( cd "$OUT_DIR" && sha256sum "$(basename "$f")" ) >> "$HASHES"
  fi
done

echo "Hashes: $HASHES"
