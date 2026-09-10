/* te_bytecode.c — Acelerador de bytecode del intérprete TypeEasy.
 *
 * Ver te_bytecode.h: el walker (ast.c) define la semántica; este archivo solo
 * compila el subconjunto numérico a una tira plana de opcodes y la ejecuta con
 * despacho por computed-goto. Reglas de la casa:
 *   1. Ninguna operación tiene semántica propia: aritmética -> te_num_binop /
 *      te_num_unop; almacenar -> te_num_store / te_num_store_i64; entero exacto
 *      -> espejo 1:1 de te_eval_i64 (sección BC_I64_*); acceso lista[i].attr con
 *      clase distinta -> te_walk_expression (walker) sobre el mismo nodo.
 *   2. Lo que no se pueda delegar no se compila (BC_NOT_COMPILABLE) y el
 *      walker ejecuta el nodo.
 *   3. Las Variable* cacheadas se validan en la entrada (BCGuard); si cambió el
 *      tipo desde la compilación el programa no corre (deopt) y el walker
 *      ejecuta el nodo. Durante la ejecución solo se escriben valores numéricos,
 *      así que ningún tipo puede cambiar a mitad de programa.
 *   4. Sin estado global propio: registro de nodos compilados, `this` actual y
 *      pila de `this` viven en TeVM (g_vm.bc_*).
 */

#include "te_bytecode.h"
#include "ast.h"
#include "te_num.h"
#include "te_vm.h"
#include "ast_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bridge to walker (ast.c) symbols not exported via ast.h. The struct
 * layout MUST stay in sync with the canonical typedef in ast.c. */
typedef struct TEListIdx {
    int len;
    int cap;
    ASTNode **items;
} TEListIdx;

extern int is_string_type(ASTNode *node);

/* nk_of: ast_internal.h */

/* ------------------------------------------------------------------------ */
/* Registro de programas compilados (para invalidar entre requests)          */
/* ------------------------------------------------------------------------ */

static void bc_register_node(ASTNode *n) {
    TeVM *vm = &g_vm;
    if (vm->bc_nodes_n == vm->bc_nodes_cap) {
        int ncap = vm->bc_nodes_cap ? vm->bc_nodes_cap * 2 : 256;
        ASTNode **p = (ASTNode **)realloc(vm->bc_nodes, sizeof(ASTNode *) * (size_t)ncap);
        if (!p) return;
        vm->bc_nodes = p; vm->bc_nodes_cap = ncap;
    }
    vm->bc_nodes[vm->bc_nodes_n++] = n;
}
static void bc_register_method(MethodNode *m) {
    TeVM *vm = &g_vm;
    if (vm->bc_methods_n == vm->bc_methods_cap) {
        int ncap = vm->bc_methods_cap ? vm->bc_methods_cap * 2 : 64;
        MethodNode **p = (MethodNode **)realloc(vm->bc_methods, sizeof(MethodNode *) * (size_t)ncap);
        if (!p) return;
        vm->bc_methods = p; vm->bc_methods_cap = ncap;
    }
    vm->bc_methods[vm->bc_methods_n++] = m;
}

/* NOTE: we do NOT free the BCInfo / Instrs blocks. Some code paths (inherited
 * methods sharing AST, recursive compile) may register the same pointer twice;
 * the leak is bounded by the program's compiled code size and happens once per
 * script reload boundary, not per request. */
void bc_invalidate_all(void) {
    TeVM *vm = &g_vm;
    for (int i = 0; i < vm->bc_nodes_n; i++) {
        ASTNode *n = vm->bc_nodes[i];
        if (n && n->bc != BC_NOT_COMPILABLE) n->bc = NULL;
    }
    vm->bc_nodes_n = 0;
    for (int i = 0; i < vm->bc_methods_n; i++) {
        MethodNode *mm = vm->bc_methods[i];
        if (mm && mm->bc_body != BC_NOT_COMPILABLE) mm->bc_body = NULL;
    }
    vm->bc_methods_n = 0;
}

/* ------------------------------------------------------------------------ */
/* Compilador                                                                */
/* ------------------------------------------------------------------------ */

#define BC_MAX_GUARDS 128

typedef struct BCC {
    Instr *out;
    int pos, max;
    BCGuard guards[BC_MAX_GUARDS];
    int n_guards;
    int this_slots[8];
    int n_this;
    ClassNode *cls;          /* clase del método en compilación (this.attr) */
    int failed;
} BCC;

static int bc_emit(BCC *c, BCOp op) {
    if (c->pos >= c->max) { c->failed = 1; return -1; }
    c->out[c->pos].op = (uint8_t)op;
    return c->pos++;
}
static int bc_emit_const(BCC *c, double v) {
    int i = bc_emit(c, BC_LOAD_CONST); if (i >= 0) c->out[i].u.constant = v; return i;
}
static int bc_emit_var(BCC *c, BCOp op, Variable *v) {
    int i = bc_emit(c, op); if (i >= 0) c->out[i].u.var = v; return i;
}

static int bc_guard(BCC *c, Variable *v, int kind, ClassNode *cls, const char *id) {
    for (int i = 0; i < c->n_guards; i++)
        if (c->guards[i].var == v && c->guards[i].kind == kind) return 1;
    if (c->n_guards >= BC_MAX_GUARDS) return 0;
    c->guards[c->n_guards].var = v; c->guards[c->n_guards].kind = kind; c->guards[c->n_guards].cls = cls; c->guards[c->n_guards].id = id;
    c->n_guards++;
    return 1;
}
static int bc_guard_this_slot(BCC *c, int slot) {
    for (int i = 0; i < c->n_this; i++) if (c->this_slots[i] == slot) return 1;
    if (c->n_this >= 8) return 0;
    c->this_slots[c->n_this++] = slot;
    return 1;
}

