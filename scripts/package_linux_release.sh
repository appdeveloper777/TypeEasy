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
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/examples" "$PKG_DIR/cli/templates" "$PKG_DIR/plugins/sqlite" "$PKG_DIR/vscode"

# Native interpreter (renombrado para no chocar con el bash CLI wrapper)
cp "$BIN_PATH" "$PKG_DIR/bin/typeeasy-bin"
chmod +x "$PKG_DIR/bin/typeeasy-bin"

# Bash CLI Rails-style + alias 'te'
cp "$ROOT_DIR/cli/typeeasy" "$PKG_DIR/bin/typeeasy"
chmod +x "$PKG_DIR/bin/typeeasy"
ln -sf typeeasy "$PKG_DIR/bin/te"

# Templates Rails-style
cp -r "$ROOT_DIR/cli/templates/." "$PKG_DIR/cli/templates/"

# VS Code extension (.vsix) so users can run `te ext install` without the repo.
# Build once here; reused for both the tarball and the .deb below.
VSIX_SRC="${TYPEEASY_VSIX:-}"
if [[ -n "$VSIX_SRC" && -f "$VSIX_SRC" ]]; then
  echo "VS Code extension (prebuilt): $VSIX_SRC"
elif command -v npx >/dev/null 2>&1; then
  VSIX_SRC="$OUT_DIR/typeeasy-debug.vsix"
  if ! bash "$ROOT_DIR/scripts/build_vscode_vsix.sh" "$VSIX_SRC" >/dev/null 2>&1; then
    echo "WARN: no se pudo construir el .vsix; los paquetes no incluiran la extension VS Code" >&2
    VSIX_SRC=""
  fi
else
  echo "WARN: npx no encontrado; los paquetes no incluiran la extension VS Code (te ext install no funcionara offline)" >&2
  VSIX_SRC=""
fi
if [[ -n "$VSIX_SRC" && -f "$VSIX_SRC" ]]; then
  cp -f "$VSIX_SRC" "$PKG_DIR/vscode/typeeasy-debug.vsix"
else
  rmdir "$PKG_DIR/vscode" 2>/dev/null || true
fi

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

# Plugins nativos (load_native("sqlite") -> plugins/sqlite/libte_sqlite.so).
# Si no existe el .so, intentamos construirlo con la amalgamation (requiere gcc).
SQLITE_SO="$ROOT_DIR/plugins/sqlite/libte_sqlite.so"
if [[ ! -f "$SQLITE_SO" ]]; then
  if command -v gcc >/dev/null 2>&1; then
    echo "Compilando plugin sqlite (amalgamation)..."
    bash "$ROOT_DIR/plugins/sqlite/build_linux.sh" || \
      echo "WARN: build del plugin sqlite falló; el paquete no incluirá libte_sqlite.so" >&2
  else
    echo "WARN: gcc no encontrado, se omite plugin sqlite. Compila manualmente con plugins/sqlite/build_linux.sh" >&2
  fi
fi
if [[ -f "$SQLITE_SO" ]]; then
  cp "$SQLITE_SO" "$PKG_DIR/plugins/sqlite/libte_sqlite.so"
  cp "$ROOT_DIR/plugins/sqlite/README.md" "$PKG_DIR/plugins/sqlite/" 2>/dev/null || true
  echo "Plugin sqlite incluido (tarball):"
  ls -lh "$PKG_DIR/plugins/sqlite/"
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
         "$DEB_DIR/usr/lib" \
         "$DEB_DIR/usr/share/typeeasy/examples" \
         "$DEB_DIR/usr/share/typeeasy/templates" \
         "$DEB_DIR/usr/share/typeeasy/nginx" \
         "$DEB_DIR/usr/share/typeeasy/plugins/sqlite" \
         "$DEB_DIR/usr/share/typeeasy/vscode" \
         "$DEB_DIR/usr/share/doc/typeeasy" \
         "$DEB_DIR/lib/systemd/system" \
         "$DEB_DIR/etc/default"

