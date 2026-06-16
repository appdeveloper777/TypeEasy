#!/usr/bin/env bash
# Build nativo de `typeeasy` en Windows usando MSYS2 (MINGW64).
# Elimina el overhead fijo de Docker (~2.5s por ejecución) en benchmarks.
#
# Prerrequisitos (una sola vez):
#   1. Instalar MSYS2:        https://www.msys2.org/
#   2. Abrir "MSYS2 MINGW64" e instalar paquetes:
#        pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-flex \
#                           mingw-w64-x86_64-bison mingw-w64-x86_64-libmariadbclient
#      (Opcional, para el conector SQL Server sqlserver_*):
#        pacman -S --needed mingw-w64-x86_64-freetds
#   3. Ejecutar este script DENTRO de la shell MINGW64 (no MSYS):
#        cd /c/Users/FERNANDO\ INGUNZA/Documents/TypeEasy\ Staging\ 2/TypeEasy
#        bash scripts/build_native_windows.sh
#
# Resultado: src/typeeasy.exe — ejecutable nativo Windows.
# Uso:       ./src/typeeasy.exe benchmarks/foo.te

set -euo pipefail

# Asegurar TMP/TEMP escribibles para gcc en Windows (evita "Cannot create
# temporary file in C:\Windows\"). Crea /c/temp si hace falta.
mkdir -p /c/temp 2>/dev/null || true
export TMP="${TMP:-C:/temp}"
export TEMP="${TEMP:-C:/temp}"
export TMPDIR="${TMPDIR:-C:/temp}"

cd "$(dirname "$0")/../src"

# Sanidad: comprobar herramientas
for tool in gcc flex bison; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "ERROR: '$tool' no encontrado. Instala MSYS2 MINGW64 y los paquetes (ver cabecera)." >&2
    exit 1
  fi
done

# Detectar libmariadbclient (alias compatible de libmysqlclient en MSYS2)
MYSQL_LIB=""
if pkg-config --exists libmariadb 2>/dev/null; then
  MYSQL_CFLAGS="$(pkg-config --cflags libmariadb)"
  MYSQL_LIB="$(pkg-config --libs libmariadb)"
elif pkg-config --exists mysqlclient 2>/dev/null; then
  MYSQL_CFLAGS="$(pkg-config --cflags mysqlclient)"
  MYSQL_LIB="$(pkg-config --libs mysqlclient)"
else
  echo "ERROR: no se encontró libmariadb ni libmysqlclient via pkg-config." >&2
  echo "       Instala con: pacman -S mingw-w64-x86_64-libmariadbclient" >&2
  exit 1
fi

# El Makefile usa <mysql/mysql.h>; MSYS2 instala en mariadb/mysql.h.
# Creamos un shim local mysql/mysql.h -> mariadb/mysql.h si hace falta.
if [[ ! -f "mysql/mysql.h" ]]; then
  mkdir -p mysql
  cat > mysql/mysql.h <<'EOF'
/* Auto-generated shim: redirige a la cabecera de MariaDB en MSYS2. */
#include <mariadb/mysql.h>
EOF
fi

