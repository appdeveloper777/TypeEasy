/* ============================================================================
 * te_linq.c — lazy LINQ pipeline (LAZY_ITER + LAZY_OP chain).
 *
 * Extracted from ast.c (Nivel B paso 1). See te_linq.h for the public surface.
 *
 * AST layout used by this module:
 *   LAZY_ITER node:
 *     ->left  = source LIST (raw ASTNode)
 *     ->right = head of LAZY_OP linked list (via ->right)
 *   LAZY_OP node:
 *     ->id    = op name ("where"|"filter"|"select"|"map"|"take"|"skip")
 *     ->left  = lambda arg (NULL for take/skip)
 *     ->value = take/skip count
 *     ->right = next op
 *
 * Intermediate methods append an op and return a NEW LAZY_ITER (no mutation
 * of the parent — same parent may still be referenced by a variable).
 * Terminal methods (toList/count/sum/first/forEach/reduce/any/every) drain
 * the pipeline in a single pass over the source list.
 * ============================================================================ */

#include "te_linq.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Deep-clone the op chain. Lambdas inside ->left are shared by reference
 * (they are immutable AST sub-trees). */
static ASTNode *lazy_clone_ops(ASTNode *head) {
    ASTNode *new_head = NULL, *tail = NULL;
    for (ASTNode *cur = head; cur; cur = cur->right) {
        ASTNode *cp = (ASTNode *)calloc(1, sizeof(ASTNode));
        cp->type = strdup("LAZY_OP");
        cp->id   = strdup(cur->id ? cur->id : "");
        cp->left = cur->left;   /* shared lambda ref */
        cp->value = cur->value; /* take/skip cap */
        if (!new_head) new_head = cp; else tail->right = cp;
        tail = cp;
    }
    return new_head;
}

ASTNode *lazy_extend(ASTNode *parent_lazy, ASTNode *src_list_raw,
                     const char *op_name, ASTNode *lambda_arg, int num_arg) {
    ASTNode *lz = (ASTNode *)calloc(1, sizeof(ASTNode));
    lz->type = strdup("LAZY_ITER");
    lz->left = parent_lazy ? parent_lazy->left : src_list_raw;
    ASTNode *ops = parent_lazy ? lazy_clone_ops(parent_lazy->right) : NULL;
    ASTNode *op = (ASTNode *)calloc(1, sizeof(ASTNode));
    op->type = strdup("LAZY_OP");
    op->id   = strdup(op_name);
    op->left = lambda_arg;
    op->value = num_arg;
    if (!ops) {
        lz->right = op;
    } else {
        ASTNode *t = ops;
        while (t->right) t = t->right;
        t->right = op;
        lz->right = ops;
    }
    return lz;
}

ASTNode *lazy_resolve_lambda_arg(ASTNode *arg) {
    if (!arg) return NULL;
    if (arg->type && strcmp(arg->type, "LAMBDA") == 0) return arg;
    if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
        Variable *fv = find_variable(arg->id);
        if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
            return (ASTNode *)(intptr_t)fv->value.object_value;
    }
    return NULL;
}

/* Truthy test mirroring fusion/where logic. */
static int lazy_truthy(ASTNode *r) {
    if (!r) return 0;
    if (r->type && strcmp(r->type, "STRING") == 0)
        return (r->str_value && r->str_value[0]) ? 1 : 0;
    if (r->type && strcmp(r->type, "NULL") == 0) return 0;
    return (evaluate_expression(r) != 0) ? 1 : 0;
}

/* Run intermediate ops on one item. Returns:
 *   1  -> item passed all ops, *out_item is set (possibly transformed)
 *   0  -> item filtered out (continue)
 *  -1  -> pipeline should STOP (take limit reached) */
static int lazy_run_item(ASTNode *ops, ASTNode *item, ASTNode **out_item,
                         int *counters) {
    ASTNode *cur = item;
    int op_idx = 0;
    for (ASTNode *op = ops; op; op = op->right, op_idx++) {
        const char *name = op->id;
        if (strcmp(name, "where") == 0 || strcmp(name, "filter") == 0) {
            if (!lazy_truthy(call_lambda(op->left, cur))) return 0;
        } else if (strcmp(name, "select") == 0 || strcmp(name, "map") == 0) {
            cur = call_lambda(op->left, cur);
            if (!cur) return 0;
        } else if (strcmp(name, "take") == 0) {
            if (counters[op_idx] >= op->value) return -1;
            counters[op_idx]++;
        } else if (strcmp(name, "skip") == 0) {
            if (counters[op_idx] < op->value) { counters[op_idx]++; return 0; }
        }
    }
    *out_item = cur;
    return 1;
}

