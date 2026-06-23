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
/* Definida en ast.c; declararla aqui evita la declaracion implicita en te_map.c
 * (error duro en clang 16+, p.ej. el NDK de Android usa clang 17). */
ASTNode *map_find_pair(ASTNode *map, const char *key);

#endif /* TE_MAP_H */
