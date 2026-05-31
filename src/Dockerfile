# TypeEasy/TypeEasy/Dockerfile

# =================================================================
# ETAPA 1: BUILDER BASE (Instala herramientas comunes)
# =================================================================
FROM gcc:13.2.0 AS base

# Instala las herramientas que ambos componentes necesitan
# libsqlite3-dev: necesario para compilar el plugin nativo libte_sqlite.so
RUN apt-get update && \
    apt-get install -y flex bison libssl-dev default-libmysqlclient-dev libcurl4-openssl-dev libpq-dev freetds-dev libsqlite3-dev && \
    apt-get clean

WORKDIR /app

# Copia todo el proyecto una sola vez
COPY . .

# =================================================================
# ETAPA 2: COMPILAR EL "MOTOR" (typeeasy y servidor_agent)
# =================================================================
FROM base AS typeeasy_builder

# 'make all' ahora construye AMBOS: 'typeeasy' y 'servidor_agent'
# Asume que tu Makefile modificado está en /app/src/
RUN make -C /app/src clean && make -C /app/src all

# Compila el plugin nativo SQLite (libte_sqlite.so) desde el código fuente,
# enlazando con la libsqlite3 del sistema. Se hace aquí (no se versiona el .so
# ni se copia del checkout) para garantizar compatibilidad de ABI con el
# intérprete recién compilado y que CI funcione sin binarios precompilados.
RUN gcc -shared -fPIC -O2 -Wall -Wno-unused-parameter \
        -I/app/src \
        /app/plugins/sqlite/sqlite_plugin.c \
        -lsqlite3 \
        -o /app/plugins/sqlite/libte_sqlite.so

# =================================================================
# ETAPA: COMPILAR EL SHIM RUST (motor_nlu_shim)
# =================================================================
FROM rust:1.87-slim AS shim_builder
WORKDIR /shim
# Copiamos sólo la carpeta del shim para mantener el contexto pequeño
COPY tools/motor_nlu_shim ./motor_nlu_shim
RUN apt-get update && apt-get install -y pkg-config libssl-dev && \
    cd motor_nlu_shim && \
    cargo build --release

# =================================================================
# ETAPA 3: COMPILAR EL SERVIDOR "typeeasy_api" (EMBEBIDO)
# =================================================================
FROM base AS api_builder

# Primero detectamos la versión de OpenSSL instalada
RUN openssl version

# Compilamos con la versión correcta de OpenSSL API
# Incluimos todos los archivos necesarios del motor embebido
RUN cd /app/src && \
    bison -d -o parser.tab.c parser.y --warnings=none && \
    flex -o lex.yy.c parser.l && \
    gcc -I../api_server -I/usr/include/postgresql -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET -c \
    ast.c bytecode.c mysql_bridge.c postgres_bridge.c sqlserver_bridge.c db_params.c orm_bridge.c typeeasy_api.c debugger.c te_builtins.c te_http.c te_json.c te_bytecode.c te_csv.c te_colcache.c te_stdlib.c te_bridge.c te_async.c te_linq.c te_linq_ops.c te_math.c te_string.c te_list.c te_map.c \
    parser.tab.c lex.yy.c strvars.c && \
    cd /app && \
    gcc -I/app/api_server -I/app/src -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET \
    /app/api_server/servidor_api.c /app/api_server/typeeasy.c /app/api_server/civetweb.c /app/api_server/te_websocket.c \
    /app/src/ast.o /app/src/bytecode.o /app/src/mysql_bridge.o /app/src/postgres_bridge.o /app/src/sqlserver_bridge.o /app/src/db_params.o /app/src/orm_bridge.o \
    /app/src/typeeasy_api.o /app/src/debugger.o /app/src/te_builtins.o /app/src/te_http.o /app/src/te_json.o /app/src/te_bytecode.o /app/src/te_csv.o /app/src/te_colcache.o /app/src/te_stdlib.o /app/src/te_bridge.o /app/src/te_async.o /app/src/te_linq.o /app/src/te_linq_ops.o /app/src/te_math.o /app/src/te_string.o /app/src/te_list.o /app/src/te_map.o /app/src/parser.tab.o /app/src/lex.yy.o /app/src/strvars.o \
    -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto -lmysqlclient -lpq -lsybdb -lfl -lm

# =================================================================
# ETAPA 4: IMAGEN FINAL PARA LA API (Lista para producción)
# =================================================================
FROM debian:12-slim AS api

WORKDIR /app

# Instala las librerías SSL de runtime Y la librería de Flex (libfl2)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 default-libmysqlclient-dev libpq5 libsybdb5 libcurl4 libgomp1 ca-certificates && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable del servidor API desde la etapa de construcción
COPY --from=api_builder /app/typeeasy_api .

# Copia el ejecutable typeeasy para invocar funciones con popen
COPY --from=typeeasy_builder /app/src/typeeasy .

# Copia los scripts de la API desde la etapa 'base'
COPY --from=base /app/typeeasycode/apis ./apis
COPY --from=base /app/typeeasycode/models ./models
COPY --from=base /app/typeeasycode/settings ./settings

EXPOSE 8080

# Comando para iniciar el servidor
CMD ["./typeeasy_api"]

# =================================================================
# ETAPA 5: IMAGEN FINAL PARA EL INTÉRPRETE (Lista para producción)
# =================================================================
FROM debian:12-slim AS typeeasy

WORKDIR /typeeasy

# Instala la librería 'libfl' que falta (+ gdbserver para depuración remota)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libfl2 default-libmysqlclient-dev libpq5 libsybdb5 libssl3 libcurl4 libgomp1 libsqlite3-0 ca-certificates wabt gdbserver && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable del intérprete desde la etapa de construcción.
COPY --from=typeeasy_builder /app/src/typeeasy .
# Copia los scripts del proyecto para que puedan ejecutarse dentro de la imagen
COPY --from=base /app/typeeasycode ./typeeasycode
# Instala el plugin SQLite (load_native("sqlite")) en la ruta que busca el
# intérprete, para que los ejemplos/tests de SQLite funcionen dentro de Docker.
# Se copia desde typeeasy_builder, donde se compila desde fuente (no del checkout).
COPY --from=typeeasy_builder /app/plugins/sqlite/libte_sqlite.so /root/.te/packages/sqlite/libte_sqlite.so
# El punto de entrada es el propio intérprete.
ENTRYPOINT ["./typeeasy"]

# =================================================================
# ETAPA 6: IMAGEN FINAL PARA EL AGENTE
# =================================================================
FROM debian:12-slim AS agent

WORKDIR /app
# Instala las dependencias de runtime: libssl (para civetweb) y libfl2 (para flex)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 default-libmysqlclient-dev libcurl4 libpq5 libsybdb5 libgomp1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable 'servidor_agent' desde el builder
COPY --from=typeeasy_builder /app/src/servidor_agent .

# Copia el script del agente
COPY --from=base /app/typeeasycode/agente_chat_whatsapp.te .
COPY --from=base /app/typeeasycode/agente_gemini_whatsapp.te .

# Copia el shim Rust compilado (si existe) y lo renombra para que el runtime
# lo encuentre como `motor_nlu_local` (libmotor_nlu_local.so)
RUN mkdir -p /app/native_libs || true
COPY --from=shim_builder /shim/motor_nlu_shim/target/release/libmotor_nlu_shim.so /app/native_libs/libmotor_nlu_local.so

EXPOSE 8081

# Comando para iniciar el servidor de agente
CMD ["./servidor_agent", "agente_chat_whatsapp.te"]