static int var_is_numeric(const Variable *v) {
    if (!v) return 0;
    if (v->vtype != VAL_INT && v->vtype != VAL_FLOAT) return 0;
    if (v->type && strcmp(v->type, TE_T_NULL) == 0) return 0;
    return 1;
}

static Variable *bc_resolve_var(ASTNode *node) {
    if (!node->id) return (Variable *)node->cached_var;
    return te_resolve_cached(node);
}

static int attr_type_is_numeric(const char *t) {
    return t && (strcmp(t, TE_DT_INT) == 0 || strcmp(t, TE_DT_FLOAT) == 0 ||
                 strcmp(t, TE_T_INT) == 0 || strcmp(t, TE_T_FLOAT) == 0);
}

static int bc_compile(BCC *c, ASTNode *node);

static BCOp bc_binop_of(NodeKind k) {
    switch (k) {
        case NK_ADD:   return BC_ADD;
        case NK_SUB:   return BC_SUB;
        case NK_MUL:   return BC_MUL;
        case NK_DIV:   return BC_DIV;
        case NK_LT:    return BC_LT;
        case NK_GT:    return BC_GT;
        /* Mismo mapeo que el walker: NK_GT_EQ evalúa <=, NK_LT_EQ evalúa >=. */
        case NK_GT_EQ: return BC_LE;
        case NK_LT_EQ: return BC_GE;
        case NK_EQ:    return BC_EQ;
        case NK_DIFF:  return BC_NEQ;
        case NK_AND:   return BC_AND;
        case NK_OR:    return BC_OR;
        case NK_MOD:     return BC_MOD;
        case NK_BIT_AND: return BC_BAND;
        case NK_BIT_OR:  return BC_BOR;
        case NK_BIT_XOR: return BC_BXOR;
        case NK_SHL:     return BC_SHL;
        case NK_SHR:     return BC_SHR;
        default:       return BC_HALT;
    }
}

/* Operador binario: compila ambos operandos; si los dos quedaron como
 * constantes, pliega con la MISMA primitiva que usa el runtime (te_num_binop),
 * salvo división/módulo por cero (el walker imprime el error en cada
 * evaluación, así que se deja para runtime). */
static int bc_c_binop(BCC *c, ASTNode *node) {
    NodeKind k = nk_of(node);
    if (k == NK_ADD && is_string_type(node)) return 0;   /* concat: walker */
    int saved = c->pos;
    if (!bc_compile(c, node->left))  return 0;
    if (!bc_compile(c, node->right)) return 0;
    if (c->pos == saved + 2 &&
        c->out[saved].op == BC_LOAD_CONST && c->out[saved + 1].op == BC_LOAD_CONST) {
        double a = c->out[saved].u.constant, b = c->out[saved + 1].u.constant;
        int zero_div = (k == NK_DIV && b == 0.0) || (k == NK_MOD && (long long)b == 0);
        if (!zero_div) {
            c->pos = saved;
            bc_emit_const(c, te_num_binop(k, a, b));
            return !c->failed;
        }
    }
    BCOp op = bc_binop_of(k);
    if (op == BC_HALT) return 0;
    bc_emit(c, op);
    return !c->failed;
}

static int bc_c_unop(BCC *c, ASTNode *node, BCOp op) {
    if (!node->left) return 0;
    int saved = c->pos;
    if (!bc_compile(c, node->left)) return 0;
    if (c->pos == saved + 1 && c->out[saved].op == BC_LOAD_CONST) {
        c->out[saved].u.constant = te_num_unop(nk_of(node), c->out[saved].u.constant);
        return 1;
    }
    bc_emit(c, op);
    return !c->failed;
}

/* obj.attr: `this.attr` en cuerpos de método, o `lista[i].attr` numérico. */
static int bc_c_access_attr(BCC *c, ASTNode *node) {
    ASTNode *objRef = node->left;
    ASTNode *attr   = node->right;
    if (!objRef || !attr || !attr->id) return 0;

    if (nk_of(objRef) == NK_ACCESS_EXPR) {
        ASTNode *list_id = objRef->left;
        ASTNode *idx_exp = objRef->right;
        if (!list_id || !idx_exp) return 0;
        if (nk_of(list_id) != NK_IDENTIFIER && nk_of(list_id) != NK_ID) return 0;
        Variable *lv = bc_resolve_var(list_id);
        if (!lv || lv->vtype != VAL_OBJECT || !lv->type || strcmp(lv->type, TE_T_LIST) != 0) return 0;
        ASTNode *list = (ASTNode *)(intptr_t)lv->value.object_value;
        if (!list) return 0;
        TEListIdx *ix = (TEListIdx *)list->extra;
        if (!ix || ix->len <= 0) return 0;
        ASTNode *first = ix->items[0];
        if (!first || !first->type || strcmp(first->type, TE_T_OBJECT) != 0) return 0;
        ObjectNode *fobj = first->extra ? (ObjectNode *)first->extra : (ObjectNode *)(intptr_t)first->value;
        if (!fobj || !fobj->class) return 0;
        int slot = -1;
        for (int i = 0; i < fobj->class->attr_count; i++) {
            if (strcmp(fobj->class->attributes[i].id, attr->id) == 0) {
                if (!attr_type_is_numeric(fobj->class->attributes[i].type)) return 0;
                slot = i; break;
            }
        }
        if (slot < 0) return 0;
        if (!bc_compile(c, idx_exp)) return 0;
        if (!bc_guard(c, lv, 2, NULL, list_id->id)) return 0;
        ListItemAttrSite *s = (ListItemAttrSite *)calloc(1, sizeof(ListItemAttrSite));
        if (!s) return 0;
        s->list_var = lv; s->expected_class = fobj->class; s->attr_slot = slot; s->node = node;
        int i = bc_emit(c, BC_LIST_ITEM_ATTR);
        if (i < 0) { free(s); return 0; }
        c->out[i].u.lia_site = s;
        return 1;
    }

    if (!objRef->id || strcmp(objRef->id, TE_SYM_THIS) != 0) return 0;
    if (!c->cls) return 0;
    int slot = -1;
    for (int i = 0; i < c->cls->attr_count; i++) {
        if (strcmp(c->cls->attributes[i].id, attr->id) == 0) {
            if (!attr_type_is_numeric(c->cls->attributes[i].type)) return 0;
            slot = i; break;
        }
    }
    if (slot < 0) return 0;
    if (!bc_guard_this_slot(c, slot)) return 0;
    int i = bc_emit(c, BC_LOAD_THIS_ATTR);
    if (i < 0) return 0;
    c->out[i].u.slot = slot;
    return 1;
}

