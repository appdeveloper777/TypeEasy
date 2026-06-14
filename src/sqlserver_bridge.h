#ifndef SQLSERVER_BRIDGE_H
#define SQLSERVER_BRIDGE_H

#include "ast.h"

// Funciones nativas SQL Server para TypeEasy (vía FreeTDS db-lib)
void native_sqlserver_connect(ASTNode* args);
void native_sqlserver_query(ASTNode* args);
void native_sqlserver_close(ASTNode* args);

/* Cierra las conexiones abiertas en el request actual que el script no cerró. */
void sqlserver_close_request_conns(void);

#endif // SQLSERVER_BRIDGE_H
