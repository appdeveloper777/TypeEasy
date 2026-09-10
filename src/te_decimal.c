/* te_decimal.c — Tipo `decimal` (ver te_decimal.h). */
#include "te_decimal.h"
#include "te_vm.h"
#include "ast_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void te_set_ret_string(const char *s);   /* ast.c */
void te_set_ret_int(int n);

static __int128 pow10_128(int n) {
    __int128 p = 1;
    while (n-- > 0) p *= 10;
    return p;
}

static int dec_overflow(const TeDec *d) {
    __int128 m = d->m < 0 ? -d->m : d->m;
    if (m >= pow10_128(38)) { printf("Error: decimal overflow.\n"); return 1; }
    return 0;
}

int te_dec_parse(const char *s, TeDec *out) {
    if (!s || !out) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    __int128 m = 0; int scale = 0, digits = 0, seen_dot = 0;
    for (; *s; s++) {
        if (*s == '_') continue;
        if (*s == '.') { if (seen_dot) return 0; seen_dot = 1; continue; }
        if (!isdigit((unsigned char)*s)) break;
        if (seen_dot) { if (scale >= TE_DEC_MAX_SCALE) continue; scale++; }   /* dígitos extra se truncan */
        m = m * 10 + (*s - '0'); digits++;
        if (digits > 38) return 0;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s || digits == 0) return 0;
    out->m = neg ? -m : m; out->scale = scale;
    return 1;
}

void te_dec_format(const TeDec *d, char *buf, size_t cap) {
    if (!buf || cap == 0) return;
    char digits[48]; int n = 0;
    __int128 m = d->m < 0 ? -d->m : d->m;
    if (m == 0) digits[n++] = '0';
    while (m > 0) { digits[n++] = (char)('0' + (int)(m % 10)); m /= 10; }
    while (n <= d->scale) digits[n++] = '0';           /* "0.05": relleno hasta el punto */
    size_t o = 0;
    if (d->m < 0 && o + 1 < cap) buf[o++] = '-';
    for (int i = n - 1; i >= 0 && o + 1 < cap; i--) {
        if (i == d->scale - 1 && o + 1 < cap) buf[o++] = '.';
        buf[o++] = digits[i];
    }
    buf[o] = '\0';
}

static void dec_align(TeDec *a, TeDec *b) {
    if (a->scale < b->scale) { a->m *= pow10_128(b->scale - a->scale); a->scale = b->scale; }
    else if (b->scale < a->scale) { b->m *= pow10_128(a->scale - b->scale); b->scale = a->scale; }
}

int te_dec_cmp(const TeDec *a, const TeDec *b) {
    TeDec x = *a, y = *b; dec_align(&x, &y);
    return (x.m > y.m) - (x.m < y.m);
}

void te_dec_neg(TeDec *d) { d->m = -d->m; }

/* Recorta ceros finales sin bajar de min_scale. */
static void dec_trim(TeDec *d, int min_scale) {
    while (d->scale > min_scale && d->m % 10 == 0) { d->m /= 10; d->scale--; }
}

/* Redondeo half-even de q = num/den (den > 0) a entero. */
static __int128 div_round_half_even(__int128 num, __int128 den) {
    __int128 q = num / den, r = num % den;
    __int128 ar = r < 0 ? -r : r, twice = ar * 2;
    if (twice > den || (twice == den && (q % 2 != 0))) q += (num < 0) ? -1 : 1;
    return q;
}