# Detectar FreeTDS (conector SQL Server). Opcional: si no está instalado, el
# binario usa los stubs de db_stubs_win.c (sqlserver_* devuelve "no disponible").
# Para habilitarlo: pacman -S mingw-w64-x86_64-freetds
#
# Control de tamaño del paquete (TE_WITH_SQLSERVER):
#   auto (default) -> incluye SQL Server si FreeTDS está disponible.
#   1 / on / yes   -> exige FreeTDS; aborta si no está (build con SQL Server).
#   0 / off / no   -> NUNCA enlaza FreeTDS aunque esté instalado; usa el stub.
#                     El .exe queda más liviano y el paquete no arrastra las
#                     DLLs de FreeTDS/GnuTLS (varios MB). Ideal para "que no pese".
TE_WITH_SQLSERVER="${TE_WITH_SQLSERVER:-auto}"
SQLSERVER_SRC=""
SQLSERVER_OBJ=""
FREETDS_CFLAGS=""
FREETDS_LIB=""
FREETDS_DEFINE=""
case "$TE_WITH_SQLSERVER" in
  0|off|no|false|OFF|NO|FALSE)
    echo "=== TE_WITH_SQLSERVER=$TE_WITH_SQLSERVER: conector SQL Server DESHABILITADO por petición (stub, paquete más liviano) ==="
    ;;
  *)
    # Detección de FreeTDS. OJO: el paquete MSYS2 mingw-w64-x86_64-freetds NO
    # provee freetds.pc (pkg-config falla) y coloca el header en el subdir
    # include/freetds/sybdb.h (no en include/sybdb.h). El bridge hace
    # `#include <sybdb.h>`, así que cuando detectamos por ruta hay que añadir
    # -I.../include/freetds a los CFLAGS o la compilación no encuentra el header.
    MGW_PREFIX_U="$(cygpath -u "${MINGW_PREFIX:-/mingw64}" 2>/dev/null || echo /mingw64)"
    if pkg-config --exists freetds 2>/dev/null; then
      FREETDS_CFLAGS="$(pkg-config --cflags freetds)"
      FREETDS_LIB="$(pkg-config --libs freetds)"
    elif [[ -f "$MGW_PREFIX_U/include/freetds/sybdb.h" ]] || [[ -f /mingw64/include/freetds/sybdb.h ]]; then
      # Ubicación real en MSYS2: include/freetds/sybdb.h
      FREETDS_INC="$MGW_PREFIX_U/include/freetds"; [[ -d "$FREETDS_INC" ]] || FREETDS_INC="/mingw64/include/freetds"
      FREETDS_CFLAGS="-I$FREETDS_INC"
      FREETDS_LIB="-lsybdb"
    elif [[ -f "$MGW_PREFIX_U/include/sybdb.h" ]] || [[ -f /mingw64/include/sybdb.h ]]; then
      # Ubicación plana (otras distros / instalación manual).
      FREETDS_LIB="-lsybdb"
    fi
    if [[ -n "$FREETDS_LIB" ]]; then
      echo "=== FreeTDS detectado: conector SQL Server (sqlserver_*) HABILITADO ==="
      SQLSERVER_SRC="sqlserver_bridge.c"
      SQLSERVER_OBJ="sqlserver_bridge.o"
      FREETDS_DEFINE="-DTE_WIN_HAVE_FREETDS"
    else
      case "$TE_WITH_SQLSERVER" in
        1|on|yes|true|ON|YES|TRUE)
          echo "ERROR: TE_WITH_SQLSERVER=$TE_WITH_SQLSERVER pero FreeTDS no está instalado." >&2
          echo "       Instala con: pacman -S mingw-w64-x86_64-freetds" >&2
          exit 1
          ;;
        *)
          echo "=== FreeTDS NO encontrado: sqlserver_* quedará deshabilitado (stub) ==="
          echo "    Para habilitarlo: pacman -S mingw-w64-x86_64-freetds" >&2
          ;;
      esac
    fi
    ;;
esac

# v0.0.13: habilitar OpenMP + AVX2/BMI para que el motor columnar (te_colcache,
# te_csv parser multihilo, te_linq_ops SIMD) tenga el mismo perfil que el build
# de Docker. Sin esto el binario nativo Windows queda en modo serial y pierde
# ~40% en cargas LINQ sobre CSV grandes.
CFLAGS_NATIVE="-O3 -fopenmp -DTE_HAVE_OPENMP -mavx2 -mbmi -Wall -I. -I../api_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET -DTE_HAVE_OPENSSL ${MYSQL_CFLAGS} ${FREETDS_CFLAGS} ${FREETDS_DEFINE}"

