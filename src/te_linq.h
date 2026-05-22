/* ============================================================================
 * te_linq.h — lazy LINQ pipeline (LAZY_ITER + LAZY_OP chain).
 *
 * Extracted from ast.c as part of the Nivel B modularization. Implements the
 * lazy evaluation infrastructure that backs `.where/.filter/.select/.map/
 * .take/.skip` (intermediate ops) and `.toList/.count/.sum/.first/.forEach/
 * .reduce/.any/.every` (terminal ops).
 *
 * The interpret_call_method dispatcher in ast.c is the only caller of the
 * public surface below; the helpers internal to the pipeline are file-static
 * in te_linq.c.
 * ============================================================================ */
#ifndef TE_LINQ_H
#define TE_LINQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

/* Build a new LAZY_ITER from an existing parent (or raw LIST) plus one extra
 * op. Intermediate dispatch builds the chain by calling this for each method
 * in the `.where().select()...` pipeline.
 *   parent_lazy : previous LAZY_ITER node, or NULL on first op.
 *   src_list_raw: source LIST when parent_lazy is NULL (ignored otherwise).
 *   op_name     : "where"|"filter"|"select"|"map"|"take"|"skip".
 *   lambda_arg  : lambda for where/filter/select/map; NULL for take/skip.
 *   num_arg     : take/skip count; 0 for lambda ops.
 */
ASTNode *lazy_extend(ASTNode *parent_lazy, ASTNode *src_list_raw,
                     const char *op_name, ASTNode *lambda_arg, int num_arg);

/* Resolve a method argument to a lambda AST node. Accepts an inline
 * "LAMBDA" node or an IDENTIFIER referencing a variable of type LAMBDA.
 * Returns NULL if the argument is not a lambda. */
ASTNode *lazy_resolve_lambda_arg(ASTNode *arg);

/* Drain the pipeline for a terminal method (toList/count/sum/first/forEach/
 * reduce/any/every). Sets __ret__ on success. Returns 1 if handled, 0 if
 * t_name in `node->id` is not a recognised terminal (caller falls through). */
int lazy_terminal(ASTNode *lazy_node, ASTNode *node);

#ifdef __cplusplus
}
#endif

#endif /* TE_LINQ_H */
