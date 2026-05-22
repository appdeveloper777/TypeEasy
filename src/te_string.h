/* ============================================================================
 * te_string.h — string instance method dispatcher.
 *
 * Extracted from interpret_call_method in ast.c (Nivel B paso 2.b).
 * Receiver can be a string literal ("foo".upper()) or a variable holding
 * a string. Method set: upper/lower/trim/contains/split/length plus
 * Ola 13 extras (replace/substr/find/starts_with/ends_with/repeat/
 * parse_int/parse_float/char_at/char_code).
 * ============================================================================ */
#ifndef TE_STRING_H
#define TE_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

/* Dispatch a <string>.<method>(args) call. Returns:
 *   1  if the receiver is a string and the method matched (handled —
 *      __ret__ has been written).
 *   0  otherwise (caller continues with the next dispatcher).
 *
 * `v` must be the Variable* already resolved from objNode->id (may be NULL
 * if the receiver is a STRING literal).
 */
int te_string_method_dispatch(ASTNode *node, ASTNode *objNode, Variable *v);

#ifdef __cplusplus
}
#endif
#endif /* TE_STRING_H */