/* obj.metodo(args) con cuerpo `{ return <expr numérica>; }`: expansión inline.
 *   STORE_VAR param(n-1..0); SET_THIS obj; <cuerpo sin HALT>; RESTORE_THIS
 * Stack neto: -n_params + 1. */
static int bc_c_call_method(BCC *c, ASTNode *node) {
    static int bcc_init = 0, bcc_enabled = 1;
    if (!bcc_init) {
        const char *e = getenv("TYPEEASY_NO_BCCALL");
        if (e && e[0] && e[0] != '0') bcc_enabled = 0;
        bcc_init = 1;
    }
    if (!bcc_enabled) return 0;

    ASTNode *objRef = node->left;
    if (!objRef || !objRef->id || !node->id) return 0;
    Variable *ov = bc_resolve_var(objRef);
    if (!ov || ov->vtype != VAL_OBJECT) return 0;
    if (ov->type && (strcmp(ov->type, TE_T_LIST) == 0 || strcmp(ov->type, TE_T_MAP) == 0 || strcmp(ov->type, TE_T_LAMBDA) == 0)) return 0;
    ObjectNode *obj = ov->value.object_value;
    if (!obj || !obj->class) return 0;

    MethodNode *mm = NULL;
    for (MethodNode *it = obj->class->methods; it; it = it->next)
        if (it->name && strcmp(it->name, node->id) == 0) { mm = it; break; }
    if (!mm) return 0;
    if (!mm->return_type || (strcmp(mm->return_type, TE_DT_INT) != 0 && strcmp(mm->return_type, TE_DT_FLOAT) != 0)) return 0;

    BCInfo *body = bc_get_or_compile_method(mm, obj->class, 0);
    if (!body) return 0;

    int n_params = 0;
    for (ParameterNode *p = mm->params; p; p = p->next) {
        if (n_params >= 8 || !p->name) return 0;
        n_params++;
    }
    int n_args = 0;
    for (ASTNode *a = node->right; a; a = a->next) n_args++;   /* gotcha #1: args via ->next */
    if (n_args != n_params) return 0;

    for (ASTNode *a = node->right; a; a = a->next) {
        NodeKind ak = nk_of(a);
        if (ak == NK_NUMBER || ak == NK_INT) bc_emit_const(c, (double)a->value);
        else if (ak == NK_FLOAT) bc_emit_const(c, a->str_value ? atof(a->str_value) : 0.0);
        else if (ak == NK_IDENTIFIER || ak == NK_ID) {
            Variable *v = bc_resolve_var(a);
            if (!var_is_numeric(v)) return 0;
            if (!bc_guard(c, v, 0, NULL, a->id)) return 0;
            bc_emit_var(c, BC_LOAD_VAR, v);
        } else return 0;
        if (c->failed) return 0;
    }

    Variable *param_vars[8];
    int idx = 0;
    for (ParameterNode *p = mm->params; p; p = p->next) {
        Variable *pv = (Variable *)p->cached_var;
        if (pv && (!pv->id || strcmp(pv->id, p->name) != 0)) pv = NULL;   /* slot liberado por un frame */
        if (!pv) pv = find_variable_for(p->name);
        if (!pv) return 0;   /* bc_get_or_compile_method ya creó los slots */
        p->cached_var = pv;
        param_vars[idx++] = pv;
    }

    int body_len = body->len - 1;   /* sin HALT final */
    if (body_len < 0) body_len = 0;
    if (c->pos + n_params + 2 + body_len >= c->max) return 0;
    for (int i = n_params - 1; i >= 0; i--) bc_emit_var(c, BC_STORE_VAR, param_vars[i]);
    if (!bc_guard(c, ov, 1, obj->class, objRef->id)) return 0;
    bc_emit_var(c, BC_SET_THIS, ov);
    if (body_len > 0) { memcpy(&c->out[c->pos], body->code, (size_t)body_len * sizeof(Instr)); c->pos += body_len; }
    /* Los guards del cuerpo (params numéricos) se heredan; sus this.attr los
     * cubre el guard de clase del objeto (kind 1). */
    for (int i = 0; i < body->n_guards; i++) bc_guard(c, body->guards[i].var, body->guards[i].kind, body->guards[i].cls, body->guards[i].id);
    bc_emit(c, BC_RESTORE_THIS);
    return !c->failed;
}

