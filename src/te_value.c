/* te_value.c — Modelo de valores unificado (Fase E) + aritmética entera exacta (Fase 1b).
 * Ver te_value.h para el contrato. te_eval_value() es la ÚNICA tabla "expresión -> valor" del
 * intérprete; declaración, asignación, return y binding de argumentos la consumen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ast.h"
#include "ast_internal.h"
#include "te_vm.h"
#include "te_value.h"
#include "te_decimal.h"

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                             */
/* ------------------------------------------------------------------------ */

void te_val_init(TeValue *v) {
    memset(v, 0, sizeof(*v));
    v->vtype = VAL_INT;
}

void te_val_free(TeValue *v) {
    if (!v) return;
    if (v->vtype == VAL_STRING && v->value.string_value) free(v->value.string_value);
    if (v->type) free(v->type);
    v->type = NULL;
    memset(&v->value, 0, sizeof(v->value));
    v->vtype = VAL_INT;
}

void te_val_copy(TeValue *dst, const Variable *src) {
    dst->id = NULL; dst->is_const = 0;
    dst->vtype = src->vtype;
    dst->type = src->type ? strdup(src->type) : NULL;
    if (src->vtype == VAL_STRING)
        dst->value.string_value = strdup(src->value.string_value ? src->value.string_value : "");
    else
        dst->value = src->value;
}

void te_val_move_into(Variable *dst, TeValue *src) {
    if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
    if (dst->type) free(dst->type);
    dst->vtype = src->vtype;
    dst->type  = src->type;
    dst->value = src->value;
    src->type = NULL;
    memset(&src->value, 0, sizeof(src->value));
    src->vtype = VAL_INT;
}

/* ------------------------------------------------------------------------ */
/* Constructores                                                             */
/* ------------------------------------------------------------------------ */

static void set_tag(TeValue *v, const char *tag) { if (v->type) free(v->type); v->type = strdup(tag); }

void te_val_set_int(TeValue *v, long long i)   { te_val_free(v); v->vtype = VAL_INT;   set_tag(v, TE_T_INT);   v->value.int_value = i; }
void te_val_set_bool(TeValue *v, int b)        { te_val_free(v); v->vtype = VAL_INT;   set_tag(v, TE_T_BOOL);  v->value.int_value = b ? 1 : 0; }
void te_val_set_float(TeValue *v, double d)    { te_val_free(v); v->vtype = VAL_FLOAT; set_tag(v, TE_T_FLOAT); v->value.float_value = d; }
void te_val_set_string(TeValue *v, const char *s) { te_val_set_string_tag(v, TE_T_STRING, s); }
void te_val_set_string_tag(TeValue *v, const char *tag, const char *s) {
    te_val_free(v); v->vtype = VAL_STRING; set_tag(v, tag); v->value.string_value = strdup(s ? s : "");
}
void te_val_set_null(TeValue *v) { te_val_free(v); v->vtype = VAL_OBJECT; set_tag(v, TE_T_NULL); v->value.object_value = NULL; }
void te_val_set_ref(TeValue *v, const char *tag, void *ref) {
    te_val_free(v); v->vtype = VAL_OBJECT; set_tag(v, tag); v->value.object_value = (ObjectNode *)ref;
}

int te_val_is_null(const Variable *v) {
    return !v || (v->vtype == VAL_OBJECT && (v->value.object_value == NULL || (v->type && strcmp(v->type, TE_T_NULL) == 0)));
}

const char *te_val_tag(const Variable *v) {
    if (v->type) return v->type;
    switch (v->vtype) {
    case VAL_INT: return TE_T_INT;
    case VAL_FLOAT: return TE_T_FLOAT;
    case VAL_STRING: return TE_T_STRING;
    default: return TE_T_OBJECT;
    }
}

