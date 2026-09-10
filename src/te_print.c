/* te_print.c — Salida a consola: print / println y el render de listas y maps (extraído de ast.c, Fase 2).
 * Movimiento puro (sin cambios semánticos). Dependencias compartidas: ast_internal.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ast.h"
#include "te_vm.h"
#include "ast_internal.h"

/* Render a LIST node. Object lists keep the legacy multi-line Mostrar
 * rendering; scalar lists (INT/NUMBER/STRING/FLOAT) render inline as
 * [a, b, c]. Mirrored to the embedded-API stdout buffer. When `nl` is set a
 * trailing newline is emitted. */
void te_print_list_node(ASTNode *listNode, int nl) {
    ASTNode *cur = listNode ? listNode->left : NULL;
    if (cur && cur->type && strcmp(cur->type, TE_T_OBJECT) == 0) {
        dbg_printf("[\n");
        while (cur) {
            if (cur->type && strcmp(cur->type, TE_T_OBJECT) == 0) {
                ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                call_method(obj, "Mostrar");
            }
            cur = cur->next;
        }
        dbg_printf("]");
        if (nl) dbg_printf("\n");
        return;
    }
    dbg_printf("["); append_to_stdout("[");
    int first = 1;
    while (cur) {
        if (!first) { dbg_printf(", "); append_to_stdout(", "); }
        first = 0;
        char buf[64];
        if (cur->type && strcmp(cur->type, TE_T_STRING) == 0) {
            const char *s = cur->str_value ? cur->str_value : "";
            dbg_printf("%s", s); append_to_stdout(s);
        } else if (cur->type && strcmp(cur->type, TE_T_FLOAT) == 0) {
            te_fmt_double(buf, sizeof(buf), cur->str_value ? atof(cur->str_value) : 0.0);
            dbg_printf("%s", buf); append_to_stdout(buf);
        } else {
            snprintf(buf, sizeof(buf), "%lld", (long long)cur->value);
            dbg_printf("%s", buf); append_to_stdout(buf);
        }
        cur = cur->next;
    }
    dbg_printf("]"); append_to_stdout("]");
    if (nl) { dbg_printf("\n"); append_to_stdout("\n"); }
}
/* Resolve `var.attr` where var holds a MAP / OBJECT_LITERAL (e.g. a `{..}`
 * literal or a lambda param bound to a map list item). Returns a newly
 * malloc'd display string (caller frees), or NULL if var is not a map.
 * Used by interpret_print/println to avoid casting the map's ASTNode* to
 * ObjectNode* (which crashes when reading obj->class). */
