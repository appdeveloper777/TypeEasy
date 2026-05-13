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

#endif /* DB_PARAMS_H */
