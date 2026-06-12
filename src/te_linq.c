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
        /* reduce(init, fn): args via node->right (init) and ->next (fn) */
        ASTNode *init_arg = node->right;
        ASTNode *fn_arg = init_arg ? init_arg->next : NULL; /* gotcha #1: 2nd arg via ->next */
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
            char buf[64]; te_fmt_double(buf, sizeof(buf), sum_acc);
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

/* ============================================================================
 * Nivel B paso 2.e: dispatchers de alto nivel para interpret_call_method.
 * ============================================================================ */

/* ast.c helpers used here. find_variable is in ast.h. */

int te_linq_lazy_method_dispatch(ASTNode *node, Variable *v) {
    if (!v || !v->type || strcmp(v->type, "LAZY_ITER") != 0 || !node || !node->id) return 0;
    ASTNode *lz = (ASTNode*)(intptr_t)v->value.object_value;
    const char *m = node->id;
    /* Intermediate: where/filter/select/map (lambda) */
    if (strcmp(m, "where") == 0 || strcmp(m, "filter") == 0 ||
        strcmp(m, "select") == 0 || strcmp(m, "map") == 0) {
        ASTNode *lam = lazy_resolve_lambda_arg(node->right);
        if (lam) {
            add_or_update_variable("__ret__", lazy_extend(lz, NULL, m, lam, 0));
            return 1;
        }
    }
    /* Intermediate: take/skip (int arg) */
    if (strcmp(m, "take") == 0 || strcmp(m, "skip") == 0) {
        int n = node->right ? (int)evaluate_expression(node->right) : 0;
        add_or_update_variable("__ret__", lazy_extend(lz, NULL, m, NULL, n));
        return 1;
    }
    /* Terminal: toList/toArray/count/sum/first/forEach/reduce/any/every */
    if (lazy_terminal(lz, node)) return 1;
    /* Unknown method on LAZY_ITER — caller may fall through. */
    return 0;
}

/* v0.0.20: structural equality used by set operations (union/intersect/except).
 * Strings compare by value; numbers by numeric value; string vs number never
 * equal (type-strict, matching C# default equality semantics). */
static int te_item_equal(ASTNode *a, ASTNode *b) {
    if (!a || !b) return a == b;
    int a_str = (a->type && strcmp(a->type, "STRING") == 0);
    int b_str = (b->type && strcmp(b->type, "STRING") == 0);
    if (a_str != b_str) return 0;
    if (a_str) {
        return a->str_value && b->str_value && strcmp(a->str_value, b->str_value) == 0;
    }
    double av = a->str_value ? atof(a->str_value) : (double)a->value;
    double bv = b->str_value ? atof(b->str_value) : (double)b->value;
    return av == bv;
}

/* v0.0.20: resolve a method argument that should be another LIST
 * (literal or identifier). Mirrors the concat/zip resolution pattern. */
static ASTNode *te_resolve_list_arg(ASTNode *arg) {
    if (!arg) return NULL;
    if (arg->type && strcmp(arg->type, "LIST") == 0) return arg;
    if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
        Variable *ov = find_variable(arg->id);
        if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "LIST") == 0) {
            return (ASTNode*)(intptr_t)ov->value.object_value;
        }
    }
    return NULL;
}

/* v0.0.20: true if `target` is already present in result list `result`. */
static int te_list_contains_item(ASTNode *result, ASTNode *target) {
    for (ASTNode *r = result->left; r; r = r->next) {
        if (te_item_equal(r, target)) return 1;
    }
    return 0;
}

