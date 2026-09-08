/* te_interp_decl.c — declaración de variables y asignación (let/var/const, x = e, x += e,
 * obj.attr = e, arr[i] = e). Extraído de ast.c (Fase 2). Movimiento puro. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ast.h"
#include "ast_internal.h"

void interpret_var_decl(TeVM *vm, ASTNode *node) {
    //printf("[DEBUG] interpret_var_decl: %s\n", node->id); fflush(stdout);
    int is_const_flag = node->value;
    const char* declared_type = node->str_value;
    ASTNode* value_node = node->left;
    Variable *evaluated_value_var = NULL;

    /* v1.0.0: deferred CSV load trigger. Placeholder tagged at parse-time
     * by te_csv_lazy_resolve_all(). Performing the I/O HERE (instead of in
     * parse_file) makes user `let t0=now_ms(); let xs=from "x.csv",T;`
     * brackets measure the actual load time. Zero overhead for non-CSV
     * decls: one strcmp on value_node->type.
     *
     * v1.1.x: for `from "request:body", T;` the loader keeps the CSV_LOAD
     * descriptor attached (value_node->extra stays non-NULL) so each
     * handler invocation re-loads from the current request body. In that
     * case we MUST NOT replace value_node — the placeholder needs to live
     * for the next invocation. */
    if (value_node && value_node->type && strcmp(value_node->type, "CSV_LOAD") == 0) {
        ASTNode *loaded = te_csv_runtime_load(value_node);
        if (loaded) {
            if (value_node->extra == NULL) {
                /* Static load: consume placeholder once. */
                if (value_node->type) free(value_node->type);
                if (value_node->id) free(value_node->id);
                free(value_node);
                node->left = loaded;
            }
            value_node = loaded;
        }
    }

    /* Fase 7: NULL_COALESCE — pick the right side at decl time */
    if (value_node && value_node->type && strcmp(value_node->type, "NULL_COALESCE") == 0) {
        ASTNode *l = value_node->left;
        value_node = te_expr_is_null(l) ? value_node->right : l;
        node->left = value_node;
    }

    /* Ternary `cond ? a : b`: pick the branch fresh each evaluation (do NOT
     * mutate node->left, so loops/functions re-evaluate the condition). Loop
     * to resolve nested ternaries (`a ? x : b ? y : z`, right-assoc). The
     * chosen branch is processed by the existing per-type logic below. */
    while (value_node && value_node->type && strcmp(value_node->type, "TERNARY") == 0) {
        int cond = (evaluate_expression(value_node->left) != 0.0);
        value_node = cond ? value_node->right : value_node->extra;
    }

    // 1. Si el valor es una llamada a función, ejecútala primero
