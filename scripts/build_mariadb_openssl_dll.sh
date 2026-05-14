#!/usr/bin/env bash
# Compila libmariadb.dll desde source contra OPENSSL en MSYS2 MINGW64.
#
# RAZON: el paquete oficial mingw-w64-x86_64-libmariadbclient de MSYS2 viene
# linkeado contra Schannel (TLS de Windows). Schannel tiene un bug conocido
# contra los Network Load Balancers de AWS (TiDB Cloud, PlanetScale, Aiven):
# durante el handshake o la primera lectura de application data devuelve
# 0x80090330 SEC_E_DECRYPT_FAILURE haciendo imposible conectar a esos
# servicios. La libmariadb compilada con OpenSSL no tiene este bug.
#
# Requisitos (MSYS2 MINGW64):
#   pacman -S --needed mingw-w64-x86_64-cmake mingw-w64-x86_64-openssl \
#                      mingw-w64-x86_64-gcc mingw-w64-x86_64-make
#
# Output: el path absoluto del .dll generado se imprime en stdout (ultima linea).
# Caller puede capturarlo: DLL=$(bash scripts/build_mariadb_openssl_dll.sh)

set -euo pipefail

# Capturar repo root ANTES de cualquier cd (luego trabajamos en $WORK y $0 deja
# de resolver). Resolver con realpath para tener absoluto.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VER="${MARIADB_CC_VERSION:-3.4.8}"
URL="https://github.com/mariadb-corporation/mariadb-connector-c/archive/refs/tags/v${VER}.tar.gz"

# Workdir bajo $RUNNER_TEMP en CI, /tmp local.
WORK="${RUNNER_TEMP:-/tmp}/mariadb-cc-build"
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Descargando MariaDB Connector/C v${VER} desde GitHub..." >&2
curl -fsSL "$URL" -o "src.tar.gz"
tar -xzf src.tar.gz
SRC_DIR="mariadb-connector-c-${VER}"
test -d "$SRC_DIR" || { echo "ERROR: no existe $SRC_DIR tras extraer" >&2; exit 1; }

# Sanidad: cmake y openssl visibles
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake no encontrado. pacman -S mingw-w64-x86_64-cmake" >&2; exit 1; }
pkg-config --exists openssl || { echo "ERROR: openssl no encontrado. pacman -S mingw-w64-x86_64-openssl" >&2; exit 1; }

mkdir -p "$SRC_DIR/build"
cd "$SRC_DIR/build"

# WITH_SSL=OPENSSL: forzar OpenSSL en lugar del default Schannel en Windows.
# WITH_UNIT_TESTS=OFF, WITH_CURL=OFF: no necesitamos tests ni HTTP plugin.
# CLIENT_PLUGIN_DIALOG=STATIC: incrustamos plugin de auth (no DLL extra).
echo "==> Configurando con cmake (WITH_SSL=OPENSSL)..." >&2
cmake .. \
  -G "MSYS Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_SSL=OPENSSL \
  -DWITH_UNIT_TESTS=OFF \
  -DWITH_CURL=OFF \
  -DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=OFF \
  -DCLIENT_PLUGIN_DIALOG=STATIC >&2

echo "==> Compilando libmariadb..." >&2
cmake --build . --target libmariadb -- -j"$(nproc 2>/dev/null || echo 2)" >&2

DLL="$WORK/$SRC_DIR/build/libmariadb/libmariadb.dll"
if [[ ! -f "$DLL" ]]; then
  echo "ERROR: no se genero $DLL" >&2
  find "$WORK/$SRC_DIR/build" -name 'libmariadb*.dll' >&2 || true
  exit 1
fi

echo "==> Verificando dependencias del DLL (debe linkear libssl/libcrypto, NO secur32):" >&2
ldd "$DLL" >&2 || true

# Copiar a un path estable dentro del repo para que sobreviva entre pasos de
# GitHub Actions (cada step abre un shell nuevo y $RUNNER_TEMP/cygpath no
# siempre coinciden). Caller usa LIBMARIADB_OPENSSL_DLL=dist/external/libmariadb.dll
OUT_DIR="$REPO_ROOT/dist/external"
mkdir -p "$OUT_DIR"
OUT_DLL="$OUT_DIR/libmariadb.dll"
cp -f "$DLL" "$OUT_DLL"
echo "==> DLL copiado a: $OUT_DLL" >&2

# Imprimir solo el path absoluto en la ultima linea (para captura por caller).
echo "$OUT_DLL"
