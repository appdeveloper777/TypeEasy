# TypeEasy/TypeEasy/Dockerfile

# =================================================================
# ETAPA 1: BUILDER BASE (Instala herramientas comunes)
# =================================================================
FROM gcc:13.2.0 AS base

# Instala las herramientas que ambos componentes necesitan
RUN apt-get update && \
    apt-get install -y flex bison libssl-dev && \
    apt-get clean

WORKDIR /app

# Copia todo el proyecto una sola vez
COPY . .

# =================================================================
# ETAPA 2: COMPILAR EL INTÉRPRETE "typeeasy"
# =================================================================
FROM base AS typeeasy_builder

# Usamos el Makefile para construir el ejecutable "typeeasy"
RUN make -C src clean && make -C src

# =================================================================
# ETAPA 3: COMPILAR EL SERVIDOR "typeeasy_api"
# =================================================================
FROM base AS api_builder

# Primero detectamos la versión de OpenSSL instalada
RUN openssl version

# Compilamos con la versión correcta de OpenSSL API
# Para OpenSSL 3.x usa OPENSSL_API_1_1, para versiones anteriores usa OPENSSL_API_1_0
# Corrected line for your Dockerfile
RUN gcc -Iapi_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 \
    api_server/servidor_api.c api_server/civetweb.c \
    -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto



# Si la versión anterior falla, prueba con OPENSSL_API_1_0
# RUN gcc -Iapi_server -DUSE_OPENSSL -DOPENSSL_API_1_0 \
#     api_server/servidor_test.c api_server/civetweb.c \
#     -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto

# =================================================================
# ETAPA 4: IMAGEN FINAL PARA LA API (Lista para producción)
# =================================================================
FROM debian:12-slim AS api

WORKDIR /app

# Instala las librerías SSL de runtime
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable desde la etapa de construcción LOCAL api_builder
COPY --from=api_builder /app/typeeasy_api .

EXPOSE 8080

# Comando para iniciar el servidor
CMD ["./typeeasy_api", "8080"]

# =================================================================
# ETAPA 5: IMAGEN FINAL PARA EL INTÉRPRETE (Lista para producción)
# =================================================================
FROM debian:12-slim AS typeeasy

WORKDIR /typeeasy

# Copia el ejecutable del intérprete desde la etapa de construcción.
# Asumimos que el Makefile lo crea en 'src/typeeasy'.
COPY --from=typeeasy_builder /app/src/typeeasy .

# El punto de entrada es el propio intérprete.
ENTRYPOINT ["./typeeasy"]