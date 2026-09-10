/* te_interp_decl.c — declaración de variables y asignación (let/var/const, x = e, x += e,
 * obj.attr = e, arr[i] = e). Extraído de ast.c (Fase 2). Movimiento puro. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ast.h"
#include "te_num.h"
#include "te_decimal.h"
#include "te_value.h"
#include "te_vm.h"
#include "ast_internal.h"

/* Tipo estático "efectivo" de la expresión de una declaración tipada, para el chequeo
 * `TypeError: cannot assign a value of type X to a variable of type Y`. Devuelve NULL cuando el
 * tipo solo se conoce en runtime (accesos, identificadores, NEG/MOD): en esos casos no se
 * chequea, igual que antes de la Fase E. */
static const char *te_decl_static_type(ASTNode *n, const TeValue *v) {
    if (!n || !n->type) return NULL;
    switch (nk_of(n)) {
    case NK_ACCESS_ATTR: case NK_ACCESS_EXPR: case NK_IDENTIFIER: case NK_ID: case NK_NEG: case NK_MOD:
        return NULL;
    default: break;
    }
    if (strcmp(n->type, TE_T_ACCESS_INDEX) == 0) return NULL;
    return te_val_tag(v);
}

void interpret_var_decl(TeVM *vm, ASTNode *node) {
    int is_const_flag = node->value;
    const char *declared_type = node->str_value;
    ASTNode *value_node = node->left;

    /* v1.0.0: deferred CSV load trigger (`let xs = from "x.csv", T;`). El placeholder
     * CSV_LOAD se materializa AQUÍ para que `now_ms()` alrededor mida la carga real.
     * `from "request:body"` conserva el descriptor (value_node->extra) para recargar
     * en cada invocación del handler. */
    if (value_node && value_node->type && strcmp(value_node->type, TE_T_CSV_LOAD) == 0) {
        ASTNode *loaded = te_csv_runtime_load(value_node);
        if (loaded) {
            if (value_node->extra == NULL) {
                if (value_node->type) free(value_node->type);
                if (value_node->id) free(value_node->id);
                free(value_node);
                node->left = loaded;
            }
            value_node = loaded;
        }
    }
    if (!value_node) return;

    /* Fase E: UN solo camino. te_eval_value resuelve ternario, ??, llamadas, accesos,
     * aritmética exacta, concatenación, literales, `new X()`... */
    TeValue v;
    te_eval_value(value_node, &v);

    if (declared_type != NULL) {
        const char *eff = te_decl_static_type(value_node, &v);
        if (eff && strcmp(declared_type, eff) != 0) {
            int allow_int_to_float   = (strcmp(declared_type, TE_T_FLOAT) == 0 && strcmp(eff, TE_T_INT) == 0);
            int allow_int_to_decimal = (strcmp(declared_type, TE_T_DECIMAL) == 0 && strcmp(eff, TE_T_INT) == 0);
            /* v1.0.0: DATETIME y UUID son alias de almacenamiento de STRING. */
            int allow_string_alias = (strcmp(eff, TE_T_STRING) == 0 &&
                                      (strcmp(declared_type, TE_T_DATETIME) == 0 || strcmp(declared_type, TE_T_UUID) == 0));
            int allow_bool_alias = (strcmp(declared_type, TE_T_BOOL) == 0 &&
                                    (strcmp(eff, TE_T_INT) == 0 || strcmp(eff, TE_T_NUMBER) == 0));
            if (!allow_int_to_float && !allow_int_to_decimal && !allow_string_alias && !allow_bool_alias) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "TypeError: cannot assign a value of type '%s' to a variable of type '%s'.",
                    eff, declared_type);
                te_val_free(&v);
                if (throw_message) free(throw_message);
                throw_message = strdup(buf);
                vm->throw_flag = 1;
                return;
            }
        }
        if (strcmp(declared_type, TE_T_DECIMAL) == 0 && v.vtype == VAL_INT && (!v.type || strcmp(v.type, TE_T_BOOL) != 0)) {
            char b[32]; snprintf(b, sizeof(b), "%lld", v.value.int_value);
            te_val_set_string_tag(&v, TE_T_DECIMAL, b);
        } else if (strcmp(declared_type, TE_T_FLOAT) == 0 && v.vtype == VAL_INT && (!v.type || strcmp(v.type, TE_T_BOOL) != 0)) {
            te_val_set_float(&v, (double)v.value.int_value);
        }
    }

    te_declare_value(node->id, &v, is_const_flag);

    /* El valor de una llamada ya fue consumido: dejar __ret__/return limpios (como antes). */
    NodeKind vk = nk_of(value_node);
    if (vk == NK_CALL_FUNC || vk == NK_CALL_METHOD || vk == NK_FILTER_CALL || vk == NK_PREDICT ||
        (value_node->type && strcmp(value_node->type, TE_T_CALL_EXPR) == 0)) {
        if (g_vm.ret_var_active) {
            if (g_vm.ret_var.vtype == VAL_STRING && g_vm.ret_var.value.string_value) free(g_vm.ret_var.value.string_value);
            if (g_vm.ret_var.id) free(g_vm.ret_var.id);
            if (g_vm.ret_var.type) free(g_vm.ret_var.type);
            memset(&g_vm.ret_var, 0, sizeof(Variable));
        }
        vm->return_flag = 0;
        vm->return_node = NULL;
    }
}
void interpret_assign_attr(TeVM *vm, ASTNode *node) {
    /* trace removed */
    ASTNode *access = node->left;
    /* access internals trace removed */
    ASTNode *value_node = node->right;
    Variable *var = find_variable(access->left->id);
    /* find_variable trace removed */
    if (!var || var->vtype!=VAL_OBJECT) {
        printf("Error: object '%s' is not defined or is not an object.\n", access->left->id); return;
    }
    if (!var->type || strcmp(var->type, TE_T_OBJECT) != 0) {
        printf("Error: object '%s' is not defined or is not an object.\n", access->left->id); return;
    }
    ObjectNode *obj = var->value.object_value;
    /* v0.0.13 (perf) Invalidate columnar mirror cache if this object is in a
     * cached list. */
    if (obj && obj->owning_list) te_colcache_invalidate((ASTNode*)obj->owning_list);
    const char *attr_name = access->right->id;
    int idx=-1;
    for(int i=0;i<obj->class->attr_count;i++){
        if(strcmp(obj->class->attributes[i].id,attr_name)==0){idx=i;break;}
    }
    if(idx<0){ printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name); return; }
    if (!te_attr_access_ok(obj->class, idx, access->left)) return;
    /* Fase F: el tipo DECLARADO vive en la clase; en la instancia `type` es el tag de runtime para
     * atributos `dynamic` (LAMBDA/LIST/MAP/OBJECT/STRING/...). */
    const char *declared = obj->class->attributes[idx].type ? obj->class->attributes[idx].type : obj->attributes[idx].type;
    if (declared && strcmp(declared, TE_DT_DYNAMIC) == 0) {
        TeValue v; te_eval_value(value_node, &v);
        if (v.vtype == VAL_OBJECT && v.type && strcmp(v.type, TE_T_LAMBDA) == 0 && v.value.object_value)
            v.value.object_value = (void *)te_capture_lambda((ASTNode *)(intptr_t)v.value.object_value);   /* escapa del frame */
        te_val_move_into(&obj->attributes[idx], &v);
        return;
    }

    /* detailed attribute assignment trace removed */

    if (declared == NULL) {
        fprintf(stderr, "Error: attribute '%s' has no defined type.\n", attr_name);
        return;
    }    

    /* Fase 7b: nullable attribute support ("T?"). A trailing '?' marks the
     * attribute as nullable. Assigning `null` stores a runtime null marker
     * (VAL_OBJECT + NULL object_value); assigning null to a non-nullable
     * attribute is an error. */
    int decl_nullable = 0;
    {
        size_t dl = strlen(declared);
        if (dl > 0 && declared[dl - 1] == '?') decl_nullable = 1;
    }
    if (value_node && value_node->type && strcmp(value_node->type, TE_T_NULL) == 0) {
        if (!decl_nullable) {
            int ln = node->line ? node->line : (access->line ? access->line : 0);
            const char *fname = g_vm.script_path ? g_vm.script_path : "<script>";
            fprintf(stderr,
                    "%s%s:%d: Error: cannot assign null to non-nullable attribute '%s' of type %s.%s\n",
                    TE_ERR_RED, fname, ln, attr_name, declared, TE_ERR_RESET);
            return;
        }
        obj->attributes[idx].vtype = VAL_OBJECT;
        obj->attributes[idx].value.object_value = NULL;
        return;
    }

    /* decimal: evaluación exacta (literal, variable, atributo, expresión o llamada). */
    if (strcmp(declared, TE_DT_DECIMAL) == 0 || strcmp(declared, TE_DT_DECIMAL_OPT) == 0) {
        char dec[TE_DEC_TEXT_MAX];
        if (!te_dec_eval(value_node, dec, sizeof(dec))) {
            char *s = get_node_string(value_node);
            TeDec d;
            if (!s || !te_dec_parse(s, &d)) {
                fprintf(stderr, "Error: cannot assign '%s' to decimal attribute '%s'.\n", s ? s : "", attr_name);
                if (s) free(s);
                return;
            }
            te_dec_format(&d, dec, sizeof(dec));
            free(s);
        }
        if (obj->attributes[idx].vtype == VAL_STRING && obj->attributes[idx].value.string_value)
            free(obj->attributes[idx].value.string_value);
        obj->attributes[idx].vtype = VAL_STRING;
        obj->attributes[idx].value.string_value = strdup(dec);
        return;
    }

    /* Validación de tipo: no permitir asignar un valor de tipo incompatible.
     * Se determina si el atributo declarado es de texto (string) o numérico,
     * y si el valor asignado es de texto o numérico. Un desajuste aborta. */
    {
        int decl_is_str = (strcmp(declared, TE_DT_STRING) == 0 || strcmp(declared, TE_DT_STRING_OPT) == 0 ||
                           strcmp(declared, TE_DT_UUID) == 0 || strcmp(declared, TE_DT_UUID_OPT) == 0 ||
                           strcmp(declared, TE_DT_DATETIME) == 0 || strcmp(declared, TE_DT_DATETIME_OPT) == 0);
        /* val_kind: 0 = desconocido, 1 = string, 2 = numérico */
        int val_kind = 0;
        if (value_node->type) {
            if (strcmp(value_node->type, TE_T_STRING) == 0) {
                val_kind = 1;
            } else if (strcmp(value_node->type, TE_T_NUMBER) == 0 ||
                       strcmp(value_node->type, TE_T_INT) == 0 ||
                       strcmp(value_node->type, TE_T_FLOAT) == 0) {
                val_kind = 2;
            } else if (strcmp(value_node->type, TE_T_IDENTIFIER) == 0 ||
                       strcmp(value_node->type, TE_T_ID) == 0) {
                Variable *vv = find_variable(value_node->id ? value_node->id : value_node->str_value);
                if (vv) {
                    if (vv->vtype == VAL_STRING) val_kind = 1;
                    else if (vv->vtype == VAL_INT || vv->vtype == VAL_FLOAT) val_kind = 2;
                }
            }
        }
        if (val_kind != 0) {
            const char *val_tname = (val_kind == 1) ? TE_DT_STRING : TE_DT_INT;
            if ((decl_is_str && val_kind == 2) || (!decl_is_str && val_kind == 1)) {
                int ln = node->line ? node->line
                       : (value_node->line ? value_node->line
                       : (access->line ? access->line : 0));
                const char *fname = g_vm.script_path ? g_vm.script_path : "<script>";
                fprintf(stderr,
                        "%s%s:%d: Error: cannot assign %s to attribute '%s' of type %s.%s\n",
                        TE_ERR_RED, fname, ln, val_tname, attr_name, declared, TE_ERR_RESET);
                return;
            }
        }
    }

    if (strcmp(declared, TE_DT_STRING) == 0 || strcmp(declared, TE_DT_STRING_OPT) == 0 ||
        strcmp(declared, TE_DT_UUID) == 0 || strcmp(declared, TE_DT_UUID_OPT) == 0 ||
        strcmp(declared, TE_DT_DATETIME) == 0 || strcmp(declared, TE_DT_DATETIME_OPT) == 0) {
        if (value_node->type && strcmp(value_node->type, TE_T_STRING) == 0) {
          obj->attributes[idx].value.string_value = strdup(value_node->str_value);
          if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (STRING)\n", attr_name, value_node->str_value);
        }
        else if (value_node->type && (strcmp(value_node->type, TE_T_IDENTIFIER) == 0 || strcmp(value_node->type, TE_T_ID) == 0)) {
          Variable *v2 = find_variable(value_node->id ? value_node->id : value_node->str_value);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string or variable not found.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR)\n", attr_name, v2->value.string_value);
        }
        else if (value_node->id) {
          Variable *v2 = find_variable(value_node->id);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR ID)\n", attr_name, v2->value.string_value);
        }
        else if (strcmp(value_node->type, TE_T_CALL_FUNC) == 0) {
          interpret_ast(value_node);
          Variable *r = find_variable(TE_SYM_RET);
          if (!r || r->vtype != VAL_STRING) {
            fprintf(stderr, "Error: function result is not a string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(r->value.string_value);
          if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (CALL)\n", attr_name, r->value.string_value);
        }
        obj->attributes[idx].vtype = VAL_STRING;
      } else {
        double val = evaluate_expression(value_node);
        int decl_is_float = (strcmp(declared, TE_DT_FLOAT) == 0 || strcmp(declared, TE_DT_FLOAT_OPT) == 0);
        if (decl_is_float) {
            obj->attributes[idx].value.float_value = val;
            obj->attributes[idx].vtype = VAL_FLOAT;
            if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %f (FLOAT)\n", attr_name, val);
        } else {
            obj->attributes[idx].value.int_value = (long long)val;
            obj->attributes[idx].vtype = VAL_INT;
            if (g_vm.debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %d (INT)\n", attr_name, (int)val);
        }
    }
}
/* Rama `x = f(...)`: ejecuta la llamada y, si el destino y el resultado son numéricos, copia
 * directo (sin strdup). Devuelve 1 si terminó; 0 -> el llamador sigue por te_eval_value (que
 * lee __ret__ sin re-ejecutar). */
static int te_assign_from_call_fast(TeVM *vm, ASTNode *var_node, ASTNode *value_node) {
    interpret_ast(value_node);
    static int fr_init = 0;
    static int fr_enabled = 1;
    if (!fr_init) {
        const char *e = getenv("TYPEEASY_NO_FASTRET");
        if (e && e[0] && e[0] != '0') fr_enabled = 0;
        fr_init = 1;
    }
    if (!(fr_enabled && g_vm.ret_var_active
          && (g_vm.ret_var.vtype == VAL_INT || g_vm.ret_var.vtype == VAL_FLOAT)
          && (!g_vm.ret_var.type || strcmp(g_vm.ret_var.type, TE_T_BOOL) != 0))) return 0;
    Variable *fv = te_resolve_cached(var_node);
    if (!(fv && !fv->is_const && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)
          && (!fv->type || strcmp(fv->type, TE_T_BOOL) != 0))) return 0;
    if (g_vm.ret_var.vtype == VAL_FLOAT) { fv->vtype = VAL_FLOAT; fv->value.float_value = g_vm.ret_var.value.float_value; if (fv->type && strcmp(fv->type, TE_T_FLOAT) != 0) { free(fv->type); fv->type = strdup(TE_T_FLOAT); } }
    else { fv->vtype = VAL_INT; fv->value.int_value = g_vm.ret_var.value.int_value; if (fv->type && strcmp(fv->type, TE_T_INT) != 0) { free(fv->type); fv->type = strdup(TE_T_INT); } }
    vm->return_flag = 0;
    vm->return_node = NULL;
    return 1;
}

