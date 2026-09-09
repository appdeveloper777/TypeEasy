/* te_num.h — Semántica numérica ÚNICA del intérprete (walker y acelerador bytecode).
 *
 * Todo lo que define "qué hace + - * / % && || comparaciones sobre números" y
 * "cómo se almacena un número en una Variable" vive aquí como static inline:
 *   - una sola definición (ast.c y te_bytecode.c la incluyen, no la copian);
 *   - cero overhead en el bucle caliente del bytecode (se inlinea en bc_exec).
 * Cualquier cambio de regla se hace en este archivo y aplica a ambos motores.
 */
#ifndef TE_NUM_H
#define TE_NUM_H

#include <stdio.h>
#include "ast.h"

static inline double te_num_binop(NodeKind k, double a, double b) {
    switch (k) {
    case NK_ADD: return a + b;
    case NK_SUB: return a - b;
    case NK_MUL: return a * b;
    case NK_DIV:
        if (b == 0.0) { printf("Error: division by zero.\n"); return 0; }
        return a / b;
    case NK_MOD: {
        long long lv = (long long)a, rv = (long long)b;
        if (rv == 0) { printf("Error: modulo by zero.\n"); return 0; }
        return (double)(lv % rv);
    }
    case NK_GT:    return a > b;
    case NK_LT:    return a < b;
    /* Mapeo histórico del parser: NK_GT_EQ evalúa <=, NK_LT_EQ evalúa >=. */
    case NK_GT_EQ: return a <= b;
    case NK_LT_EQ: return a >= b;
    case NK_EQ:    return a == b;
    case NK_DIFF:  return a != b;
    case NK_AND:   return (a != 0.0 && b != 0.0) ? 1 : 0;
    case NK_OR:    return (a != 0.0 || b != 0.0) ? 1 : 0;
    case NK_BIT_AND: return (double)((long long)a & (long long)b);
    case NK_BIT_OR:  return (double)((long long)a | (long long)b);
    case NK_BIT_XOR: return (double)((long long)a ^ (long long)b);
    case NK_SHL:     return (double)((long long)a << (long long)b);
    case NK_SHR:     return (double)((long long)a >> (long long)b);
    default: return 0;
    }
}

static inline double te_num_unop(NodeKind k, double a) {
    switch (k) {
    case NK_NEG:     return -a;
    case NK_NOT:     return a != 0.0 ? 0 : 1;
    case NK_BIT_NOT: return (double)(~(long long)a);
    default: return a;
    }
}

/* Regla del fast-path de asignación numérica (interpret_assign / BC_STORE_VAR):
 * un double integral queda INT, si no FLOAT. Solo cambia vtype; el tag `type`
 * lo mantiene la declaración. */
static inline void te_num_store(Variable *v, double r) {
    if (r == (double)(long long)r) {
        v->vtype = VAL_INT;
        v->value.int_value = (long long)r;
    } else {
        v->vtype = VAL_FLOAT;
        v->value.float_value = r;
    }
}

static inline void te_num_store_i64(Variable *v, long long r) {
    v->vtype = VAL_INT;
    v->value.int_value = r;
}

#endif /* TE_NUM_H */