static int bc_compile(BCC *c, ASTNode *node) {
    if (!node || c->failed || c->pos >= c->max - 1) return 0;
    NodeKind k = nk_of(node);
    switch (k) {
    case NK_NUMBER:
    case NK_INT:
        bc_emit_const(c, (double)node->value);
        return !c->failed;
    case NK_FLOAT:
        bc_emit_const(c, node->str_value ? atof(node->str_value) : 0.0);
        return !c->failed;
    case NK_IDENTIFIER: {
        Variable *v = bc_resolve_var(node);
        if (!var_is_numeric(v)) return 0;
        if (!bc_guard(c, v, 0, NULL, node->id)) return 0;
        bc_emit_var(c, BC_LOAD_VAR, v);
        return !c->failed;
    }
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_DIV:
    case NK_LT:  case NK_GT:  case NK_LT_EQ: case NK_GT_EQ:
    case NK_EQ:  case NK_DIFF:
    case NK_AND: case NK_OR:
    case NK_MOD: case NK_BIT_AND: case NK_BIT_OR: case NK_BIT_XOR:
    case NK_SHL: case NK_SHR:
        return bc_c_binop(c, node);
    case NK_NOT:     return bc_c_unop(c, node, BC_NOT);
    case NK_BIT_NOT: return bc_c_unop(c, node, BC_BNOT);
    case NK_NEG:     return bc_c_unop(c, node, BC_NEG);
    case NK_ACCESS_ATTR: return bc_c_access_attr(c, node);
    case NK_CALL_METHOD: return bc_c_call_method(c, node);
    default:
        return 0;
    }
}

/* ---- Sección entera exacta (espejo de te_eval_i64) ---------------------- */

/* Elegibilidad ESTÁTICA: exactamente los NodeKind que te_eval_i64 acepta y que
 * el bytecode compila (ACCESS_EXPR no se compila -> el árbol entero va al walker). */
static int i64_eligible(ASTNode *n) {
    if (!n) return 0;
    switch (nk_of(n)) {
    case NK_NUMBER: case NK_INT: case NK_IDENTIFIER: case NK_ID: return 1;
    case NK_NEG: return i64_eligible(n->left);
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_MOD: case NK_DIV:
    case NK_GT: case NK_LT: case NK_GT_EQ: case NK_LT_EQ: case NK_EQ: case NK_DIFF:
        return i64_eligible(n->left) && i64_eligible(n->right);
    default: return 0;
    }
}

static int bc_compile_i64(BCC *c, ASTNode *n) {
    if (!n || c->failed) return 0;
    NodeKind k = nk_of(n);
    switch (k) {
    case NK_NUMBER: case NK_INT: {
        int i = bc_emit(c, BC_I64_CONST); if (i < 0) return 0;
        c->out[i].u.ival = n->value; return 1;
    }
    case NK_IDENTIFIER: case NK_ID: {
        Variable *v = bc_resolve_var(n);
        if (!v) return 0;
        bc_emit_var(c, BC_I64_VAR, v);
        return !c->failed;
    }
    case NK_NEG:
        if (!bc_compile_i64(c, n->left)) return 0;
        bc_emit(c, BC_I64_NEG); return !c->failed;
    default: break;
    }
    if (!bc_compile_i64(c, n->left) || !bc_compile_i64(c, n->right)) return 0;
    BCOp op;
    switch (k) {
    case NK_ADD: op = BC_I64_ADD; break;
    case NK_SUB: op = BC_I64_SUB; break;
    case NK_MUL: op = BC_I64_MUL; break;
    case NK_MOD: op = BC_I64_MOD; break;
    case NK_DIV: op = BC_I64_DIV; break;
    case NK_GT:  op = BC_I64_GT;  break;
    case NK_LT:  op = BC_I64_LT;  break;
    case NK_GT_EQ: op = BC_I64_LE; break;   /* mismo mapeo invertido que el walker */
    case NK_LT_EQ: op = BC_I64_GE; break;
    case NK_EQ:  op = BC_I64_EQ;  break;
    case NK_DIFF: op = BC_I64_NEQ; break;
    default: return 0;
    }
    bc_emit(c, op);
    return !c->failed;
}

/* ------------------------------------------------------------------------ */
/* Ejecución                                                                 */
/* ------------------------------------------------------------------------ */

static int bc_guards_ok(const BCInfo *info) {
    for (int i = 0; i < info->n_guards; i++) {
        const BCGuard *g = &info->guards[i];
        if (!g->var) return 0;
        if (g->id && g->var != &g_vm.this_reg) {   /* identidad: el slot VISIBLE con ese nombre */
            int idx = te_sym_lookup(g->id);
            if (idx < 0 || &g_vm.vars[idx] != g->var) return 0;
        }
        switch (g->kind) {
        case 0: if (!var_is_numeric(g->var)) return 0; break;
        case 1: {
            if (!g->var || g->var->vtype != VAL_OBJECT) return 0;
            ObjectNode *o = g->var->value.object_value;
            if (!o || o->class != g->cls) return 0;
            break;
        }
        case 2: if (!g->var || g->var->vtype != VAL_OBJECT || !g->var->type || strcmp(g->var->type, TE_T_LIST) != 0) return 0; break;
        default: return 0;
        }
    }
    if (info->n_this_slots) {
        ObjectNode *t = g_vm.bc_this;
        if (!t || !t->class) return 0;
        for (int i = 0; i < info->n_this_slots; i++) {
            int s = info->this_slots[i];
            if (s < 0 || s >= t->class->attr_count) return 0;
            if (!var_is_numeric(&t->attributes[s])) return 0;
        }
    }
    return 1;
}

static inline double num_of(Variable *v) {
    if (v->vtype == VAL_INT) return (double)v->value.int_value;
    if (v->vtype == VAL_FLOAT) return v->value.float_value;
    return te_var_as_double(v, v->id);
}

