# Build container for libte_sqlite.so
# Uso:
#   docker build -f plugins/sqlite/Dockerfile -t te-sqlite-builder .
#   docker run --rm -v "$PWD/plugins/sqlite:/out" te-sqlite-builder
# Resultado: plugins/sqlite/libte_sqlite.so

FROM gcc:13.2.0 AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends libsqlite3-dev pkg-config && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Headers del host (te_builtins.h + ast.h)
COPY src/te_builtins.h /build/include/te_builtins.h
COPY src/ast.h         /build/include/ast.h

# Plugin
COPY plugins/sqlite/sqlite_plugin.c /build/sqlite_plugin.c

RUN gcc -shared -fPIC -O2 -Wall -Wno-unused-parameter \
        -I/build/include \
        sqlite_plugin.c \
        -lsqlite3 \
        -o /build/libte_sqlite.so

# Copia el .so al volumen montado
ENTRYPOINT ["sh", "-c", "cp /build/libte_sqlite.so /out/libte_sqlite.so && echo '[ok] /out/libte_sqlite.so' && ls -lh /out/libte_sqlite.so"]