char *te_map_field_display(Variable *v, const char *key) {
    if (!v || !v->type) return NULL;
    if (strcmp(v->type, TE_T_MAP) != 0 && strcmp(v->type, TE_T_OBJECT_LITERAL) != 0) return NULL;
    ASTNode *map  = (ASTNode*)(intptr_t)v->value.object_value;
    ASTNode *pair = (map && key) ? map_find_pair(map, key) : NULL;
    ASTNode *val  = pair ? pair->left : NULL;
    if (!val || !val->type) return strdup("null");
    if (strcmp(val->type, TE_T_BOOL) == 0) return strdup(val->value ? "true" : "false");
    if (strcmp(val->type, TE_T_NULL) == 0) return strdup("null");
    if (strcmp(val->type, TE_T_STRING) == 0 || strcmp(val->type, TE_T_DATETIME) == 0 ||
        strcmp(val->type, TE_T_UUID) == 0)
        return strdup(val->str_value ? val->str_value : "");
    if (strcmp(val->type, TE_T_NUMBER) == 0 || strcmp(val->type, TE_T_INT) == 0) {
        char b[32]; snprintf(b, sizeof(b), "%lld", (long long)val->value); return strdup(b);
    }
    if (strcmp(val->type, TE_T_FLOAT) == 0) {
        char b[64]; te_fmt_double(b, sizeof(b), val->str_value ? atof(val->str_value) : 0.0);
        return strdup(b);
    }
    { char b[64]; te_fmt_double(b, sizeof(b), evaluate_expression(val)); return strdup(b); }
}
void interpret_print(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf("Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_NULL) == 0) {
        dbg_printf("null");
        append_to_stdout("null");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_BOOL) == 0) {
        const char *s = arg->value ? "true" : "false";
        dbg_printf("%s", s);
        append_to_stdout(s);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_IDENTIFIER) == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, TE_T_NULL) == 0) { dbg_printf("null"); append_to_stdout("null"); return; }
        if (_v && _v->type && strcmp(_v->type, TE_T_BOOL) == 0) {
            const char *s = _v->value.int_value ? "true" : "false";
            dbg_printf("%s", s); append_to_stdout(s); return;
        }
    }
    if (arg->type && strcmp(arg->type, TE_T_STRING) == 0) {
        dbg_printf("%s", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_STRING_INTERP) == 0) {
        char *s = expand_interp_string(arg->str_value);
        dbg_printf("%s", s);
        append_to_stdout(s);
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_ADD) == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        dbg_printf("%s", s);
        append_to_stdout(s);
        free(s);
        return;
    }
    /* println(xs.distinct()) — método que retorna LIST/escalar vía __ret__.
     * Sin esto, un método que retorna lista caía al fallback numérico e
     * imprimía vacío. */
    if (arg->type && strcmp(arg->type, TE_T_CALL_METHOD) == 0) {
        interpret_call_method(arg);
        Variable *r = find_variable(TE_SYM_RET);
        if (r) {
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_LIST) == 0) {
                te_print_list_node((ASTNode *)(intptr_t)r->value.object_value, 0);
                return;
            }
            if (r->vtype == VAL_STRING) { dbg_printf("%s", r->value.string_value ? r->value.string_value : ""); append_to_stdout(r->value.string_value ? r->value.string_value : ""); return; }
            if (r->vtype == VAL_INT)    { char b[32]; snprintf(b, sizeof(b), "%lld", r->value.int_value); dbg_printf("%s", b); append_to_stdout(b); return; }
            if (r->vtype == VAL_FLOAT)  { char b[64]; te_fmt_double(b, sizeof(b), r->value.float_value); dbg_printf("%s", b); append_to_stdout(b); return; }
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_NULL) == 0) { dbg_printf("null"); append_to_stdout("null"); return; }
        }
        return;
    }
    /* gotcha #19: print(builtin(...)) — a nested function call such as
     * print(mysql_query(conn, sql, "json")) used to fall through to the
     * `arg->id` branch and report `variable 'mysql_query' is not defined`,
     * because a CALL_FUNC node carries the function name in arg->id. Dispatch
     * it like CALL_METHOD and print the captured __ret__ value. */
    if (arg->type && strcmp(arg->type, TE_T_CALL_FUNC) == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable(TE_SYM_RET);
        if (r) {
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_LIST) == 0) {
                te_print_list_node((ASTNode *)(intptr_t)r->value.object_value, 0);
                return;
            }
            if (r->vtype == VAL_STRING) { dbg_printf("%s", r->value.string_value ? r->value.string_value : ""); append_to_stdout(r->value.string_value ? r->value.string_value : ""); return; }
            if (r->vtype == VAL_INT)    { char b[32]; snprintf(b, sizeof(b), "%lld", r->value.int_value); dbg_printf("%s", b); append_to_stdout(b); return; }
            if (r->vtype == VAL_FLOAT)  { char b[64]; te_fmt_double(b, sizeof(b), r->value.float_value); dbg_printf("%s", b); append_to_stdout(b); return; }
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_NULL) == 0) { dbg_printf("null"); append_to_stdout("null"); return; }
        }
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_ACCESS_EXPR) == 0) {
        ASTNode *map = resolve_to_map(arg->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(arg->right, keybuf, sizeof(keybuf));
            if (!key) { dbg_printf("Error: Map key must be a string.\n"); return; }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) { dbg_printf("Error: key '%s' not found.\n", key); return; }
            ASTNode *val = pair->left;
            if (val && val->type && strcmp(val->type, TE_T_STRING) == 0) dbg_printf("%s", val->str_value);
            else if (val && val->type && strcmp(val->type, TE_T_FLOAT) == 0) { char b[64]; te_fmt_double(b, sizeof(b), atof(val->str_value)); dbg_printf("%s", b); }
            else { double v_ = evaluate_expression(val); char b[64]; te_fmt_double(b, sizeof(b), v_); dbg_printf("%s", b); }
            return;
        }
        ASTNode *list = resolve_to_list(arg->left);
        if (!list) { dbg_printf("Error: not a list or Map.\n"); return; }
        int idx = (int)evaluate_expression(arg->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            /* gotcha #4: índice fuera de rango imprime `null` (consistente
             * con el assign), en vez de "Error: index N out of range". */
            dbg_printf("null"); append_to_stdout("null");
            return;
        }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return;
        if (item->type && strcmp(item->type, TE_T_STRING) == 0) dbg_printf("%s", item->str_value);
        else if (item->type && strcmp(item->type, TE_T_FLOAT) == 0) { char b[64]; te_fmt_double(b, sizeof(b), atof(item->str_value)); dbg_printf("%s", b); }
        else { double v_ = evaluate_expression(item); char b[64]; te_fmt_double(b, sizeof(b), v_); dbg_printf("%s", b); }
        return;
    }
    /* Fase 1a: print(arr[i]) — soporta strings y números */
    if (arg->type && strcmp(arg->type, TE_T_ACCESS_ATTR) == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        /* Fase 1a: arr.length / map.length / str.length */
        if (a && a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) { dbg_printf("%d", list_length(list)); return; }
            ASTNode *map = resolve_to_map(o);
            if (map) { dbg_printf("%d", map_length(map)); return; }
            /* str.length — mayo 2026 */
            if (o && o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    dbg_printf("%zu", sv->value.string_value ? strlen(sv->value.string_value) : 0);
                    return;
                }
            }
        }
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_printf("Error: object '%s' is not defined or is not an object.\n", o->id);
            return;
        }
        /* v1.0.0 fix: var is a MAP / OBJECT_LITERAL → resolve `o.attr` as a
         * map lookup instead of casting to ObjectNode* (which crashes). */
        if (v->type && (strcmp(v->type, TE_T_MAP) == 0 || strcmp(v->type, TE_T_OBJECT_LITERAL) == 0)) {
            char *s = te_map_field_display(v, a->id);
            if (s) { dbg_printf("%s", s); append_to_stdout(s); free(s); }
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_printf("Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        if (!te_attr_access_ok(obj->class, idx, o)) return;
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_OBJECT && attr->value.object_value == NULL)
            dbg_printf("null");
        else if (attr->vtype == VAL_STRING)
            dbg_printf("%s", attr->value.string_value);
        else
            dbg_printf("%lld", (long long)attr->value.int_value);
        return;
    }

    if (arg->id) { 
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_printf("Error: variable '%s' is not defined.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, TE_T_LIST) == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, TE_T_LIST) == 0) {
                te_print_list_node(listNode, 0);
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_printf("%s", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_printf("%lld", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            { char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value); dbg_printf("%s", b); }
        else
            dbg_printf("Object of class: %s\n", v->value.object_value->class->name);
    } else {
        long long i64v; double val;
        if (te_eval_num(arg, &i64v, &val)) {   /* Fase 1b */
            dbg_printf("%lld", i64v);
        } else {
            char b[64]; te_fmt_double(b, sizeof(b), val); dbg_printf("%s", b);
        }
    }

    if (g_vm.ret_var_active) {
        if (g_vm.ret_var.vtype == VAL_STRING && g_vm.ret_var.value.string_value) free(g_vm.ret_var.value.string_value);
        if (g_vm.ret_var.id) free(g_vm.ret_var.id);
        if (g_vm.ret_var.type) free(g_vm.ret_var.type);
        memset(&g_vm.ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}
/* Extraído de interpret_println (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_println_access_expr(ASTNode *arg) {
    if (arg->type && strcmp(arg->type, TE_T_ACCESS_EXPR) == 0) {
        /* Fase 1c: println(m["k"]) */
        ASTNode *map = resolve_to_map(arg->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(arg->right, keybuf, sizeof(keybuf));
            if (!key) { dbg_printf("Error: Map key must be a string.\n"); return 1; }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) { dbg_printf("Error: key '%s' not found.\n", key); return 1; }
            ASTNode *val = pair->left;
            if (val && val->type && strcmp(val->type, TE_T_STRING) == 0) dbg_printf("%s\n", val->str_value);
            else if (val && val->type && strcmp(val->type, TE_T_FLOAT) == 0) { char b[64]; te_fmt_double(b, sizeof(b), atof(val->str_value)); dbg_printf("%s\n", b); }
            else { double v_ = evaluate_expression(val); char b[64]; te_fmt_double(b, sizeof(b), v_); dbg_printf("%s\n", b); }
            return 1;
        }
        ASTNode *list = resolve_to_list(arg->left);
        if (!list) { dbg_printf("Error: not a list.\n"); return 1; }
        int idx = (int)evaluate_expression(arg->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            /* gotcha #4: índice fuera de rango en uso directo imprime `null`,
             * consistente con `let v = xs[99]` (que asigna null). Antes
             * imprimía "Error: index N out of range". */
            dbg_printf("null\n"); append_to_stdout("null\n");
            return 1;
        }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return 1;
        if (item->type && strcmp(item->type, TE_T_STRING) == 0) {
            dbg_printf("%s\n", item->str_value);
        } else if (item->type && strcmp(item->type, TE_T_FLOAT) == 0) {
            char b[64]; te_fmt_double(b, sizeof(b), atof(item->str_value)); dbg_printf("%s\n", b);
        } else {
            double v = evaluate_expression(item);
            char b[64]; te_fmt_double(b, sizeof(b), v); dbg_printf("%s\n", b);
        }
        return 1;
    }
    return 0;
}

