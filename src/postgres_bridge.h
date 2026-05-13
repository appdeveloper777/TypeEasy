#ifndef POSTGRES_BRIDGE_H
#define POSTGRES_BRIDGE_H

#include "ast.h"

// Funciones nativas PostgreSQL para TypeEasy
void native_postgres_connect(ASTNode* args);
void native_postgres_query(ASTNode* args);
void native_postgres_close(ASTNode* args);

#endif // POSTGRES_BRIDGE_H
