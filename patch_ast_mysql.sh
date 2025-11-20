#!/bin/bash
# Script para aplicar cambios MySQL a ast.c

cd "$(dirname "$0")"

# 1. Agregar include de mysql_bridge.h después de bytecode.h
sed -i '10 a #include "mysql_bridge.h"' src/ast.c

# 2. Reemplazar call_native_function
sed -i '/^int call_native_function/,/^}/c\
int call_native_function(const char *name, ASTNode *arg) {\
    if (strcmp(name, "json") == 0) {\
        native_json(arg);\
        return 1;\
    }\
    if (strcmp(name, "mysql_connect") == 0) {\
        native_mysql_connect(arg);\
        return 1;\
    }\
    if (strcmp(name, "mysql_query") == 0) {\
        native_mysql_query(arg);\
        return 1;\
    }\
    if (strcmp(name, "mysql_close") == 0) {\
        native_mysql_close(arg);\
        return 1;\
    }\
    return 0;\
}' src/ast.c

echo "✓ ast.c actualizado con soporte MySQL"