char *te_var_to_string(const Variable *v) {
    char tmp[64];
    if (!v) return strdup("");
    switch (v->vtype) {
    case VAL_STRING: return strdup(v->value.string_value ? v->value.string_value : "");
    case VAL_INT:    snprintf(tmp, sizeof(tmp), "%lld", v->value.int_value); return strdup(tmp);
    case VAL_FLOAT:  te_fmt_double(tmp, sizeof(tmp), v->value.float_value); return strdup(tmp);
    default: break;
    }
    const char *tag = te_val_tag(v);
    if (te_val_is_null(v)) return strdup("null");
    if (strcmp(tag, TE_T_LIST) == 0) return te_list_node_to_string((ASTNode *)(intptr_t)v->value.object_value);
    if (strcmp(tag, TE_T_MAP) == 0 || strcmp(tag, TE_T_OBJECT_LITERAL) == 0) return te_map_node_to_string((ASTNode *)(intptr_t)v->value.object_value);
    return strdup("");   /* OBJECT/LAMBDA en contexto string: "" (histórico) */
}

/* ------------------------------------------------------------------------ */
/* Nodos de datos -> valor                                                   */
/* ------------------------------------------------------------------------ */

void te_leaf_to_value(ASTNode *val, TeValue *out) {
    te_val_init(out);
    if (!val || !val->type) { te_val_set_null(out); return; }
    const char *t = val->type;
    switch (nk_of(val)) {
    case NK_STRING: case NK_STRING_LITERAL:
        te_val_set_string(out, val->str_value); return;
    case NK_NUMBER: case NK_INT:
        if (strcmp(t, TE_T_BOOL) == 0) te_val_set_bool(out, val->value != 0);
        else te_val_set_int(out, val->value);
        return;
    case NK_FLOAT:  te_val_set_float(out, val->str_value ? atof(val->str_value) : 0.0); return;
    case NK_DECIMAL: te_val_set_string_tag(out, TE_T_DECIMAL, val->str_value ? val->str_value : "0"); return;
    case NK_NULL:   te_val_set_null(out); return;
    case NK_OBJECT_LITERAL: te_val_set_ref(out, TE_T_MAP, val); return;
    case NK_LIST:   te_val_set_ref(out, TE_T_LIST, val); return;
    case NK_OBJECT: {
        ObjectNode *o = val->extra ? (ObjectNode *)val->extra : (ObjectNode *)(intptr_t)val->value;
        if (o) te_val_set_ref(out, TE_T_OBJECT, o); else te_val_set_null(out);
        return;
    }
    default: break;
    }
    if (strcmp(t, TE_T_MAP) == 0)    { te_val_set_ref(out, TE_T_MAP, val); return; }
    if (strcmp(t, TE_T_LAMBDA) == 0) { te_val_set_ref(out, TE_T_LAMBDA, val); return; }
    if (strcmp(t, TE_T_DATETIME) == 0 || strcmp(t, TE_T_UUID) == 0) { te_val_set_string_tag(out, t, val->str_value); return; }
    /* valor de KV_PAIR que es una expresión (`{ a: x + 1 }`): evaluar */
    te_eval_value(val, out);
}

ASTNode *te_val_to_leaf(const Variable *v) {
    if (!v) return create_ast_leaf(TE_T_NULL, 0, NULL, NULL);
    const char *tag = te_val_tag(v);
    switch (v->vtype) {
    case VAL_INT:
        return create_ast_leaf_number(strcmp(tag, TE_T_BOOL) == 0 ? TE_T_BOOL : TE_T_NUMBER, v->value.int_value, NULL, NULL);
    case VAL_FLOAT: {
        char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value);
        return create_ast_leaf(TE_T_FLOAT, 0, b, NULL);
    }
    case VAL_STRING:
        if (strcmp(tag, TE_T_DECIMAL) == 0) return te_dec_leaf(v->value.string_value);
        return create_ast_leaf((char *)((strcmp(tag, TE_T_DATETIME) == 0 || strcmp(tag, TE_T_UUID) == 0) ? tag : TE_T_STRING),
                               0, v->value.string_value ? v->value.string_value : "", NULL);
    default:
        if (te_val_is_null(v)) return create_ast_leaf(TE_T_NULL, 0, NULL, NULL);
        if (strcmp(tag, TE_T_OBJECT) == 0) return create_object_node(v->value.object_value);
        return (ASTNode *)(intptr_t)v->value.object_value;   /* LIST/MAP/LAMBDA/LAZY_ITER: alias */
    }
}

