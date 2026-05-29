/* ============================================================================
 * te_string.c — string method dispatch (Nivel B paso 2.b).
 *
 * Methods (instance, on STRING receivers):
 *   upper, lower, trim, contains, split, length,
 *   replace, substr, find, starts_with, ends_with, repeat,
 *   parse_int, parse_float, char_at, char_code.
 *
 * All write the result to __ret__ via add_or_update_variable().
 * ============================================================================ */

#include "te_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int te_string_method_dispatch(ASTNode *node, ASTNode *objNode, Variable *v) {
    if (!node || !node->id) return 0;

    const char *raw_str_lit = NULL;
    if (objNode && objNode->type && strcmp(objNode->type, "STRING") == 0) {
        raw_str_lit = objNode->str_value ? objNode->str_value : "";
    }
    if (!raw_str_lit && !(v && v->vtype == VAL_STRING)) return 0;

    const char *m = node->id;
    const char *s = raw_str_lit ? raw_str_lit
                                : (v->value.string_value ? v->value.string_value : "");

    if (strcmp(m, "upper") == 0) {
        char *out = strdup(s);
        for (char *p = out; *p; p++) *p = toupper((unsigned char)*p);
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "lower") == 0) {
        char *out = strdup(s);
        for (char *p = out; *p; p++) *p = tolower((unsigned char)*p);
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "trim") == 0) {
        const char *start = s;
        while (*start && isspace((unsigned char)*start)) start++;
        const char *end = s + strlen(s);
        while (end > start && isspace((unsigned char)*(end - 1))) end--;
        char *out = strndup(start, end - start);
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "contains") == 0) {
        const char *needle = NULL;
        ASTNode *arg = node->right;
        char *tmp = NULL;
        if (arg && arg->type && strcmp(arg->type, "STRING") == 0) needle = arg->str_value;
        else if (arg) { tmp = get_node_string(arg); needle = tmp; }
        int found = (needle && strstr(s, needle)) ? 1 : 0;
        if (tmp) free(tmp);
        ASTNode *r = create_ast_leaf_number("INT", found, NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "split") == 0) {
        const char *sep = NULL;
        ASTNode *arg = node->right;
        char *tmp = NULL;
        if (arg && arg->type && strcmp(arg->type, "STRING") == 0) sep = arg->str_value;
        else if (arg) { tmp = get_node_string(arg); sep = tmp; }
        if (!sep || !*sep) sep = " ";
        ASTNode *list = create_list_node(NULL);
        const char *cur = s;
        size_t seplen = strlen(sep);
        while (1) {
            const char *next = strstr(cur, sep);
            size_t partlen = next ? (size_t)(next - cur) : strlen(cur);
            char *part = strndup(cur, partlen);
            ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(item, 0, sizeof(ASTNode));
            item->type = strdup("STRING");
            item->str_value = part;
            if (!list->left) list->left = item;
            else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
            if (!next) break;
            cur = next + seplen;
        }
        if (tmp) free(tmp);
        add_or_update_variable("__ret__", list);
        return 1;
    }
    if (strcmp(m, "length") == 0) {
        ASTNode *r = create_ast_leaf_number("INT", (int)strlen(s), NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    /* ---- Ola 13: replace, substr, find, starts_with, ends_with, repeat,
     *      parse_int, parse_float, char_at, char_code ---- */
    if (strcmp(m, "replace") == 0) {
        ASTNode *arg = node->right;
        ASTNode *arg2 = arg ? arg->next : NULL; /* gotcha #1: 2nd arg via ->next */
        char *t1 = arg  ? get_node_string(arg)  : NULL;
        char *t2 = arg2 ? get_node_string(arg2) : NULL;
        const char *needle = t1 ? t1 : "";
        const char *repl   = t2 ? t2 : "";
        size_t nl = strlen(needle), rl = strlen(repl);
        if (nl == 0) {
            ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
            add_or_update_variable("__ret__", r);
        } else {
            int count = 0; const char *p = s;
            while ((p = strstr(p, needle)) != NULL) { count++; p += nl; }
            size_t out_len = strlen(s) + count * (rl > nl ? rl - nl : 0);
            char *out = (char*)malloc(out_len + 1);
            char *o = out; const char *cur = s;
            while (1) {
                const char *next = strstr(cur, needle);
                if (!next) { strcpy(o, cur); break; }
                size_t pre = (size_t)(next - cur);
                memcpy(o, cur, pre); o += pre;
                memcpy(o, repl, rl); o += rl;
                cur = next + nl;
            }
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            free(out);
            add_or_update_variable("__ret__", r);
        }
        if (t1) free(t1);
        if (t2) free(t2);
        return 1;
    }
    if (strcmp(m, "substr") == 0) {
        ASTNode *arg = node->right;
        ASTNode *arg2 = arg ? arg->next : NULL; /* gotcha #1: 2nd arg via ->next */
        int start = arg  ? (int)evaluate_expression(arg)  : 0;
        int slen = (int)strlen(s);
        int len  = arg2 ? (int)evaluate_expression(arg2) : slen - start;
        if (start < 0) start = 0;
        if (start > slen) start = slen;
        if (len < 0) len = 0;
        if (start + len > slen) len = slen - start;
        char *out = strndup(s + start, len);
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        free(out);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "find") == 0) {
        ASTNode *arg = node->right;
        char *t = arg ? get_node_string(arg) : NULL;
        int idx = -1;
        if (t) {
            const char *p = strstr(s, t);
            if (p) idx = (int)(p - s);
            free(t);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", idx, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "starts_with") == 0) {
        ASTNode *arg = node->right;
        char *t = arg ? get_node_string(arg) : NULL;
        int ok = 0;
        if (t) { ok = (strncmp(s, t, strlen(t)) == 0) ? 1 : 0; free(t); }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "ends_with") == 0) {
        ASTNode *arg = node->right;
        char *t = arg ? get_node_string(arg) : NULL;
        int ok = 0;
        if (t) {
            size_t sl = strlen(s), tl = strlen(t);
            ok = (tl <= sl && memcmp(s + sl - tl, t, tl) == 0) ? 1 : 0;
            free(t);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
        return 1;
    }
    if (strcmp(m, "repeat") == 0) {
        ASTNode *arg = node->right;
        int n = arg ? (int)evaluate_expression(arg) : 0;
        if (n < 0) n = 0;
        size_t sl = strlen(s);
        char *out = (char*)malloc(sl * (size_t)n + 1);
        for (int i = 0; i < n; i++) memcpy(out + i * sl, s, sl);
        out[sl * n] = 0;
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        free(out);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "parse_int") == 0) {
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", atoi(s), NULL, NULL));
        return 1;
    }
    if (strcmp(m, "parse_float") == 0) {
        char buf[64]; snprintf(buf, 64, "%f", atof(s));
        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        return 1;
    }
    if (strcmp(m, "char_at") == 0) {
        ASTNode *arg = node->right;
        int idx = arg ? (int)evaluate_expression(arg) : 0;
        int sl = (int)strlen(s);
        char buf[2] = {0, 0};
        if (idx >= 0 && idx < sl) buf[0] = s[idx];
        ASTNode *r = create_ast_leaf("STRING", 0, buf, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(m, "char_code") == 0) {
        ASTNode *arg = node->right;
        int idx = arg ? (int)evaluate_expression(arg) : 0;
        int sl = (int)strlen(s);
        int c = (idx >= 0 && idx < sl) ? (unsigned char)s[idx] : 0;
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", c, NULL, NULL));
        return 1;
    }
    return 0;
}
