/* ============================================================================
 * te_math.h — Math.* pseudo-static method dispatcher.
 *
 * Extracted from interpret_call_method in ast.c (Nivel B paso 2.a).
 * The receiver is the identifier "Math" (no instance state); methods are
 * pure functions over one or two numeric args.
 * ============================================================================ */
#ifndef TE_MATH_H
#define TE_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

/* Dispatch a Math.<method>(args) call. Returns:
 *   1  if the receiver is "Math" (handled — __ret__ already written, or error
 *      printed for unknown methods).
 *   0  if the receiver is not "Math" (caller continues with the next
 *      dispatcher in the interpret_call_method chain).
 */
int te_math_method_dispatch(ASTNode *node, ASTNode *objNode);

#ifdef __cplusplus
}
#endif
#endif /* TE_MATH_H */
