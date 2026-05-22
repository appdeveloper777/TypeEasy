/* te_list.h - LIST instance methods dispatcher (Nivel B paso 2.c).
 *
 * Handles .size/.length/.contains/.reverse/.sort/.get/.join over a LIST
 * Variable. The caller must have already resolved the receiver and obtained
 * the underlying `list` ASTNode (the head of the linked list chain).
 *
 * Returns 1 if the method name matched and was dispatched (with __ret__
 * possibly set), 0 otherwise (caller should fall through to its remaining
 * dispatch logic).
 */
#ifndef TE_LIST_H
#define TE_LIST_H

#include "ast.h"

int te_list_method_dispatch(ASTNode *node, ASTNode *list);

#endif /* TE_LIST_H */
