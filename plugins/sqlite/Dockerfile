# Build container for libte_sqlite.so (static SQLite amalgamation; no host libsqlite3 dep).
# Uso:
#   docker build -f plugins/sqlite/Dockerfile -t te-sqlite-builder .
#   docker run --rm -v "$PWD/plugins/sqlite:/out" te-sqlite-builder
# Resultado: plugins/sqlite/libte_sqlite.so

FROM gcc:13.2.0 AS builder

ARG SQLITE_VERSION=3530100
ARG SQLITE_YEAR=2026

RUN apt-get update && \
    apt-get install -y --no-install-recommends curl unzip ca-certificates && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Headers del host (te_builtins.h + ast.h)
COPY src/te_builtins.h /build/include/te_builtins.h
COPY src/ast.h         /build/include/ast.h

# Plugin
COPY plugins/sqlite/sqlite_plugin.c /build/sqlite_plugin.c

# SQLite amalgamation (sqlite3.c) — compilado estáticamente dentro del .so
RUN curl -fL -o sqlite.zip \
        "https://sqlite.org/${SQLITE_YEAR}/sqlite-amalgamation-${SQLITE_VERSION}.zip" && \
    unzip -q sqlite.zip && \
    mv "sqlite-amalgamation-${SQLITE_VERSION}" sqlite-amalgamation && \
    rm sqlite.zip

RUN gcc -shared -fPIC -O2 -Wall -Wno-unused-parameter \
        -I/build/include \
        -Isqlite-amalgamation \
        -DSQLITE_THREADSAFE=1 \
        -DSQLITE_ENABLE_FTS5 \
        -DSQLITE_ENABLE_JSON1 \
        -DSQLITE_OMIT_LOAD_EXTENSION \
        sqlite_plugin.c \
        sqlite-amalgamation/sqlite3.c \
        -lpthread -ldl -lm \
        -o /build/libte_sqlite.so

# Copia el .so al volumen montado
ENTRYPOINT ["sh", "-c", "cp /build/libte_sqlite.so /out/libte_sqlite.so && echo '[ok] /out/libte_sqlite.so' && ls -lh /out/libte_sqlite.so"]
