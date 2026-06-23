#!/usr/bin/env bash
# build_android_arm64.sh — cross-compila TypeEasy para Android (Bionic) arm64.
#
# Produce un binario PIE (position-independent executable) nativo aarch64-android
# que NO depende de glibc ni de las libs pesadas (mysql/pq/sybdb/curl/gomp). Es
# el perfil "sqlite-only" pensado para empaquetar dentro de un APK:
#
#   - Se nombra libtypeeasy.so (un ELF PIE; el nombre lib*.so es lo que Android
#     extrae a nativeLibraryDir, que SI es ejecutable bajo el W^X de Android 10+).
#   - Enlaza OpenSSL ESTATICO (sha1()/sha256()/md5_hex()/JWT siguen funcionando).
#   - SQLite es un plugin .so aparte (plugins/sqlite/build_android.sh).
#   - Sirve el UI bundleado y la API en 127.0.0.1:<port>; DB local SQLite.
#
# NO toca el build de siempre: usa los flags WITH_*=0 + TE_FL_LIBS= +
# TE_ARCH_FLAGS= del Makefile (todos opt-in, default ON).
#
# Requisitos:
#   - Android NDK (r23+). Autodetecta $ANDROID_NDK_HOME / $ANDROID_NDK_LATEST_HOME
#     / $ANDROID_NDK_ROOT, o pasa NDK=/ruta/al/ndk.
#   - bison, flex, curl, perl, make (host).
#
# Uso:
#   bash scripts/build_android_arm64.sh                # API 24, OpenSSL auto
#   NDK=$HOME/android-ndk-r26d ANDROID_API=26 bash scripts/build_android_arm64.sh
#
# Salida: dist/android-arm64/libtypeeasy.so  (+ el plugin si se corre su build)
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

ANDROID_API="${ANDROID_API:-24}"
ABI="arm64-v8a"
TRIPLE="aarch64-linux-android"
OUT_DIR="${OUT_DIR:-$ROOT/dist/android-arm64}"
TE_VERSION="${TYPEEASY_VERSION:-0.0.0}"

# --- 1) Localizar el NDK -----------------------------------------------------
NDK="${NDK:-${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_ROOT:-}}}}"
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
  echo "ERROR: Android NDK no encontrado. Define NDK=/ruta/al/android-ndk-rXX" >&2
  echo "       (o exporta ANDROID_NDK_HOME / ANDROID_NDK_ROOT)." >&2
  exit 1
fi
HOST_TAG="linux-x86_64"
[[ "$(uname -s)" == "Darwin" ]] && HOST_TAG="darwin-x86_64"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
CC_ANDROID="$TOOLCHAIN/bin/${TRIPLE}${ANDROID_API}-clang"
AR_ANDROID="$TOOLCHAIN/bin/llvm-ar"
if [[ ! -x "$CC_ANDROID" ]]; then
  echo "ERROR: no existe el compilador NDK: $CC_ANDROID" >&2
  echo "       Revisa la version del NDK y ANDROID_API ($ANDROID_API)." >&2
  exit 1
fi
echo "[android] NDK:        $NDK"
echo "[android] clang:      $CC_ANDROID"
echo "[android] API level:  $ANDROID_API   ABI: $ABI"

# --- 2) OpenSSL estatico para android-arm64 ----------------------------------
# Necesario para sha1/sha256/md5/HMAC/JWT. Se puede saltar pasando
# OPENSSL_ANDROID_ROOT=/ruta (con include/ y lib/libssl.a libcrypto.a).
OPENSSL_VERSION="${OPENSSL_VERSION:-3.0.15}"
OPENSSL_ANDROID_ROOT="${OPENSSL_ANDROID_ROOT:-$ROOT/.android-deps/openssl-$OPENSSL_VERSION-$ABI}"
if [[ -f "$OPENSSL_ANDROID_ROOT/lib/libcrypto.a" && -f "$OPENSSL_ANDROID_ROOT/lib/libssl.a" ]]; then
  echo "[android] OpenSSL (cache): $OPENSSL_ANDROID_ROOT"
