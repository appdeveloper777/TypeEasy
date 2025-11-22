#!/bin/bash
# Script para aplicar el fix de salida XML duplicada

cd "c:\Users\FERNANDO INGUNZA\Documents\TypeEasy Staging 2\TypeEasy"

# Backup del archivo original
cp src/ast.c src/ast.c.backup

# Aplicar el fix usando sed
# Reemplazar las líneas 1955-1956
sed -i '1955,1956c\
                ASTNode *ret_node = create_ast_leaf("STRING", 0, strdup(var->value.string_value), NULL);\
                add_or_update_variable("__ret__", ret_node);\
                free_ast(ret_node);\
                return;' src/ast.c

echo "Fix aplicado. Compilando..."
docker compose build api && docker compose up -d api

echo "Probando endpoint..."
sleep 5
curl http://localhost:8080/api/mysql/usuarios/xml 2>&1 | grep -v "DEBUG" | head -50
