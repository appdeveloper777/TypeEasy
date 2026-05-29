/* te_linq_ops.c — LINQ-with-lambda dispatcher (Nivel B paso 2.f).
 * Extracted verbatim from ast.c L4836..L5874. See te_linq_ops.h. */
#include "te_linq_ops.h"
#include "te_csv.h"
#include "te_colcache.h"
#include "te_builtins.h"
#include "te_linq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Externs implemented in ast.c — NOT in ast.h to avoid name collision with
 * mariadb's <ma_list.h> list_length() (LIST*) that mysql_bridge.c pulls in. */
extern int list_length(ASTNode *list);
extern int is_string_type(ASTNode *node);

/* ---- orderBy / groupBy support (moved from ast.c L4440 in Nivel B paso 2.f).
 * qsort comparator: sort indices by precomputed key. Globals are safe because
 * TypeEasy LINQ runs on the main interpreter thread (the parallelism is in
 * CSV loading only). */
static double *g_sort_keys_d = NULL;
static char  **g_sort_keys_s = NULL;
static int     g_sort_is_str = 0;
static int     g_sort_desc   = 0;
static int te_sort_cmp_idx(const void *pa, const void *pb) {
    int a = *(const int*)pa, b = *(const int*)pb;
    if (g_sort_is_str) {
        const char *sa = g_sort_keys_s[a] ? g_sort_keys_s[a] : "";
        const char *sb = g_sort_keys_s[b] ? g_sort_keys_s[b] : "";
        int c = strcmp(sa, sb);
        return g_sort_desc ? -c : c;
    }
    double da = g_sort_keys_d[a], db = g_sort_keys_d[b];
    if (da == db) return 0;
    int c = (da < db) ? -1 : 1;
    return g_sort_desc ? -c : c;
}

