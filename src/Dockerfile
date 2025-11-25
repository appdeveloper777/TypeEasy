# TypeEasy/TypeEasy/Dockerfile

# =================================================================
# ETAPA 1: BUILDER BASE (Instala herramientas comunes)
# =================================================================
FROM gcc:13.2.0 AS base

# Instala las herramientas que ambos componentes necesitan
RUN apt-get update && \
    apt-get install -y flex bison libssl-dev default-libmysqlclient-dev && \
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

# =================================================================
# ETAPA: COMPILAR EL SHIM RUST (motor_nlu_shim)
# =================================================================
FROM rust:1.83-slim AS shim_builder
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
    gcc -I../api_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -c \
    ast.c bytecode.c mysql_bridge.c orm_bridge.c typeeasy_api.c \
    parser.tab.c lex.yy.c strvars.c && \
    cd /app && \
    gcc -I/app/api_server -I/app/src -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 \
    /app/api_server/servidor_api.c /app/api_server/typeeasy.c /app/api_server/civetweb.c \
    /app/src/ast.o /app/src/bytecode.o /app/src/mysql_bridge.o /app/src/orm_bridge.o \
    /app/src/typeeasy_api.o /app/src/parser.tab.o /app/src/lex.yy.o /app/src/strvars.o \
    -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto -lmysqlclient -lfl -lm

# =================================================================
# ETAPA 4: IMAGEN FINAL PARA LA API (Lista para producción)
# =================================================================
FROM debian:12-slim AS api

WORKDIR /app

# Instala las librerías SSL de runtime Y la librería de Flex (libfl2)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 default-libmysqlclient-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable del servidor API desde la etapa de construcción
COPY --from=api_builder /app/typeeasy_api .

# Copia el ejecutable typeeasy para invocar funciones con popen
COPY --from=typeeasy_builder /app/src/typeeasy .

# Copia los scripts de la API desde la etapa 'base'
COPY --from=base /app/typeeasycode/apis ./apis

EXPOSE 8080

# Comando para iniciar el servidor
CMD ["./typeeasy_api"]

# =================================================================
# ETAPA 5: IMAGEN FINAL PARA EL INTÉRPRETE (Lista para producción)
# =================================================================
FROM debian:12-slim AS typeeasy

WORKDIR /typeeasy

# Instala la librería 'libfl' que falta
RUN apt-get update && \
    apt-get install -y --no-install-recommends libfl2 default-libmysqlclient-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable del intérprete desde la etapa de construcción.
COPY --from=typeeasy_builder /app/src/typeeasy .
# Copia los scripts del proyecto para que puedan ejecutarse dentro de la imagen
COPY --from=base /app/typeeasycode ./typeeasycode
# El punto de entrada es el propio intérprete.
ENTRYPOINT ["./typeeasy"]

# =================================================================
# ETAPA 6: IMAGEN FINAL PARA EL AGENTE
# =================================================================
FROM debian:12-slim AS agent

WORKDIR /app
# Instala las dependencias de runtime: libssl (para civetweb) y libfl2 (para flex)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 default-libmysqlclient-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable 'servidor_agent' desde el builder
COPY --from=typeeasy_builder /app/src/servidor_agent .

# Copia el script del agente
COPY --from=base /app/typeeasycode/agente_chat_whatsapp.te .

# Copia el shim Rust compilado (si existe) y lo renombra para que el runtime
# lo encuentre como `motor_nlu_local` (libmotor_nlu_local.so)
RUN mkdir -p /app/native_libs || true
COPY --from=shim_builder /shim/motor_nlu_shim/target/release/libmotor_nlu_shim.so /app/native_libs/libmotor_nlu_local.so

EXPOSE 8081

# Comando para iniciar el servidor de agente
CMD ["./servidor_agent", "agente_chat_whatsapp.te"]