void te_set_ret_value(TeValue *v) {
    if (g_vm.ret_var_active) {
        if (g_vm.ret_var.vtype == VAL_STRING && g_vm.ret_var.value.string_value) free(g_vm.ret_var.value.string_value);
        if (g_vm.ret_var.type) free(g_vm.ret_var.type);
        g_vm.ret_var.value.string_value = NULL; g_vm.ret_var.type = NULL;
    }
    if (!g_vm.ret_var.id) g_vm.ret_var.id = strdup(TE_SYM_RET);
    g_vm.ret_var.is_const = 0;
    g_vm.ret_var.vtype = v->vtype; g_vm.ret_var.type = v->type; g_vm.ret_var.value = v->value;
    v->type = NULL; memset(&v->value, 0, sizeof(v->value)); v->vtype = VAL_INT;
    g_vm.ret_var_active = 1;
}

/* ------------------------------------------------------------------------ */
/* Evaluación                                                                */
/* ------------------------------------------------------------------------ */

static void eval_numeric(ASTNode *n, TeValue *out) {
    char dec[TE_DEC_TEXT_MAX];
    long long i64;
    if (te_dec_expr_has_decimal(n) && te_dec_eval(n, dec, sizeof(dec))) { te_val_set_string_tag(out, TE_T_DECIMAL, dec); return; }
    if (te_eval_i64(n, &i64)) { te_val_set_int(out, i64); return; }
    double d = evaluate_expression(n);
    if (d == (double)(long long)d) te_val_set_int(out, (long long)d); else te_val_set_float(out, d);
}

static void eval_call(ASTNode *n, TeValue *out) {
    interpret_ast(n);
    Variable *r = find_variable(TE_SYM_RET);
    if (!r) {
        te_runtime_fatalf("Error: no return value captured from expression '%s'.", n->type ? n->type : "unknown");
        te_val_set_int(out, 0);
        return;
    }
    te_val_copy(out, r);
}

/* obj.attr sobre una instancia de clase (respeta private). */
static int eval_obj_attr(ObjectNode *obj, ASTNode *objRef, const char *attr, TeValue *out) {
    if (!obj || !obj->class || !attr) return 0;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (obj->class->attributes[i].id && strcmp(obj->class->attributes[i].id, attr) == 0) {
            if (!te_attr_access_ok(obj->class, i, objRef)) { te_val_set_int(out, 0); return 1; }
            Variable *a = &obj->attributes[i];
            if (a->vtype == VAL_OBJECT && a->value.object_value == NULL) { te_val_set_null(out); return 1; }
            te_val_copy(out, a);
            /* atributos decimal (tipo declarado "decimal") -> tag de runtime DECIMAL */
            if (te_var_is_decimal(a) && (!out->type || strcmp(out->type, TE_T_DECIMAL) != 0)) set_tag(out, TE_T_DECIMAL);
            return 1;
        }
    }
    return 0;
}