int te_linq_list_method_dispatch(ASTNode *node, ASTNode *list) {
    if (!list || !node || !node->id) return 0;
    const char *fname = node->id;

    /* ===== v0.0.12 #8 Lazy iterator promotion: xs.lazy() -> LAZY_ITER ===== */
    if (strcmp(fname, "lazy") == 0) {
        ASTNode *lz = (ASTNode*)calloc(1, sizeof(ASTNode));
        lz->type = strdup("LAZY_ITER");
        lz->left = list;   /* source LIST (shared ref) */
        lz->right = NULL;  /* empty op chain */
        add_or_update_variable("__ret__", lz);
        return 1;
    }

    /* ===== xs.count() no-arg: equivalente a C# .Count() => longitud =====
     * Antes `count` solo se reconocía en cadenas lazy y en CSV; sobre una
     * LIST plana caía a un path no manejado y segfaulteaba. La forma con
     * predicado es `countWhere(pred)` (manejada en te_linq_ops.c). */
    if (strcmp(fname, "count") == 0 && !node->right) {
        long long n = 0;
        for (ASTNode *it = list->left; it; it = it->next) n++;
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)n, NULL, NULL));
        return 1;
    }

    /* ===== v0.0.11 LINQ: numeric / no-arg methods on LIST ===== */
    if (!(strcmp(fname, "sum") == 0 ||
          strcmp(fname, "avg") == 0 ||
          strcmp(fname, "average") == 0 ||
          strcmp(fname, "minVal") == 0 ||
          strcmp(fname, "maxVal") == 0 ||
          strcmp(fname, "first") == 0 ||
          strcmp(fname, "last") == 0 ||
          strcmp(fname, "single") == 0 ||
          strcmp(fname, "firstOrDefault") == 0 ||
          strcmp(fname, "lastOrDefault") == 0 ||
          strcmp(fname, "take") == 0 ||
          strcmp(fname, "skip") == 0 ||
          strcmp(fname, "distinct") == 0 ||
          strcmp(fname, "toList") == 0 ||
          strcmp(fname, "concat") == 0 ||
          strcmp(fname, "union") == 0 ||
          strcmp(fname, "intersect") == 0 ||
          strcmp(fname, "except") == 0 ||
          strcmp(fname, "zip") == 0)) {
        return 0;
    }

    if (strcmp(fname, "sum") == 0) {
        double acc = 0.0; int is_int = 1;
        ASTNode *it = list->left;
        while (it) {
            double v = it->str_value ? atof(it->str_value) : (double)it->value;
            if (v != (double)(long long)v) is_int = 0;
            acc += v;
            it = it->next;
        }
        if (is_int && acc == (double)(long long)acc) {
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)acc, NULL, NULL));
        } else {
            char buf[64]; te_fmt_double(buf, sizeof(buf), acc);
            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        }
        return 1;
    }
    if (strcmp(fname, "avg") == 0 || strcmp(fname, "average") == 0) {
        double acc = 0.0; int cnt = 0;
        ASTNode *it = list->left;
        while (it) {
            acc += it->str_value ? atof(it->str_value) : (double)it->value;
            cnt++; it = it->next;
        }
        double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
        char buf[64]; te_fmt_double(buf, sizeof(buf), res);
        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        return 1;
    }
    if (strcmp(fname, "minVal") == 0 || strcmp(fname, "maxVal") == 0) {
        int want_max = (strcmp(fname, "maxVal") == 0);
        ASTNode *it = list->left;
        if (!it) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
        int is_str = (it->type && strcmp(it->type, "STRING") == 0);
        double best_n = 0.0; char *best_s = NULL;
        if (is_str) best_s = it->str_value ? it->str_value : "";
        else best_n = it->str_value ? atof(it->str_value) : (double)it->value;
        ASTNode *best_node = it;
        it = it->next;
        while (it) {
            if (is_str) {
                const char *cs = it->str_value ? it->str_value : "";
                int c = strcmp(cs, best_s);
                if (want_max ? (c > 0) : (c < 0)) { best_s = (char*)cs; best_node = it; }
            } else {
                double v = it->str_value ? atof(it->str_value) : (double)it->value;
                if (want_max ? (v > best_n) : (v < best_n)) { best_n = v; best_node = it; }
            }
            it = it->next;
        }
        add_or_update_variable("__ret__", build_item_from_value(best_node));
        return 1;
    }
    if (strcmp(fname, "first") == 0) {
        if (list->left) add_or_update_variable("__ret__", build_item_from_value(list->left));
        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
        return 1;
    }
    if (strcmp(fname, "firstOrDefault") == 0) {
        if (list->left) add_or_update_variable("__ret__", build_item_from_value(list->left));
        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
        return 1;
    }
    if (strcmp(fname, "last") == 0) {
        ASTNode *cur = list->left;
        if (!cur) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
        while (cur->next) cur = cur->next;
        add_or_update_variable("__ret__", build_item_from_value(cur));
        return 1;
    }
    if (strcmp(fname, "lastOrDefault") == 0) {
        ASTNode *cur = list->left;
        if (!cur) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
        while (cur->next) cur = cur->next;
        add_or_update_variable("__ret__", build_item_from_value(cur));
        return 1;
    }
    if (strcmp(fname, "single") == 0) {
        ASTNode *cur = list->left;
        if (!cur) {
            printf("Error: single() requires exactly one element, list is empty.\n");
            add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
            return 1;
        }
        if (cur->next) {
            printf("Error: single() requires exactly one element, list has more than one.\n");
            add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
            return 1;
        }
        add_or_update_variable("__ret__", build_item_from_value(cur));
        return 1;
    }
    if (strcmp(fname, "take") == 0) {
        int n = node->right ? (int)evaluate_expression(node->right) : 0;
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = list->left;
        int i = 0;
        while (it && i < n) {
            te_list_append(result, build_item_from_value(it));
            it = it->next; i++;
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "skip") == 0) {
        int n = node->right ? (int)evaluate_expression(node->right) : 0;
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = list->left;
        int i = 0;
        while (it) {
            if (i >= n) te_list_append(result, build_item_from_value(it));
            it = it->next; i++;
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "distinct") == 0) {
        /* Hash-based dedupe with parallel int/string tables. */
        ASTNode *result = create_list_node(NULL);
        size_t icap = 64, scap = 64;
        size_t icount = 0, scount = 0;
        long long *ikeys = (long long*)calloc(icap, sizeof(long long));
        unsigned char *iused = (unsigned char*)calloc(icap, 1);
        char **skeys = (char**)calloc(scap, sizeof(char*));
        ASTNode *it = list->left;
        while (it) {
            int is_str = (it->type && strcmp(it->type, "STRING") == 0);
            int dup = 0;
            if (is_str) {
                const char *s = it->str_value ? it->str_value : "";
                uint32_t h = 2166136261u;
                for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
                    h ^= *p; h *= 16777619u;
                }
                size_t mask = scap - 1;
                size_t i_h = (size_t)h & mask;
                while (skeys[i_h] && strcmp(skeys[i_h], s) != 0) i_h = (i_h + 1) & mask;
                if (skeys[i_h]) dup = 1;
                else {
                    skeys[i_h] = strdup(s);
                    scount++;
                    if (scount * 2 >= scap) {
                        size_t nc = scap * 2;
                        char **ns = (char**)calloc(nc, sizeof(char*));
                        size_t nm = nc - 1;
                        for (size_t k = 0; k < scap; k++) if (skeys[k]) {
                            uint32_t h2 = 2166136261u;
                            for (const unsigned char *p = (const unsigned char*)skeys[k]; *p; p++) {
                                h2 ^= *p; h2 *= 16777619u;
                            }
                            size_t j = (size_t)h2 & nm;
                            while (ns[j]) j = (j + 1) & nm;
                            ns[j] = skeys[k];
                        }
                        free(skeys); skeys = ns; scap = nc;
                    }
                }
            } else {
                long long key = it->str_value ? (long long)atof(it->str_value) : (long long)it->value;
                uint64_t x = (uint64_t)key;
                x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
                x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
                size_t mask = icap - 1;
                size_t i_h = (size_t)x & mask;
                while (iused[i_h] && ikeys[i_h] != key) i_h = (i_h + 1) & mask;
                if (iused[i_h]) dup = 1;
                else {
                    iused[i_h] = 1; ikeys[i_h] = key; icount++;
                    if (icount * 2 >= icap) {
                        size_t nc = icap * 2;
                        long long *nk = (long long*)calloc(nc, sizeof(long long));
                        unsigned char *nu = (unsigned char*)calloc(nc, 1);
                        size_t nm = nc - 1;
                        for (size_t k = 0; k < icap; k++) if (iused[k]) {
                            uint64_t x2 = (uint64_t)ikeys[k];
                            x2 ^= x2 >> 33; x2 *= 0xff51afd7ed558ccdULL;
                            x2 ^= x2 >> 33; x2 *= 0xc4ceb9fe1a85ec53ULL; x2 ^= x2 >> 33;
                            size_t j = (size_t)x2 & nm;
                            while (nu[j]) j = (j + 1) & nm;
                            nu[j] = 1; nk[j] = ikeys[k];
                        }
                        free(ikeys); free(iused); ikeys = nk; iused = nu; icap = nc;
                    }
                }
            }
            if (!dup) te_list_append(result, build_item_from_value(it));
            it = it->next;
        }
        for (size_t k = 0; k < scap; k++) if (skeys[k]) free(skeys[k]);
        free(skeys); free(ikeys); free(iused);
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "toList") == 0) {
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = list->left;
        while (it) { te_list_append(result, build_item_from_value(it)); it = it->next; }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "concat") == 0) {
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = list->left;
        while (it) { te_list_append(result, build_item_from_value(it)); it = it->next; }
        ASTNode *arg = node->right;
        ASTNode *other_list = NULL;
        if (arg) {
            if (arg->type && strcmp(arg->type, "LIST") == 0) other_list = arg;
            else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                Variable *ov = find_variable(arg->id);
                if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "LIST") == 0) {
                    other_list = (ASTNode*)(intptr_t)ov->value.object_value;
                }
            }
        }
        if (other_list) {
            ASTNode *oi = other_list->left;
            while (oi) { te_list_append(result, build_item_from_value(oi)); oi = oi->next; }
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "union") == 0) {
        /* Distinct elements present in either list (this first, then other). */
        ASTNode *result = create_list_node(NULL);
        ASTNode *other_list = te_resolve_list_arg(node->right);
        for (ASTNode *it = list->left; it; it = it->next) {
            if (!te_list_contains_item(result, it)) te_list_append(result, build_item_from_value(it));
        }
        if (other_list) {
            for (ASTNode *oi = other_list->left; oi; oi = oi->next) {
                if (!te_list_contains_item(result, oi)) te_list_append(result, build_item_from_value(oi));
            }
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "intersect") == 0) {
        /* Distinct elements present in both lists (preserving this list order). */
        ASTNode *result = create_list_node(NULL);
        ASTNode *other_list = te_resolve_list_arg(node->right);
        for (ASTNode *it = list->left; it; it = it->next) {
            int in_other = 0;
            if (other_list) {
                for (ASTNode *oi = other_list->left; oi; oi = oi->next) {
                    if (te_item_equal(it, oi)) { in_other = 1; break; }
                }
            }
            if (!in_other) continue;
            if (!te_list_contains_item(result, it)) te_list_append(result, build_item_from_value(it));
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "except") == 0) {
        /* Distinct elements of this list not present in the other list. */
        ASTNode *result = create_list_node(NULL);
        ASTNode *other_list = te_resolve_list_arg(node->right);
        for (ASTNode *it = list->left; it; it = it->next) {
            int in_other = 0;
            if (other_list) {
                for (ASTNode *oi = other_list->left; oi; oi = oi->next) {
                    if (te_item_equal(it, oi)) { in_other = 1; break; }
                }
            }
            if (in_other) continue;
            if (!te_list_contains_item(result, it)) te_list_append(result, build_item_from_value(it));
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    if (strcmp(fname, "zip") == 0) {
        ASTNode *result = create_list_node(NULL);
        ASTNode *arg = node->right;
        ASTNode *other_list = NULL;
        if (arg) {
            if (arg->type && strcmp(arg->type, "LIST") == 0) other_list = arg;
            else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                Variable *ov = find_variable(arg->id);
                if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "LIST") == 0) {
                    other_list = (ASTNode*)(intptr_t)ov->value.object_value;
                }
            }
        }
        if (!other_list) {
            add_or_update_variable("__ret__", result);
            return 1;
        }
        ASTNode *a = list->left;
        ASTNode *b = other_list->left;
        while (a && b) {
            ASTNode *lit = (ASTNode*)calloc(1, sizeof(ASTNode));
            lit->type = strdup("OBJECT_LITERAL");
            ASTNode *p1 = create_kv_pair_node("left",  build_item_from_value(a));
            ASTNode *p2 = create_kv_pair_node("right", build_item_from_value(b));
            lit->left = p1;
            p1->right = p2;
            te_list_append(result, lit);
            a = a->next;
            b = b->next;
        }
        add_or_update_variable("__ret__", result);
        return 1;
    }
    return 0;
}