if (value_node && (strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0 || strcmp(value_node->type, "FILTER_CALL") == 0 || strcmp(value_node->type, "CALL_FUNC") == 0 || strcmp(value_node->type, "CALL_EXPR") == 0)) {        
        //printf("[DEBUG] interpret_var_decl: executing function call\n"); fflush(stdout);
        interpret_ast(value_node);
        //printf("[DEBUG] interpret_var_decl: function call returned\n"); fflush(stdout);
        evaluated_value_var = find_variable("__ret__");
        //printf("[DEBUG] interpret_var_decl: find_variable returned %p\n", (void*)evaluated_value_var); fflush(stdout);
        if (!evaluated_value_var) {
            te_runtime_fatalf("Error: no return value captured from expression '%s'. __ret_var_active=%d", value_node->type ? value_node->type : "unknown", __ret_var_active);
        }
    }
    // ... (rest of function)
    // I will just replace the beginning to add debug prints.
    // But I need to include the rest of the function or use a smaller range.
    // I will use a smaller range.


    // 2. Comprobación de tipos (Tu lógica es correcta)
    //printf("[DEBUG] interpret_var_decl: type checking\n"); fflush(stdout);
    const char* effective_value_type_str = NULL;
    if (evaluated_value_var != NULL) {
        effective_value_type_str = evaluated_value_var->type;
        //printf("[DEBUG] interpret_var_decl: effective_value_type_str from var: %s\n", effective_value_type_str ? effective_value_type_str : "NULL"); fflush(stdout);
    } else if (value_node != NULL) {
        //printf("[DEBUG] interpret_var_decl: effective_value_type_str from node\n"); fflush(stdout);
        if (strcmp(value_node->type, "NUMBER") == 0) {
            effective_value_type_str = "INT";
        } else if (strcmp(value_node->type, "STRING_LITERAL") == 0 || strcmp(value_node->type, "STRING") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "STRING_INTERP") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0 || strcmp(value_node->type, "MOD") == 0 || strcmp(value_node->type, "NEG") == 0 || strcmp(value_node->type, "BIT_AND") == 0 || strcmp(value_node->type, "BIT_OR") == 0 || strcmp(value_node->type, "BIT_XOR") == 0 || strcmp(value_node->type, "BIT_NOT") == 0 || strcmp(value_node->type, "SHL") == 0 || strcmp(value_node->type, "SHR") == 0 || strcmp(value_node->type, "IN") == 0) {
            if (strcmp(value_node->type, "ADD") == 0 && is_string_type(value_node)) {
                effective_value_type_str = "STRING";
            } else {
                double result = evaluate_expression(value_node);
                if (result == (int)result) {
                    effective_value_type_str = "INT";
                } else {
                    effective_value_type_str = "FLOAT";
                }
            }
        } else if (strcmp(value_node->type, "GT") == 0 || strcmp(value_node->type, "LT") == 0 ||
                   strcmp(value_node->type, "EQ") == 0 || strcmp(value_node->type, "GT_EQ") == 0 ||
                   strcmp(value_node->type, "LT_EQ") == 0 || strcmp(value_node->type, "DIFF") == 0 ||
                   strcmp(value_node->type, "AND") == 0 || strcmp(value_node->type, "OR") == 0 ||
                   strcmp(value_node->type, "NOT") == 0) {
            /* Comparison / logical operators produce a boolean. */
            effective_value_type_str = "BOOL";
        } else {
            effective_value_type_str = value_node->type;
        }
    }

    /* Si el tipo "efectivo" es en realidad un AST node type que requiere
     * evaluación (ej. ACCESS_ATTR, ACCESS_INDEX, NEG, MOD, IDENTIFIER),
     * deferimos la verificación a runtime. La fase posterior ya valida
     * el tipo concreto cuando se asigna efectivamente. */
    if (declared_type != NULL && effective_value_type_str != NULL) {
        int is_runtime_only =
            strcmp(effective_value_type_str, "ACCESS_ATTR") == 0 ||
            strcmp(effective_value_type_str, "ACCESS_INDEX") == 0 ||
            strcmp(effective_value_type_str, "NEG") == 0 ||
            strcmp(effective_value_type_str, "MOD") == 0 ||
            strcmp(effective_value_type_str, "IDENTIFIER") == 0;
        if (is_runtime_only) {
            effective_value_type_str = NULL; /* skip static check */
        }
    }

    if (declared_type != NULL && effective_value_type_str != NULL) {
        if (strcmp(declared_type, effective_value_type_str) != 0) {
            int allow_int_to_float = (strcmp(declared_type, "FLOAT") == 0 && strcmp(effective_value_type_str, "INT") == 0);
            /* v1.0.0: DATETIME and UUID are storage-aliased to STRING. */
            int allow_string_alias = (strcmp(effective_value_type_str, "STRING") == 0 &&
                                       (strcmp(declared_type, "DATETIME") == 0 ||
                                        strcmp(declared_type, "UUID") == 0));
            /* BOOL accepts BOOL literals, INT 0/1, and other BOOL exprs. */
            int allow_bool_alias = (strcmp(declared_type, "BOOL") == 0 &&
                                     (strcmp(effective_value_type_str, "INT") == 0 ||
                                      strcmp(effective_value_type_str, "BOOL") == 0 ||
                                      strcmp(effective_value_type_str, "NUMBER") == 0));
            if (!allow_int_to_float && !allow_string_alias && !allow_bool_alias) {
                /* Fase 2: lanzar como excepción capturable en lugar de abortar */
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "TypeError: cannot assign a value of type '%s' to a variable of type '%s'.",
                    effective_value_type_str, declared_type);
                if (throw_message) free(throw_message);
                throw_message = strdup(buf);
                vm->throw_flag = 1;
                return;
            }
        }
    }

    /* Bug fix mayo 2026: `let p = arr[i]` cuando arr es LIST de OBJECT/STRING/INT.
     * Sin esto, declare_variable cae al final-else y trata p como INT 0, perdiendo
     * la referencia al ObjectNode. Aplicable a CSV-loaded lists y a literales.
     * Si el ítem no se puede resolver, cae al flujo legacy. */
    if (evaluated_value_var == NULL && value_node && value_node->type &&
        strcmp(value_node->type, "ACCESS_EXPR") == 0) {
        /* Bug fix: `let v = m["key"]` cuando m es un MAP (p.ej. de json_parse).
         * Sin esto, declare_variable caía al final-else y trataba v como INT 0,
         * perdiendo los valores STRING/OBJECT del mapa. */
        ASTNode *map = resolve_to_map(value_node->left);
        if (map) {
            const char *key = NULL;
            if (value_node->right && value_node->right->type) {
                if (strcmp(value_node->right->type, "STRING") == 0) key = value_node->right->str_value;
                else if (strcmp(value_node->right->type, "IDENTIFIER") == 0 ||
                         strcmp(value_node->right->type, "ID") == 0) {
                    Variable *kv = find_variable(value_node->right->id);
                    if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                }
            }
            ASTNode *pair = key ? map_find_pair(map, key) : NULL;
            ASTNode *val  = pair ? pair->left : NULL;
            if (val && val->type) {
                Variable *var = te_decl_slot(node->id);
                if (!var) return;
                var->is_const = is_const_flag;
                if (strcmp(val->type, "STRING") == 0) {
                    var->vtype = VAL_STRING; var->type = strdup("STRING");
                    var->value.string_value = strdup(val->str_value ? val->str_value : "");
                } else if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) {
                    var->vtype = VAL_INT; var->type = strdup("INT");
                    var->value.int_value = val->value;
                } else if (strcmp(val->type, "FLOAT") == 0) {
                    var->vtype = VAL_FLOAT; var->type = strdup("FLOAT");
                    var->value.float_value = val->str_value ? atof(val->str_value) : 0.0;
                } else if (strcmp(val->type, "OBJECT_LITERAL") == 0 || strcmp(val->type, "MAP") == 0) {
                    var->vtype = VAL_OBJECT; var->type = strdup("MAP");
                    var->value.object_value = (void *)(intptr_t)val;
                } else if (strcmp(val->type, "LIST") == 0) {
                    var->vtype = VAL_OBJECT; var->type = strdup("LIST");
                    var->value.object_value = (void *)(intptr_t)val;
                } else if (strcmp(val->type, "OBJECT") == 0) {
                    var->vtype = VAL_OBJECT; var->type = strdup("OBJECT");
                    var->value.object_value = val->extra ? (void*)val->extra
                                                         : (void *)(intptr_t)val->value;
                } else {
                    var->vtype = VAL_STRING; var->type = strdup("STRING");
                    var->value.string_value = strdup("");
                }
                return;
            }
            /* clave ausente -> string vacío (permite chequear == "") */
            Variable *var = te_decl_slot(node->id);
            if (!var) return;
            var->is_const = is_const_flag;
            var->vtype = VAL_STRING; var->type = strdup("STRING");
            var->value.string_value = strdup("");
            return;
        }
        ASTNode *list = resolve_to_list(value_node->left);
        if (list) {
            int idx = (int)evaluate_expression(value_node->right);
            int len = list_length(list);
            if (idx < 0 || idx >= len) {
                /* Out-of-range index -> first-class null (e.g. `xs[99]` => null),
                 * enabling `xs[99] ?? default`. Previously this fell through to
                 * the legacy path and collapsed silently to INT 0. */
                Variable *var = te_decl_slot(node->id);
                if (!var) return;
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("NULL");
                var->value.object_value = NULL;
                return;
            }
            {
                ASTNode *item = list_get_item(list, idx);
                if (item && item->type) {
                    if (strcmp(item->type, "OBJECT") == 0) {
                        ObjectNode *obj = item->extra
                            ? (ObjectNode*)item->extra
                            : (ObjectNode*)(intptr_t)item->value;
                        if (obj) {
                            Variable *var = te_decl_slot(node->id);
                            if (!var) return;
                            var->is_const = is_const_flag;
                            var->vtype = VAL_OBJECT;
                            var->type = strdup("OBJECT");
                            var->value.object_value = obj;
                            return;
                        }
                    } else if (strcmp(item->type, "OBJECT_LITERAL") == 0 ||
                               strcmp(item->type, "MAP") == 0) {
                        /* item de json_parse("[{...}]")[i] -> un MAP. */
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_OBJECT;
                        var->type = strdup("MAP");
                        var->value.object_value = (void *)(intptr_t)item;
                        return;
                    } else if (strcmp(item->type, "STRING") == 0) {
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_STRING;
                        var->type = strdup("STRING");
                        var->value.string_value = strdup(item->str_value ? item->str_value : "");
                        return;
                    } else if (strcmp(item->type, "NUMBER") == 0 || strcmp(item->type, "INT") == 0) {
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_INT;
                        var->type = strdup("INT");
                        var->value.int_value = item->value;
                        return;
                    } else if (strcmp(item->type, "FLOAT") == 0) {
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_FLOAT;
                        var->type = strdup("FLOAT");
                        var->value.float_value = item->str_value ? atof(item->str_value) : 0.0;
                        return;
                    } else if (strcmp(item->type, "LIST") == 0) {
                        /* B5: nested list item (`let fila = matriz[i]`) fell to the
                         * legacy path -> "object 'fila' is not defined". Alias it. */
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_OBJECT;
                        var->type = strdup("LIST");
                        var->value.object_value = (void *)(intptr_t)item;
                        return;
                    } else if (strcmp(item->type, "BOOL") == 0) {
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_INT;
                        var->type = strdup("BOOL");
                        var->value.int_value = item->value;
                        return;
                    } else if (strcmp(item->type, "NULL") == 0) {
                        Variable *var = te_decl_slot(node->id);
                        if (!var) return;
                        var->is_const = is_const_flag;
                        var->vtype = VAL_OBJECT;
                        var->type = strdup("NULL");
                        var->value.object_value = NULL;
                        return;
                    }
                }
            }
        }
    }

    // 3. Asignación
    //printf("[DEBUG] interpret_var_decl: assignment\n"); fflush(stdout);
    ASTNode *value_to_assign_node = NULL;
    if (evaluated_value_var != NULL) {
        // ---- Si el valor vino de una función (como __ret_var) ----
        //printf("[DEBUG] interpret_var_decl: assigning from evaluated_value_var (vtype=%d)\n", evaluated_value_var->vtype); fflush(stdout);
        
        // Crea un nuevo nodo AST persistente para almacenar el valor
        if (evaluated_value_var->vtype == VAL_STRING) {
            /* create_ast_leaf copia el string -> pasar el puntero directo.
             * El strdup() previo quedaba huerfano (fuga por cada `let x=func()`
             * que devuelve string: request_param, concat, jwt_sign, etc.). */
            value_to_assign_node = create_ast_leaf("STRING", 0, evaluated_value_var->value.string_value, NULL);
        } else if (evaluated_value_var->vtype == VAL_INT) {
            //printf("[DEBUG] interpret_var_decl: creating INT node\n"); fflush(stdout);
            /* gotcha #6: preservar el tag BOOL cuando la función devuelve un
             * booleano (uuid_valid, any/all/none, etc.). Sin esto el resultado
             * se reetiquetaba "INT" y println mostraba 1/0 en vez de true/false. */
            const char *ntag = (evaluated_value_var->type &&
                                strcmp(evaluated_value_var->type, "BOOL") == 0)
                               ? "BOOL" : "INT";
            value_to_assign_node = create_ast_leaf_number(ntag, evaluated_value_var->value.int_value, NULL, NULL);
            //printf("[DEBUG] interpret_var_decl: created INT node\n"); fflush(stdout);
        } else if (evaluated_value_var->vtype == VAL_FLOAT) {
            /* double_to_string() devuelve malloc; create_ast_leaf copia -> liberar. */
            char *fs = double_to_string(evaluated_value_var->value.float_value);
            value_to_assign_node = create_ast_leaf("FLOAT", 0, fs, NULL);
            free(fs);
        } else if (evaluated_value_var->vtype == VAL_OBJECT) {
            // Si es LIST, asignar como VAL_OBJECT y type LIST, y value.object_value apunta al nodo LIST
            if (strcmp(evaluated_value_var->type, "LIST") == 0) {
                Variable *var = te_decl_slot(node->id);
                if (!var) return;
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("LIST");
                var->value.object_value = (ObjectNode *)evaluated_value_var->value.object_value; // Apunta al nodo LIST
               // printf("[DEBUG] interpret_var_decl: declared LIST variable\n"); fflush(stdout);
                // Limpia la variable de retorno
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
                    if (__ret_var.id) free(__ret_var.id);
                    if (__ret_var.type) free(__ret_var.type);
                    memset(&__ret_var, 0, sizeof(Variable));
                    // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
                }
                vm->return_flag = 0;
                vm->return_node = NULL;
                return;
            } else if (strcmp(evaluated_value_var->type, "MAP") == 0) {
                /* Phase D: fast-path for MAP returned from a builtin (e.g. json_parse). */
                Variable *var = te_decl_slot(node->id);
                if (!var) return;
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("MAP");
                var->value.object_value = (ObjectNode *)evaluated_value_var->value.object_value;
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
                    if (__ret_var.id) free(__ret_var.id);
                    if (__ret_var.type) free(__ret_var.type);
                    memset(&__ret_var, 0, sizeof(Variable));
                }
                vm->return_flag = 0;
                vm->return_node = NULL;
                return;
            } else if (strcmp(evaluated_value_var->type, "LAMBDA") == 0) {
                /* gotcha closure-return: una función/lambda devolvió un lambda
                 * (currying). Lo almacenamos como first-class value, igual que
                 * LIST/MAP: object_value apunta al nodo LAMBDA (ya capturado por
                 * te_capture_lambda con sus variables libres sustituidas). */
                Variable *var = te_decl_slot(node->id);
                if (!var) return;
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("LAMBDA");
                var->value.object_value = (ObjectNode *)evaluated_value_var->value.object_value;
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
                    if (__ret_var.id) free(__ret_var.id);
                    if (__ret_var.type) free(__ret_var.type);
                    memset(&__ret_var, 0, sizeof(Variable));
                }
                vm->return_flag = 0;
                vm->return_node = NULL;
                return;
            } else {
                value_to_assign_node = calloc(1, sizeof(ASTNode));
                value_to_assign_node->type = strdup(evaluated_value_var->type);
                value_to_assign_node->extra = (struct ASTNode*)evaluated_value_var->value.object_value;
                value_to_assign_node->id = evaluated_value_var->id ? strdup(evaluated_value_var->id) : NULL;
                value_to_assign_node->str_value = NULL;
                value_to_assign_node->left = NULL;
                value_to_assign_node->right = NULL;
                value_to_assign_node->next = NULL;
            }
        } else {
            te_runtime_fatalf("Internal error: unknown return type for variable assignment '%s'.", node->id);
        }
        
        //printf("[DEBUG] interpret_var_decl: declaring variable %s\n", node->id); fflush(stdout);
        declare_variable(node->id, value_to_assign_node, is_const_flag);
       // printf("[DEBUG] interpret_var_decl: declared variable\n"); fflush(stdout);

    /* Liberar el nodo transitorio. declare_variable COPIA el valor:
     *   - STRING/INT/FLOAT: strdup del payload -> el leaf es desechable.
     *   - OBJECT: vars[].object_value = value->extra (alias); free_ast NUNCA
     *     toca ->extra, asi que liberar el shell no afecta al objeto.
     * Las ramas LIST/MAP/LAMBDA ya hicieron `return` antes de llegar aqui.
     * El bloque constructor de mas abajo solo usa value_to_assign_node cuando
     * node->left->type=="OBJECT", imposible en esta rama (es un CALL_*), por lo
     * que liberarlo aca no produce use-after-free. Antes se omitia el free y
     * el nodo + su copia de string se fugaban en cada `let x = call()`. */
        if (value_to_assign_node) { free_ast(value_to_assign_node); value_to_assign_node = NULL; }
        
        // Limpia la variable de retorno
       // printf("[DEBUG] interpret_var_decl: cleaning __ret_var\n"); fflush(stdout);
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
            // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
        }
        vm->return_flag = 0;
        vm->return_node = NULL;
        // ¡OJO! No pongas un 'return' aquí, el código del constructor debe ejecutarse
    
    } else {
        // ---- Si el valor es un literal (ej. let x = 10) ----
        declare_variable(node->id, value_node, is_const_flag);
    }

    // 4. Ejecutar el constructor (Tu lógica es correcta)
    // (Este código se ejecuta para *ambos* casos, lo cual es correcto)
   // printf("[DEBUG] interpret_var_decl: checking constructor\n"); fflush(stdout);
    if (node->left && strcmp(node->left->type, "OBJECT")==0) {
        Variable *var = find_variable(node->id);
        if (!var || var->vtype!=VAL_OBJECT) return;
        
        // (Corrección sutil: `value_node` puede no ser el correcto si vino de __ret_var)
        // Obtener el ASTNode que *realmente* se usó para la declaración
        ASTNode* object_node_for_constructor = (evaluated_value_var != NULL) ? value_to_assign_node : value_node;
        
        if (!object_node_for_constructor || !object_node_for_constructor->left) {
             // Si no hay argumentos (ej. NLU.parse), no hay nada que hacer.
             return;
        }

        MethodNode *m = var->value.object_value->class->methods;
        while (m && strcmp(m->name,"__constructor")!=0) m=m->next;
        if (m) {
            ParameterNode *p = m->params;
            ASTNode      *arg = object_node_for_constructor->left; // Usar el nodo correcto
            while (p && arg) {
                ASTNode *vn = NULL;
               // fprintf(stderr, "[DEBUG] Constructor arg: param=%s, arg->type=%s\n", p->name, arg->type ? arg->type : "NULL");
                if (arg->type && (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)) {
                    vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    //fprintf(stderr, "[DEBUG] Constructor arg string val: %s\n", arg->str_value);
                } else if (arg->type && strcmp(arg->type, "FLOAT") == 0) {
                    vn = create_ast_leaf("FLOAT", 0, arg->str_value, NULL);
                } else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                    Variable *v = find_variable(arg->id);
                    if (!v) {
                        fprintf(stderr, "Error: variable '%s' not found.\n", arg->id);
                        return;
                    }
                    if (v->vtype == VAL_STRING) {
                        vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                      //  fprintf(stderr, "[DEBUG] Constructor arg var string val: %s\n", v->value.string_value);
                    } else if (v->vtype == VAL_FLOAT) {
                        char fbuf[64];
                        te_fmt_double(fbuf, sizeof(fbuf), v->value.float_value);
                        vn = create_ast_leaf("FLOAT", 0, fbuf, NULL);
                    } else {
                        vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                      //  fprintf(stderr, "[DEBUG] Constructor arg var int val: %d\n", v->value.int_value);
                    }
                } else {
                    int val = evaluate_expression(arg);
                    vn = create_ast_leaf_number("INT", val, NULL, NULL);
                  //  fprintf(stderr, "[DEBUG] Constructor arg expr int val: %d\n", val);
                }
                add_or_update_variable(p->name, vn);
                if (g_debug_mode) fprintf(stderr, "[DEBUG] Constructor param '%s' set\n", p->name);
                p   = p->next;
                arg = arg->next; /* gotcha #1: step ctor args via ->next */
            }
            if (g_debug_mode) fprintf(stderr, "[DEBUG] Calling __constructor for class '%s'\n", var->value.object_value->class->name);
            call_method(var->value.object_value, "__constructor");
            if (g_debug_mode) fprintf(stderr, "[DEBUG] __constructor completed\n");
        }
    }
   // printf("[DEBUG] interpret_var_decl: done\n"); fflush(stdout);
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
    if (!var->type || strcmp(var->type, "OBJECT") != 0) {
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
    const char *declared = obj->attributes[idx].type; 

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
    if (value_node && value_node->type && strcmp(value_node->type, "NULL") == 0) {
        if (!decl_nullable) {
            int ln = node->line ? node->line : (access->line ? access->line : 0);
            const char *fname = g_script_path ? g_script_path : "<script>";
            fprintf(stderr,
                    "%s%s:%d: Error: cannot assign null to non-nullable attribute '%s' of type %s.%s\n",
                    TE_ERR_RED, fname, ln, attr_name, declared, TE_ERR_RESET);
            return;
        }
        obj->attributes[idx].vtype = VAL_OBJECT;
        obj->attributes[idx].value.object_value = NULL;
        return;
    }

    /* Validación de tipo: no permitir asignar un valor de tipo incompatible.
     * Se determina si el atributo declarado es de texto (string) o numérico,
     * y si el valor asignado es de texto o numérico. Un desajuste aborta. */
    {
        int decl_is_str = (strcmp(declared, "string") == 0 || strcmp(declared, "string?") == 0 ||
                           strcmp(declared, "uuid") == 0 || strcmp(declared, "uuid?") == 0 ||
                           strcmp(declared, "datetime") == 0 || strcmp(declared, "datetime?") == 0);
        /* val_kind: 0 = desconocido, 1 = string, 2 = numérico */
        int val_kind = 0;
        if (value_node->type) {
            if (strcmp(value_node->type, "STRING") == 0) {
                val_kind = 1;
            } else if (strcmp(value_node->type, "NUMBER") == 0 ||
                       strcmp(value_node->type, "INT") == 0 ||
                       strcmp(value_node->type, "FLOAT") == 0) {
                val_kind = 2;
            } else if (strcmp(value_node->type, "IDENTIFIER") == 0 ||
                       strcmp(value_node->type, "ID") == 0) {
                Variable *vv = find_variable(value_node->id ? value_node->id : value_node->str_value);
                if (vv) {
                    if (vv->vtype == VAL_STRING) val_kind = 1;
                    else if (vv->vtype == VAL_INT || vv->vtype == VAL_FLOAT) val_kind = 2;
                }
            }
        }
        if (val_kind != 0) {
            const char *val_tname = (val_kind == 1) ? "string" : "int";
            if ((decl_is_str && val_kind == 2) || (!decl_is_str && val_kind == 1)) {
                int ln = node->line ? node->line
                       : (value_node->line ? value_node->line
                       : (access->line ? access->line : 0));
                const char *fname = g_script_path ? g_script_path : "<script>";
                fprintf(stderr,
                        "%s%s:%d: Error: cannot assign %s to attribute '%s' of type %s.%s\n",
                        TE_ERR_RED, fname, ln, val_tname, attr_name, declared, TE_ERR_RESET);
                return;
            }
        }
    }

    if (strcmp(declared, "string") == 0 || strcmp(declared, "string?") == 0 ||
        strcmp(declared, "uuid") == 0 || strcmp(declared, "uuid?") == 0 ||
        strcmp(declared, "datetime") == 0 || strcmp(declared, "datetime?") == 0) {
        if (value_node->type && strcmp(value_node->type, "STRING") == 0) {
          obj->attributes[idx].value.string_value = strdup(value_node->str_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (STRING)\n", attr_name, value_node->str_value);
        }
        else if (value_node->type && (strcmp(value_node->type, "IDENTIFIER") == 0 || strcmp(value_node->type, "ID") == 0)) {
          Variable *v2 = find_variable(value_node->id ? value_node->id : value_node->str_value);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string or variable not found.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR)\n", attr_name, v2->value.string_value);
        }
        else if (value_node->id) {
          Variable *v2 = find_variable(value_node->id);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR ID)\n", attr_name, v2->value.string_value);
        }
        else if (strcmp(value_node->type, "CALL_FUNC") == 0) {
          interpret_ast(value_node);
          Variable *r = find_variable("__ret__");
          if (!r || r->vtype != VAL_STRING) {
            fprintf(stderr, "Error: function result is not a string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(r->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (CALL)\n", attr_name, r->value.string_value);
        }
        obj->attributes[idx].vtype = VAL_STRING;
      } else {
        double val = evaluate_expression(value_node);
        int decl_is_float = (strcmp(declared, "float") == 0 || strcmp(declared, "float?") == 0);
        if (decl_is_float) {
            obj->attributes[idx].value.float_value = val;
            obj->attributes[idx].vtype = VAL_FLOAT;
            if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %f (FLOAT)\n", attr_name, val);
        } else {
            obj->attributes[idx].value.int_value = (long long)val;
            obj->attributes[idx].vtype = VAL_INT;
            if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %d (INT)\n", attr_name, (int)val);
        }
    }
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
    while (value_node && value_node->type && strcmp(value_node->type, "TERNARY") == 0) {
        int cond = (evaluate_expression(value_node->left) != 0.0);
        value_node = cond ? value_node->right : value_node->extra;
    }
    if (!value_node) return;

    /* Fase 2 (perf): fast-path for "x = numeric_expr" with cached Variable*.
     * Hot in for/while loops. Skips strdup/find_variable_for/temp_node alloc. */
    {
        Variable *fv = (Variable *)var_node->cached_var;
        if (fv && (!fv->id || strcmp(fv->id, var_node->id) != 0)) {
            /* slot reciclado (reset/unwind): revalidar por id como NK_IDENTIFIER */
            fv = NULL;
            var_node->cached_var = NULL;
        }
        if (!fv) {
            fv = find_variable_for(var_node->id);
            if (fv) var_node->cached_var = fv;
        }
        if (fv && !fv->is_const && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)) {
            NodeKind vk = nk_of(value_node);
            if (vk == NK_ADD || vk == NK_SUB || vk == NK_MUL || vk == NK_DIV
                || vk == NK_NUMBER || vk == NK_INT || vk == NK_FLOAT
                || vk == NK_IDENTIFIER) {
                /* Avoid string-typed ADD (concat) which needs the slow path. */
                if (!(vk == NK_ADD && is_string_type(value_node))) {
                    long long i64v; double r;
                    if (te_eval_num(value_node, &i64v, &r)) {   /* Fase 1b: entero exacto */
                        fv->vtype = VAL_INT;
                        fv->value.int_value = i64v;
                    } else {
                        fv->vtype = VAL_FLOAT;
                        fv->value.float_value = r;
                    }
                    return;
                }
            }
        }
    }

    // --- INICIO DE LA CORRECCIÓN ---

    // ¿Es una llamada a función (como concat)?
    if (strcmp(value_node->type, "CALL_FUNC") == 0 || strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0) {
        
        // 1. Ejecutar la función (ej. concat)
        interpret_ast(value_node);

        /* ============================================================
         * Ola 3 Fase B: FAST READ of __ret_var when target var is
         * already int/float. Skip create_ast_leaf*+add_or_update+free.
         * Switch: TYPEEASY_NO_FASTRET=1 (shared with FASTRET path)
         * ============================================================ */
        {
            static int fr_init = 0;
            static int fr_enabled = 1;
            if (!fr_init) {
                const char *e = getenv("TYPEEASY_NO_FASTRET");
                if (e && e[0] && e[0] != '0') fr_enabled = 0;
                fr_init = 1;
            }
            if (fr_enabled && __ret_var_active
                && (__ret_var.vtype == VAL_INT || __ret_var.vtype == VAL_FLOAT)) {
                Variable *fv = (Variable *)var_node->cached_var;
                if (fv && (!fv->id || strcmp(fv->id, var_node->id) != 0)) {
                    /* slot reciclado: revalidar por id */
                    fv = NULL;
                    var_node->cached_var = NULL;
                }
                if (!fv) {
                    fv = find_variable_for(var_node->id);
                    if (fv) var_node->cached_var = fv;
                }
                if (fv && !fv->is_const
                    && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)) {
                    if (__ret_var.vtype == VAL_FLOAT) {
                        fv->vtype = VAL_FLOAT;
                        fv->value.float_value = __ret_var.value.float_value;
                    } else {
                        fv->vtype = VAL_INT;
                        fv->value.int_value = __ret_var.value.int_value;
                    }
                    /* Reset return state but DO NOT free __ret_var fields:
                     * leaving them avoids strdup/free churn. */
                    vm->return_flag = 0;
                    vm->return_node = NULL;
                    return;
                }
            }
        }

        // 2. Obtener el resultado de __ret_var
        Variable *ret_val = find_variable("__ret__");
        if (!ret_val) {
            printf("Error: function in assignment returned nothing.\n");
            return;
        }

        // 3. Crear un nodo temporal para el valor
        ASTNode *temp_node = NULL;
        if (ret_val->vtype == VAL_STRING) {
            /* create_ast_leaf interna/copia el string: pasar el puntero directo.
             * El strdup() previo quedaba huerfano (fuga por cada `var x=func()`). */
            temp_node = create_ast_leaf("STRING", 0, ret_val->value.string_value, NULL);
        } else if (ret_val->vtype == VAL_INT) {
            temp_node = create_ast_leaf_number("INT", ret_val->value.int_value, NULL, NULL);
        } else if (ret_val->vtype == VAL_FLOAT) {
            /* double_to_string() devuelve malloc; create_ast_leaf copia -> liberar. */
            char *fs = double_to_string(ret_val->value.float_value);
            temp_node = create_ast_leaf("FLOAT", 0, fs, NULL);
            free(fs);
        } else if (ret_val->vtype == VAL_OBJECT) {
            /* Fix var-reassign-a-nativo (UAF/lectura vacia): un objeto NATIVO
             * (json_parse / envelope de db_exec = MAP, o LIST/LAMBDA/OBJECT)
             * guarda su arbol REAL en object_value; los lectores (map_find_pair)
             * esperan ese nodo directo. El path viejo lo envolvia en
             * temp_node->extra y hacia free_ast(temp_node): te_value_to_variable
             * para MAP/LIST guarda el WRAPPER (cuyo ->left es NULL) como
             * object_value -> lectura vacia; y liberar el wrapper dejaba la var
             * colgando -> SIGSEGV en la siguiente lectura de campo
             * (`var x={..}; x=json_parse(..); x.success`). Aliasamos object_value
             * directo, igual que interpret_var_decl en la 1a ligadura: sin
             * wrapper, sin free (el arbol lo libera el cleanup de request/programa). */
            Variable *dv = (Variable *)var_node->cached_var;
            if (dv && (!dv->id || strcmp(dv->id, var_node->id) != 0)) {
                /* slot reciclado: revalidar por id */
                dv = NULL;
                var_node->cached_var = NULL;
            }
            if (!dv) {
                dv = find_variable_for(var_node->id);
                if (dv) var_node->cached_var = dv;
            }
            if (dv) {
                if (dv->is_const)
                    te_runtime_fatalf("Error: cannot assign to constant variable '%s'.", var_node->id);
                if (dv->vtype == VAL_STRING && dv->value.string_value) free(dv->value.string_value);
                free(dv->type);
                dv->vtype = VAL_OBJECT;
                dv->type = strdup(ret_val->type ? ret_val->type : "OBJECT");
                dv->value.object_value = ret_val->value.object_value;
            } else if (vm->var_count < MAX_VARS) {
                vm->vars[vm->var_count].id = strdup(var_node->id);
                vm->vars[vm->var_count].is_const = 0;
                vm->vars[vm->var_count].vtype = VAL_OBJECT;
                vm->vars[vm->var_count].type = strdup(ret_val->type ? ret_val->type : "OBJECT");
                vm->vars[vm->var_count].value.object_value = ret_val->value.object_value;
                te_sym_insert(vm->vars[vm->var_count].id, vm->var_count);
                vm->var_count++;
            }
            /* temp_node queda NULL: se salta el bloque wrapper+free de abajo. */
        }
        
        if (temp_node) {
            // 4. Asignar el valor
            add_or_update_variable(var_node->id, temp_node);
            // 5. Limpiar el nodo temporal
            free_ast(temp_node);
        }

        // 6. Limpiar __ret_var
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
            // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
        }
        vm->return_flag = 0;
        vm->return_node = NULL;
    }
    // ¿Es un acceso a atributo (como intencion.item)?
    else if (strcmp(value_node->type, "ACCESS_ATTR") == 0) {
        // Esta lógica ya la escribimos para declare_variable, la usamos aquí
        ASTNode *o = value_node->left, *a = value_node->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
             printf("Error: object '%s' not found for assignment.\n", o->id);
             return;
        }
        
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
                if (!te_attr_access_ok(obj->class, i, o)) return;
                // Encontramos el atributo. Creamos un nodo temporal y lo asignamos.
                ASTNode* temp_node = NULL;
                if (obj->attributes[i].vtype == VAL_STRING) {
                    temp_node = create_ast_leaf("STRING", 0, strdup(obj->attributes[i].value.string_value), NULL);
                } else if (obj->attributes[i].vtype == VAL_INT) {
                    temp_node = create_ast_leaf_number("INT", obj->attributes[i].value.int_value, NULL, NULL);
                } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                    temp_node = create_ast_leaf("FLOAT", 0, double_to_string(obj->attributes[i].value.float_value), NULL);
                }
                
                if(temp_node) {
                    add_or_update_variable(var_node->id, temp_node);
                    free_ast(temp_node);
                }
                return; // ¡Asignación completada!
            }
        }
        fprintf(stderr, "Error: attribute '%s' not found in '%s'.\n", a->id, o->id);
        return;
    }
    // ¿Es una expresión matemática?
    else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0 || strcmp(value_node->type, "MOD") == 0 || strcmp(value_node->type, "NEG") == 0 || strcmp(value_node->type, "BIT_AND") == 0 || strcmp(value_node->type, "BIT_OR") == 0 || strcmp(value_node->type, "BIT_XOR") == 0 || strcmp(value_node->type, "BIT_NOT") == 0 || strcmp(value_node->type, "SHL") == 0 || strcmp(value_node->type, "SHR") == 0 || strcmp(value_node->type, "IN") == 0) {
        if (strcmp(value_node->type, "ADD") == 0 && is_string_type(value_node)) {
            char *s = get_node_string(value_node);
            ASTNode* temp_node = create_ast_leaf("STRING", 0, s, NULL);
            add_or_update_variable(var_node->id, temp_node);
            free_ast(temp_node);
            return;
        }
        long long i64v; double result;
        ASTNode* temp_node = NULL;
        if (te_eval_num(value_node, &i64v, &result)) {   /* Fase 1b */
            temp_node = create_ast_leaf_number("INT", i64v, NULL, NULL);
        } else {
            char* str_res = double_to_string(result);
            temp_node = create_ast_leaf("FLOAT", 0, str_res, NULL);
            free(str_res);
        }
        add_or_update_variable(var_node->id, temp_node);
        free_ast(temp_node);
    } 
    /* Operadores de comparación / lógicos producen un booleano (0|1). Espejo de
     * la rama homónima de declare_variable: una ASIGNACIÓN `x = (a == b)` (donde
     * x ya existe) DEBE evaluar el operador. Sin esta rama estos node types
     * caían al `else` final -> add_or_update_variable guardaba el nodo crudo vía
     * te_value_to_variable, cuyo catch-all almacena value->value (0), perdiendo
     * SIEMPRE el resultado real (p.ej. `coincide = (clave == esperada)` daba
     * false aun con strings iguales). */
    else if (strcmp(value_node->type, "GT") == 0 || strcmp(value_node->type, "LT") == 0 ||
             strcmp(value_node->type, "EQ") == 0 || strcmp(value_node->type, "GT_EQ") == 0 ||
             strcmp(value_node->type, "LT_EQ") == 0 || strcmp(value_node->type, "DIFF") == 0 ||
             strcmp(value_node->type, "AND") == 0 || strcmp(value_node->type, "OR") == 0 ||
             strcmp(value_node->type, "NOT") == 0) {
        int b = evaluate_expression(value_node) != 0 ? 1 : 0;
        ASTNode *temp_node = create_ast_leaf_number("BOOL", b, NULL, NULL);
        add_or_update_variable(var_node->id, temp_node);
        free_ast(temp_node);
    }
    /* Índice / clave `x = arr[i]` / `x = m["k"]` sobre variable existente.
     * resolve_access_item devuelve el nodo-elemento tipado (STRING/INT/FLOAT/
     * BOOL/OBJECT/LIST/MAP); add_or_update_variable lo copia (escalares) o
     * aliasa (contenedores) vía te_value_to_variable. Sin esta rama el
     * ACCESS_EXPR caía al catch-all y se guardaba 0 (p.ej. `y = arr[1]` -> 0). */
    else if (strcmp(value_node->type, "ACCESS_EXPR") == 0) {
        ASTNode *item = resolve_access_item(value_node);
        if (item) {
            add_or_update_variable(var_node->id, item);
        } else {
            ASTNode *nn = create_ast_leaf("NULL", 0, NULL, NULL);
            add_or_update_variable(var_node->id, nn);
            free_ast(nn);
        }
    }
    /* `target = sourceVar;` — copiar el VALOR de otra variable. Sin esta rama
     * un identificador desnudo caía al `else` final y se pasaba crudo a
     * add_or_update_variable, cuyo te_value_to_variable NO tiene caso
     * IDENTIFIER → catch-all lo guardaba como INT 0, perdiendo el valor
     * (p.ej. `msg = x` dentro de un for-in dejaba msg en 0). Espeja la rama
     * de copia-por-identificador de declare_variable: escalares vía nodo
     * temporal; LIST/MAP/OBJECT/LAMBDA/NULL se aliasan por referencia (sin
     * deep-copy, igual que el resto del intérprete). */
    else if (strcmp(value_node->type, "IDENTIFIER") == 0 || strcmp(value_node->type, "ID") == 0) {
        Variable *src = find_variable(value_node->id);
        if (!src) {
            fprintf(stderr, "Error: variable '%s' not found.\n",
                    value_node->id ? value_node->id : "?");
            return;
        }
        if (src->vtype == VAL_STRING) {
            ASTNode *tn = create_ast_leaf("STRING", 0,
                src->value.string_value ? src->value.string_value : "", NULL);
            add_or_update_variable(var_node->id, tn);
            free_ast(tn);
        } else if (src->vtype == VAL_INT) {
            ASTNode *tn = create_ast_leaf_number("INT", src->value.int_value, NULL, NULL);
            add_or_update_variable(var_node->id, tn);
            free_ast(tn);
        } else if (src->vtype == VAL_FLOAT) {
            char *fs = double_to_string(src->value.float_value);
            ASTNode *tn = create_ast_leaf("FLOAT", 0, fs, NULL);
            free(fs);
            add_or_update_variable(var_node->id, tn);
            free_ast(tn);
        } else {
            /* VAL_OBJECT: LIST/MAP/OBJECT/LAMBDA/LAZY_ITER/NULL. value.object_value
             * es la referencia compartida (ObjectNode* para OBJECT; ASTNode* del
             * LIST/MAP/... en el resto). Se copia el contenedor por referencia,
             * exactamente como declare_variable. */
            Variable *dst = find_variable_for(var_node->id);
            if (!dst) {
                ASTNode *nn = create_ast_leaf("NULL", 0, NULL, NULL);
                add_or_update_variable(var_node->id, nn);
                free_ast(nn);
                dst = find_variable_for(var_node->id);
            }
            if (dst) {
                if (dst->is_const) {
                    te_runtime_fatalf("Error: cannot assign to constant variable '%s'.", var_node->id);
                    return;
                }
                if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
                free(dst->type);
                dst->vtype = src->vtype;
                dst->type = strdup(src->type ? src->type : "NULL");
                dst->value.object_value = src->value.object_value; /* alias */
            }
        }
    }
    // Es un valor simple (literal, variable)
    else {
        /* Gotcha 30c: `arr = []` must bind a fresh instance, like declare_variable. */
        if (strcmp(value_node->type, "LIST") == 0 && value_node->value != 1)
            add_or_update_variable(var_node->id, te_list_literal_instance(value_node));
        else
            add_or_update_variable(var_node->id, value_node);
    }
    // --- FIN DE LA CORRECCIÓN ---
}