static void eval_access_attr(ASTNode *n, TeValue *out) {
    ASTNode *o = n->left, *a = n->right;
    if (!o || !a || !a->id) { te_val_set_int(out, 0); return; }
    /* .length / .size: virtual, ya resuelto por el walker numérico */
    if (strcmp(a->id, "length") == 0 || strcmp(a->id, "size") == 0) {
        if (resolve_to_list(o) || resolve_to_map(o) ||
            (o->id && find_variable(o->id) && find_variable(o->id)->vtype == VAL_STRING) ||
            nk_of(o) == NK_STRING) {
            te_val_set_int(out, (long long)evaluate_expression(n));
            return;
        }
    }
    TeValue base; te_val_init(&base);
    if (o->id && (nk_of(o) == NK_IDENTIFIER || nk_of(o) == NK_ID)) {
        Variable *v = find_variable(o->id);
        if (!v) {
            if (n->value == 1) { te_val_set_null(out); return; }   /* ?. sobre indefinido */
            printf("Error: object '%s' not found for assignment.\n", o->id);
            te_val_set_int(out, 0); return;
        }
        te_val_copy(&base, v);
    } else {
        te_eval_value(o, &base);
    }
    const char *bt = te_val_tag(&base);
    if (te_val_is_null(&base)) {
        te_val_set_null(out);                       /* `?.` y acceso sobre null -> null */
    } else if (base.vtype != VAL_OBJECT) {
        printf("Error: object '%s' not found for assignment.\n", o->id ? o->id : "<expr>");
        te_val_set_int(out, 0);
    } else if (strcmp(bt, TE_T_MAP) == 0 || strcmp(bt, TE_T_OBJECT_LITERAL) == 0) {
        ASTNode *map = (ASTNode *)(intptr_t)base.value.object_value;
        ASTNode *pair = map ? map_find_pair(map, a->id) : NULL;
        if (pair && pair->left) te_leaf_to_value(pair->left, out); else te_val_set_null(out);
    } else if (strcmp(bt, TE_T_OBJECT) == 0) {
        if (!eval_obj_attr(base.value.object_value, o, a->id, out)) {
            fprintf(stderr, "Error: attribute '%s' not found in '%s'.\n", a->id, o->id ? o->id : "<expr>");
            te_val_set_int(out, 0);
        }
    } else {
        te_val_set_null(out);                       /* LIST/LAMBDA/... sin ese atributo */
    }
    te_val_free(&base);
}

static void eval_access_expr(ASTNode *n, TeValue *out) {
    ASTNode *map = resolve_to_map(n->left);
    if (map) {
        char keybuf[1024];
        const char *key = te_map_key_coerce(n->right, keybuf, sizeof(keybuf));
        ASTNode *pair = key ? map_find_pair(map, key) : NULL;
        if (pair && pair->left) te_leaf_to_value(pair->left, out);
        else te_val_set_string(out, "");            /* clave ausente -> "" (permite `== ""`) */
        return;
    }
    ASTNode *list = resolve_to_list(n->left);
    if (list) {
        int idx = (int)evaluate_expression(n->right);
        if (idx < 0 || idx >= list_length(list)) { te_val_set_null(out); return; }   /* fuera de rango -> null */
        ASTNode *item = list_get_item(list, idx);
        if (!item) { te_val_set_null(out); return; }
        te_leaf_to_value(item, out);
        return;
    }
    fprintf(stderr, "Error: indexed object is neither a list nor a Map.\n");
    te_val_set_int(out, 0);
}

/* Binding de argumentos a parámetros con el valor REAL de cada expresión. Un `int` declarado
 * trunca floats y un `float` promueve enteros (misma semántica que el fast-path numérico). */
void te_bind_args(ParameterNode *p, ASTNode *args) {
    /* Todos los argumentos se evalúan en el scope del LLAMADOR antes de ligar el primero:
     * `f(b, a)` con params (a, b) no debe ver la `a` recién ligada. */
    TeValue stackv[16]; TeValue *vals = stackv;
    int n = 0;
    for (ParameterNode *q = p; q; q = q->next) n++;
    if (n > 16) vals = (TeValue *)calloc((size_t)n, sizeof(TeValue));
    int i = 0;
    ASTNode *arg = args;
    for (ParameterNode *q = p; q && arg; q = q->next, arg = arg->next) te_eval_value(arg, &vals[i++]);   /* gotcha #1: args por ->next */
    int bound = i;
    i = 0;
    for (ParameterNode *q = p; q && i < bound; q = q->next, i++) {
        TeValue *v = &vals[i];
        if (q->type) {
            if ((strcmp(q->type, TE_DT_INT) == 0 || strcmp(q->type, TE_T_INT) == 0) && v->vtype == VAL_FLOAT)
                te_val_set_int(v, (long long)v->value.float_value);
            else if ((strcmp(q->type, TE_DT_FLOAT) == 0 || strcmp(q->type, TE_T_FLOAT) == 0) && v->vtype == VAL_INT &&
                     (!v->type || strcmp(v->type, TE_T_BOOL) != 0))
                te_val_set_float(v, (double)v->value.int_value);
        }
        te_bind_param(q->name, v);
    }
    if (vals != stackv) free(vals);
}