int te_dec_binop(NodeKind k, const TeDec *pa, const TeDec *pb, TeDec *out) {
    TeDec a = *pa, b = *pb;
    switch (k) {
    case NK_ADD: case NK_SUB:
        dec_align(&a, &b);
        out->m = (k == NK_ADD) ? a.m + b.m : a.m - b.m; out->scale = a.scale;
        return !dec_overflow(out);
    case NK_MUL:
        out->m = a.m * b.m; out->scale = a.scale + b.scale;
        if (out->scale > TE_DEC_MAX_SCALE) {   /* reducir a 18 decimales, half-even */
            out->m = div_round_half_even(out->m, pow10_128(out->scale - TE_DEC_MAX_SCALE));
            out->scale = TE_DEC_MAX_SCALE;
        }
        return !dec_overflow(out);
    case NK_DIV: {
        if (b.m == 0) { printf("Error: division by zero.\n"); out->m = 0; out->scale = 0; return 0; }
        int min_scale = a.scale > b.scale ? a.scale : b.scale;
        /* q = a/b con TE_DEC_MAX_SCALE decimales: (a.m * 10^(18 + b.scale - a.scale)) / b.m */
        int shift = TE_DEC_MAX_SCALE + b.scale - a.scale;
        __int128 num = a.m;
        if (shift >= 0) num *= pow10_128(shift);
        else num = div_round_half_even(num, pow10_128(-shift));
        out->m = div_round_half_even(num, b.m); out->scale = TE_DEC_MAX_SCALE;
        dec_trim(out, min_scale);
        return !dec_overflow(out);
    }
    case NK_MOD:
        if (b.m == 0) { printf("Error: modulo by zero.\n"); out->m = 0; out->scale = 0; return 0; }
        dec_align(&a, &b);
        out->m = a.m % b.m; out->scale = a.scale;
        return 1;
    case NK_GT:    out->m = te_dec_cmp(pa, pb) > 0;  out->scale = 0; return 1;
    case NK_LT:    out->m = te_dec_cmp(pa, pb) < 0;  out->scale = 0; return 1;
    case NK_GT_EQ: out->m = te_dec_cmp(pa, pb) <= 0; out->scale = 0; return 1;   /* mapeo del parser: GT_EQ evalúa <= */
    case NK_LT_EQ: out->m = te_dec_cmp(pa, pb) >= 0; out->scale = 0; return 1;
    case NK_EQ:    out->m = te_dec_cmp(pa, pb) == 0; out->scale = 0; return 1;
    case NK_DIFF:  out->m = te_dec_cmp(pa, pb) != 0; out->scale = 0; return 1;
    default: return 0;
    }
}

void te_dec_round(const TeDec *d, int n, TeDec *out) {
    if (n < 0) n = 0;
    if (n > TE_DEC_MAX_SCALE) n = TE_DEC_MAX_SCALE;
    if (d->scale <= n) {   /* rellena a n decimales: round(100m, 2) = 100.00 (formato dinero) */
        out->m = d->m * pow10_128(n - d->scale); out->scale = n; return;
    }
    __int128 den = pow10_128(d->scale - n);
    __int128 q = d->m / den, r = d->m % den;
    __int128 ar = r < 0 ? -r : r;
    if (ar * 2 >= den) q += (d->m < 0) ? -1 : 1;   /* half away from zero */
    out->m = q; out->scale = n;
}

double te_dec_to_double(const TeDec *d) {
    char buf[TE_DEC_TEXT_MAX]; te_dec_format(d, buf, sizeof(buf));
    return strtod(buf, NULL);
}

/* ------------------------------------------------------------------------ */
/* Integración con el walker                                                 */
/* ------------------------------------------------------------------------ */

int te_var_is_decimal(const Variable *v) {
    /* variables: tag "DECIMAL"; atributos de clase: tipo declarado "decimal"/"decimal?" */
    return v && v->vtype == VAL_STRING && v->type &&
           (strcmp(v->type, TE_T_DECIMAL) == 0 || strcmp(v->type, TE_DT_DECIMAL) == 0 || strcmp(v->type, TE_DT_DECIMAL_OPT) == 0);
}

ASTNode *te_dec_leaf(const char *text) {
    return create_ast_leaf(TE_T_DECIMAL, 0, (char *)(text ? text : "0"), NULL);
}

static Variable *dec_var_of(ASTNode *n) {
    if (!n || !n->id) return NULL;
    Variable *v = (Variable *)n->cached_var;
    if (v && (!v->id || strcmp(v->id, n->id) != 0)) v = NULL;
    if (!v) v = find_variable(n->id);
    return v;
}