/* Stack-based VM with computed goto dispatch (GCC/Clang extension). */
static double bc_exec(Instr *code) {
    static void *table[] = {
        [BC_HALT]           = &&do_halt,
        [BC_LOAD_CONST]     = &&do_const,
        [BC_LOAD_VAR]       = &&do_var,
        [BC_ADD]            = &&do_add,
        [BC_SUB]            = &&do_sub,
        [BC_MUL]            = &&do_mul,
        [BC_DIV]            = &&do_div,
        [BC_LT]             = &&do_lt,
        [BC_GT]             = &&do_gt,
        [BC_LE]             = &&do_le,
        [BC_GE]             = &&do_ge,
        [BC_EQ]             = &&do_eq,
        [BC_NEQ]            = &&do_neq,
        [BC_AND]            = &&do_and,
        [BC_OR]             = &&do_or,
        [BC_NOT]            = &&do_not,
        [BC_NEG]            = &&do_neg,
        [BC_MOD]            = &&do_mod,
        [BC_BAND]           = &&do_band,
        [BC_BOR]            = &&do_bor,
        [BC_BXOR]           = &&do_bxor,
        [BC_BNOT]           = &&do_bnot,
        [BC_SHL]            = &&do_shl,
        [BC_SHR]            = &&do_shr,
        [BC_STORE_VAR]      = &&do_store,
        [BC_JUMP]           = &&do_jump,
        [BC_JUMP_IF_FALSE]  = &&do_jump_if_false,
        [BC_POP]            = &&do_pop,
        [BC_LOAD_THIS_ATTR] = &&do_this_attr,
        [BC_SET_THIS]       = &&do_set_this,
        [BC_RESTORE_THIS]   = &&do_restore_this,
        [BC_LIST_ITEM_ATTR] = &&do_list_item_attr,
        [BC_I64_BEGIN]      = &&do_i64_begin,
        [BC_I64_CONST]      = &&do_i64_const,
        [BC_I64_VAR]        = &&do_i64_var,
        [BC_I64_ADD]        = &&do_i64_add,
        [BC_I64_SUB]        = &&do_i64_sub,
        [BC_I64_MUL]        = &&do_i64_mul,
        [BC_I64_MOD]        = &&do_i64_mod,
        [BC_I64_DIV]        = &&do_i64_div,
        [BC_I64_NEG]        = &&do_i64_neg,
        [BC_I64_LT]         = &&do_i64_lt,
        [BC_I64_GT]         = &&do_i64_gt,
        [BC_I64_LE]         = &&do_i64_le,
        [BC_I64_GE]         = &&do_i64_ge,
        [BC_I64_EQ]         = &&do_i64_eq,
        [BC_I64_NEQ]        = &&do_i64_neq,
        [BC_I64_STORE]      = &&do_i64_store,
    };
    double stack[64];
    int sp = 0;
    long long istack[64];
    int isp = 0;
    Instr *i64_fail = NULL;
    Instr *ip = code;

    #define DISPATCH() goto *table[ip->op]
    /* Mismas primitivas que el walker: te_num_binop / te_num_unop / te_num_store. */
    #define BIN(K) do { stack[sp-2] = te_num_binop(K, stack[sp-2], stack[sp-1]); sp--; ip++; DISPATCH(); } while (0)
    #define UN(K)  do { stack[sp-1] = te_num_unop(K, stack[sp-1]); ip++; DISPATCH(); } while (0)
    #define I64_FAIL() do { ip = i64_fail; isp = 0; DISPATCH(); } while (0)
    #define IBIN(EXPR) do { long long a = istack[isp-2], b = istack[isp-1]; (void)a; (void)b; istack[isp-2] = (EXPR); isp--; ip++; DISPATCH(); } while (0)
    DISPATCH();

do_const:
    stack[sp++] = ip->u.constant; ip++; DISPATCH();
do_var:
    stack[sp++] = num_of(ip->u.var); ip++; DISPATCH();
do_add:  BIN(NK_ADD);
do_sub:  BIN(NK_SUB);
do_mul:  BIN(NK_MUL);
do_div:  BIN(NK_DIV);
do_lt:   BIN(NK_LT);
do_gt:   BIN(NK_GT);
do_le:   BIN(NK_GT_EQ);   /* NK_GT_EQ evalúa <= en el walker */
do_ge:   BIN(NK_LT_EQ);   /* NK_LT_EQ evalúa >= en el walker */
do_eq:   BIN(NK_EQ);
do_neq:  BIN(NK_DIFF);
do_and:  BIN(NK_AND);
do_or:   BIN(NK_OR);
do_mod:  BIN(NK_MOD);
do_band: BIN(NK_BIT_AND);
do_bor:  BIN(NK_BIT_OR);
do_bxor: BIN(NK_BIT_XOR);
do_shl:  BIN(NK_SHL);
do_shr:  BIN(NK_SHR);
do_not:  UN(NK_NOT);
do_bnot: UN(NK_BIT_NOT);
do_neg:  UN(NK_NEG);
do_store:
    te_num_store(ip->u.var, stack[--sp]); ip++; DISPATCH();
do_jump:
    ip += 1 + ip->u.offset; DISPATCH();
do_jump_if_false: {
    double v = stack[--sp];
    if (v == 0.0) ip += 1 + ip->u.offset; else ip++;
    DISPATCH();
}
do_pop:
    sp--; ip++; DISPATCH();
do_this_attr:
    stack[sp++] = num_of(&g_vm.bc_this->attributes[ip->u.slot]); ip++; DISPATCH();
do_set_this:
    g_vm.bc_this_stack[g_vm.bc_this_sp++] = g_vm.bc_this;
    g_vm.bc_this = ip->u.var->value.object_value;
    ip++; DISPATCH();
do_restore_this:
    g_vm.bc_this = g_vm.bc_this_stack[--g_vm.bc_this_sp];
    ip++; DISPATCH();
do_list_item_attr: {
    /* arr[idx].attr sobre lista homogénea; ante cualquier desvío (índice fuera
     * de rango, ítem de otra clase) el valor lo calcula el walker para el
     * MISMO nodo, así el resultado es idéntico por construcción. */
    ListItemAttrSite *s = ip->u.lia_site;
    int idx = (int)stack[sp - 1];
    ASTNode *list = (ASTNode *)(intptr_t)s->list_var->value.object_value;
    TEListIdx *ix = list ? (TEListIdx *)list->extra : NULL;
    int hit = 0;
    if (ix && idx >= 0 && idx < ix->len) {
        ASTNode *item = ix->items[idx];
        ObjectNode *obj = NULL;
        if (item) obj = item->extra ? (ObjectNode *)item->extra : (ObjectNode *)(intptr_t)item->value;
        if (obj && obj->class == s->expected_class && s->attr_slot < obj->class->attr_count) {
            stack[sp - 1] = num_of(&obj->attributes[s->attr_slot]);
            hit = 1;
        }
    }
    if (!hit) stack[sp - 1] = te_walk_expression(s->node);
    ip++; DISPATCH();
}
do_i64_begin:
    i64_fail = ip + 1 + ip->u.offset; isp = 0; ip++; DISPATCH();
do_i64_const:
    istack[isp++] = ip->u.ival; ip++; DISPATCH();
do_i64_var: {
    Variable *v = ip->u.var;
    if (v->vtype != VAL_INT || (v->type && strcmp(v->type, TE_T_NULL) == 0)) I64_FAIL();
    istack[isp++] = v->value.int_value; ip++; DISPATCH();
}
do_i64_add: IBIN(a + b);
do_i64_sub: IBIN(a - b);
do_i64_mul: IBIN(a * b);
do_i64_mod: { if (istack[isp-1] == 0) I64_FAIL(); IBIN(a % b); }
do_i64_div: { if (istack[isp-1] == 0 || istack[isp-2] % istack[isp-1] != 0) I64_FAIL(); IBIN(a / b); }
do_i64_neg: istack[isp-1] = -istack[isp-1]; ip++; DISPATCH();
do_i64_lt:  IBIN(a < b);
do_i64_gt:  IBIN(a > b);
do_i64_le:  IBIN(a <= b);
do_i64_ge:  IBIN(a >= b);
do_i64_eq:  IBIN(a == b);
do_i64_neq: IBIN(a != b);
do_i64_store:
    te_num_store_i64(ip->u.var, istack[--isp]); ip++; DISPATCH();
do_halt:
    return sp > 0 ? stack[sp-1] : 0;
    #undef DISPATCH
    #undef BIN
    #undef UN
    #undef IBIN
    #undef I64_FAIL
}

