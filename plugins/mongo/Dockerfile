# Build container for libte_mongo.so
# Uso:
#   docker build -f plugins/mongo/Dockerfile -t te-mongo-builder .
#   docker run --rm -v "$PWD/plugins/mongo:/out" te-mongo-builder
# Resultado: plugins/mongo/libte_mongo.so

FROM gcc:13.2.0 AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends libmongoc-dev libbson-dev pkg-config && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Headers del host (te_builtins.h + ast.h)
COPY src/te_builtins.h /build/include/te_builtins.h
COPY src/ast.h         /build/include/ast.h

# Plugin
COPY plugins/mongo/mongo_plugin.c /build/mongo_plugin.c

RUN gcc -shared -fPIC -O2 -Wall -Wno-unused-parameter \
        -I/build/include \
        $(pkg-config --cflags libmongoc-1.0) \
        mongo_plugin.c \
        $(pkg-config --libs libmongoc-1.0) \
        -o /build/libte_mongo.so

# Copia el .so al volumen montado
ENTRYPOINT ["sh", "-c", "cp /build/libte_mongo.so /out/libte_mongo.so && echo '[ok] /out/libte_mongo.so' && ls -lh /out/libte_mongo.so"]