/* Atributo obj.attr (obj = identificador de OBJECT) -> Variable del atributo o NULL. */
static Variable *dec_attr_of(ASTNode *n) {
    if (!n || !n->left || !n->right || !n->right->id) return NULL;
    ASTNode *o = n->left;
    ObjectNode *obj = NULL;
    if (o->id) {
        if (strcmp(o->id, TE_SYM_THIS) == 0) {
            Variable *tv = find_variable(TE_SYM_THIS);
            if (tv && tv->vtype == VAL_OBJECT) obj = tv->value.object_value;
        } else {
            Variable *v = find_variable(o->id);
            if (v && v->vtype == VAL_OBJECT && v->type && strcmp(v->type, TE_T_OBJECT) == 0) obj = v->value.object_value;
        }
    }
    if (!obj || !obj->class) return NULL;
    for (int i = 0; i < obj->class->attr_count; i++)
        if (obj->class->attributes[i].id && strcmp(obj->class->attributes[i].id, n->right->id) == 0) return &obj->attributes[i];
    return NULL;
}

static int is_decimal_call(ASTNode *n) {
    return n && n->type && strcmp(n->type, TE_T_CALL_FUNC) == 0 && n->id && strcmp(n->id, TE_DT_DECIMAL) == 0;
}

int te_dec_expr_has_decimal(ASTNode *n) {
    if (!n) return 0;
    switch (nk_of(n)) {
    case NK_IDENTIFIER: case NK_ID: return te_var_is_decimal(dec_var_of(n));
    case NK_ACCESS_ATTR: return te_var_is_decimal(dec_attr_of(n));
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_DIV: case NK_MOD:
    case NK_GT: case NK_LT: case NK_GT_EQ: case NK_LT_EQ: case NK_EQ: case NK_DIFF:
        return te_dec_expr_has_decimal(n->left) || te_dec_expr_has_decimal(n->right);
    case NK_NEG: return te_dec_expr_has_decimal(n->left);
    default:
        if (n->type && strcmp(n->type, TE_T_DECIMAL) == 0) return 1;
        return is_decimal_call(n);
    }
}

/* Convierte cualquier operando a TeDec: decimal, INT/FLOAT literal, variable numérica,
 * string numérico, llamada decimal(...), o subexpresión exacta. */
static int dec_operand(ASTNode *n, TeDec *out) {
    if (!n) return 0;
    if (n->type && strcmp(n->type, TE_T_DECIMAL) == 0) return te_dec_parse(n->str_value, out);
    NodeKind k = nk_of(n);
    switch (k) {
    case NK_NUMBER: case NK_INT: out->m = n->value; out->scale = 0; return 1;
    case NK_FLOAT: return te_dec_parse(n->str_value, out);
    case NK_IDENTIFIER: case NK_ID: {
        Variable *v = dec_var_of(n);
        if (!v) return 0;
        if (te_var_is_decimal(v)) return te_dec_parse(v->value.string_value, out);
        if (v->vtype == VAL_INT) { out->m = v->value.int_value; out->scale = 0; return 1; }
        if (v->vtype == VAL_FLOAT) { char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value); return te_dec_parse(b, out); }
        if (v->vtype == VAL_STRING) return te_dec_parse(v->value.string_value, out);
        return 0;
    }
    case NK_ACCESS_ATTR: {
        Variable *a = dec_attr_of(n);
        if (!a) return 0;
        if (te_var_is_decimal(a) || a->vtype == VAL_STRING) return te_dec_parse(a->value.string_value, out);
        if (a->vtype == VAL_INT) { out->m = a->value.int_value; out->scale = 0; return 1; }
        if (a->vtype == VAL_FLOAT) { char b[64]; te_fmt_double(b, sizeof(b), a->value.float_value); return te_dec_parse(b, out); }
        return 0;
    }
    case NK_NEG: { if (!dec_operand(n->left, out)) return 0; te_dec_neg(out); return 1; }
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_DIV: case NK_MOD:
    case NK_GT: case NK_LT: case NK_GT_EQ: case NK_LT_EQ: case NK_EQ: case NK_DIFF: {
        TeDec a, b;
        if (!dec_operand(n->left, &a) || !dec_operand(n->right, &b)) return 0;
        return te_dec_binop(k, &a, &b, out);
    }
    default:
        if (is_decimal_call(n) || (n->type && strcmp(n->type, TE_T_CALL_FUNC) == 0) || (n->type && strcmp(n->type, TE_T_CALL_METHOD) == 0)) {
            /* evaluar la llamada y leer __ret__ */
            interpret_ast(n);
            Variable *r = find_variable(TE_SYM_RET);
            if (!r) return 0;
            if (te_var_is_decimal(r) || r->vtype == VAL_STRING) return te_dec_parse(r->value.string_value, out);
            if (r->vtype == VAL_INT) { out->m = r->value.int_value; out->scale = 0; return 1; }
            if (r->vtype == VAL_FLOAT) { char b[64]; te_fmt_double(b, sizeof(b), r->value.float_value); return te_dec_parse(b, out); }
            return 0;
        }
        return 0;
    }
}

