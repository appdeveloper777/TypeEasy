/* db_stubs_optional.c — stubs de los conectores SQL DESACTIVADOS en build.
 *
 * Permite un binario "sqlite-only" (p.ej. Android NDK/Bionic, o cualquier
 * target sin las libs pesadas de glibc) que NO enlaza libmysqlclient / libpq /
 * libsybdb. Se compila SIEMPRE (esta en MOTOR_C_FILES), pero cada bloque solo
 * aporta simbolos cuando su motor esta APAGADO:
 *
 *   - WITH_MYSQL  definido (default) -> bloque MySQL vacio; se enlaza el bridge
 *                                       real mysql_bridge.c (cero cambios).
 *   - WITH_MYSQL  NO definido        -> aqui van los stubs (no-op + error claro)
 *                                       y el Makefile NO compila mysql_bridge.c.
 *
 * Lo mismo para WITH_PG (postgres) y WITH_SYBASE (sql server / freetds).
 *
 * IMPORTANTE: con la config por defecto (WITH_MYSQL=WITH_PG=WITH_SYBASE=1)
 * este archivo compila a NADA -> el binario y su comportamiento son identicos
 * a los de siempre. Solo el build opt-in sqlite-only (Android) activa stubs.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ast.h"

#if !defined(WITH_MYSQL) || !defined(WITH_PG) || !defined(WITH_SYBASE)
/* Mensaje uniforme: el motor X no esta compilado en esta build sqlite-only. */
static void te_db_unavailable(const char *engine, const char *fn) {
    fprintf(stderr,
        "[typeeasy] '%s' is not available: the %s connector was not compiled into\n"
        "           this build (sqlite-only / Android). Use DB_ENGINE=sqlite, or a\n"
        "           full build (Linux x86_64/arm64) that ships %s.\n",
        fn, engine, engine);
}
/* Deja un -1 en __ret__ para que `if (conn < 0)` funcione igual que en Linux. */
static void te_db_ret_fail_int(void) {
    ASTNode *r = create_ast_leaf("NUMBER", -1, NULL, NULL);
    add_or_update_variable("__ret__", r); free_ast(r);
}
static void te_db_ret_fail_str(const char *engine) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s_unavailable_sqlite_only_build\"}", engine);
    ASTNode *r = create_ast_leaf("STRING", 0, strdup(buf), NULL);
    add_or_update_variable("__ret__", r); free_ast(r);
}
#endif

/* ------------------------------------------------------------------ MySQL */
#ifndef WITH_MYSQL
void native_mysql_connect(ASTNode *args) { (void)args; te_db_unavailable("MySQL", "mysql_connect"); te_db_ret_fail_int(); }
void native_mysql_query  (ASTNode *args) { (void)args; te_db_unavailable("MySQL", "mysql_query");   te_db_ret_fail_str("mysql"); }
void native_mysql_close  (ASTNode *args) { (void)args; te_db_unavailable("MySQL", "mysql_close"); }
void mysql_close_request_conns(void) { }
ASTNode* mysql_query_result(int conn_id, const char *query) { (void)conn_id; (void)query; return NULL; }
/* Helpers usados por orm_bridge.c (extern). Sin MySQL, el ORM via mysql no
 * tiene conexion -> NULL/no-op. */
char* mysql_escape_cb(const char *in, void *ctx) { (void)ctx; return in ? strdup(in) : NULL; }
void* mysql_get_conn(int conn_id) { (void)conn_id; return NULL; }
ASTNode* mysql_query_to_objects_fast(int conn_id, const char *query, ClassNode *cls) {
    (void)conn_id; (void)query; (void)cls; return NULL;
}
#endif

/* -------------------------------------------------------------- PostgreSQL */
#ifndef WITH_PG
void native_postgres_connect(ASTNode *args) { (void)args; te_db_unavailable("PostgreSQL", "postgres_connect"); te_db_ret_fail_int(); }
void native_postgres_query  (ASTNode *args) { (void)args; te_db_unavailable("PostgreSQL", "postgres_query");   te_db_ret_fail_str("postgres"); }
void native_postgres_close  (ASTNode *args) { (void)args; te_db_unavailable("PostgreSQL", "postgres_close"); }
void postgres_close_request_conns(void) { }
#endif

/* ------------------------------------------------------ SQL Server / Sybase */
#ifndef WITH_SYBASE
void native_sqlserver_connect(ASTNode *args) { (void)args; te_db_unavailable("SQL Server (FreeTDS)", "sqlserver_connect"); te_db_ret_fail_int(); }
void native_sqlserver_query  (ASTNode *args) { (void)args; te_db_unavailable("SQL Server (FreeTDS)", "sqlserver_query");   te_db_ret_fail_str("sqlserver"); }
void native_sqlserver_close  (ASTNode *args) { (void)args; te_db_unavailable("SQL Server (FreeTDS)", "sqlserver_close"); }
void sqlserver_close_request_conns(void) { }
#endif
