#!/usr/bin/env bash
# build_android.sh — compila libte_sqlite.so para Android (Bionic) arm64.
#
# Igual que build_linux.sh pero con el clang del NDK (target aarch64-android).
# Embebe la amalgamation de SQLite (sqlite3.c) -> sin dependencia de libsqlite3
# en el dispositivo. El binario libtypeeasy.so lo carga via dlopen en runtime.
#
# Uso:
#   NDK=/ruta/al/android-ndk ANDROID_API=24 bash plugins/sqlite/build_android.sh
#
# Env overrides:
#   NDK / ANDROID_NDK_HOME / ANDROID_NDK_ROOT  (NDK r23+)
#   ANDROID_API     default 24
#   SQLITE_VERSION  default "3530100" (3.53.1)
#   SQLITE_YEAR     default "2026"
set -euo pipefail

cd "$(dirname "$0")"

ANDROID_API="${ANDROID_API:-24}"
TRIPLE="aarch64-linux-android"

NDK="${NDK:-${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_ROOT:-}}}}"
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
  echo "ERROR: Android NDK no encontrado. Define NDK=/ruta/al/android-ndk-rXX" >&2
  exit 1
fi
HOST_TAG="linux-x86_64"
[[ "$(uname -s)" == "Darwin" ]] && HOST_TAG="darwin-x86_64"
CC_ANDROID="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/${TRIPLE}${ANDROID_API}-clang"
if [[ ! -x "$CC_ANDROID" ]]; then
  echo "ERROR: no existe el compilador NDK: $CC_ANDROID" >&2
  exit 1
fi

SQLITE_VERSION="${SQLITE_VERSION:-3530100}"
SQLITE_YEAR="${SQLITE_YEAR:-2026}"
AMALGAMATION_ZIP="sqlite-amalgamation-${SQLITE_VERSION}.zip"
AMALGAMATION_URL="${AMALGAMATION_URL:-https://sqlite.org/${SQLITE_YEAR}/${AMALGAMATION_ZIP}}"
AMALGAMATION_DIR="sqlite-amalgamation-${SQLITE_VERSION}"

if [[ ! -f "$AMALGAMATION_DIR/sqlite3.c" ]]; then
    echo "[sqlite-android] downloading amalgamation: $AMALGAMATION_URL"
    rm -f "$AMALGAMATION_ZIP"
    curl -fL -o "$AMALGAMATION_ZIP" "$AMALGAMATION_URL"
    unzip -o "$AMALGAMATION_ZIP" >/dev/null
    rm -f "$AMALGAMATION_ZIP"
fi

echo "[sqlite-android] compiling libte_sqlite.so (NDK clang, $TRIPLE$ANDROID_API)..."
# Bionic trae dlopen/pthread en libc -> no se necesita -ldl/-lpthread explicito,
# pero el NDK los acepta como stubs; los omitimos para portabilidad.
"$CC_ANDROID" -shared -fPIC -O2 -Wall -Wno-unused-parameter \
    -I../../src \
    -I"$AMALGAMATION_DIR" \
    -DSQLITE_THREADSAFE=1 \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    sqlite_plugin.c \
    "$AMALGAMATION_DIR/sqlite3.c" \
    -lm \
    -o libte_sqlite.so
echo "[sqlite-android] done:"
ls -la libte_sqlite.so
file libte_sqlite.so 2>/dev/null || true
