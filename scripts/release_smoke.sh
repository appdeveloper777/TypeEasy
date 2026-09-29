#!/usr/bin/env bash
# Prueba el ARTEFACTO del release (no el build del repo) antes de publicarlo:
#   1) SHA256SUMS del paquete  2) instala el .deb (resuelve Depends en un sistema limpio)
#   3) el binario del tar.gz es el mismo del .deb  4) suite tests/lang  5) suites HTTP tests/api
#      (conformidad: verbos/405, status, headers, cookies, query, encodings, 413).
# Uso: bash scripts/release_smoke.sh <version> [amd64|arm64]   (tras package_linux_release.sh)
set -euo pipefail
VERSION="${1:?uso: release_smoke.sh <version> [amd64|arm64]}"
ARCH="${2:-amd64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/dist/linux"
SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

echo "== 1) SHA256SUMS"
( cd "$DIST" && sha256sum -c "SHA256SUMS-${VERSION}.txt" )

echo "== 2) instalar el .deb"
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq "$DIST/typeeasy_${VERSION}_${ARCH}.deb" python3 > /dev/null
got="$(typeeasy-bin --version)"
[ "$got" = "TypeEasy ${VERSION}" ] || { echo "FAIL: typeeasy-bin --version = '$got' (esperado 'TypeEasy ${VERSION}')"; exit 1; }
command -v te > /dev/null || { echo "FAIL: el .deb no instalo /usr/bin/te"; exit 1; }

echo "== 3) binario del tar.gz"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
tar -xzf "$DIST/TypeEasy-${VERSION}-linux-${ARCH}.tar.gz" -C "$TMP"
BIN="$TMP/TypeEasy-${VERSION}-linux-${ARCH}/bin/typeeasy-bin"
cmp -s "$BIN" /usr/bin/typeeasy-bin || { echo "FAIL: el binario del tar.gz difiere del instalado por el .deb"; exit 1; }

echo "== 4) tests/lang con el binario empaquetado"
python3 "$ROOT/tools/te-test/run_tests.py" "$ROOT/tests/lang" --bin "$BIN"

echo "== 5) tests/api con el binario empaquetado"
python3 "$ROOT/tools/te-test/run_api_tests.py" "$ROOT/tests/api" --bin "$BIN" --port 18181

echo "release smoke OK: ${VERSION} ${ARCH}"