# Intérprete C → /usr/bin/typeeasy-bin (renombrado: el wrapper bash ocupa /usr/bin/typeeasy)
install -m 0755 "$BIN_PATH" "$DEB_DIR/usr/bin/typeeasy-bin"

# CLI Rails-style → /usr/bin/typeeasy (dispatcher; se auto-detecta y nunca se llama a sí mismo)
install -m 0755 "$ROOT_DIR/cli/typeeasy" "$DEB_DIR/usr/bin/typeeasy"

# Alias corto 'te'
ln -sf typeeasy "$DEB_DIR/usr/bin/te"

# Templates para 'typeeasy new'
cp -r "$ROOT_DIR/cli/templates/." "$DEB_DIR/usr/share/typeeasy/templates/"

# VS Code extension (.vsix) -> /usr/share/typeeasy/vscode/ (found by `te ext install`)
if [[ -n "$VSIX_SRC" && -f "$VSIX_SRC" ]]; then
  install -m 0644 "$VSIX_SRC" "$DEB_DIR/usr/share/typeeasy/vscode/typeeasy-debug.vsix"
else
  rmdir "$DEB_DIR/usr/share/typeeasy/vscode" 2>/dev/null || true
fi

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

# Plugin sqlite (cargado por load_native("sqlite")):
#   - Copia canónica: /usr/lib/libte_sqlite.so  (encontrada por el loader sin variables)
#   - Réplica documental en: /usr/share/typeeasy/plugins/sqlite/
if [[ -f "$SQLITE_SO" ]]; then
  install -m 0644 "$SQLITE_SO" "$DEB_DIR/usr/lib/libte_sqlite.so"
  install -m 0644 "$SQLITE_SO" "$DEB_DIR/usr/share/typeeasy/plugins/sqlite/libte_sqlite.so"
  if [[ -f "$ROOT_DIR/plugins/sqlite/README.md" ]]; then
    install -m 0644 "$ROOT_DIR/plugins/sqlite/README.md" \
                    "$DEB_DIR/usr/share/typeeasy/plugins/sqlite/README.md"
  fi
  echo "Plugin sqlite incluido en .deb (usr/lib + usr/share/typeeasy/plugins/sqlite/)."
fi

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
Depends: libc6, libfl2, libmariadb3, libpq5, libsybdb5, libssl3, libcurl4, ca-certificates
Description: TypeEasy interpreter and framework
 Interprete y framework experimental escrito en C que permite crear
 sintaxis propias, scripts y endpoints REST sin depender de Docker.
EOF

# Marca /etc/default/typeeasy-api como conffile (no se sobreescribe en upgrades)
cat > "$DEB_DIR/DEBIAN/conffiles" <<EOF
/etc/default/typeeasy-api
EOF

# Hook post-install: systemd daemon-reload + offer VS Code extension install.
# La extension es per-user; intentamos instalarla para $SUDO_USER si tiene
# 'code' en PATH. Si no es posible (no-interactivo, sin SUDO_USER, sin 'code'),
# se imprime un hint para que el usuario corra 'te ext install' a mano.
cat > "$DEB_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi

VSIX="/usr/share/typeeasy/vscode/typeeasy-debug.vsix"
if [ -f "$VSIX" ]; then
    target_user="${SUDO_USER:-}"
    if [ -n "$target_user" ] && [ "$target_user" != "root" ]; then
        if su - "$target_user" -c 'command -v code >/dev/null 2>&1'; then
            if su - "$target_user" -c "code --install-extension '$VSIX' --force" >/dev/null 2>&1; then
                echo "TypeEasy: extension VS Code instalada para $target_user."
                echo "          Reinicia VS Code para activar resaltado .te + F5 debug."
            else
                echo "TypeEasy: no se pudo instalar la extension VS Code automaticamente."
                echo "          Ejecuta:  te ext install"
            fi
        else
            echo "TypeEasy: VS Code no detectado en el PATH de $target_user."
            echo "          Para instalar la extension despues:  te ext install"
        fi
    else
        echo "TypeEasy: extension VS Code disponible en $VSIX"
        echo "          Para instalarla en tu sesion de usuario:  te ext install"
    fi
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