int lazy_terminal(ASTNode *lazy_node, ASTNode *node) {
    const char *t_name = node->id;
    if (!t_name) return 0;
    int is_toList  = (strcmp(t_name, "toList") == 0 || strcmp(t_name, "toArray") == 0);
    int is_count   = (strcmp(t_name, "count") == 0);
    int is_sum     = (strcmp(t_name, "sum") == 0);
    int is_first   = (strcmp(t_name, "first") == 0);
    int is_forEach = (strcmp(t_name, "forEach") == 0);
    int is_reduce  = (strcmp(t_name, "reduce") == 0);
    int is_any     = (strcmp(t_name, "any") == 0);
    int is_every   = (strcmp(t_name, "every") == 0);
    if (!(is_toList || is_count || is_sum || is_first || is_forEach ||
          is_reduce || is_any || is_every)) return 0;

    ASTNode *ops = lazy_node->right;
    ASTNode *src = lazy_node->left;
    int n_ops = 0;
    for (ASTNode *o = ops; o; o = o->right) n_ops++;
    int *counters = n_ops > 0 ? (int *)calloc((size_t)n_ops, sizeof(int)) : NULL;

    ASTNode *t_lam = NULL;
    ASTNode *reduce_acc = NULL;
    if (is_reduce) {
        /* reduce(init, fn): args via node->right (init) and ->right->right (fn) */
        ASTNode *init_arg = node->right;
        ASTNode *fn_arg = init_arg ? init_arg->right : NULL;
        if (init_arg) {
            ASTNode *iv = build_item_from_value(init_arg);
            reduce_acc = iv ? iv : init_arg;
        }
        t_lam = lazy_resolve_lambda_arg(fn_arg);
    } else if (is_forEach || is_any || is_every) {
        t_lam = lazy_resolve_lambda_arg(node->right);
    }

    ASTNode *result = is_toList ? create_list_node(NULL) : NULL;
    long long count = 0;
    double sum_acc = 0.0; int sum_is_int = 1;
    ASTNode *first_out = NULL;
    int any_match = 0, every_match = 1;

    ASTNode *item = src ? src->left : NULL;
    while (item) {
        ASTNode *out = item;
        int r = lazy_run_item(ops, item, &out, counters);
        if (r == -1) break;
        if (r == 0) { item = item->next; continue; }
        count++;
        if (is_toList) {
            te_list_append(result, build_item_from_value(out));
        } else if (is_sum) {
            double v = out->str_value ? atof(out->str_value) : (double)out->value;
            if (v != (double)(long long)v) sum_is_int = 0;
            sum_acc += v;
        } else if (is_first) {
            first_out = build_item_from_value(out);
            break;
        } else if (is_forEach && t_lam) {
            call_lambda(t_lam, out);
        } else if (is_reduce && t_lam && reduce_acc) {
            ASTNode *saved_right = reduce_acc->right;
            reduce_acc->right = out;
            ASTNode *r2 = call_lambda(t_lam, reduce_acc);
            reduce_acc->right = saved_right;
            if (r2) reduce_acc = r2;
        } else if (is_any && t_lam) {
            if (lazy_truthy(call_lambda(t_lam, out))) { any_match = 1; break; }
        } else if (is_every && t_lam) {
            if (!lazy_truthy(call_lambda(t_lam, out))) { every_match = 0; break; }
        }
        item = item->next;
    }
    if (counters) free(counters);

    if (is_toList) { add_or_update_variable("__ret__", result); return 1; }
    if (is_count)  { add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)count, NULL, NULL)); return 1; }
    if (is_sum) {
        if (sum_is_int && sum_acc == (double)(long long)sum_acc)
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)sum_acc, NULL, NULL));
        else {
            char buf[64]; snprintf(buf, sizeof(buf), "%g", sum_acc);
            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        }
        return 1;
    }
    if (is_first) {
        add_or_update_variable("__ret__", first_out ? first_out : create_ast_leaf("NULL", 0, NULL, NULL));
        return 1;
    }
    if (is_forEach) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
    if (is_reduce)  { add_or_update_variable("__ret__", reduce_acc ? reduce_acc : create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
    if (is_any)     { add_or_update_variable("__ret__", create_ast_leaf_number("INT", any_match,   NULL, NULL)); return 1; }
    if (is_every)   { add_or_update_variable("__ret__", create_ast_leaf_number("INT", every_match, NULL, NULL)); return 1; }
    return 0;
}