else
  echo "[android] Construyendo OpenSSL $OPENSSL_VERSION estatico para $ABI..."
  mkdir -p "$ROOT/.android-deps"
  SSL_SRC="$ROOT/.android-deps/openssl-$OPENSSL_VERSION"
  if [[ ! -d "$SSL_SRC" ]]; then
    ( cd "$ROOT/.android-deps"
      curl -fL -o "openssl-$OPENSSL_VERSION.tar.gz" \
        "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
        || curl -fL -o "openssl-$OPENSSL_VERSION.tar.gz" \
             "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz"
      tar xzf "openssl-$OPENSSL_VERSION.tar.gz"
      rm -f "openssl-$OPENSSL_VERSION.tar.gz" )
  fi
  ( cd "$SSL_SRC"
    export ANDROID_NDK_ROOT="$NDK"
    export PATH="$TOOLCHAIN/bin:$PATH"
    # Configure android-arm64: solo libs estaticas, sin apps ni tests (rapido).
    # (apps/tests no se compilan porque usamos el target `build_libs`.)
    ./Configure android-arm64 -D__ANDROID_API__="$ANDROID_API" \
      no-shared no-tests \
      --prefix="$OPENSSL_ANDROID_ROOT" --openssldir="$OPENSSL_ANDROID_ROOT/ssl"
    make -j"$(nproc 2>/dev/null || echo 2)" build_libs
    # Instala headers + libs estaticas en el prefix.
    make install_dev )
  echo "[android] OpenSSL listo: $OPENSSL_ANDROID_ROOT"
fi

# --- 3) Cross-compilar el motor (perfil sqlite-only, PIE) --------------------
echo "[android] Compilando libtypeeasy.so (PIE, sqlite-only)..."
make -C "$ROOT/src" clean >/dev/null 2>&1 || true
# Notas de flags:
#  - WITH_*=0           -> sin mysql/pq/sybdb/curl/openmp (db_stubs_optional.c)
#  - TE_FL_LIBS=        -> sin -lfl (parser.l trae su propio yywrap)
#  - TE_ARCH_FLAGS=     -> sin -mavx2/-mbmi (x86; el motor cae a escalar en ARM)
#  - EXEC_SCRIPT=...    -> nombre de salida libtypeeasy.so
#  - CFLAGS_EXTRA va al compile Y al link (la regla linkea con $(CFLAGS)):
#       -fPIE -pie               PIE obligatorio en Android
#       -I.../include -L.../lib  OpenSSL estatico
make -C "$ROOT/src" libtypeeasy.so \
  CC="$CC_ANDROID" \
  AR="$AR_ANDROID" \
  EXEC_SCRIPT=libtypeeasy.so \
  WITH_MYSQL=0 WITH_PG=0 WITH_SYBASE=0 WITH_CURL=0 WITH_OPENMP=0 \
  TE_FL_LIBS= \
  TE_PTHREAD_LIBS= \
  TE_ARCH_FLAGS= \
  TE_OPT=-O2 \
  CFLAGS_EXTRA="-fPIE -pie -DTYPEEASY_VERSION=$TE_VERSION -I$OPENSSL_ANDROID_ROOT/include -L$OPENSSL_ANDROID_ROOT/lib"

mkdir -p "$OUT_DIR"
cp "$ROOT/src/libtypeeasy.so" "$OUT_DIR/libtypeeasy.so"
echo "[android] OK -> $OUT_DIR/libtypeeasy.so"
file "$OUT_DIR/libtypeeasy.so" 2>/dev/null || true
"$TOOLCHAIN/bin/llvm-readelf" -h "$OUT_DIR/libtypeeasy.so" 2>/dev/null | grep -E "Type|Machine" || true

# --- 4) Plugin SQLite para android-arm64 -------------------------------------
echo "[android] Compilando plugin SQLite android-arm64..."
NDK="$NDK" ANDROID_API="$ANDROID_API" bash "$ROOT/plugins/sqlite/build_android.sh"
cp "$ROOT/plugins/sqlite/libte_sqlite.so" "$OUT_DIR/libte_sqlite.so"

echo ""
echo "=== Listo: artefactos Android arm64 en $OUT_DIR ==="
ls -lh "$OUT_DIR"
echo ""
echo "Empaquetado en APK: copia ambos .so a jniLibs/arm64-v8a/ y ejecuta"
echo "  libtypeeasy.so --api app.te --port <N> --host 127.0.0.1"
echo "con DB_ENGINE=sqlite DB_SQLITE_PATH=<ruta .db en filesDir>."
