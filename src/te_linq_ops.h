/* te_linq_ops.h — LINQ-with-lambda dispatcher (Nivel B paso 2.f).
 *
 * Extracted from ast.c interpret_call_method. Contains the ~1040-LOC
 * dispatcher for all LINQ ops that REQUIRE a lambda argument:
 *   map/filter/reduce/forEach/find/any/every/none, where/select/all,
 *   firstWhere/lastWhere, countWhere/sumBy/avgBy/minBy/maxBy,
 *   takeWhile/skipWhile, flatMap/selectMany, groupBy,
 *   orderBy/orderByDescending, distinctBy, aggregate/fold, toMap/toDictionary.
 *
 * Includes COLUMNAR fast-paths and parallel filter/sumBy/count via OpenMP
 * when the receiver list is an OBJECT[] with a FastLambda-pathable body.
 */
#ifndef TE_LINQ_OPS_H
#define TE_LINQ_OPS_H

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the method name matched a LINQ-with-lambda op and was
 * dispatched (caller must return immediately). Returns 0 to fall through. */
int te_linq_ops_method_dispatch(ASTNode *node, ASTNode *list);

#ifdef __cplusplus
}
#endif

#endif /* TE_LINQ_OPS_H */
