#ifndef DB_PARAMS_H
#define DB_PARAMS_H

#include "ast.h"

/* Estilo Dapper para los bridges MySQL/Postgres/SQL Server.
 *
 * Sintaxis en TypeEasy:
 *   let r = new mysql_query(c,
 *       "SELECT * FROM users WHERE id = @id AND name = @name",
 *       { "@id": 5, "@name": "foo" },
 *       "json"); // formato opcional, default "json"
 *
 * Los placeholders son @nombre. Las claves del map pueden ir con o sin '@'.
 * Tipos soportados: NUMBER (int), STRING, NULL, IDENTIFIER (resuelto a su valor).
 */

/* Devuelve la cabeza (head) de la lista de KV_PAIR si args[idx] resuelve a un MAP
 * o a una instancia de clase (sus atributos se exponen como pares clave/valor).
 * Si la lista se sintetizó (caso de instancia de objeto), *out_owned se setea a 1
 * y el caller debe liberarla con free_ast(head). Caso contrario *out_owned = 0.
 * Devuelve NULL si args[idx] no aplica. out_owned puede ser NULL. */
ASTNode* db_arg_as_map_head(ASTNode* args, int idx, int* out_owned);

/* Función de escape para strings, provista por cada driver.
 * Debe devolver un buffer malloc'd con la versión escapada de `in` (sin comillas).
 * `ctx` es opaco (típicamente el handle de conexión). */
typedef char* (*db_escape_fn)(const char* in, void* ctx);

/* Sustituye placeholders @nombre en `sql` por los valores del map.
 * Devuelve un buffer malloc'd con el SQL final. Caller hace free.
 * Strings: se escapan con `escape` y se envuelven en comillas simples.
 * Numbers: se interpolan crudo. NULL → NULL.
 * Respeta strings literales en el SQL ('texto', "texto"). */
char* db_substitute_params(const char* sql, ASTNode* params_head,
                           db_escape_fn escape, void* ctx);

/* Flag opt-in: si != 0, los parametros STRING cuyo valor es "" se enlazan
 * como SQL NULL en vez de ''. Se cambia desde TypeEasy con el builtin
 * sql_set_empty_as_null(true) (o env TYPEEASY_SQL_EMPTY_AS_NULL=1). Pensado
 * para eliminar la ceremonia COALESCE(NULLIF(@col,''),...) cuando un form
 * manda "" a columnas numericas/fecha bajo STRICT_TRANS_TABLES. */
extern int g_db_empty_as_null;

/* Flag opt-in: si != 0 Y el proceso corre en modo --api (g_api_mode=1), un
 * fallo de query (mysql/postgres) fija automaticamente response_status(500)
 * ademas de devolver el string {"error":...}. Se cambia con sql_set_strict_
 * errors() (o env TYPEEASY_SQL_STRICT_ERRORS=1). Evita responder HTTP 200 OK
 * cuando la fila no se guardo. En CLI scripts es no-op: no hay respuesta
 * HTTP que cambiar, el script sigue recibiendo el mismo string de error y
 * decide que hacer (chequearlo, ignorarlo, lanzar throw, etc.). */
extern int g_db_strict_errors;

/* Flag opt-in: si != 0, la fachada generica sql_query/sql_exec envuelve el
 * resultado en { success:bool, data|error } uniforme para todos los motores.
 * Se cambia con sql_set_envelope() (o env TYPEEASY_SQL_ENVELOPE=1). OFF por
 * defecto: no rompe el codigo existente. */
extern int g_db_envelope;

#endif /* DB_PARAMS_H */