/* Extraído de interpret_println (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_println_access_attr(ASTNode *arg) {
    if (arg->type && strcmp(arg->type, TE_T_ACCESS_ATTR) == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        /* Fase 1a: arr.length / map.length / str.length */
        if (a && a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) {
                int n = list_length(list);
                dbg_printf("%d\n", n);
                char tmp[32]; snprintf(tmp, 32, "%d\n", n);
                append_to_stdout(tmp);
                return 1;
            }
            ASTNode *map = resolve_to_map(o);
            if (map) {
                int n = map_length(map);
                dbg_printf("%d\n", n);
                char tmp[32]; snprintf(tmp, 32, "%d\n", n);
                append_to_stdout(tmp);
                return 1;
            }
            /* str.length — mayo 2026 */
            if (o && o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    size_t n = sv->value.string_value ? strlen(sv->value.string_value) : 0;
                    dbg_printf("%zu\n", n);
                    char tmp[32]; snprintf(tmp, 32, "%zu\n", n);
                    append_to_stdout(tmp);
                    return 1;
                }
            }
        }
        /* Bug fix: println(arr[i].attr) — o es ACCESS_EXPR.
         * Va antes de find_variable porque o->id es NULL aquí. */
        if (o && o->type && strcmp(o->type, TE_T_ACCESS_EXPR) == 0) {
            ASTNode *list2 = resolve_to_list(o->left);
            if (list2 && o->right) {
                int idx2 = (int)evaluate_expression(o->right);
                int len2 = list_length(list2);
                if (idx2 < 0 || idx2 >= len2) {
                    dbg_printf("Error: index %d out of range (length=%d).\n", idx2, len2);
                    return 1;
                }
                ASTNode *item = list_get_item(list2, idx2);
                ObjectNode *iobj = NULL;
                if (item && item->type && strcmp(item->type, TE_T_OBJECT) == 0) {
                    if (item->extra) iobj = (ObjectNode*)item->extra;
                    else iobj = (ObjectNode*)(intptr_t)item->value;
                }
                if (iobj && iobj->class) {
                    for (int i = 0; i < iobj->class->attr_count; i++) {
                        if (strcmp(iobj->class->attributes[i].id, a->id) == 0) {
                            Variable *attr2 = &iobj->attributes[i];
                            if (attr2->vtype == VAL_STRING) {
                                dbg_printf("%s\n", attr2->value.string_value);
                                append_to_stdout(attr2->value.string_value);
                                append_to_stdout("\n");
                            } else if (attr2->vtype == VAL_FLOAT) {
                                char b[64]; te_fmt_double(b, sizeof(b), attr2->value.float_value); dbg_printf("%s\n", b);
                            } else {
                                dbg_printf("%d\n", attr2->value.int_value);
                                char tmpx[32]; snprintf(tmpx, 32, "%lld\n", (long long)attr2->value.int_value);
                                append_to_stdout(tmpx);
                            }
                            return 1;
                        }
                    }
                    dbg_printf("Error: attribute '%s' not found in indexed object.\n", a->id);
                    return 1;
                }
            }
            /* m["k"].attr — MAP-indexed object attr access (toMap result). */
            ASTNode *map2 = resolve_to_map(o->left);
            if (map2 && o->right) {
                const char *key2 = NULL;
                if (nk_of(o->right) == NK_STRING) key2 = o->right->str_value;
                else if (nk_of(o->right) == NK_IDENTIFIER || nk_of(o->right) == NK_ID) {
                    Variable *kv = find_variable(o->right->id);
                    if (kv && kv->vtype == VAL_STRING) key2 = kv->value.string_value;
                }
                if (key2) {
                    ASTNode *pair = map_find_pair(map2, key2);
                    ASTNode *val  = pair ? pair->left : NULL;
                    ObjectNode *iobj = NULL;
                    if (val && val->type && strcmp(val->type, TE_T_OBJECT) == 0) {
                        if (val->extra) iobj = (ObjectNode*)val->extra;
                        else iobj = (ObjectNode*)(intptr_t)val->value;
                    }
                    if (iobj && iobj->class) {
                        for (int i = 0; i < iobj->class->attr_count; i++) {
                            if (strcmp(iobj->class->attributes[i].id, a->id) == 0) {
                                Variable *attr2 = &iobj->attributes[i];
                                if (attr2->vtype == VAL_STRING) {
                                    dbg_printf("%s\n", attr2->value.string_value);
                                    append_to_stdout(attr2->value.string_value);
                                    append_to_stdout("\n");
                                } else if (attr2->vtype == VAL_FLOAT) {
                                    char b[64]; te_fmt_double(b, sizeof(b), attr2->value.float_value); dbg_printf("%s\n", b);
                                } else {
                                    dbg_printf("%d\n", attr2->value.int_value);
                                    char tmpx[32]; snprintf(tmpx, 32, "%lld\n", (long long)attr2->value.int_value);
                                    append_to_stdout(tmpx);
                                }
                                return 1;
                            }
                        }
                        dbg_printf("Error: attribute '%s' not found in mapped object.\n", a->id);
                        return 1;
                    }
                }
            }
            dbg_printf("Error: cannot resolve indexed expression for attribute access.\n");
            return 1;
        }
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_printf("Error: object '%s' is not defined or is not an object.\n",
                       (o && o->id) ? o->id : "<expr>");
            return 1;
        }
        /* v1.0.0 fix: var is a MAP / OBJECT_LITERAL → resolve `o.attr` as a
         * map lookup instead of casting to ObjectNode* (which crashes). */
        if (v->type && (strcmp(v->type, TE_T_MAP) == 0 || strcmp(v->type, TE_T_OBJECT_LITERAL) == 0)) {
            char *s = te_map_field_display(v, a->id);
            if (s) { dbg_printf("%s\n", s); append_to_stdout(s); append_to_stdout("\n"); free(s); }
            else { dbg_printf("null\n"); append_to_stdout("null\n"); }
            return 1;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_printf("Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return 1;
        }
        if (!te_attr_access_ok(obj->class, idx, o)) return 1;
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_OBJECT && attr->value.object_value == NULL) {
            dbg_printf("null\n");
            append_to_stdout("null\n");
        }
        else if (attr->vtype == VAL_STRING) {
            dbg_printf("%s\n", attr->value.string_value);
            append_to_stdout(attr->value.string_value);
            append_to_stdout("\n");
        }
        else {
            dbg_printf("%d\n", attr->value.int_value);
            char temp[32]; snprintf(temp, 32, "%lld\n", (long long)attr->value.int_value);
            append_to_stdout(temp);
        }
        return 1;
    }
    return 0;
}