int te_linq_ops_method_dispatch(ASTNode *node, ASTNode *list) {
        /* ---- Fase B: higher-order methods sobre LIST ----
         * .map(fn), .filter(fn), .reduce(fn, init), .forEach(fn),
         * .find(fn), .any(fn)/.every(fn). El argumento puede ser un lambda
         * inline (NK_LAMBDA / type=="LAMBDA") o una variable de tipo LAMBDA. */
        if (list && node->id && (
                strcmp(node->id, "map") == 0 ||
                strcmp(node->id, "filter") == 0 ||
                strcmp(node->id, "reduce") == 0 ||
                strcmp(node->id, "forEach") == 0 ||
                strcmp(node->id, "find") == 0 ||
                strcmp(node->id, "any") == 0 ||
                strcmp(node->id, "every") == 0 ||
                /* v0.0.11 LINQ-style operators (lambda required) */
                strcmp(node->id, "where") == 0 ||
                strcmp(node->id, "select") == 0 ||
                strcmp(node->id, "all") == 0 ||
                strcmp(node->id, "none") == 0 ||
                strcmp(node->id, "firstWhere") == 0 ||
                strcmp(node->id, "lastWhere") == 0 ||
                strcmp(node->id, "countWhere") == 0 ||
                strcmp(node->id, "sumBy") == 0 ||
                strcmp(node->id, "avgBy") == 0 ||
                strcmp(node->id, "minBy") == 0 ||
                strcmp(node->id, "maxBy") == 0 ||
                strcmp(node->id, "takeWhile") == 0 ||
                strcmp(node->id, "skipWhile") == 0 ||
                strcmp(node->id, "flatMap") == 0 ||
                strcmp(node->id, "selectMany") == 0 ||
                strcmp(node->id, "groupBy") == 0 ||
                strcmp(node->id, "orderBy") == 0 ||
                strcmp(node->id, "orderByDescending") == 0 ||
                strcmp(node->id, "distinctBy") == 0 ||
                strcmp(node->id, "aggregate") == 0 ||
                strcmp(node->id, "fold") == 0 ||
                strcmp(node->id, "toMap") == 0 ||
                strcmp(node->id, "toDictionary") == 0)) {
            ASTNode *arg = node->right;
            ASTNode *fn = NULL;
            if (arg && arg->type && strcmp(arg->type, "LAMBDA") == 0) {
                fn = arg;
            } else if (arg && arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                Variable *fv = find_variable(arg->id);
                if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0) {
                    fn = (ASTNode*)(intptr_t)fv->value.object_value;
                }
            }
            if (!fn) {
                printf("Error: %s() requires a lambda or function value.\n", node->id);
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return 1;
            }
            /* v0.0.11: normalize aliases so we can reuse existing branches */
            const char *fname = node->id;
            if (strcmp(fname, "where") == 0) fname = "filter";
            else if (strcmp(fname, "select") == 0) fname = "map";
            else if (strcmp(fname, "all") == 0) fname = "every";
            else if (strcmp(fname, "firstWhere") == 0) fname = "find";
            else if (strcmp(fname, "aggregate") == 0) fname = "reduce";
            else if (strcmp(fname, "fold") == 0) fname = "reduce";
            else if (strcmp(fname, "toDictionary") == 0) fname = "toMap";
            /* Phase 2 fast-path: pre-analyse the lambda body once. */
            FastLambda fl; fast_lambda_analyze(fn, &fl);
            if (strcmp(fname, "map") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) te_list_append(result, r);
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "filter") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                /* v0.0.13 (perf) COLUMNAR FAST PATH: list has col_cache and
                 * predicate matches a typed column. Skips per-item pointer
                 * chase entirely. Uses AVX2 + OpenMP inside helper.
                 * v0.0.14 pulimiento #5: vista LAZY — sin compactación
                 * física ni wrappers. Aggregates posteriores (sumBy, count,
                 * length) redirigen a parent+mask. Fusion implícita. */
                if (fl.spec != SPEC_NONE && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        int n = cc->n_rows;
                        char *mask = (char*)malloc((size_t)n);
                        if (te_colcache_eval_pred(cc, aidx, &fl, mask)) {
                            /* Conteo de matches (popcount) y attach lazy. */
                            int new_n = 0;
                            for (int k = 0; k < n; k++) if (mask[k]) new_n++;
                            /* PREINIT flag: skip clone+ctor en declare_variable
                             * (igual que listas CSV puras). */
                            result->value = 1;
                            te_colcache_attach_lazy(cc, mask, result, new_n);
                            /* mask ownership pasó a la vista — NO free. */
                            add_or_update_variable("__ret__", result);
                            return 1;
                        }
                        free(mask);
                    }
                }
                if (fl.spec != SPEC_NONE) {
#ifdef TE_HAVE_OPENMP
                    /* v0.0.12 #N: parallel filter on fast-path. Pattern:
                     *   1) materialize item pointers into arr[n] (sequential, cheap)
                     *   2) parallel evaluate predicate into mask[n] of chars
                     *   3) sequential build of result list from mask
                     * The build is sequential because te_list_append walks the
                     * list tail and updates ->next chains; doing it in the main
                     * thread avoids needing a thread-safe append. The cost is
                     * O(matches), which is much smaller than O(n) predicate eval. */
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            char *mask = (char*)malloc((size_t)n);
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            int fail = 0;
                            #pragma omp parallel
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) {
                                        mask[k] = 0; fail = 1;
                                    } else {
                                        mask[k] = (v != 0.0) ? 1 : 0;
                                    }
                                }
                            }
                            if (!fail) {
                                for (int k = 0; k < n; k++) {
                                    if (mask[k]) te_list_append(result, build_item_from_value(arr[k]));
                                }
                                free(arr); free(mask);
                                add_or_update_variable("__ret__", result);
                                return 1;
                            }
                            free(arr); free(mask);
                            /* fall through to sequential fallback */
                            result = create_list_node(NULL);
                            item = list->left;
                        }
                    }
