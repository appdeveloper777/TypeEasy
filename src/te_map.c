/* te_map.c - MAP instance methods (Nivel B paso 2.d).
 *
 * Extracted from src/ast.c (Fase 1c + Ola 13 map extras).
 * See te_map.h for API contract.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "te_map.h"

/* Public helpers defined in ast.c but not prototyped in ast.h. */
extern void te_invalidate_map_cache(ASTNode *root);
extern int map_length(ASTNode *map);

int te_map_method_dispatch(ASTNode *node, ASTNode *map) {
    if (!map || !node || !node->id) return 0;
    const char *m = node->id;

    if (strcmp(m, "keys") == 0) {
        ASTNode *list = create_list_node(NULL);
        ASTNode *cur = map->left;
        while (cur) {
            ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(item, 0, sizeof(ASTNode));
            item->type = strdup("STRING");
            item->str_value = strdup(cur->id ? cur->id : "");
            if (!list->left) list->left = item;
            else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
            cur = cur->right;
        }
        add_or_update_variable("__ret__", list);
        return 1;
    }
    if (strcmp(m, "values") == 0) {
        ASTNode *list = create_list_node(NULL);
        ASTNode *cur = map->left;
        while (cur) {
            ASTNode *src = cur->left;
            ASTNode *item = build_item_from_value(src);
            if (!list->left) list->left = item;
            else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
            cur = cur->right;
        }
        add_or_update_variable("__ret__", list);
        return 1;
    }
    if (strcmp(m, "has") == 0) {
        const char *key = NULL;
        ASTNode *arg = node->right;
        if (arg && arg->type && strcmp(arg->type, "STRING") == 0) key = arg->str_value;
        else if (arg && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
            Variable *kv = find_variable(arg->id);
            if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
        }
        int found = (key && map_find_pair(map, key)) ? 1 : 0;
        ASTNode *r = (ASTNode*)calloc(1, sizeof(ASTNode));
        memset(r, 0, sizeof(ASTNode));
        r->type = strdup("NUMBER"); r->value = found;
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "remove") == 0) {
        const char *key = NULL;
        ASTNode *arg = node->right;
        if (arg && arg->type && strcmp(arg->type, "STRING") == 0) key = arg->str_value;
        else if (arg && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
            Variable *kv = find_variable(arg->id);
            if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
        }
        if (!key) return 1;
        ASTNode *prev = NULL, *cur = map->left;
        while (cur) {
            if (cur->id && strcmp(cur->id, key) == 0) {
                if (prev) prev->right = cur->right; else map->left = cur->right;
                te_invalidate_map_cache(map);  /* Ola 14 */
                return 1;
            }
            prev = cur; cur = cur->right;
        }
        return 1;
    }
    /* ---- Ola 13: map size/length, clear ---- */
    if (strcmp(m, "size") == 0 || strcmp(m, "length") == 0) {
        int n = map_length(map);  /* uses cache */
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", n, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "clear") == 0) {
        map->left = NULL;  /* leak: deliberate; lifetime tied to AST. */
        te_invalidate_map_cache(map);  /* Ola 14 */
        return 1;
    }
    return 0;
}
