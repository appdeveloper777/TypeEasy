/* te_value.h — aritmética entera exacta de 64 bits (Fase 1b). Ver te_value.c. */
#ifndef TE_VALUE_H
#define TE_VALUE_H

#include "ast.h"

/* 1 y *out si todo el subárbol es entero (sin efectos secundarios); 0 -> usar evaluate_expression. */
int te_eval_i64(ASTNode *node, long long *out);

#endif /* TE_VALUE_H */