#endif
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return 1;
                    }
                    /* fallback: rebuild a fresh list (cannot reuse partial). */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                while (item) {
                    ASTNode *next_item = item->next; /* v1.0.0: save before append; build_item_from_value may share OBJECT_LITERAL/MAP/LIST and te_list_append clobbers ->next */
                    ASTNode *r = call_lambda(fn, item);
                    int truthy = 0;
                    if (r) {
                        if (r->type && strcmp(r->type, "STRING") == 0) {
                            truthy = (r->str_value && r->str_value[0]) ? 1 : 0;
                        } else if (r->type && strcmp(r->type, "NULL") == 0) {
                            truthy = 0;
                        } else {
                            truthy = (evaluate_expression(r) != 0) ? 1 : 0;
                        }
                    }
                    if (truthy) {
                        ASTNode *copy = build_item_from_value(item);
                        te_list_append(result, copy);
                    }
                    item = next_item;
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "reduce") == 0) {
                /* arg list: (fn, init). arg->next es init. */
                ASTNode *initArg = arg->next; /* gotcha #1: 2nd arg via ->next */
                ASTNode *acc = NULL;
                if (initArg) {
                    if (is_string_type(initArg)) {
                        char *s = get_node_string(initArg);
                        acc = create_ast_leaf("STRING", 0, s, NULL); free(s);
                    } else {
                        double r = evaluate_expression(initArg);
                        if (r == (double)(long long)r) acc = create_ast_leaf_number("NUMBER", (int)r, NULL, NULL);
                        else { char b[64]; snprintf(b,sizeof(b),"%g",r); acc = create_ast_leaf("FLOAT", 0, b, NULL); }
                    }
                } else {
                    acc = create_ast_leaf_number("NUMBER", 0, NULL, NULL);
                }
                ASTNode *item = list->left;
                while (item) {
                    /* Construir args: acc, item — los enlazamos por ->right. */
                    ASTNode *acc_copy = acc;
                    acc_copy->right = item;
                    ASTNode saved_item_right;
                    saved_item_right = *item; /* unused but keeps pattern */
                    (void)saved_item_right;
                    ASTNode *prev_item_right = item->right;
                    /* call_lambda lee acc->right como segundo arg */
                    ASTNode *r = call_lambda(fn, acc);
                    /* restaurar */
                    item->right = prev_item_right;
                    acc->right = NULL;
                    if (r) acc = r;
                    item = item->next;
                }
                add_or_update_variable("__ret__", acc);
                return 1;
            }
            if (strcmp(fname, "forEach") == 0) {
                ASTNode *item = list->left;
                while (item) {
                    call_lambda(fn, item);
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "find") == 0) {
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) {
                            add_or_update_variable("__ret__", build_item_from_value(item));
                            return 1;
                        }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return 1;
                    }
                    item = list->left;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) {
                        add_or_update_variable("__ret__", build_item_from_value(item));
                        return 1;
                    }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "any") == 0) {
                ASTNode *item = list->left;
                int found = 0;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) { found = 1; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", found, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "every") == 0) {
                ASTNode *item = list->left;
                int all = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v == 0.0) { all = 0; break; }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", all, NULL, NULL));
                        return 1;
                    }
                    item = list->left; all = 1;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (!r || evaluate_expression(r) == 0) { all = 0; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", all, NULL, NULL));
                return 1;
            }
            /* ===== v0.0.11 LINQ-style higher-order operators ===== */
            if (strcmp(fname, "none") == 0) {
                ASTNode *item = list->left;
                int none_match = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) { none_match = 0; break; }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", none_match, NULL, NULL));
                        return 1;
                    }
                    item = list->left; none_match = 1;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) { none_match = 0; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", none_match, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "lastWhere") == 0) {
                ASTNode *item = list->left;
                ASTNode *last_match = NULL;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) last_match = item;
                        item = item->next;
                    }
                    if (ok) {
                        if (last_match) add_or_update_variable("__ret__", build_item_from_value(last_match));
                        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return 1;
                    }
                    item = list->left; last_match = NULL;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) last_match = item;
                    item = item->next;
                }
                if (last_match) add_or_update_variable("__ret__", build_item_from_value(last_match));
                else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "countWhere") == 0) {
                ASTNode *item = list->left;
                int cnt = 0;
                /* v0.0.13 (perf) COLUMNAR FAST PATH for countWhere. */
                if (fl.spec != SPEC_NONE && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        int n = cc->n_rows;
                        char *mask = (char*)malloc((size_t)n);
                        if (te_colcache_eval_pred(cc, aidx, &fl, mask)) {
                            long long total = te_colcache_count(cc, mask);
                            free(mask);
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)total, NULL, NULL));
                            return 1;
                        }
                        free(mask);
                    }
                }
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
#ifdef TE_HAVE_OPENMP
                    /* Parallel path: count list, materialize pointers, parallel reduce. */
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            long long total = 0; int fail = 0;
                            #pragma omp parallel reduction(+:total)
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) { fail = 1; }
                                    else if (v != 0.0) total++;
                                }
                            }
                            free(arr);
                            if (!fail) {
                                add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)total, NULL, NULL));
                                return 1;
                            }
                            /* fall through to sequential fallback */
                            ok = 0;
                        }
                    }
                    if (ok)