int bc_run(BCInfo *info, double *result) {
    if (!info || !bc_guards_ok(info)) return 0;
    double r = bc_exec(info->code);
    if (result) *result = r;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Puntos de entrada de compilación                                          */
/* ------------------------------------------------------------------------ */

static BCInfo *bc_finish(BCC *c) {
    if (c->failed || c->pos >= c->max) return NULL;
    c->out[c->pos++].op = BC_HALT;
    BCInfo *info = (BCInfo *)calloc(1, sizeof(BCInfo));
    if (!info) return NULL;
    info->code = (Instr *)malloc(sizeof(Instr) * (size_t)c->pos);
    if (!info->code) { free(info); return NULL; }
    memcpy(info->code, c->out, sizeof(Instr) * (size_t)c->pos);
    info->len = c->pos;
    if (c->n_guards) {
        info->guards = (BCGuard *)malloc(sizeof(BCGuard) * (size_t)c->n_guards);
        if (!info->guards) { free(info->code); free(info); return NULL; }
        memcpy(info->guards, c->guards, sizeof(BCGuard) * (size_t)c->n_guards);
        info->n_guards = c->n_guards;
    }
    memcpy(info->this_slots, c->this_slots, sizeof(info->this_slots));
    info->n_this_slots = c->n_this;
    return info;
}

BCInfo *bc_get_or_compile(ASTNode *node) {
    if (!node) return NULL;
    void *p = node->bc;
    if (p == BC_NOT_COMPILABLE) return NULL;
    if (p) return (BCInfo *)p;

    /* Only arithmetic/comparisons are worth it. Do NOT mark non-worth nodes as
     * BC_NOT_COMPILABLE: the statement compiler uses node->bc too. */
    NodeKind k = nk_of(node);
    int worth = (k == NK_ADD || k == NK_SUB || k == NK_MUL || k == NK_DIV ||
                 k == NK_LT  || k == NK_GT  || k == NK_LT_EQ || k == NK_GT_EQ ||
                 k == NK_EQ  || k == NK_DIFF || k == NK_AND || k == NK_OR || k == NK_NOT);
    if (!worth) return NULL;

    Instr buf[64];
    BCC c = { .out = buf, .pos = 0, .max = 64, .cls = NULL };
    BCInfo *info = bc_compile(&c, node) ? bc_finish(&c) : NULL;
    if (!info) { node->bc = BC_NOT_COMPILABLE; return NULL; }
    node->bc = info;
    bc_register_node(node);
    return info;
}

/* Cuerpo `{ return <expr>; }` (saltando STATEMENT_LIST de un solo hijo). */
static ASTNode *bc_find_single_return(ASTNode *body) {
    while (body) {
        NodeKind k = nk_of(body);
        if (k == NK_RETURN) return body;
        if (k != NK_STATEMENT_LIST) return NULL;
        if (body->left && !body->right) { body = body->left; continue; }
        if (body->right && !body->left) { body = body->right; continue; }
        if (body->left && body->right) {
            if (nk_of(body->left) == NK_STATEMENT_LIST && !body->left->left && !body->left->right) { body = body->right; continue; }
            if (nk_of(body->right) == NK_STATEMENT_LIST && !body->right->left && !body->right->right) { body = body->left; continue; }
        }
        return NULL;
    }
    return NULL;
}

BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls, int in_method_frame) {
    if (!m) return NULL;
    if (m->bc_body == BC_NOT_COMPILABLE) return NULL;
    if (m->bc_body) return (BCInfo *)m->bc_body;

    ASTNode *ret = bc_find_single_return(m->body);
    if (!ret || !ret->left) { m->bc_body = BC_NOT_COMPILABLE; return NULL; }

    /* Pre-crear los slots de los parámetros para que los identificadores del
     * cuerpo resuelvan aunque el método nunca se haya invocado. */
    for (ParameterNode *p = m->params; p; p = p->next) {
        if (!p->name) continue;
        Variable *pv = (Variable *)p->cached_var;
        if (pv && (!pv->id || strcmp(pv->id, p->name) != 0)) pv = NULL;   /* slot liberado por un frame */
        if (!pv) pv = find_variable_for(p->name);
        if (pv && !in_method_frame) return NULL;   /* variable del llamador con el nombre del param: no inline */
        if (!pv && g_vm.var_count < MAX_VARS) {
            int is_float = p->type && (strcmp(p->type, TE_DT_FLOAT) == 0 || strcmp(p->type, TE_T_FLOAT) == 0);
            Variable *nv = &g_vm.vars[g_vm.var_count++];
            nv->id = strdup(p->name);
            nv->type = strdup(is_float ? TE_T_FLOAT : TE_T_INT);
            nv->is_const = 0;
            nv->vtype = is_float ? VAL_FLOAT : VAL_INT;
            if (is_float) nv->value.float_value = 0.0; else nv->value.int_value = 0;
            te_sym_insert(nv->id, (int)(nv - g_vm.vars));
            pv = nv;
        }
        if (pv) p->cached_var = pv;
    }

    Instr buf[64];
    BCC c = { .out = buf, .pos = 0, .max = 63, .cls = cls };
    BCInfo *info = bc_compile(&c, ret->left) ? bc_finish(&c) : NULL;
    if (!info) { m->bc_body = BC_NOT_COMPILABLE; return NULL; }
    m->bc_body = info;
    bc_register_method(m);
    return info;
}

