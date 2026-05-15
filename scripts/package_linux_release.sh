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
if [[ -f "$ROOT_DIR/typeeasycode/endpoint.te" ]]; then
  cp "$ROOT_DIR/typeeasycode/endpoint.te" "$PKG_DIR/examples/"
fi

cat > "$PKG_DIR/README-LINUX.txt" <<EOF
TypeEasy Linux Package v${VERSION} (${ARCH})

Estructura:
- bin/typeeasy
- examples/crear_const_variable.te
- examples/endpoint.te

Uso rapido:
  Script normal:
    ./bin/typeeasy ./examples/crear_const_variable.te

  Servidor HTTP embebido (--api):
    ./bin/typeeasy --api ./examples/endpoint.te --port 9000
    # luego en otra terminal:
    curl http://localhost:9000/ping

Flags utiles:
  --api <archivo.te>     Levanta servidor HTTP con los endpoints del .te
  --port <p>             Puerto (default 8080)
  --host <h>             Host bind (default 0.0.0.0)
  --help                 Lista todas las opciones

Dependencias runtime (instalar en el host):
  sudo apt-get install -y libmariadb3 libpq5 libsybdb5 libssl3 libcurl4

Correr como servicio (recomendado en produccion, instalado por el .deb):
  # 1) Editar el archivo .te a servir y el host de bind
  sudo vi /etc/default/typeeasy-api

  # 2) Habilitar 1 o varias instancias (cada una en un puerto distinto)
  sudo systemctl enable --now typeeasy-api@9001
  sudo systemctl enable --now typeeasy-api@9002

  # 3) Para escalar: poner nginx delante con load balancing
  sudo cp /usr/share/typeeasy/nginx/typeeasy-api.conf \
          /etc/nginx/sites-available/typeeasy-api
  sudo ln -s /etc/nginx/sites-available/typeeasy-api /etc/nginx/sites-enabled/
  sudo nginx -t && sudo systemctl reload nginx

Este paquete corresponde a una release inicial (0.0.1).
EOF

tar -C "$OUT_DIR" -czf "$TARBALL" "$PKG_NAME"
echo "Tarball generado: $TARBALL"

# --- 2) Paquete .deb --------------------------------------------------------
rm -rf "$DEB_DIR" "$DEB_FILE"
mkdir -p "$DEB_DIR/DEBIAN" \
         "$DEB_DIR/usr/bin" \
         "$DEB_DIR/usr/share/typeeasy/examples" \
         "$DEB_DIR/usr/share/typeeasy/nginx" \
         "$DEB_DIR/usr/share/doc/typeeasy" \
         "$DEB_DIR/lib/systemd/system" \
         "$DEB_DIR/etc/default"

install -m 0755 "$BIN_PATH" "$DEB_DIR/usr/bin/typeeasy"

# systemd unit (instanced) + defaults + nginx sample
if [[ -f "$ROOT_DIR/installer/linux/typeeasy-api@.service" ]]; then
  install -m 0644 "$ROOT_DIR/installer/linux/typeeasy-api@.service" \
                  "$DEB_DIR/lib/systemd/system/typeeasy-api@.service"
fi
if [[ -f "$ROOT_DIR/installer/linux/typeeasy-api.default" ]]; then
  install -m 0644 "$ROOT_DIR/installer/linux/typeeasy-api.default" \
                  "$DEB_DIR/etc/default/typeeasy-api"
fi
if [[ -f "$ROOT_DIR/installer/linux/nginx/typeeasy-api.conf" ]]; then
  install -m 0644 "$ROOT_DIR/installer/linux/nginx/typeeasy-api.conf" \
                  "$DEB_DIR/usr/share/typeeasy/nginx/typeeasy-api.conf"
fi

if [[ -f "$ROOT_DIR/typeeasycode/crear_const_variable.te" ]]; then
  install -m 0644 "$ROOT_DIR/typeeasycode/crear_const_variable.te" \
                  "$DEB_DIR/usr/share/typeeasy/examples/"
fi
if [[ -f "$ROOT_DIR/typeeasycode/endpoint.te" ]]; then
  install -m 0644 "$ROOT_DIR/typeeasycode/endpoint.te" \
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
Depends: libc6, libmariadb3, libpq5, libsybdb5, libssl3, libcurl4, ca-certificates
Description: TypeEasy interpreter and framework
 Interprete y framework experimental escrito en C que permite crear
 sintaxis propias, scripts y endpoints REST sin depender de Docker.
EOF

# Marca /etc/default/typeeasy-api como conffile (no se sobreescribe en upgrades)
cat > "$DEB_DIR/DEBIAN/conffiles" <<EOF
/etc/default/typeeasy-api
EOF

# Hook post-install: systemd daemon-reload (no habilita ni arranca instancias automaticamente)
cat > "$DEB_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$DEB_DIR/DEBIAN/postinst"

# Hook pre-remove: detiene cualquier instancia activa de typeeasy-api@*
cat > "$DEB_DIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
    for unit in $(systemctl list-units --no-legend 'typeeasy-api@*.service' 2>/dev/null | awk '{print $1}'); do
        systemctl stop "$unit" >/dev/null 2>&1 || true
        systemctl disable "$unit" >/dev/null 2>&1 || true
    done
fi
exit 0
EOF
chmod 0755 "$DEB_DIR/DEBIAN/prerm"

# Hook post-remove: daemon-reload tras desinstalar el unit
cat > "$DEB_DIR/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$DEB_DIR/DEBIAN/postrm"

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
