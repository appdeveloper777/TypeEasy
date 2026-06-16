#ifndef SQLSERVER_BRIDGE_H
#define SQLSERVER_BRIDGE_H

#include "ast.h"

// Funciones nativas SQL Server para TypeEasy (vía FreeTDS db-lib)
//
// Firma TypeEasy:
//   sqlserver_connect(host, user, password, database [, port=1433] [, opts])
//     opts (mapa opcional) — control TLS por conexión:
//       tls|ssl|encrypt:            1/"require" exige TLS; 0/"off" sin cifrado.
//       tls_skip_verify|tls_insecure|trust_server_certificate:
//                                   1 acepta cert self-signed (= sqlcmd -C).
//       tls_ca|ca:                  ruta PEM de la CA para validar la cadena.
//     Devuelve conn_id (>=0) o -1 en error; la causa real queda en la variable
//     de script __sqlserver_error__.
//   sqlserver_query(conn_id, sql [, params_map] [, "json"|"xml"])
//   sqlserver_close(conn_id)
void native_sqlserver_connect(ASTNode* args);
void native_sqlserver_query(ASTNode* args);
void native_sqlserver_close(ASTNode* args);

/* Cierra las conexiones abiertas en el request actual que el script no cerró. */
void sqlserver_close_request_conns(void);

#endif // SQLSERVER_BRIDGE_H