#endif
                    {
                        while (item) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (v != 0.0) cnt++;
                            item = item->next;
                        }
                        if (ok) {
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", cnt, NULL, NULL));
                            return 1;
                        }
                    }
                    item = list->left; cnt = 0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) cnt++;
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", cnt, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "sumBy") == 0) {
                ASTNode *item = list->left;
                /* v0.0.13 (perf) COLUMNAR FAST PATH: sumBy(fn(p)=>p.attr) over
                 * an int/float column. Uses parallel reduction inside helper. */
                if (fl.spec == SPEC_ATTR && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        long long itotal = 0; double dtotal = 0.0; int is_int = 1;
                        if (te_colcache_sum(cc, aidx, NULL, &itotal, &dtotal, &is_int)) {
                            if (is_int) {
                                if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return 1;
                        }
                    }
                }
                /* Fast-path: numeric pattern over OBJECT list — use int64 acc to
                 * avoid the 32-bit overflow that hits at ~10M-row benches. */
                if (fl.spec != SPEC_NONE) {
                    long long iacc = 0; double dacc = 0.0;
                    int is_int = 1, ok = 1;
#ifdef TE_HAVE_OPENMP
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            long long itotal = 0; double dtotal = 0.0;
                            int fail = 0, any_float = 0;
                            #pragma omp parallel reduction(+:itotal,dtotal)
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) { fail = 1; continue; }
                                    if (vint) itotal += (long long)v;
                                    else { dtotal += v; any_float = 1; }
                                }
                            }
                            free(arr);
                            if (!fail) {
                                if (!any_float) {
                                    if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                    } else {
                                        char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                } else {
                                    char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal + (double)itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                                return 1;
                            }
                            ok = 0;
                        }
                    }
                    if (ok)