/* `new X(args)` como EXPRESIÓN: instancia fresca (clon del template de parse) + constructor.
 * Antes la declaración aliasaba el template y el constructor lo re-inicializaba en sitio: dos
 * `let p = new X()` en la misma función compartían el MISMO objeto. */
static void eval_new_object(ASTNode *n, TeValue *out) {
    ObjectNode *tmpl = n->extra ? (ObjectNode *)n->extra : (ObjectNode *)(intptr_t)n->value;
    if (!tmpl || !tmpl->class) { te_val_set_ref(out, TE_T_OBJECT, tmpl); return; }   /* nodos MODEL/ML */
    ObjectNode *obj = clone_object(tmpl);
    te_req_owned_obj_register(obj);
    te_call_ctor(obj, n->left);   /* Fase F: frame propio (args evaluados en el scope del llamador) */
    te_val_set_ref(out, TE_T_OBJECT, obj);
}

int te_eval_value(ASTNode *n, TeValue *out) {
    te_val_init(out);
    if (!n) return 0;
    const char *t = n->type;
    switch (nk_of(n)) {
    case NK_NUMBER: case NK_INT:
        if (t && strcmp(t, TE_T_BOOL) == 0) te_val_set_bool(out, n->value != 0); else te_val_set_int(out, n->value);
        return 1;
    case NK_FLOAT:   te_val_set_float(out, n->str_value ? atof(n->str_value) : 0.0); return 1;
    case NK_DECIMAL: te_val_set_string_tag(out, TE_T_DECIMAL, n->str_value ? n->str_value : "0"); return 1;
    case NK_STRING: case NK_STRING_LITERAL: te_val_set_string(out, n->str_value); return 1;
    case NK_STRING_INTERP: { char *s = expand_interp_string(n->str_value ? n->str_value : ""); te_val_set_string(out, s); free(s); return 1; }
    case NK_NULL:    te_val_set_null(out); return 1;
    case NK_IDENTIFIER: case NK_ID: {
        Variable *v = find_variable(n->id);
        if (!v) { fprintf(stderr, "Error: variable '%s' not found.\n", n->id ? n->id : "?"); te_val_set_int(out, 0); return 1; }
        te_val_copy(out, v);
        return 1;
    }
    case NK_LIST:
        if (n->value == 1) { te_val_set_ref(out, TE_T_LIST, n); return 1; }   /* lista CSV ya materializada */
        te_list_literal_construct_objects(n);
        te_val_set_ref(out, TE_T_LIST, te_list_literal_instance(n));
        return 1;
    case NK_OBJECT_LITERAL: te_val_set_ref(out, TE_T_MAP, n); return 1;
    case NK_OBJECT:
        if (n->is_new_expr) eval_new_object(n, out); else te_leaf_to_value(n, out);   /* dato: alias */
        return 1;
    case NK_TERNARY:
        return te_eval_value(evaluate_expression(n->left) != 0.0 ? n->right : n->extra, out);
    case NK_NULL_COALESCE:
        return te_eval_value(te_expr_is_null(n->left) ? n->right : n->left, out);
    case NK_ADD:
        if (is_string_type(n)) { char *s = get_node_string(n); te_val_set_string(out, s); free(s); return 1; }
        eval_numeric(n, out); return 1;
    case NK_SUB: case NK_MUL: case NK_DIV: case NK_MOD: case NK_NEG:
    case NK_BIT_AND: case NK_BIT_OR: case NK_BIT_XOR: case NK_BIT_NOT: case NK_SHL: case NK_SHR: case NK_IN:
        eval_numeric(n, out); return 1;
    case NK_GT: case NK_LT: case NK_EQ: case NK_GT_EQ: case NK_LT_EQ: case NK_DIFF:
    case NK_AND: case NK_OR: case NK_NOT:
        te_val_set_bool(out, evaluate_expression(n) != 0); return 1;
    case NK_ACCESS_ATTR: eval_access_attr(n, out); return 1;
    case NK_ACCESS_EXPR: eval_access_expr(n, out); return 1;
    case NK_CALL_FUNC: case NK_CALL_METHOD: case NK_FILTER_CALL: case NK_LIST_FUNC_CALL: case NK_PREDICT:
        eval_call(n, out); return 1;
    default: break;
    }
    if (t) {
        if (strcmp(t, TE_T_CALL_EXPR) == 0) { eval_call(n, out); return 1; }
        if (strcmp(t, TE_T_LAMBDA) == 0)    { te_val_set_ref(out, TE_T_LAMBDA, te_closure_make(n)); return 1; }
        if (strcmp(t, TE_T_LAZY_ITER) == 0) { te_val_set_ref(out, TE_T_LAZY_ITER, n->extra ? n->extra : n); return 1; }
        if (strcmp(t, TE_T_MAP) == 0)       { te_val_set_ref(out, TE_T_MAP, n); return 1; }
        if (strcmp(t, TE_T_DATETIME) == 0 || strcmp(t, TE_T_UUID) == 0) { te_val_set_string_tag(out, t, n->str_value); return 1; }
    }
    /* Resto (EXPRESSION, nodos ya evaluados, ...): número, como el catch-all histórico */
    {
        double d = evaluate_expression(n);
        if (d == (double)(long long)d) te_val_set_int(out, (long long)d); else te_val_set_float(out, d);
    }
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Fase 1b: aritmética entera exacta de 64 bits                              */
/* ------------------------------------------------------------------------ */

static int lookup_int(ASTNode *n, long long *out) {
    if (!n->id) return 0;
    Variable *v = te_resolve_cached(n);
    if (!v || v->vtype != VAL_INT) return 0;
    if (v->type && strcmp(v->type, TE_T_NULL) == 0) return 0;
    *out = v->value.int_value;
    return 1;
}

int te_eval_num(ASTNode *node, long long *iv, double *dv) {
    if (te_eval_i64(node, iv)) { *dv = (double)*iv; return 1; }
    *dv = evaluate_expression(node);
    if (*dv == (double)(long long)*dv) { *iv = (long long)*dv; return 1; }
    return 0;
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

/* ======================================================================================
 * Fase F: closures por referencia (upvalues).
 * ====================================================================================== */

typedef struct { char **names; int n, cap; } TeNameSet;

static int nameset_has(const TeNameSet *s, const char *id) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->names[i], id) == 0) return 1;
    return 0;
}
static void nameset_add(TeNameSet *s, const char *id) {
    if (!id || nameset_has(s, id)) return;
    if (s->n >= s->cap) {
        int nc = s->cap ? s->cap * 2 : 8;
        char **g = (char **)realloc(s->names, (size_t)nc * sizeof(char *));
        if (!g) return;
        s->names = g; s->cap = nc;
    }
    s->names[s->n++] = strdup(id);
}
static void nameset_add_csv(TeNameSet *s, const char *csv) {   /* '\1'-separado (params de lambda) */
    const char *p = csv;
    while (p && *p) {
        const char *e = p; while (*e && *e != '\1') e++;
        char nm[128]; size_t n = (size_t)(e - p); if (n >= sizeof nm) n = sizeof nm - 1;
        memcpy(nm, p, n); nm[n] = 0;
        nameset_add(s, nm);
        p = (*e == '\1') ? e + 1 : e;
    }
}
static void nameset_free(TeNameSet *s) {
    for (int i = 0; i < s->n; i++) free(s->names[i]);
    free(s->names); s->names = NULL; s->n = s->cap = 0;
}