void interpret_println(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf("Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_NULL) == 0) {
        dbg_printf("null\n");
        append_to_stdout("null\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_BOOL) == 0) {
        const char *s = arg->value ? "true" : "false";
        dbg_printf("%s\n", s);
        append_to_stdout(s); append_to_stdout("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_IDENTIFIER) == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, TE_T_NULL) == 0) { dbg_printf("null\n"); append_to_stdout("null\n"); return; }
        if (_v && _v->type && strcmp(_v->type, TE_T_BOOL) == 0) {
            const char *s = _v->value.int_value ? "true" : "false";
            dbg_printf("%s\n", s); append_to_stdout(s); append_to_stdout("\n"); return;
        }
    }
    if (arg->type && strcmp(arg->type, TE_T_STRING) == 0) {
        dbg_printf("%s\n", arg->str_value);
        append_to_stdout(arg->str_value);
        append_to_stdout("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_STRING_INTERP) == 0) {
        char *s = expand_interp_string(arg->str_value);
        dbg_printf("%s\n", s);
        append_to_stdout(s);
        append_to_stdout("\n");
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_ADD) == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        dbg_printf("%s\n", s);
        append_to_stdout(s);
        append_to_stdout("\n");
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_CALL_METHOD) == 0) {
        interpret_call_method(arg);
        Variable *r = find_variable(TE_SYM_RET);
        if (!r) { dbg_printf("\n"); return; }
        if (r->vtype == VAL_STRING) { dbg_printf("%s\n", r->value.string_value ? r->value.string_value : ""); return; }
        if (r->vtype == VAL_INT && r->type && strcmp(r->type, TE_T_BOOL) == 0) {
            const char *s = r->value.int_value ? "true" : "false";
            dbg_printf("%s\n", s); return;
        }
        if (r->vtype == VAL_INT) { dbg_printf("%lld\n", r->value.int_value); return; }
        if (r->vtype == VAL_FLOAT) { char b[64]; te_fmt_double(b, sizeof(b), r->value.float_value); dbg_printf("%s\n", b); return; }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_NULL) == 0) { dbg_printf("null\n"); return; }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_LIST) == 0) {
            ASTNode *listNode = (ASTNode*)(intptr_t)r->value.object_value;
            if (listNode) { te_print_list_node(listNode, 1); return; }
        }
        dbg_printf("\n");
        return;
    }
    /* Ola 13: println(builtin(...)) — dispatch via __ret__ */
    if (arg->type && strcmp(arg->type, TE_T_CALL_FUNC) == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable(TE_SYM_RET);
        if (!r) { dbg_printf("\n"); return; }
        if (r->vtype == VAL_STRING) { dbg_printf("%s\n", r->value.string_value ? r->value.string_value : ""); append_to_stdout(r->value.string_value ? r->value.string_value : ""); append_to_stdout("\n"); return; }
        if (r->vtype == VAL_INT && r->type && strcmp(r->type, TE_T_BOOL) == 0) {
            const char *s = r->value.int_value ? "true" : "false";
            dbg_printf("%s\n", s); append_to_stdout(s); append_to_stdout("\n"); return;
        }
        if (r->vtype == VAL_INT)    { dbg_printf("%lld\n", r->value.int_value); char tmp[32]; snprintf(tmp,32,"%lld\n",r->value.int_value); append_to_stdout(tmp); return; }
        if (r->vtype == VAL_FLOAT)  { char b[64]; te_fmt_double(b, sizeof(b), r->value.float_value); dbg_printf("%s\n", b); append_to_stdout(b); append_to_stdout("\n"); return; }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, TE_T_LIST) == 0) {
            ASTNode *listNode = (ASTNode*)(intptr_t)r->value.object_value;
            if (listNode) { te_print_list_node(listNode, 1); return; }
        }
        dbg_printf("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_NULL_COALESCE) == 0) {
        ASTNode *l = arg->left;
        ASTNode *chosen = te_expr_is_null(l) ? arg->right : l;
        ASTNode wrapper; memset(&wrapper, 0, sizeof(ASTNode));
        wrapper.type = strdup(TE_T_PRINTLN); wrapper.left = chosen;
        interpret_println(&wrapper);
        free(wrapper.type);
        return;
    }
    if (arg->type && strcmp(arg->type, TE_T_TERNARY) == 0) {
        int cond = (evaluate_expression(arg->left) != 0.0);
        ASTNode *chosen = cond ? arg->right : arg->extra;
        ASTNode wrapper; memset(&wrapper, 0, sizeof(ASTNode));
        wrapper.type = strdup(TE_T_PRINTLN); wrapper.left = chosen;
        interpret_println(&wrapper);
        free(wrapper.type);
        return;
    }
    if (te_println_access_expr(arg)) return;
    if (te_println_access_attr(arg)) return;

    if (arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_printf("Error: variable '%s' is not defined.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, TE_T_LIST) == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, TE_T_LIST) == 0) {
                te_print_list_node(listNode, 1);
                return;
            }
        }
        if (v->vtype == VAL_STRING) {
            dbg_printf("%s\n", v->value.string_value);
            append_to_stdout(v->value.string_value);
            append_to_stdout("\n");
        }
        else if (v->vtype == VAL_INT) {
            dbg_printf("%lld\n", v->value.int_value);
            char temp[32]; snprintf(temp, 32, "%lld\n", v->value.int_value);
            append_to_stdout(temp);
        }
        else if (v->vtype == VAL_FLOAT) {
            char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value);
            dbg_printf("%s\n", b);
            append_to_stdout(b); append_to_stdout("\n");
        }
        else {
            dbg_printf("Object of class: %s\n", v->value.object_value->class->name);
            // Don't append object description to stdout for API response usually
        }
    } else {
        long long i64v; double val;
        if (te_eval_num(arg, &i64v, &val)) {   /* Fase 1b */
            dbg_printf("%lld\n", i64v);
        } else {
            char b[64]; te_fmt_double(b, sizeof(b), val); dbg_printf("%s\n", b);
        }
    }

    if (g_vm.ret_var_active) {
        if (g_vm.ret_var.vtype == VAL_STRING && g_vm.ret_var.value.string_value) free(g_vm.ret_var.value.string_value);
        if (g_vm.ret_var.id) free(g_vm.ret_var.id);
        if (g_vm.ret_var.type) free(g_vm.ret_var.type);
        memset(&g_vm.ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}
