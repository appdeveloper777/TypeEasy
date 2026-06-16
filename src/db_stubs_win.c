/* db_stubs_win.c — Stubs de Postgres/SQL Server para builds nativos en Windows.
 *
 * En MSYS2 no instalamos libpq para mantener el instalador ligero
 * de la primera release (v0.0.1). El interprete sigue funcionando para
 * todo lo demas (MySQL incluido); las funciones postgres_xxx
 * devuelven "no soportado" en esta plataforma.
 * En Linux el Makefile sigue compilando los bridges reales
 * (postgres_bridge.c / sqlserver_bridge.c).
 *
 * SQL Server: si el build define TE_WIN_HAVE_FREETDS (FreeTDS disponible via
 * MSYS2 mingw-w64-x86_64-freetds), se compila el bridge real sqlserver_bridge.c
 * y estos stubs de sqlserver_* se omiten. Sin esa macro, se usan los stubs.
 */

#include <stdio.h>
#include "postgres_bridge.h"
#include "sqlserver_bridge.h"

static void warn_unsupported(const char* name) {
    fprintf(stderr,
        "[typeeasy] '%s' no esta disponible en esta build de Windows.\n",
        name);
}

void native_postgres_connect(ASTNode* args) { (void)args; warn_unsupported("postgres_connect"); }
void native_postgres_query  (ASTNode* args) { (void)args; warn_unsupported("postgres_query");   }
void native_postgres_close  (ASTNode* args) { (void)args; warn_unsupported("postgres_close");   }

#ifndef TE_WIN_HAVE_FREETDS
void native_sqlserver_connect(ASTNode* args) { (void)args; warn_unsupported("sqlserver_connect"); }
void native_sqlserver_query  (ASTNode* args) { (void)args; warn_unsupported("sqlserver_query");   }
void native_sqlserver_close  (ASTNode* args) { (void)args; warn_unsupported("sqlserver_close");   }
#endif

/* Auto-cierre de conexiones por request: postgres es no-op en Windows (no hay
 * bridge real). ast.c los llama incondicionalmente al final de cada request,
 * asi que deben existir aqui para enlazar. sqlserver_close_request_conns lo
 * provee el bridge real cuando TE_WIN_HAVE_FREETDS esta definido. */
void postgres_close_request_conns(void)  { }
#ifndef TE_WIN_HAVE_FREETDS
void sqlserver_close_request_conns(void) { }
#endif
