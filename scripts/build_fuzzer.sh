#!/usr/bin/env bash
# build_fuzzer.sh — compila el harness libFuzzer del front-end de TypeEasy.
#
# Estrategia:
#   1) Compila TODOS los objetos del motor con clang + ASan + fuzzer-no-link
#      (reusando el Makefile, que ya conoce bison/flex y la lista de fuentes).
#   2) Linkea tests/fuzz/fuzz_parser.c con -fsanitize=fuzzer,address contra esos
#      objetos (excluyendo typeeasy_main.o; libFuzzer aporta su propio main()).
#
# Solo Linux + clang (libFuzzer es de Clang). En local usa un contenedor con
# clang (ver scripts/run_fuzz.sh). Requiere las mismas deps de build que ASan:
#   flex bison libssl-dev default-libmysqlclient-dev libcurl4-openssl-dev
#   libpq-dev freetds-dev libsqlite3-dev libomp-dev clang
#
# Uso:
#   bash scripts/build_fuzzer.sh           # produce src/fuzz_parser
#
# Salida: src/fuzz_parser (binario libFuzzer).
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
SRC="$ROOT/src"

CC="${CC:-clang}"
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "ERROR: '$CC' no encontrado. Instala clang (libFuzzer es de Clang)." >&2
  exit 1
fi

# fuzzer-no-link: instrumenta cobertura en los objetos del motor sin aportar
# main() (eso lo hace el harness al linkear con -fsanitize=fuzzer).
FUZZ_CFLAGS="-fsanitize=fuzzer-no-link,address -g -O1 -fno-omit-frame-pointer"

# Objetos del motor (misma lista que el target 'typeeasy' del Makefile, sin el
# main typeeasy_main.o que reemplaza el harness).
MOTOR_OBJS="ast.o bytecode.o mysql_bridge.o postgres_bridge.o sqlserver_bridge.o \
db_params.o orm_bridge.o typeeasy_api.o typeeasy_api_server.o wasm_backend.o \
debugger.o te_builtins.o te_http.o te_json.o te_bytecode.o te_csv.o te_colcache.o \
te_stdlib.o te_bridge.o te_async.o te_evloop.o te_linq.o te_linq_ops.o te_math.o te_string.o \
te_list.o te_map.o"
GLUE_OBJS="parser.tab.o lex.yy.o strvars.o civetweb.o te_websocket.o"
ALL_OBJS="$MOTOR_OBJS $GLUE_OBJS"

echo "=== [fuzz] limpiando objetos previos (Makefile mezcla gcc/clang) ==="
make -C "$SRC" clean >/dev/null 2>&1 || true

echo "=== [fuzz] compilando objetos del motor con clang+ASan+fuzzer-no-link ==="
# Construye SOLO los .o necesarios (no el ejecutable: evita el link con gcc/libs
# que haría el target por defecto). El Makefile genera parser.tab.c/lex.yy.c.
make -C "$SRC" CC="$CC" CFLAGS_EXTRA="$FUZZ_CFLAGS" $ALL_OBJS

echo "=== [fuzz] compilando harness fuzz_parser.c ==="
"$CC" $FUZZ_CFLAGS -I"$SRC" -I"$ROOT/api_server" \
  -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET -DTE_HAVE_LIBCURL \
  -c "$ROOT/tests/fuzz/fuzz_parser.c" -o "$SRC/fuzz_parser.o"

echo "=== [fuzz] linkeando src/fuzz_parser ==="
# Mismas libs que LIBS_SCRIPT del Makefile. -fsanitize=fuzzer aporta main().
# -fopenmp: los objetos del motor se compilan con -fopenmp (CFLAGS del Makefile)
#   y referencian el runtime OpenMP; clang lo resuelve con libomp al linkear.
#   (El Makefile usa -lgomp porque compila con gcc; con clang es libomp.)
# Sin -lfl: parser.l define su propio yywrap() y no usamos el main() de flex,
#   así evitamos la dependencia de runtime libfl.so.2 (más portable).
( cd "$SRC" && "$CC" -fsanitize=fuzzer,address -g -fno-omit-frame-pointer -fopenmp \
  -o fuzz_parser fuzz_parser.o $ALL_OBJS \
  -lm -lmysqlclient -lpq -lsybdb -lpthread -ldl -lssl -lcrypto -lcurl )

echo ""
echo "=== Listo: src/fuzz_parser ==="
ls -lh "$SRC/fuzz_parser"
