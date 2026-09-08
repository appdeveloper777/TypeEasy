/* te_value.c — Fase 1b del plan de deuda: aritmética entera EXACTA de 64 bits.
 *
 * evaluate_expression() devuelve double (y su fast-path de bytecode también), así que
 * cualquier entero > 2^53 pierde precisión al pasar por ahí. te_eval_i64() evalúa un
 * subárbol en `long long` cuando TODO él es entero (literales, identificadores VAL_INT,
 * + - * % división exacta, negación, comparaciones). Si encuentra algo no-entero devuelve 0
 * SIN efectos secundarios y el llamador cae a evaluate_expression() como antes.
 * Los sitios que almacenan en int_value (declaración, asignación, +=, return, argumentos)
 * lo intentan primero. Es una vía paralela, no un reemplazo: cero cambio para floats/strings.
 */
#include <string.h>
#include "ast.h"
#include "ast_internal.h"
#include "te_vm.h"
#include "te_value.h"

static int lookup_int(ASTNode *n, long long *out) {
    if (!n->id) return 0;
    Variable *v = (Variable *)n->cached_var;
    if (v && (!v->id || strcmp(v->id, n->id) != 0)) v = NULL;   /* slot reciclado entre requests */
    if (!v) v = find_variable(n->id);
    if (!v || v->vtype != VAL_INT) return 0;
    if (v->type && strcmp(v->type, "NULL") == 0) return 0;
    *out = v->value.int_value;
    return 1;
}

int te_eval_i64(ASTNode *node, long long *out) {
    if (!node || !out) return 0;
    long long a, b;
    switch (nk_of(node)) {
    case NK_NUMBER:
    case NK_INT:
        *out = node->value; return 1;
    case NK_IDENTIFIER:
    case NK_ID:
        return lookup_int(node, out);
    case NK_ACCESS_EXPR: {   /* lista[i] con item entero */
        ASTNode *lst = resolve_to_list(node->left);
        long long idx;
        if (!lst || !te_eval_i64(node->right, &idx) || idx < 0) return 0;
        ASTNode *it = list_get_item(lst, (int)idx);
        if (!it || (nk_of(it) != NK_NUMBER && nk_of(it) != NK_INT)) return 0;
        *out = it->value; return 1;
    }
    case NK_NEG:
        if (!te_eval_i64(node->left, &a)) return 0;
        *out = -a; return 1;
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_MOD: case NK_DIV:
    case NK_GT: case NK_LT: case NK_GT_EQ: case NK_LT_EQ: case NK_EQ: case NK_DIFF:
        if (!te_eval_i64(node->left, &a) || !te_eval_i64(node->right, &b)) return 0;
        break;
    default:
        return 0;
    }
    switch (nk_of(node)) {
    case NK_ADD: *out = a + b; return 1;
    case NK_SUB: *out = a - b; return 1;
    case NK_MUL: *out = a * b; return 1;
    case NK_MOD: if (b == 0) return 0; *out = a % b; return 1;
    case NK_DIV: if (b == 0 || a % b != 0) return 0; *out = a / b; return 1;   /* no exacta -> float */
    case NK_GT:    *out = a > b;  return 1;
    case NK_LT:    *out = a < b;  return 1;
    /* Mismo mapeo que evaluate_expression (NK_GT_EQ evalúa <=, NK_LT_EQ evalúa >=). */
    case NK_GT_EQ: *out = a <= b; return 1;
    case NK_LT_EQ: *out = a >= b; return 1;
    case NK_EQ:    *out = a == b; return 1;
    case NK_DIFF:  *out = a != b; return 1;
    default: return 0;
    }
}