/* Recorre el body: `refs` = identificadores usados; `bound` = nombres ligados dentro (let/var,
 * variables de loop, params de lambdas anidados). Solo baja por extra en LAMBDA (en OBJECT es
 * un ObjectNode*). */
static void closure_scan(ASTNode *n, TeNameSet *refs, TeNameSet *bound, int *uses_this) {
    if (!n) return;
    NodeKind k = nk_of(n);
    if (k == NK_IDENTIFIER || k == NK_ID) {
        if (n->id) { if (strcmp(n->id, TE_SYM_THIS) == 0) *uses_this = 1; else nameset_add(refs, n->id); }
        return;
    }
    if (n->type && strcmp(n->type, TE_T_LAMBDA) == 0) {
        if (n->id) nameset_add_csv(bound, n->id);
        closure_scan(n->left, refs, bound, uses_this);
        closure_scan(n->extra, refs, bound, uses_this);
        return;
    }
    if (k == NK_VAR_DECL || k == NK_FOR || k == NK_FOR_IN || k == NK_FOR_C) { if (n->id) nameset_add(bound, n->id); }
    closure_scan(n->left, refs, bound, uses_this);
    closure_scan(n->right, refs, bound, uses_this);
    closure_scan(n->next, refs, bound, uses_this);
}

