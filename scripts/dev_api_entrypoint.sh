#!/bin/sh
# Dev entrypoint: rebuild typeeasy_api from mounted /app/src + /app/api_server, then exec it.
# Used by the api_dev mode (docker-compose.override.yml). For prod use the multi-stage Dockerfile.
set -e

echo "[dev] regenerating parser..."
cd /app/src
bison -d -o parser.tab.c parser.y --warnings=none
flex -o lex.yy.c parser.l

echo "[dev] compiling interpreter objects..."
gcc -I../api_server -I/usr/include/postgresql -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -O0 -g -c \
    ast.c bytecode.c mysql_bridge.c postgres_bridge.c sqlserver_bridge.c db_params.c orm_bridge.c typeeasy_api.c debugger.c te_builtins.c te_http.c te_json.c te_bytecode.c te_csv.c te_xlsx.c te_colcache.c te_stdlib.c te_bridge.c te_async.c te_evloop.c te_linq.c te_linq_ops.c te_math.c te_string.c te_list.c te_map.c \
    parser.tab.c lex.yy.c strvars.c

echo "[dev] linking typeeasy_api..."
cd /app
gcc -I/app/api_server -I/app/src -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -O0 -g \
    /app/api_server/servidor_api.c /app/api_server/typeeasy.c /app/api_server/civetweb.c \
    /app/src/ast.o /app/src/bytecode.o /app/src/mysql_bridge.o /app/src/postgres_bridge.o /app/src/sqlserver_bridge.o /app/src/db_params.o /app/src/orm_bridge.o \
    /app/src/typeeasy_api.o /app/src/debugger.o /app/src/te_builtins.o /app/src/te_http.o /app/src/te_json.o /app/src/te_bytecode.o /app/src/te_csv.o /app/src/te_xlsx.o /app/src/te_colcache.o /app/src/te_stdlib.o /app/src/te_bridge.o /app/src/te_async.o /app/src/te_evloop.o /app/src/te_linq.o /app/src/te_linq_ops.o /app/src/te_math.o /app/src/te_string.o /app/src/te_list.o /app/src/te_map.o /app/src/parser.tab.o /app/src/lex.yy.o /app/src/strvars.o \
    -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto -lmysqlclient -lpq -lsybdb -lfl -lm -lz

echo "[dev] starting typeeasy_api on :8080"
exec /app/typeeasy_api