/* ---- Statements: ASSIGN / WHILE / IF / FOR ------------------------------- */

#define BC_STMT_MAX 1024

static int bc_compile_stmt(BCC *c, ASTNode *node);

/* `var = expr` sobre una variable numérica existente: espejo del fast-path de
 * interpret_assign (mismos NodeKind admitidos en la raíz; primero el camino
 * entero exacto te_eval_i64, si algo no es entero la evaluación double). Otros
 * RHS (comparaciones -> BOOL, % y bits -> cambian el tag `type`) van al walker. */
static int bc_compile_assign(BCC *c, ASTNode *node) {
    ASTNode *var_node = node->left, *value_node = node->right;
    if (!var_node || !var_node->id || !value_node) return 0;
    NodeKind vk = nk_of(value_node);
    if (vk != NK_ADD && vk != NK_SUB && vk != NK_MUL && vk != NK_DIV &&
        vk != NK_NUMBER && vk != NK_INT && vk != NK_FLOAT && vk != NK_IDENTIFIER) return 0;
    Variable *fv = te_resolve_cached(var_node);
    if (!fv || fv->is_const || !var_is_numeric(fv)) return 0;
    if (vk == NK_ADD && is_string_type(value_node)) return 0;
    if (!bc_guard(c, fv, 0, NULL, var_node->id)) return 0;

    if (i64_eligible(value_node)) {
        int begin = bc_emit(c, BC_I64_BEGIN);
        if (begin < 0) return 0;
        if (!bc_compile_i64(c, value_node)) return 0;
        bc_emit_var(c, BC_I64_STORE, fv);
        int jmp = bc_emit(c, BC_JUMP);
        if (jmp < 0) return 0;
        c->out[begin].u.offset = c->pos - (begin + 1);
        if (!bc_compile(c, value_node)) return 0;
        bc_emit_var(c, BC_STORE_VAR, fv);
        if (c->failed) return 0;
        c->out[jmp].u.offset = c->pos - (jmp + 1);
        return 1;
    }
    if (!bc_compile(c, value_node)) return 0;
    bc_emit_var(c, BC_STORE_VAR, fv);
    return !c->failed;
}

static int bc_compile_while(BCC *c, ASTNode *node) {
    if (!node->left || !node->right) return 0;
    int start = c->pos;
    if (!bc_compile(c, node->left)) return 0;
    int jif = bc_emit(c, BC_JUMP_IF_FALSE);
    if (jif < 0) return 0;
    if (!bc_compile_stmt(c, node->right)) return 0;
    int back = bc_emit(c, BC_JUMP);
    if (back < 0) return 0;
    c->out[back].u.offset = start - (back + 1);
    c->out[jif].u.offset = c->pos - (jif + 1);
    return 1;
}