static void closures_register_request(ASTNode *cl) {
    if (!g_vm.te_request_active) return;   /* script: vive hasta el fin del proceso */
    if (g_vm.req_closures_n >= g_vm.req_closures_cap) {
        int nc = g_vm.req_closures_cap ? g_vm.req_closures_cap * 2 : 32;
        ASTNode **g = (ASTNode **)realloc(g_vm.req_closures, (size_t)nc * sizeof(ASTNode *));
        if (!g) return;
        g_vm.req_closures = g; g_vm.req_closures_cap = nc;
    }
    g_vm.req_closures[g_vm.req_closures_n++] = cl;
}

static void frame_register_closure(TeFrame *f, ASTNode *cl) {
    for (TeClosureRef *r = f->closures; r; r = r->next) if (r->cl == cl) return;
    TeClosureRef *r = (TeClosureRef *)malloc(sizeof(TeClosureRef));
    if (!r) return;
    r->cl = cl; r->next = f->closures; f->closures = r;
}

ASTNode *te_closure_make(ASTNode *lam) {
    if (!lam || lam->closure || !g_vm.frame_top) return lam;   /* ya es closure / top-level: dinámico */
    int floor = g_vm.var_count;
    for (TeFrame *f = g_vm.frame_top; f; f = f->prev) floor = f->base;

    TeNameSet refs = {0}, bound = {0};
    int uses_this = 0;
    if (lam->id) nameset_add_csv(&bound, lam->id);   /* params propios */
    closure_scan(lam->left, &refs, &bound, &uses_this);

    /* candidatas: libres, resuelven AHORA a un slot de un frame activo */
    int cap = 0; Variable **slots = NULL; char **names = NULL;
    for (int i = 0; i < refs.n; i++) {
        const char *id = refs.names[i];
        if (nameset_has(&bound, id) || strcmp(id, TE_SYM_RET) == 0) continue;
        Variable *v = find_variable((char *)id);
        if (!v || v < g_vm.vars || v >= g_vm.vars + MAX_VARS) continue;
        if ((int)(v - g_vm.vars) < floor) continue;
        if (cap == 0) { slots = (Variable **)calloc((size_t)refs.n, sizeof(Variable *)); names = (char **)calloc((size_t)refs.n, sizeof(char *)); }
        slots[cap] = v; names[cap] = strdup(id); cap++;
    }
    nameset_free(&refs);
    nameset_free(&bound);

    if (cap == 0 && !(uses_this && g_vm.this_active)) return lam;

    TeClosureEnv *env = (TeClosureEnv *)calloc(1, sizeof(TeClosureEnv));
    env->n = cap; env->names = names; env->open = slots;
    env->cells = (Variable *)calloc((size_t)(cap ? cap : 1), sizeof(Variable));
    env->has_this = (uses_this && g_vm.this_active);
    env->this_obj = env->has_this ? g_vm.this_reg.value.object_value : NULL;

    ASTNode *cl = (ASTNode *)calloc(1, sizeof(ASTNode));
    cl->type = strdup(TE_T_LAMBDA);
    cl->kind = lam->kind;
    cl->id = strdup(lam->id ? lam->id : "");
    cl->left = lam->left;                 /* body: ALIAS al template (no se libera con la closure) */
    cl->extra = lam->extra;
    cl->line = lam->line; cl->file_id = lam->file_id;
    cl->bc = BC_NOT_COMPILABLE;
    cl->borrowed_children = 1;
    cl->closure = env;

    /* registrar en cada frame dueño de algún slot capturado (se cierra al pop de ese frame) */
    for (int i = 0; i < cap; i++) {
        int idx = (int)(slots[i] - g_vm.vars);
        TeFrame *owner = NULL;
        for (TeFrame *f = g_vm.frame_top; f; f = f->prev) { if (idx >= f->base) { owner = f; break; } }
        if (owner) frame_register_closure(owner, cl);
    }
    closures_register_request(cl);
    return cl;
}