int te_dec_eval(ASTNode *node, char *out, size_t cap) {
    TeDec d;
    if (!dec_operand(node, &d)) return 0;
    te_dec_format(&d, out, cap);
    return 1;
}

int te_dec_eval_double(ASTNode *node, double *out) {
    TeDec d;
    if (!dec_operand(node, &d)) return 0;
    *out = te_dec_to_double(&d);
    return 1;
}

ASTNode *te_dec_arg_leaf(ASTNode *arg) {
    if (!arg || !te_dec_expr_has_decimal(arg)) return NULL;
    char buf[TE_DEC_TEXT_MAX];
    if (!te_dec_eval(arg, buf, sizeof(buf))) return NULL;
    return te_dec_leaf(buf);
}

static void dec_set_ret(const TeDec *d) {
    char buf[TE_DEC_TEXT_MAX]; te_dec_format(d, buf, sizeof(buf));
    ASTNode *r = te_dec_leaf(buf);
    add_or_update_variable(TE_SYM_RET, r);
    free_ast(r);
}

/* decimal(x): desde decimal/int/float/string. */
int te_dec_builtin(const char *fn, ASTNode *args) {
    if (!fn || strcmp(fn, TE_DT_DECIMAL) != 0) return 0;
    TeDec d = { 0, 0 };
    if (args) {
        if (!dec_operand(args, &d)) {
            char *s = get_node_string(args);
            if (!s || !te_dec_parse(s, &d)) { printf("Error: decimal(): '%s' is not a valid decimal.\n", s ? s : ""); d.m = 0; d.scale = 0; }
            if (s) free(s);
        }
    }
    dec_set_ret(&d);
    return 1;
}

/* d.round(n) / d.to_float() / d.to_string() / d.scale() / d.abs() sobre una variable,
 * un literal o una expresión decimal: (10.005m).round(2), (a * b).round(2). */
int te_dec_method_dispatch(ASTNode *node, ASTNode *objNode, Variable *v) {
    if (!node || !node->id) return 0;
    TeDec d;
    if (te_var_is_decimal(v)) {
        if (!te_dec_parse(v->value.string_value, &d)) return 0;
    } else if (objNode && !objNode->id && te_dec_expr_has_decimal(objNode)) {
        if (!dec_operand(objNode, &d)) return 0;
    } else return 0;
    const char *m = node->id;
    ASTNode *a0 = node->right;
    if (strcmp(m, "round") == 0) {
        int n = a0 ? (int)evaluate_expression(a0) : 0;
        TeDec r; te_dec_round(&d, n, &r); dec_set_ret(&r); return 1;
    }
    if (strcmp(m, "to_float") == 0) {
        char b[64]; te_fmt_double(b, sizeof(b), te_dec_to_double(&d));
        ASTNode *r = create_ast_leaf(TE_T_FLOAT, 0, b, NULL); add_or_update_variable(TE_SYM_RET, r); free_ast(r); return 1;
    }
    if (strcmp(m, "to_string") == 0) { char b[TE_DEC_TEXT_MAX]; te_dec_format(&d, b, sizeof(b)); te_set_ret_string(b); return 1; }
    if (strcmp(m, "scale") == 0) { te_set_ret_int(d.scale); return 1; }
    if (strcmp(m, "abs") == 0) { if (d.m < 0) d.m = -d.m; dec_set_ret(&d); return 1; }
    return 0;
}
