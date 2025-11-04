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
# ETAPA 2: COMPILAR EL "MOTOR" (typeeasy y servidor_agent)
# =================================================================
FROM base AS typeeasy_builder

# 'make all' ahora construye AMBOS: 'typeeasy' y 'servidor_agent'
# Asume que tu Makefile modificado está en /app/src/
RUN make -C /app/src clean && make -C /app/src all

# =================================================================
# ETAPA 3: COMPILAR EL SERVIDOR "typeeasy_api"
# =================================================================
FROM base AS api_builder

# Primero detectamos la versión de OpenSSL instalada
RUN openssl version

# Compilamos con la versión correcta de OpenSSL API
RUN gcc -I/app/api_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 \
    /app/api_server/servidor_api.c /app/api_server/civetweb.c \
    -o /app/typeeasy_api -lpthread -ldl -lssl -lcrypto

# =================================================================
# ETAPA 4: IMAGEN FINAL PARA LA API (Lista para producción)
# =================================================================
# =================================================================
# ETAPA 4: IMAGEN FINAL PARA LA API (Lista para producción)
# =================================================================
FROM debian:12-slim AS api

WORKDIR /app

# ¡¡ARREGLO AQUÍ!!
# Instala las librerías SSL de runtime Y la librería de Flex (libfl2)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable desde la etapa de construcción LOCAL api_builder
COPY --from=api_builder /app/typeeasy_api .
# Copia el ejecutable del INTÉRPRETE desde la etapa 'typeeasy_builder',
# ya que el servidor lo necesita para ejecutar los scripts.
COPY --from=typeeasy_builder /app/src/typeeasy .

# Copia los scripts de la API desde la etapa 'base' donde se copiaron originalmente.
COPY typeeasycode/apis ./apis

EXPOSE 8080

# Comando para iniciar el servidor
CMD ["./typeeasy_api"]

# =================================================================
# ETAPA 5: IMAGEN FINAL PARA EL INTÉRPRETE (Lista para producción)
# =================================================================
FROM debian:12-slim AS typeeasy

WORKDIR /typeeasy

# ¡¡ARREGLO AQUÍ!! Instala la librería 'libfl' que falta
RUN apt-get update && \
    apt-get install -y --no-install-recommends libfl2 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable del intérprete desde la etapa de construcción.
COPY --from=typeeasy_builder /app/src/typeeasy .
# El punto de entrada es el propio intérprete.
ENTRYPOINT ["./typeeasy"]

# =================================================================
# ETAPA 6: ¡NUEVA! IMAGEN FINAL PARA EL AGENTE
# =================================================================
FROM debian:12-slim AS agent

WORKDIR /app
# Instala las dependencias de runtime: libssl (para civetweb) y libfl2 (para flex)
RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl3 libfl2 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Copia el ejecutable 'servidor_agent' desde el builder
COPY --from=typeeasy_builder /app/src/servidor_agent .

# Copia el script del agente
COPY typeeasycode/agente_chat_whatsapp.te .

EXPOSE 8081

# Comando para iniciar el servidor de agente
CMD ["./servidor_agent", "agente_chat_whatsapp.te"]