void te_frame_close_upvalues(TeFrame *f) {
    TeClosureRef *r = f->closures;
    while (r) {
        TeClosureEnv *env = r->cl->closure;
        if (env) {
            for (int i = 0; i < env->n; i++) {
                Variable *o = env->open[i];
                if (o && (int)(o - g_vm.vars) >= f->base) {
                    te_val_free(&env->cells[i]);
                    te_val_copy(&env->cells[i], o);
                    env->open[i] = NULL;
                }
            }
        }
        TeClosureRef *nx = r->next; free(r); r = nx;
    }
    f->closures = NULL;
}

void te_closure_bind_in(ASTNode *cl) {
    TeClosureEnv *env = cl ? cl->closure : NULL;
    if (!env) return;
    for (int i = 0; i < env->n; i++) {
        TeValue v; te_val_init(&v);
        te_val_copy(&v, env->open[i] ? env->open[i] : &env->cells[i]);
        te_bind_param(env->names[i], &v);
    }
    if (env->has_this) te_set_this(env->this_obj);
}

void te_closure_snapshot(ASTNode *cl, Variable *tmp) {
    TeClosureEnv *env = cl ? cl->closure : NULL;
    if (!env) return;
    for (int i = 0; i < env->n; i++) {
        te_val_init(&tmp[i]);
        Variable *b = find_variable_for(env->names[i]);
        if (b) te_val_copy(&tmp[i], b);
    }
}

void te_closure_write_back(ASTNode *cl, Variable *tmp) {
    TeClosureEnv *env = cl ? cl->closure : NULL;
    if (!env) return;
    for (int i = 0; i < env->n; i++) {
        Variable *dst = env->open[i] ? env->open[i] : &env->cells[i];
        if (dst != &env->cells[i]) {
            /* slot abierto: conservar id/is_const del slot; mover valor */
            int is_const = dst->is_const;
            te_val_move_into(dst, &tmp[i]);
            dst->is_const = is_const;
        } else {
            te_val_free(dst);
            te_val_move_into(dst, &tmp[i]);
        }
    }
}

static void closure_free(ASTNode *cl) {
    TeClosureEnv *env = cl->closure;
    if (env) {
        for (int i = 0; i < env->n; i++) { free(env->names[i]); te_val_free(&env->cells[i]); }
        free(env->names); free(env->open); free(env->cells); free(env);
    }
    free(cl->type); free(cl->id); free(cl);
}

void te_closures_free_request(void) {
    for (int i = 0; i < g_vm.req_closures_n; i++) closure_free(g_vm.req_closures[i]);
    g_vm.req_closures_n = 0;
}