/* Asigna *v a la variable `name` (existente: respeta const; nueva: la crea). */
void te_assign_value(ASTNode *var_node, TeValue *v) {
    Variable *dst = te_resolve_cached(var_node);
    if (dst) {
        if (dst->is_const) { te_val_free(v); te_runtime_fatalf("Error: cannot assign to constant variable '%s'.", var_node->id); return; }
        te_val_move_into(dst, v);
        return;
    }
    if (g_vm.var_count >= MAX_VARS) { te_val_free(v); te_runtime_fatalf("Error: too many declared variables (limit %d).", MAX_VARS); return; }
    Variable *nv = &g_vm.vars[g_vm.var_count];
    memset(nv, 0, sizeof(*nv));
    nv->id = strdup(var_node->id);
    g_vm.var_count++;
    te_sym_insert(nv->id, (int)(nv - g_vm.vars));
    te_val_move_into(nv, v);
}

void interpret_assign(TeVM *vm, ASTNode *node) {
    /* trace removed */
    ASTNode *var_node = node->left;
    ASTNode *value_node = node->right;
    if (!var_node || !var_node->id || !value_node) {
        printf("Error: invalid assignment.\n"); 
        return; 
    }

    /* Ternario `x = cond ? a : b` sobre variable existente: elegir la rama
     * fresca SIN mutar node->right (para que loops/funciones re-evalúen la
     * condición). Loop para ternarios anidados (right-assoc). Espejo de
     * interpret_var_decl; sin esto el nodo TERNARY caía al catch-all de
     * te_value_to_variable y se guardaba 0 SIEMPRE. */
    while (value_node && value_node->type && strcmp(value_node->type, TE_T_TERNARY) == 0) {
        int cond = (evaluate_expression(value_node->left) != 0.0);
        value_node = cond ? value_node->right : value_node->extra;
    }
    if (!value_node) return;

    /* Fase 2 (perf): fast-path for "x = numeric_expr" with cached Variable*.
     * Hot in for/while loops. Skips strdup/find_variable_for/temp_node alloc. */
    {
        Variable *fv = te_resolve_cached(var_node);
        if (fv && !fv->is_const && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)) {
            NodeKind vk = nk_of(value_node);
            if (vk == NK_ADD || vk == NK_SUB || vk == NK_MUL || vk == NK_DIV
                || vk == NK_NUMBER || vk == NK_INT || vk == NK_FLOAT
                || vk == NK_IDENTIFIER) {
                /* Avoid string-typed ADD (concat) which needs the slow path. */
                if (!(vk == NK_ADD && is_string_type(value_node))) {
                    /* Mismas primitivas que BC_I64_STORE / BC_STORE_VAR (te_bytecode.c). */
                    long long i64v;
                    if (te_eval_i64(value_node, &i64v)) te_num_store_i64(fv, i64v);   /* Fase 1b: entero exacto */
                    else                                te_num_store(fv, evaluate_expression(value_node));
                    return;
                }
            }
        }
    }

    /* Fase E: UN solo camino para el resto (llamadas, accesos, aritmética, comparaciones,
     * literales, identificadores, `new`...): evaluar y asignar. */
    NodeKind vk = nk_of(value_node);
    if (vk == NK_CALL_FUNC || vk == NK_CALL_METHOD || vk == NK_PREDICT) {
        if (te_assign_from_call_fast(vm, var_node, value_node)) return;
        /* la llamada ya corrió: tomar el valor de __ret__ sin re-ejecutarla */
        Variable *r = find_variable(TE_SYM_RET);
        if (!r) { printf("Error: function in assignment returned nothing.\n"); return; }
        TeValue v; te_val_copy(&v, r);
        te_assign_value(var_node, &v);
        vm->return_flag = 0;
        vm->return_node = NULL;
        return;
    }
    TeValue v;
    te_eval_value(value_node, &v);
    te_assign_value(var_node, &v);
}
