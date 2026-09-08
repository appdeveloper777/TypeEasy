/* te_interp_flow.c — Control de flujo: for clásico / rango, for estilo Java, for-in, if (extraído de ast.c, Fase 2).
 * Movimiento puro (sin cambios semánticos). Dependencias compartidas: ast_internal.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ast.h"
#include "ast_internal.h"

void interpret_for_in(ASTNode *node) {
    if (!node->right) {
        /* for-in empty body (debug log removed) */
        return;
    }
    /* debug print removed */
    ASTNode *list_expr = node->left;
    ASTNode *listNode = NULL;
    if (list_expr->type && (
        strcmp(list_expr->type, "ID") == 0 ||
        strcmp(list_expr->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(list_expr->id);
        if (!v) {
            /* variable not found (debug log removed) */
            return;
        }       
        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            /* null / "" iterate zero times (json_parse("") yields ""). Anything
             * else is a real mistake: say WHAT it is. The ERP case was a
             * db_query error envelope `{"error":...}` being for-in'd. */
            int silent = (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "NULL") == 0) ||
                         (v->vtype == VAL_STRING && (!v->value.string_value || !v->value.string_value[0]));
            if (!silent) {
                char loc[512]; te_runtime_location(loc, sizeof(loc));
                const char *tn = v->vtype == VAL_STRING ? "a string" : v->vtype == VAL_INT ? "a number" :
                                 v->vtype == VAL_FLOAT ? "a number" : (v->type ? v->type : "an object");
                char *detail = NULL;
                if (v->vtype == VAL_OBJECT && v->type && v->value.object_value &&
                    (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0))
                    detail = te_map_node_to_string((ASTNode *)(intptr_t)v->value.object_value);
                else if (v->vtype == VAL_STRING) detail = strdup(v->value.string_value);
                if (detail && strlen(detail) > 160) { detail[157] = '.'; detail[158] = '.'; detail[159] = '.'; detail[160] = 0; }
                fprintf(stderr, "Error: for-in over '%s': it is %s, not a list%s%s%s\n%s\n",
                        list_expr->id, tn, detail ? " (" : "", detail ? detail : "", detail ? ")" : "", loc);
                free(detail);
            }
            return;
        }
        listNode = (ASTNode *)(intptr_t)v->value.object_value;
    }
    else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        listNode = list_expr;
    }
    else {
        /* Gotcha #2: expresión inline que produce una lista (p.ej. una
         * llamada a método/función LINQ como `a.union(b)` o `nums.filter(...)`).
         * Antes esto fallaba con "unsupported for-in expression" y obligaba a
         * un `let tmp = ...;` previo. Ahora evaluamos la expresión y leemos el
         * resultado capturado en __ret__; si es una LIST, iteramos sobre ella.
         * Es seguro: al reasignarse __ret__ dentro del cuerpo NO se libera el
         * object_value previo (ver add_or_update_variable), así que el listNode
         * capturado aquí sigue válido durante todo el bucle. */
        interpret_ast(list_expr);
        if (throw_flag || return_flag) return;
        Variable *r = find_variable("__ret__");
        if (r && r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "LIST") == 0) {
            listNode = (ASTNode *)(intptr_t)r->value.object_value;
        } else {
            printf("Error: unsupported for-in expression (type: %s).\n", list_expr->type);
            return;
        }
    }
    if (!listNode || strcmp(listNode->type, "LIST") != 0) {
        printf("Error: node is not a valid list.\n");
        return;
    }
    ASTNode *items = listNode->left;   
    /* Block scope: snapshot the variable count so each iteration's body-local
     * `let`s are reclaimed before the next pass (see te_scope_unwind_to). The
     * loop binding lives at/above this mark and is re-bound every iteration;
     * the final iteration's locals stay live after the loop, preserving the
     * pre-existing "value visible after the loop" behavior. */
    int te_loop_scope_mark = var_count;
    for (ASTNode *item = items; item; item = item->next) {
        debugger_on_loop_iteration();
        te_scope_unwind_to(te_loop_scope_mark);
        if (item->type && strcmp(item->type, "OBJECT") == 0) {
            // Get ObjectNode from extra field (where create_object_with_args stores it)
            // For backward compatibility, also check value field for objects created differently
            ObjectNode *obj = (ObjectNode *)item->extra;
            if (!obj) {
                // Fallback for objects that might store pointer in value field
                obj = (ObjectNode *)(intptr_t)item->value;
            }
            if (!list_expr->id || strcmp(node->id, list_expr->id) != 0) {
                ASTNode *wrapper = calloc(1, sizeof(ASTNode));
                wrapper->type = strdup("OBJECT");
                wrapper->id = strdup(node->id);
                wrapper->left = wrapper->right = NULL;
                /* Store pointer in 'extra' to be consistent with create_object_with_args()/declare_variable */
                wrapper->extra = (struct ASTNode*)obj;
                wrapper->value = 0;
                /* debug print removed */
                add_or_update_variable(node->id, wrapper);
            } 
        } else {
            /* Gotcha #6: items escalares (NUMBER/STRING/FLOAT/...) — ligar la
             * variable del bucle y ejecutar el cuerpo igual que para objetos.
             * Antes el cuerpo SOLO corría dentro del branch OBJECT, así que
             * `for x in nums { total = total + x }` se saltaba todas las
             * iteraciones y el acumulador externo nunca se actualizaba. */
            add_or_update_variable(node->id, item);
        }
        /* debug print removed */
        interpret_ast(node->right);
        if (break_flag) { break_flag = 0; break; }
        if (continue_flag) { continue_flag = 0; continue; }
        if (throw_flag || return_flag) break;
    }
}
void interpret_if(ASTNode *node) {
    /* v0.0.30: recorrer la cadena `if / else if / ... / else` de forma ITERATIVA.
     * Cada rama `else if` es otro nodo IF encadenado por ->next (create_if_node);
     * el `interpret_ast(node->next)` recursivo hacía profundidad == nº de ramas ->
     * mismo riesgo de stack overflow que interpret_statement_list en cadenas
     * grandes. Semántica idéntica al original. */
    while (node) {
        if (!node->left) return;
        if (evaluate_condition(node->left)) {
            interpret_ast(node->right);          /* rama then */
            return;
        }
        ASTNode *els = node->next;               /* else / else-if / NULL */
        if (els && nk_of(els) == NK_IF) {
            node = els;                          /* else if: iterar sin recursar */
        } else {
            interpret_ast(els);                  /* else final (bloque) o NULL: no-op */
            return;
        }
    }
}
void interpret_for(ASTNode *node) {
    /* Seed the control variable. node->left is the INIT: a NUMBER literal in the
     * classic `for(i=0; ...)` form, or an arbitrary expression in the literal-free
     * `for(START; STOP; STEP)` / `for(START, STOP, STEP)` forms. evaluate_expression
     * collapses both to an int; we store via a stack INT node (no per-entry alloc). */
    {
        ASTNode seed;
        memset(&seed, 0, sizeof(seed));
        seed.type = "INT";
        seed.kind = NK_NUMBER;
        seed.value = (long long)evaluate_expression(node->left);
        add_or_update_variable(node->id, &seed);
    }
    Variable *var = find_variable(node->id);
    if (!var || var->vtype != VAL_INT) {
        printf("Error: invalid control variable in FOR.\n");
        return;
    }

    /* Ola 1 (perf): try compiled-bytecode FOR. Fall back to AST walker if any
     * statement in the body is not bytecode-compilable. */
    {
        static int bc_for_init = 0;
        static int bc_for_enabled = 1;
        if (!bc_for_init) {
            const char *e = getenv("TYPEEASY_NO_BC");
            if (e && e[0] && e[0] != '0') bc_for_enabled = 0;
            bc_for_init = 1;
        }
        if (bc_for_enabled && !g_debug_enabled) {
            BCInfo *info = bc_get_or_compile_stmt(node);
            if (info) { bc_exec(info->code); return; }
        }
    }

    /* Evaluate step and limit as expressions (not raw node->value): this lets
     * the limit/step be variables or arithmetic, not just NUMBER literals.
     * Both are evaluated once, before the loop, matching "for to N" semantics. */
    int incremento = (int)evaluate_expression(node->right->right->left);
    int limite = (int)evaluate_expression(node->right);
    ASTNode *body = node->right->right->right;
    if (!body) {
        printf("Warning: FOR without body\n");
        return;
    }
    /* Block scope: snapshot AFTER the control variable is seeded (it lives
     * below this mark) so per-iteration body `let`s reuse one slot instead of
     * accumulating against MAX_VARS. */
    int te_loop_scope_mark = var_count;
    while (var->value.int_value < limite) {
        /* Interpret the whole body once per iteration. body is a
         * statement_list node; interpret_statement_list walks every statement
         * via ->left/->right recursion. (Manually chaining stmt=stmt->right
         * here double-executed the last statement.) */
        debugger_on_loop_iteration();
        te_scope_unwind_to(te_loop_scope_mark);
        interpret_ast(body);
        if (break_flag) { break_flag = 0; break; }
        if (continue_flag) { continue_flag = 0; }
        if (throw_flag || return_flag) break;
        var->value.int_value += incremento;
    }
}
void interpret_for_c(ASTNode *node) {
    ASTNode *fb = (ASTNode *)node->extra;
    ASTNode *update = fb ? fb->left : NULL;
    ASTNode *body = fb ? fb->right : NULL;
    int loop_scope_mark = var_count;             /* INIT's var lives above this mark */
    if (node->left) interpret_ast(node->left);
    int body_scope_mark = var_count;
    while (1) {
        if (throw_flag || return_flag) break;
        if (node->right && !evaluate_condition(node->right)) break;
        debugger_on_loop_iteration();
        te_scope_unwind_to(body_scope_mark);
        if (body) interpret_ast(body);
        if (break_flag) { break_flag = 0; break; }
        if (continue_flag) { continue_flag = 0; }
        if (throw_flag || return_flag) break;
        if (update) interpret_ast(update);
    }
    te_scope_unwind_to(loop_scope_mark);
}