static int bc_compile_if(BCC *c, ASTNode *node) {
    if (!node->left) return 0;
    if (!bc_compile(c, node->left)) return 0;
    int jif = bc_emit(c, BC_JUMP_IF_FALSE);
    if (jif < 0) return 0;
    if (node->right && !bc_compile_stmt(c, node->right)) return 0;
    if (node->next) {
        int jmp = bc_emit(c, BC_JUMP);
        if (jmp < 0) return 0;
        c->out[jif].u.offset = c->pos - (jif + 1);
        if (!bc_compile_stmt(c, node->next)) return 0;
        c->out[jmp].u.offset = c->pos - (jmp + 1);
    } else {
        c->out[jif].u.offset = c->pos - (jif + 1);
    }
    return 1;
}

/* `for (i = INIT; LIMIT; STEP) { body }` con INIT/LIMIT/STEP literales. El
 * walker (interpret_for) evalúa límite y paso como `int` y avanza el contador
 * con `+=` entero; aquí se replica con la sección entera exacta. */
static int bc_compile_for(BCC *c, ASTNode *node) {
    if (!node || !node->id || !node->left || !node->right) return 0;
    ASTNode *update_body = node->right->right;
    if (!update_body || !update_body->left) return 0;

    Variable *fv = find_variable_for(node->id);
    if (!fv && g_vm.var_count < MAX_VARS) {
        Variable *nv = &g_vm.vars[g_vm.var_count++];
        nv->id = strdup(node->id); nv->type = strdup(TE_T_INT); nv->is_const = 0;
        nv->vtype = VAL_INT; nv->value.int_value = 0;
        te_sym_insert(nv->id, (int)(nv - g_vm.vars));
        fv = nv;
    }
    if (!fv || fv->is_const || fv->vtype != VAL_INT) return 0;
    NodeKind ik = nk_of(node->left), lk = nk_of(node->right), sk = nk_of(update_body->left);
    if ((ik != NK_NUMBER && ik != NK_INT) || (lk != NK_NUMBER && lk != NK_INT) || (sk != NK_NUMBER && sk != NK_INT)) return 0;
    long long init_val = node->left->value;
    int limit_val = (int)node->right->value;          /* mismos casts que interpret_for */
    int step_val  = (int)update_body->left->value;
    if (step_val == 0) return 0;
    if (!bc_guard(c, fv, 0, NULL, node->id)) return 0;

    bc_emit_const(c, (double)init_val);            /* literal entero -> te_num_store deja INT */
    bc_emit_var(c, BC_STORE_VAR, fv);
    int top = c->pos;
    bc_emit_var(c, BC_LOAD_VAR, fv);
    bc_emit_const(c, (double)limit_val);
    bc_emit(c, BC_LT);
    int jif = bc_emit(c, BC_JUMP_IF_FALSE);
    if (jif < 0) return 0;
    ASTNode *body = update_body->right;
    if (body && !bc_compile_stmt(c, body)) return 0;
    /* i = i + step: entero exacto; si `i` dejó de ser INT, suma en double. */
    int begin = bc_emit(c, BC_I64_BEGIN);
    if (begin < 0) return 0;
    bc_emit_var(c, BC_I64_VAR, fv);
    int k = bc_emit(c, BC_I64_CONST); if (k < 0) return 0; c->out[k].u.ival = step_val;
    bc_emit(c, BC_I64_ADD);
    bc_emit_var(c, BC_I64_STORE, fv);
    int back = bc_emit(c, BC_JUMP);
    if (back < 0) return 0;
    c->out[back].u.offset = top - (back + 1);
    c->out[begin].u.offset = c->pos - (begin + 1);
    bc_emit_var(c, BC_LOAD_VAR, fv);
    bc_emit_const(c, (double)step_val);
    bc_emit(c, BC_ADD);
    bc_emit_var(c, BC_STORE_VAR, fv);
    int back2 = bc_emit(c, BC_JUMP);
    if (back2 < 0) return 0;
    c->out[back2].u.offset = top - (back2 + 1);
    c->out[jif].u.offset = c->pos - (jif + 1);
    return !c->failed;
}

static int bc_compile_stmt(BCC *c, ASTNode *node) {
    if (!node) return 1;
    switch (nk_of(node)) {
    case NK_STATEMENT_LIST:
        return bc_compile_stmt(c, node->left) && bc_compile_stmt(c, node->right);
    case NK_ASSIGN: return bc_compile_assign(c, node);
    case NK_WHILE:  return bc_compile_while(c, node);
    case NK_IF:     return bc_compile_if(c, node);
    case NK_FOR:    return bc_compile_for(c, node);
    default:        return 0;   /* PRINT, CALL, BREAK, RETURN, THROW, VAR_DECL, ... -> walker */
    }
}

BCInfo *bc_get_or_compile_stmt(ASTNode *node) {
    if (!node) return NULL;
    void *p = node->bc;
    if (p == BC_NOT_COMPILABLE) return NULL;
    if (p) return (BCInfo *)p;
    NodeKind k = nk_of(node);
    if (k != NK_WHILE && k != NK_IF && k != NK_FOR) { node->bc = BC_NOT_COMPILABLE; return NULL; }

    Instr *buf = (Instr *)malloc(sizeof(Instr) * BC_STMT_MAX);
    if (!buf) { node->bc = BC_NOT_COMPILABLE; return NULL; }
    BCC c = { .out = buf, .pos = 0, .max = BC_STMT_MAX, .cls = NULL };
    int ok = (k == NK_WHILE) ? bc_compile_while(&c, node)
           : (k == NK_FOR)   ? bc_compile_for(&c, node)
                             : bc_compile_if(&c, node);
    BCInfo *info = ok ? bc_finish(&c) : NULL;
    free(buf);
    if (!info) { node->bc = BC_NOT_COMPILABLE; return NULL; }
    node->bc = info;
    bc_register_node(node);
    return info;
}
