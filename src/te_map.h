/* te_map.h - MAP instance methods dispatcher (Nivel B paso 2.d).
 *
 * Handles .keys/.values/.has/.remove/.size/.length/.clear over a MAP Variable.
 * The caller must have already resolved the receiver and obtained the
 * underlying `map` ASTNode (the head of the kv-pair chain).
 *
 * Returns 1 if the method name matched and was dispatched (with __ret__
 * possibly set), 0 otherwise (caller should fall through).
 */
#ifndef TE_MAP_H
#define TE_MAP_H

#include "ast.h"

int te_map_method_dispatch(ASTNode *node, ASTNode *map);

#endif /* TE_MAP_H */
