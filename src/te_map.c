/* te_map.c - MAP instance methods (Nivel B paso 2.d).
 *
 * Extracted from src/ast.c (Fase 1c + Ola 13 map extras).
 * See te_map.h for API contract.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "te_map.h"
#include "te_value.h"   /* te_eval_value / te_var_to_string (issue #15) */

/* Public helpers defined in ast.c but not prototyped in ast.h. */
extern void te_invalidate_map_cache(ASTNode *root);
extern int map_length(ASTNode *map);

/* Resuelve el argumento-clave de .has()/.remove() a un string.
 *
 * Issue #15: antes solo se manejaba un string literal o un identificador; una
 * expresion como `nombre.lower()` (METHOD_CALL) no caia en ninguna rama y la
 * clave quedaba NULL -> .has devolvia false en silencio. Ahora, para cualquier
 * otra expresion, se EVALUA normalmente con te_eval_value y se usa su texto.
 *
 * Rutas rapidas (string literal / identificador string) no asignan memoria.
 * Para la rama evaluada, *owned recibe un string malloc'd que el caller debe
 * liberar con free(*owned). *owned se deja en NULL en las rutas rapidas. */
static const char *te_map_key_arg(ASTNode *arg, char **owned) {
    *owned = NULL;
    if (!arg || !arg->type) return NULL;
    if (strcmp(arg->type, TE_T_STRING) == 0) return arg->str_value;
    if (strcmp(arg->type, TE_T_IDENTIFIER) == 0 || strcmp(arg->type, TE_T_ID) == 0) {
        Variable *kv = find_variable(arg->id);
        if (kv && kv->vtype == VAL_STRING) return kv->value.string_value;
        return NULL;
    }
    TeValue tv; te_val_init(&tv);
    const char *key = NULL;
    if (te_eval_value(arg, &tv) && !te_val_is_null(&tv)) {
        *owned = te_var_to_string(&tv);
        key = *owned;
    }
    te_val_free(&tv);
    return key;
}

int te_map_method_dispatch(ASTNode *node, ASTNode *map) {
    if (!map || !node || !node->id) return 0;
    const char *m = node->id;

    if (strcmp(m, "keys") == 0) {
        ASTNode *list = create_list_node(NULL);
        ASTNode *cur = map->left;
        while (cur) {
            ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(item, 0, sizeof(ASTNode));
            item->type = strdup(TE_T_STRING);
            item->str_value = strdup(cur->id ? cur->id : "");
            if (!list->left) list->left = item;
            else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
            cur = cur->right;
        }
        add_or_update_variable(TE_SYM_RET, list);
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
        add_or_update_variable(TE_SYM_RET, list);
        return 1;
    }
    if (strcmp(m, "has") == 0) {
        char *owned = NULL;
        const char *key = te_map_key_arg(node->right, &owned);
        int found = (key && map_find_pair(map, key)) ? 1 : 0;
        free(owned);
        ASTNode *r = (ASTNode*)calloc(1, sizeof(ASTNode));
        memset(r, 0, sizeof(ASTNode));
        r->type = strdup(TE_T_NUMBER); r->value = found;
        te_ret_scalar(r);
        return 1;
    }
    if (strcmp(m, "remove") == 0) {
        char *owned = NULL;
        const char *key = te_map_key_arg(node->right, &owned);
        if (!key) { free(owned); return 1; }
        ASTNode *prev = NULL, *cur = map->left;
        while (cur) {
            if (cur->id && strcmp(cur->id, key) == 0) {
                if (prev) prev->right = cur->right; else map->left = cur->right;
                te_invalidate_map_cache(map);  /* Ola 14 */
                free(owned);
                return 1;
            }
            prev = cur; cur = cur->right;
        }
        free(owned);
        return 1;
    }
    /* ---- Ola 13: map size/length, clear ---- */
    if (strcmp(m, "size") == 0 || strcmp(m, "length") == 0) {
        int n = map_length(map);  /* uses cache */
        te_ret_scalar( create_ast_leaf_number(TE_T_INT, n, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "clear") == 0) {
        map->left = NULL;  /* leak: deliberate; lifetime tied to AST. */
        te_invalidate_map_cache(map);  /* Ola 14 */
        return 1;
    }
    return 0;
}
