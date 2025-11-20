#ifndef MYSQL_BRIDGE_H
#define MYSQL_BRIDGE_H

#include "ast.h"

// Funciones nativas MySQL para TypeEasy
void native_mysql_connect(ASTNode* args);
void native_mysql_query(ASTNode* args);
void native_mysql_close(ASTNode* args);

#endif // MYSQL_BRIDGE_H
