/* te_list.c - LIST instance methods (Nivel B paso 2.c).
 *
 * Extracted from src/ast.c L6253-L6373 (Ola 13 list extras + join).
 * See te_list.h for API contract.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "te_list.h"
#include "te_colcache.h"

/* Public helpers defined in ast.c but not prototyped in ast.h. */
extern ASTNode *list_get_item(ASTNode *list, int idx);
extern int list_length(ASTNode *list);
extern void te_invalidate_list_cache(ASTNode *root);

int te_list_method_dispatch(ASTNode *node, ASTNode *list) {
    if (!list || !node || !node->id) return 0;
    const char *m = node->id;

    /* ---- Ola 13: list extras: size/length, contains, reverse, sort, get ---- */
    if (strcmp(m, "size") == 0 || strcmp(m, "length") == 0) {
        int n = list_length(list);  /* uses cache */
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", n, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "contains") == 0) {
        ASTNode *arg = node->right;
        int found = 0;
        if (arg) {
            ASTNode *probe = build_item_from_value(arg);
            /* compare as string if string, else as number */
            ASTNode *cur = list->left;
            while (cur) {
                if (cur->type && probe->type && strcmp(cur->type, probe->type) == 0) {
                    if (strcmp(cur->type, "STRING") == 0) {
                        if (cur->str_value && probe->str_value && strcmp(cur->str_value, probe->str_value) == 0) { found = 1; break; }
                    } else {
                        if (cur->value == probe->value) { found = 1; break; }
                    }
                } else {
                    /* loose compare via numeric */
                    double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                    double pv = probe->str_value ? atof(probe->str_value) : (double)probe->value;
                    if (cv == pv) { found = 1; break; }
                }
                cur = cur->next;
            }
            if (probe->type) free(probe->type);
            if (probe->str_value) free(probe->str_value);
            free(probe);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", found, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "reverse") == 0) {
        ASTNode *prev = NULL, *cur = list->left, *nxt = NULL;
        while (cur) { nxt = cur->next; cur->next = prev; prev = cur; cur = nxt; }
        list->left = prev;
        te_invalidate_list_cache(list);  /* Ola 14 */
        te_colcache_invalidate(list);    /* v0.0.13 (perf) */
        return 1;
    }
    if (strcmp(m, "sort") == 0) {
        /* Simple insertion sort on the linked list (numeric or string). OK for small lists.
         * For benchmark needs, replace with merge sort later. */
        int is_str = 0;
        if (list->left && list->left->type && strcmp(list->left->type, "STRING") == 0) is_str = 1;
        ASTNode *sorted = NULL;
        ASTNode *cur = list->left;
        while (cur) {
            ASTNode *nxt = cur->next;
            cur->next = NULL;
            if (!sorted) { sorted = cur; }
            else {
                int cmp_into_head;
                if (is_str) cmp_into_head = strcmp(cur->str_value ? cur->str_value : "", sorted->str_value ? sorted->str_value : "") < 0;
                else {
                    double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                    double sv = sorted->str_value ? atof(sorted->str_value) : (double)sorted->value;
                    cmp_into_head = (cv < sv);
                }
                if (cmp_into_head) { cur->next = sorted; sorted = cur; }
                else {
                    ASTNode *p = sorted;
                    while (p->next) {
                        int cmp_next;
                        if (is_str) cmp_next = strcmp(cur->str_value ? cur->str_value : "", p->next->str_value ? p->next->str_value : "") < 0;
                        else {
                            double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                            double sv = p->next->str_value ? atof(p->next->str_value) : (double)p->next->value;
                            cmp_next = (cv < sv);
                        }
                        if (cmp_next) break;
                        p = p->next;
                    }
                    cur->next = p->next;
                    p->next = cur;
                }
            }
            cur = nxt;
        }
        list->left = sorted;
        te_invalidate_list_cache(list);  /* Ola 14 */
        te_colcache_invalidate(list);    /* v0.0.13 (perf) */
        return 1;
    }
    if (strcmp(m, "get") == 0) {
        ASTNode *arg = node->right;
        int idx = arg ? (int)evaluate_expression(arg) : 0;
        ASTNode *cur = list_get_item(list, idx);  /* Ola 14: O(1) */
        if (cur) { add_or_update_variable("__ret__", build_item_from_value(cur)); }
        else { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); }
        return 1;
    }
    if (strcmp(m, "join") == 0) {
        ASTNode *arg = node->right;
        char *sep = arg ? get_node_string(arg) : strdup("");
        size_t cap = 64; size_t len = 0;
        char *out = (char*)malloc(cap);
        out[0] = 0;
        ASTNode *cur = list->left;
        int first = 1;
        while (cur) {
            char *piece = get_node_string(cur);
            size_t pl = piece ? strlen(piece) : 0;
            size_t sl = sep ? strlen(sep) : 0;
            size_t need = len + pl + (first ? 0 : sl) + 1;
            if (need > cap) { while (cap < need) cap *= 2; out = (char*)realloc(out, cap); }
            if (!first && sep) { memcpy(out + len, sep, sl); len += sl; }
            if (piece) { memcpy(out + len, piece, pl); len += pl; free(piece); }
            out[len] = 0;
            first = 0;
            cur = cur->next;
        }
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        free(out);
        if (sep) free(sep);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    return 0;
}