#endif
                    {
                        while (item) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (!vint) is_int = 0;
                            if (is_int) iacc += (long long)v; else dacc += v;
                            item = item->next;
                        }
                        if (ok) {
                            if (is_int) {
                                /* Build a NUMBER node carrying the int64 via str_value
                                 * when it overflows int32, otherwise the usual int slot. */
                                if (iacc >= INT_MIN && iacc <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)iacc, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", iacc);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; snprintf(buf, sizeof(buf), "%g", dacc + (double)iacc);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return 1;
                        }
                    }
                    item = list->left; /* reset for fallback */
                }
                double acc = 0.0;
                int is_int = 1;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) {
                        double v = evaluate_expression(r);
                        if (v != (double)(long long)v) is_int = 0;
                        acc += v;
                    }
                    item = item->next;
                }
                if (is_int && acc == (double)(long long)acc) {
                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)acc, NULL, NULL));
                } else {
                    char buf[64]; snprintf(buf, sizeof(buf), "%g", acc);
                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                }
                return 1;
            }
            if (strcmp(fname, "avgBy") == 0) {
                ASTNode *item = list->left;
                double acc = 0.0; int cnt = 0;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        acc += v; cnt++;
                        item = item->next;
                    }
                    if (ok) {
                        double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
                        char buf[64]; snprintf(buf, sizeof(buf), "%g", res);
                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                        return 1;
                    }
                    item = list->left; acc = 0.0; cnt = 0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) { acc += evaluate_expression(r); cnt++; }
                    item = item->next;
                }
                double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
                char buf[64]; snprintf(buf, sizeof(buf), "%g", res);
                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                return 1;
            }
            if (strcmp(fname, "minBy") == 0 || strcmp(fname, "maxBy") == 0) {
                int want_max = (strcmp(fname, "maxBy") == 0);
                ASTNode *item = list->left;
                ASTNode *best_item = NULL;
                double best_key = 0.0;
                int first = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (first) { best_key = v; best_item = item; first = 0; }
                        else if (want_max ? (v > best_key) : (v < best_key)) { best_key = v; best_item = item; }
                        item = item->next;
                    }
                    if (ok) {
                        if (best_item) add_or_update_variable("__ret__", build_item_from_value(best_item));
                        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return 1;
                    }
                    item = list->left; best_item = NULL; first = 1; best_key = 0.0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    double k = r ? evaluate_expression(r) : 0.0;
                    if (first) { best_key = k; best_item = item; first = 0; }
                    else if (want_max ? (k > best_key) : (k < best_key)) { best_key = k; best_item = item; }
                    item = item->next;
                }
                if (best_item) add_or_update_variable("__ret__", build_item_from_value(best_item));
                else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return 1;
            }
            if (strcmp(fname, "takeWhile") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v == 0.0) break;
                        te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return 1;
                    }
                    result = create_list_node(NULL);
                    item = list->left;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (!r || evaluate_expression(r) == 0) break;
                    te_list_append(result, build_item_from_value(item));
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "skipWhile") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                int skipping = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        if (skipping) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (v == 0.0) skipping = 0;
                        }
                        if (!skipping) te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return 1;
                    }
                    result = create_list_node(NULL);
                    item = list->left;
                    skipping = 1;
                }
                while (item) {
                    if (skipping) {
                        ASTNode *r = call_lambda(fn, item);
                        if (!r || evaluate_expression(r) == 0) skipping = 0;
                    }
                    if (!skipping) te_list_append(result, build_item_from_value(item));
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "flatMap") == 0 || strcmp(fname, "selectMany") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && r->type && strcmp(r->type, "LIST") == 0) {
                        ASTNode *inner = r->left;
                        while (inner) {
                            te_list_append(result, build_item_from_value(inner));
                            inner = inner->next;
                        }
                    } else if (r) {
                        te_list_append(result, r);
                    }
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "groupBy") == 0) {
                /* Returns LIST of OBJECT_LITERAL {key, items: LIST}.
                 * Fast-path (numeric key, int o float): linear probe sobre
                 * `double` keys con `==` (bit-exact para int hasta 2^53 y
                 * para floats producidos por la misma expresión).
                 * v0.0.14: la rama int original (`long long`) truncaba 1.5
                 * y 1.7 al mismo bucket. Ahora se usa `double` siempre. */
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE && (fl.spec == SPEC_ATTR || fl.spec == SPEC_MOD_K || fl.spec == SPEC_IDENT)) {
                    int cap = 16, nkeys = 0;
                    double   *bk = (double*)malloc(cap * sizeof(double));
                    ASTNode **bg = (ASTNode**)malloc(cap * sizeof(ASTNode*));
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        int idx_g = -1;
                        for (int g = 0; g < nkeys; g++) if (bk[g] == v) { idx_g = g; break; }
                        ASTNode *group_items;
                        if (idx_g < 0) {
                            if (nkeys == cap) {
                                cap *= 2;
                                bk = (double*)realloc(bk, cap * sizeof(double));
                                bg = (ASTNode**)realloc(bg, cap * sizeof(ASTNode*));
                            }
                            bk[nkeys] = v;
                            ASTNode *grp = (ASTNode*)calloc(1, sizeof(ASTNode));
                            grp->type = strdup("OBJECT_LITERAL");
                            /* Formato de key: enteros como "%lld" para
                             * compatibilidad con el comportamiento previo
                             * (groupBy int → key "3"); floats como "%g". */
                            char kbuf[32];
                            if (v == (double)(long long)v && v > -9.0e15 && v < 9.0e15) {
                                snprintf(kbuf, sizeof(kbuf), "%lld", (long long)v);
                            } else {
                                snprintf(kbuf, sizeof(kbuf), "%g", v);
                            }
                            ASTNode *kv_key = create_kv_pair_node("key", create_ast_leaf("STRING", 0, kbuf, NULL));
                            group_items = create_list_node(NULL);
                            ASTNode *kv_items = create_kv_pair_node("items", group_items);
                            grp->left = kv_key;
                            kv_key->next = kv_items;
                            te_list_append(result, grp);
                            bg[nkeys] = group_items;
                            nkeys++;
                        } else {
                            group_items = bg[idx_g];
                        }
                        {
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(group_items, w ? w : build_item_from_value(item));
                        }
                        item = item->next;
                    }
                    free(bk); free(bg);
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return 1;
                    }
                    /* fallback: rebuild from scratch */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                /* Slow path (string keys via lambda eval): replace O(n^2) linear
                 * scan over groups with open-addressing hash table. Critical when
                 * grouping 100K+ rows by a string attribute. */
                {
                    size_t hcap = 64;
                    size_t hcount = 0;
                    char    **hk = (char**)calloc(hcap, sizeof(char*));
                    ASTNode **hv = (ASTNode**)calloc(hcap, sizeof(ASTNode*));
                    while (item) {
                        ASTNode *kr = call_lambda(fn, item);
                        char *key_str = kr ? get_node_string(kr) : strdup("");
                        /* FNV-1a 32-bit hash */
                        uint32_t h = 2166136261u;
                        for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                            h ^= *p; h *= 16777619u;
                        }
                        size_t mask = hcap - 1;
                        size_t i_h = (size_t)h & mask;
                        while (hk[i_h] && strcmp(hk[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                        ASTNode *group_items;
                        if (!hk[i_h]) {
                            hk[i_h] = key_str;   /* hash table owns this strdup */
                            ASTNode *grp = (ASTNode*)calloc(1, sizeof(ASTNode));
                            grp->type = strdup("OBJECT_LITERAL");
                            ASTNode *kv_key = create_kv_pair_node("key",
                                create_ast_leaf("STRING", 0, key_str, NULL));
                            group_items = create_list_node(NULL);
                            ASTNode *kv_items = create_kv_pair_node("items", group_items);
                            grp->left = kv_key;
                            kv_key->next = kv_items;
                            te_list_append(result, grp);
                            hv[i_h] = group_items;
                            hcount++;
                            /* Grow at 50% load */
                            if (hcount * 2 >= hcap) {
                                size_t new_cap = hcap * 2;
                                char    **nhk = (char**)calloc(new_cap, sizeof(char*));
                                ASTNode **nhv = (ASTNode**)calloc(new_cap, sizeof(ASTNode*));
                                size_t nmask = new_cap - 1;
                                for (size_t k = 0; k < hcap; k++) {
                                    if (!hk[k]) continue;
                                    uint32_t h2 = 2166136261u;
                                    for (const unsigned char *p = (const unsigned char*)hk[k]; *p; p++) {
                                        h2 ^= *p; h2 *= 16777619u;
                                    }
                                    size_t j = (size_t)h2 & nmask;
                                    while (nhk[j]) j = (j + 1) & nmask;
                                    nhk[j] = hk[k]; nhv[j] = hv[k];
                                }
                                free(hk); free(hv);
                                hk = nhk; hv = nhv; hcap = new_cap;
                            }
                        } else {
                            group_items = hv[i_h];
                            free(key_str);  /* duplicate of existing key — drop */
                        }
                        {
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(group_items, w ? w : build_item_from_value(item));
                        }
                        item = item->next;
                    }
                    /* Free hash table: hk[] strings are owned by us (not aliased
                     * into result tree — STRING leaves were created via intern). */
                    for (size_t k = 0; k < hcap; k++) if (hk[k]) free(hk[k]);
                    free(hk); free(hv);
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "orderBy") == 0 || strcmp(fname, "orderByDescending") == 0) {
                int descending = (strcmp(fname, "orderByDescending") == 0);
                /* Materialize items + their keys into parallel arrays, sort, rebuild list.
                 * Phase 2: replaced O(n^2) insertion sort with O(n log n) qsort and
                 * added fast-path key extraction (no call_lambda when lambda is a
                 * trivial accessor / arithmetic). Sort indices, not the heavy
                 * ASTNode* themselves, to keep swaps cheap. */
                int n = list_length(list);
                if (n <= 0) { add_or_update_variable("__ret__", create_list_node(NULL)); return 1; }
                ASTNode **items_arr = (ASTNode**)calloc(n, sizeof(ASTNode*));
                double *keys = (double*)calloc(n, sizeof(double));
                char **skeys = (char**)calloc(n, sizeof(char*));
                int is_str_key = 0;
                int i = 0;
                ASTNode *it = list->left;
                int fast_ok = 0;
                if (fl.spec != SPEC_NONE) {
                    fast_ok = 1;
                    while (it && i < n) {
                        double v; int vint;
                        if (!fast_eval(&fl, it, &v, &vint)) { fast_ok = 0; break; }
                        keys[i] = v;
                        items_arr[i] = it;
                        i++; it = it->next;
                    }
                    if (!fast_ok) {
                        /* Reset and continue via slow path below. */
                        i = 0; it = list->left;
                        memset(keys, 0, n * sizeof(double));
                        memset(items_arr, 0, n * sizeof(ASTNode*));
                    }
                }
                if (!fast_ok) {
                    while (it && i < n) {
                        ASTNode *k = call_lambda(fn, it);
                        if (k && k->type && strcmp(k->type, "STRING") == 0) {
                            is_str_key = 1;
                            skeys[i] = strdup(k->str_value ? k->str_value : "");
                        } else if (k) {
                            keys[i] = evaluate_expression(k);
                        }
                        items_arr[i] = it;
                        i++; it = it->next;
                    }
                }
                int *idx = (int*)malloc(n * sizeof(int));
                for (int a = 0; a < n; a++) idx[a] = a;
                g_sort_keys_d = keys;
                g_sort_keys_s = skeys;
                g_sort_is_str = is_str_key;
                g_sort_desc   = descending;
                qsort(idx, (size_t)n, sizeof(int), te_sort_cmp_idx);
                ASTNode *result = create_list_node(NULL);
                /* Pool fast-path: if all items are plain OBJECTs (typical for LINQ
                 * over CSV), allocate wrappers from the bump pool — avoids n×calloc
                 * and n×strdup. Otherwise fall back to build_item_from_value. */
                for (int a = 0; a < n; a++) {
                    ASTNode *src = items_arr[idx[a]];
                    ASTNode *w = build_object_wrapper_pooled(src);
                    te_list_append(result, w ? w : build_item_from_value(src));
                }
                for (int a = 0; a < n; a++) if (skeys[a]) free(skeys[a]);
                free(items_arr); free(keys); free(skeys); free(idx);
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "distinctBy") == 0) {
                /* Like distinct but keys come from a lambda. int64 fast-path via
                 * fast_eval (open-addressing hash); else string-keyed hash. */
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE && (fl.spec == SPEC_ATTR || fl.spec == SPEC_MOD_K || fl.spec == SPEC_IDENT)) {
                    size_t icap = 64, icount = 0;
                    long long *ikeys = (long long*)calloc(icap, sizeof(long long));
                    unsigned char *iused = (unsigned char*)calloc(icap, 1);
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        long long key = (long long)v;
                        uint64_t x = (uint64_t)key;
                        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
                        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
                        size_t mask = icap - 1;
                        size_t i_h = (size_t)x & mask;
                        while (iused[i_h] && ikeys[i_h] != key) i_h = (i_h + 1) & mask;
                        if (!iused[i_h]) {
                            iused[i_h] = 1; ikeys[i_h] = key; icount++;
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(result, w ? w : build_item_from_value(item));
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
                        item = item->next;
                    }
                    free(ikeys); free(iused);
                    if (ok) { add_or_update_variable("__ret__", result); return 1; }
                    /* fallback */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                {
                    size_t scap = 64, scount = 0;
                    char **skeys = (char**)calloc(scap, sizeof(char*));
                    while (item) {
                        ASTNode *kr = call_lambda(fn, item);
                        char *key_str = kr ? get_node_string(kr) : strdup("");
                        uint32_t h = 2166136261u;
                        for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                            h ^= *p; h *= 16777619u;
                        }
                        size_t mask = scap - 1;
                        size_t i_h = (size_t)h & mask;
                        while (skeys[i_h] && strcmp(skeys[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                        if (skeys[i_h]) {
                            free(key_str);
                        } else {
                            skeys[i_h] = key_str;
                            scount++;
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(result, w ? w : build_item_from_value(item));
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
                        item = item->next;
                    }
                    for (size_t k = 0; k < scap; k++) if (skeys[k]) free(skeys[k]);
                    free(skeys);
                }
                add_or_update_variable("__ret__", result);
                return 1;
            }
            if (strcmp(fname, "toMap") == 0) {
                /* toMap(keyFn): builds OBJECT_LITERAL {key1: item1, key2: item2, ...}.
                 * Last write wins on duplicate keys. Hash table dedupes keys in O(1). */
                ASTNode *result = (ASTNode*)calloc(1, sizeof(ASTNode));
                result->type = strdup("OBJECT_LITERAL");
                ASTNode *tail = NULL;
                size_t hcap = 64, hcount = 0;
                char    **hk = (char**)calloc(hcap, sizeof(char*));
                ASTNode **hv = (ASTNode**)calloc(hcap, sizeof(ASTNode*));
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *kr = call_lambda(fn, item);
                    char *key_str = kr ? get_node_string(kr) : strdup("");
                    uint32_t h = 2166136261u;
                    for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                        h ^= *p; h *= 16777619u;
                    }
                    size_t mask = hcap - 1;
                    size_t i_h = (size_t)h & mask;
                    while (hk[i_h] && strcmp(hk[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                    ASTNode *val = build_object_wrapper_pooled(item);
                    if (!val) val = build_item_from_value(item);
                    if (hk[i_h]) {
                        ASTNode *pair = hv[i_h];
                        if (pair->left) free_ast(pair->left);
                        pair->left = val;
                        free(key_str);
                    } else {
                        hk[i_h] = key_str;
                        ASTNode *pair = create_kv_pair_node(key_str, val);
                        /* MAP pairs are linked via ->right (see resolve_to_map /
                         * te_map_get_hash which iterate cur = cur->right). */
                        if (!result->left) result->left = pair;
                        else tail->right = pair;
                        tail = pair;
                        hv[i_h] = pair;
                        hcount++;
                        if (hcount * 2 >= hcap) {
                            size_t nc = hcap * 2;
                            char    **nhk = (char**)calloc(nc, sizeof(char*));
                            ASTNode **nhv = (ASTNode**)calloc(nc, sizeof(ASTNode*));
                            size_t nm = nc - 1;
                            for (size_t k = 0; k < hcap; k++) if (hk[k]) {
                                uint32_t h2 = 2166136261u;
                                for (const unsigned char *p = (const unsigned char*)hk[k]; *p; p++) {
                                    h2 ^= *p; h2 *= 16777619u;
                                }
                                size_t j = (size_t)h2 & nm;
                                while (nhk[j]) j = (j + 1) & nm;
                                nhk[j] = hk[k]; nhv[j] = hv[k];
                            }
                            free(hk); free(hv); hk = nhk; hv = nhv; hcap = nc;
                        }
                    }
                    item = item->next;
                }
                for (size_t k = 0; k < hcap; k++) if (hk[k]) free(hk[k]);
                free(hk); free(hv);
                add_or_update_variable("__ret__", result);
                return 1;
            }
        }

    return 0;
}
