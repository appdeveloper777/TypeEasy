#ifndef POSTGRES_BRIDGE_H
#define POSTGRES_BRIDGE_H

#include "ast.h"

// Funciones nativas PostgreSQL para TypeEasy
void native_postgres_connect(ASTNode* args);
void native_postgres_query(ASTNode* args);
void native_postgres_close(ASTNode* args);

/* Cierra las conexiones abiertas en el request actual que el script no cerró. */
void postgres_close_request_conns(void);

#endif // POSTGRES_BRIDGE_H