# Compatibilidad con GCC 14+ (MSYS2 actual): el codigo asume C11/POSIX implicito,
# pero GCC 14 promovio varios warnings a errores por defecto (C23). Los demotamos
# a warnings para no tener que tocar el codigo fuente en esta release.
CFLAGS_NATIVE+=" -std=gnu11"
CFLAGS_NATIVE+=" -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L"
CFLAGS_NATIVE+=" -Wno-error=implicit-function-declaration"
CFLAGS_NATIVE+=" -Wno-error=incompatible-pointer-types"
CFLAGS_NATIVE+=" -Wno-error=int-conversion"
CFLAGS_NATIVE+=" -Wno-error=implicit-int"
CFLAGS_NATIVE+=" -Wno-error=discarded-qualifiers"
CFLAGS_NATIVE+=" -fcommon"

# Version del binario: tomada de la env var TYPEEASY_VERSION (CI la pasa desde el tag).
# Pasamos como token bare (sin comillas) — typeeasy_main.c lo stringifica con TE_XSTR.
TE_VER="${TYPEEASY_VERSION:-0.0.1}"
CFLAGS_NATIVE+=" -DTYPEEASY_VERSION=${TE_VER}"

# Trace JIT habilitado en el build release (Microsoft confirmo "No malware
# detected" para el binario firmado). El JIT sigue OFF en runtime salvo que se
# exporte TYPEEASY_JIT=1, asi que el usuario normal no nota cambio alguno.
CFLAGS_NATIVE+=" -DTE_ENABLE_JIT=1"

# Flags extra opcionales adicionales pasados por el entorno.
CFLAGS_NATIVE+=" ${EXTRA_CFLAGS:-}"

echo "=== Generando parser/lexer ==="
bison -d -o parser.tab.c parser.y --warnings=none
flex -o lex.yy.c parser.l

echo "=== Compilando módulos del motor ==="
gcc $CFLAGS_NATIVE -fno-semantic-interposition -c \
    ast.c bytecode.c mysql_bridge.c orm_bridge.c typeeasy_api.c typeeasy_api_server.c \
    wasm_backend.c debugger.c db_params.c te_builtins.c te_http.c te_json.c te_bytecode.c te_csv.c te_colcache.c te_stdlib.c te_bridge.c te_async.c te_evloop.c te_linq.c te_linq_ops.c te_math.c te_string.c te_list.c te_map.c db_stubs_win.c ${SQLSERVER_SRC}
gcc $CFLAGS_NATIVE -c parser.tab.c lex.yy.c strvars.c typeeasy_main.c
gcc $CFLAGS_NATIVE -c ../api_server/civetweb.c -o civetweb.o
gcc $CFLAGS_NATIVE -c ../api_server/te_websocket.c -o te_websocket.o

echo "=== Linkando typeeasy.exe ==="
gcc -O3 -fopenmp -o typeeasy.exe \
    typeeasy_main.o parser.tab.o lex.yy.o \
    ast.o bytecode.o mysql_bridge.o orm_bridge.o typeeasy_api.o typeeasy_api_server.o wasm_backend.o debugger.o \
    db_params.o te_builtins.o te_http.o te_json.o te_bytecode.o te_csv.o te_colcache.o te_stdlib.o te_bridge.o te_async.o te_evloop.o te_linq.o te_linq_ops.o te_math.o te_string.o te_list.o te_map.o db_stubs_win.o ${SQLSERVER_OBJ} \
    civetweb.o te_websocket.o \
    strvars.o \
    ${MYSQL_LIB} ${FREETDS_LIB} -lm -lws2_32 -lpthread -lssl -lcrypto -lgomp

echo ""
echo "=== Listo: src/typeeasy.exe ==="
ls -lh typeeasy.exe
echo ""
echo "Prueba rápida:"
echo "  ./src/typeeasy.exe benchmarks/bench_obj_list_smoke2.te"
