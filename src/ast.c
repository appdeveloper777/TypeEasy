#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bytecode.h"

/* Allow runtime debug mode controlled by TYPEEASY_DEBUG env var (set in main) */
extern int g_debug_mode;

/* --- ELIMINADOS: g_runtime y los prototipos del servidor --- */

// ====================== CONSTANTES Y ESTRUCTURAS GLOBALES ======================
#define MAX_CLASSES 50
#define MAX_VARS 100
static int g_initial_var_count = 0;
// Variables globales
Variable vars[MAX_VARS];
ClassNode *classes[MAX_CLASSES];
Variable __ret_var; // Global variable for return values
int __ret_var_active = 0; // Flag to indicate if __ret_var holds a valid value
int var_count = 0;
int class_count = 0;

// Estado de retorno
static int return_flag = 0;
static ASTNode *return_node = NULL;

// --- INICIO MEJORA: Punteros a los manejadores de bridges ---
static BridgeHandlers g_bridge_handlers = {NULL, NULL, NULL};
void runtime_save_initial_var_count() {
    g_initial_var_count = var_count;
    printf("[TypeEasy] Estado inicial guardado. %d variables globales (bridges) retenidas.\n", g_initial_var_count);
}

void runtime_reset_vars_to_initial_state() {
    // Libera la memoria de todas las variables CREADAS DURANTE LA ÚLTIMA EJECUCIÓN
    // (es decir, todas las variables DESPUÉS de los bridges)
    for (int i = g_initial_var_count; i < var_count; i++) {
        if (vars[i].id) free(vars[i].id);
        if (vars[i].type) free(vars[i].type);
        if (vars[i].vtype == VAL_STRING && vars[i].value.string_value) {
            free(vars[i].value.string_value);
        }
        
        // ¡Importante! Si la variable es un Objeto (como 'intencion')
        // debemos liberar el objeto en sí (que está en 'extra')
        // PERO 'declare_variable'  y 'add_or_update_variable' 
        // copian el puntero, y los 'free_ast'  ya liberan los nodos.
        // No necesitamos liberar 'extra' aquí, solo el contenedor de la variable.
    }

    // Resetea el contador de variables a su estado "limpio"
    var_count = g_initial_var_count;

    // También limpia la variable de retorno global
    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 0;
    }
}
void runtime_register_bridge_handlers(BridgeHandlers handlers) {
    g_bridge_handlers.handle_chat_bridge = handlers.handle_chat_bridge;
    g_bridge_handlers.handle_nlu_bridge = handlers.handle_nlu_bridge;
    g_bridge_handlers.handle_api_bridge = handlers.handle_api_bridge;
}
// --- FIN MEJORA ---


/* --- ELIMINADAS: Las 5 funciones del Agente (webhook_handler, runtime_start_bridges, etc.) --- */
/* --- (Ahora viven en servidor_agent.c) --- */


// ====================== FUNCIONES AUXILIARES ======================

/**
 * Convierte un entero a string dinámico
 */
static char* int_to_string(int x) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", x);
    return strdup(buf);
}

/**
 * Convierte un double a string dinámico
 */
static char* double_to_string(double x) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%f", x);
    return strdup(buf);
}

/**
 * Obtiene representación string de cualquier ASTNode
 */
/* --- ELIMINADA LA PALABRA 'static' --- */
char* get_node_string(ASTNode *node) {
    if (!node) return strdup("");
 
    /* get_node_string debug log removed */

    if (strcmp(node->type, "STRING_LITERAL") == 0) {
        return strdup(node->str_value);
    }

    if (strcmp(node->type, "IDENTIFIER") == 0) {
        Variable *v = find_variable(node->id);
        if (!v) {
            printf("Error: Variable '%s' no encontrada.\n", node->id);
            return strdup("[variable no encontrada]");
        }
        if (v->vtype == VAL_STRING) {
            return strdup(v->value.string_value);
        }
        if (v->vtype == VAL_INT) {
            return int_to_string(v->value.int_value);
        }
        if (v->vtype == VAL_FLOAT) {
            return double_to_string(v->value.float_value);
        }
        return strdup("[tipo de variable no imprimible]");
    }

    // Literal string
    if (strcmp(node->type, "STRING") == 0) {
        return strdup(node->str_value);
    }

    // Acceso a atributo (VERSIÓN CORREGIDA Y ROBUSTA)
    if (strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left, *a = node->right;
        Variable *v = find_variable(o->id); // Busca el objeto, ej: 'intencion'

        if (v && v->vtype == VAL_OBJECT) {
            ObjectNode *obj = v->value.object_value;

            /* debug: obj pointer log removed */

            // --- BÚSQUEDA ROBUSTA ---
            // Itera sobre los atributos de la INSTANCIA
            for (int i = 0; i < obj->class->attr_count; i++) {
                // Compara el nombre del atributo (ej: "tipo")
                if (strcmp(obj->attributes[i].id, a->id) == 0) { 
                    // Si el tipo es string
                    if (obj->attributes[i].vtype == VAL_STRING) {
                        // Devuelve el VALOR de la INSTANCIA
                        return strdup(obj->attributes[i].value.string_value);
                    } else {
                        // Devuelve el VALOR (int) de la INSTANCIA como string
                        return int_to_string(obj->attributes[i].value.int_value);
                    }
                }
            }
            // --- FIN BÚSQUEDA ROBUSTA ---
        }
        // Si no se encuentra el objeto o el atributo, devuelve esto
        return strdup("[attr not found]");
    }

    // Nodo ADD (sin cambios)
    if (strcmp(node->type, "ADD") == 0) {
        // ... (código sin cambios) ...
    char *s1 = get_node_string(node->left);
    char *s2 = get_node_string(node->right);
    size_t len = strlen(s1) + strlen(s2) + 1;
    char *res = malloc(len);
    strcpy(res, s1);
    strcat(res, s2);
    free(s1);
    free(s2);
    return res;
    }

    // Expresión numérica (sin cambios)
double v = evaluate_expression(node);
return double_to_string(v);
}

// ====================== MANEJO DE CLASES Y OBJETOS ======================

ClassNode *create_class(char *name) {
    ClassNode *class_node = (ClassNode *)malloc(sizeof(ClassNode));
    class_node->name = strdup(name);
    class_node->attributes = NULL;
    class_node->attr_count = 0;
    class_node->methods = NULL;
    class_node->next = NULL;
    return class_node;
}

void add_class(ClassNode *class) {
    if (class_count < MAX_CLASSES) {
        classes[class_count++] = class;
    }
}

ClassNode *find_class(char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i]->name, name) == 0) {
            return classes[i];
        }
    }
    printf("Error: Clase '%s' no encontrada.\n", name);
    return NULL;
}

void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type) {
    if (!class) return;
    
    class->attributes = realloc(class->attributes, (class->attr_count + 1) * sizeof(Variable));
    class->attributes[class->attr_count].id = strdup(attr_name);
    class->attributes[class->attr_count].type = strdup(attr_type);
    class->attr_count++;
}

void add_method_to_class(ClassNode *cls, char *method, ParameterNode *params, ASTNode *body) {
    MethodNode *m = malloc(sizeof(MethodNode));
    if (!m) exit(1);
    m->name = strdup(method);
    m->params = params;
    m->body = body;
    m->next = cls->methods;
    cls->methods = m;
}
ObjectNode* clone_object(ObjectNode *original);

void add_constructor_to_class(ClassNode *class, ParameterNode *params, ASTNode *body) {
    MethodNode *ctor = malloc(sizeof(MethodNode));
    if (!ctor) exit(1);
    ctor->name = strdup("__constructor");
    ctor->params = params;
    ctor->body = body;
    ctor->next = class->methods;
    class->methods = ctor;
}

ObjectNode *create_object(ClassNode *class) {
    ObjectNode *obj = (ObjectNode *)malloc(sizeof(ObjectNode));
    obj->class = class;
    obj->attributes = malloc(class->attr_count * sizeof(Variable));
    for (int i = 0; i < class->attr_count; i++) {
        obj->attributes[i].id    = strdup(class->attributes[i].id);
        obj->attributes[i].type  = strdup(class->attributes[i].type);
        // Inicialización por defecto (ej. int 0)
        if (strcmp(class->attributes[i].type, "string") == 0) {
            obj->attributes[i].vtype = VAL_STRING;
            obj->attributes[i].value.string_value = strdup("");
        } else if (strcmp(class->attributes[i].type, "float") == 0) {
            obj->attributes[i].vtype = VAL_FLOAT;
            obj->attributes[i].value.float_value = 0.0;
        } else {
            obj->attributes[i].vtype = VAL_INT;
            obj->attributes[i].value.int_value = 0;
        }
    }
    
    return obj;
}

// ====================== MANEJO DE VARIABLES ======================

Variable *find_variable(char *id) {    
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    for (int i = 0; i < var_count; i++) {
        if (vars[i].id) {           
            if (strcmp(vars[i].id, id) == 0) {                
                return &vars[i];
            }
        }
    }
    // Comentado para reducir ruido, pero es útil para debug:
    // printf("El ID '%s' no fue encontrado.\n", id); 
    return NULL;
}

ASTNode *create_agent_node(char *name, ASTNode *body) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para AGENT node.\n");
        exit(1);
    }
    node->type = strdup("AGENT");
    node->id = strdup(name);
    node->left = body; // 'body' es la lista de listeners
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_listener_node(ASTNode *event_expr, ASTNode *body) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para LISTENER node.\n");
        exit(1);
    }
    node->type = strdup("LISTENER");
    node->left = event_expr; // La expresión p.ej. Chat.onMessage(mensaje)
    node->right = body;     // El bloque de código
    node->id = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

Variable *find_variable_for(char *id) {
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}

void declare_variable(char *id, ASTNode *value, int is_const) {
    if (var_count >= MAX_VARS) {
        printf("Error: Demasiadas variables declaradas.\n");
        return;
    }

    int my_index = var_count;
    var_count++;

    vars[my_index].id = strdup(id);
    vars[my_index].is_const = is_const;

    // Aseguramos que el tipo siempre sea una copia dinámica para poder liberarlo después
    if (strcmp(value->type, "NUMBER") == 0) {
        vars[my_index].type = strdup("INT");
    } else if (strcmp(value->type, "STRING_LITERAL") == 0) {
        vars[my_index].type = strdup("STRING");
    } else if (strcmp(value->type, "FLOAT") == 0) {
        vars[my_index].type = strdup("FLOAT");
    } else {
        vars[my_index].type = strdup(value->type);
    }

   if (strcmp(value->type, "STRING") == 0 || strcmp(value->type, "STRING_LITERAL") == 0) {
        vars[my_index].vtype = VAL_STRING;
        vars[my_index].value.string_value = strdup(value->str_value);
    }
    else if (strcmp(value->type, "LIST") == 0) {
        /* debug print removed */
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (void *)(intptr_t)value;

        ASTNode *cur = value->left;
        int list_count = 0;
        while (cur) {
            list_count++;
            /* debug print removed */
            if (strcmp(cur->type, "OBJECT") != 0) {
                return;
            }

            /* Read the original Object pointer from either 'extra' (preferred)
               or 'value' (legacy). Then clone and store the cloned object in
               both fields so future code can find it. */
            ObjectNode *obj_original = NULL;
            if (cur->extra) obj_original = (ObjectNode *)(cur->extra);
            else obj_original = (ObjectNode *)(intptr_t)cur->value;
            ObjectNode *obj_clonado = clone_object(obj_original);
            ASTNode *arg = cur->left;
            cur->value = (int)(intptr_t)obj_clonado;
            cur->extra = (struct ASTNode*)obj_clonado;
            MethodNode *m = obj_clonado->class->methods;
            while (m && strcmp(m->name, "__constructor") != 0) {
                m = m->next;
            }

            if (m) {
                ParameterNode *p = m->params;
                while (p && arg) {
                    ASTNode *vn = NULL;
                    if (arg->type && strcmp(arg->type, "STRING") == 0) {
                        vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    } 
                    else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                        Variable *v = find_variable(arg->id);
                        if (!v) {
                            printf("Error: Variable '%s' no encontrada.\n", arg->id);
                            return;
                        }
                        if (v->vtype == VAL_STRING) {
                            vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                        } else {
                            vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                        }
                    } 
                    else {
                        int val = evaluate_expression(arg);
                        vn = create_ast_leaf_number("INT", val, NULL, NULL);
                    }
                    add_or_update_variable(p->name, vn);
                    p = p->next;
                    arg = arg->right;
                }
                call_method(obj_clonado, "__constructor");
                /* Ensure constructor side-effects (like return_flag) don't block later AST execution */
                return_flag = 0;
                return_node = NULL;
                /* debug print removed */
            }
            cur->left = NULL;
            /* debug print removed */
            cur = cur->next;
        }
    /* debug print removed */
    }
    else if (strcmp(value->type, "FLOAT") == 0) {
        vars[my_index].vtype = VAL_FLOAT;
        vars[my_index].value.float_value = atof(value->str_value);
    }
    else if (strcmp(value->type, "ADD") == 0 || strcmp(value->type, "SUB") == 0 || 
             strcmp(value->type, "MUL") == 0 || strcmp(value->type, "DIV") == 0) {
        double result = evaluate_expression(value);
        if (result == (int)result) {
            vars[my_index].vtype = VAL_INT;
            vars[my_index].value.int_value = (int)result;
            vars[my_index].type = strdup("INT");
        } else {
            vars[my_index].vtype = VAL_FLOAT;
            vars[my_index].value.float_value = result;
            vars[my_index].type = strdup("FLOAT");
        }
    }
    else if (strcmp(value->type, "OBJECT") == 0) {
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (ObjectNode *)value->extra;
    }
    else if (strcmp(value->type, "ACCESS_ATTR") == 0) {
        // Esto maneja: let item = intencion.item;
        ASTNode *o = value->left, *a = value->right;
        Variable *v = find_variable(o->id);
        
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: Objeto '%s' no encontrado para asignación.\n", o->id);
            vars[my_index].vtype = VAL_INT;
            vars[my_index].value.int_value = 0; // Valor de error
            return;
        }
        
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
                // Encontramos el atributo. Copiamos su valor.
                if (obj->attributes[i].vtype == VAL_STRING) {
                    vars[my_index].vtype = VAL_STRING;
                    vars[my_index].value.string_value = strdup(obj->attributes[i].value.string_value);
                } else if (obj->attributes[i].vtype == VAL_INT) {
                    vars[my_index].vtype = VAL_INT;
                    vars[my_index].value.int_value = obj->attributes[i].value.int_value;
                } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                    vars[my_index].vtype = VAL_FLOAT;
                    vars[my_index].value.float_value = obj->attributes[i].value.float_value;
                }
                return; // ¡Asignación completada!
            }
        }
        
        printf("Error: Atributo '%s' no encontrado en '%s'.\n", a->id, o->id);
        vars[my_index].vtype = VAL_INT;
        vars[my_index].value.int_value = 0; // Valor de error
        return;
    }
    else {
        vars[my_index].vtype = VAL_INT;
        vars[my_index].value.int_value = value->value;
    }
}


void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;

    if (strcmp(id, "__ret__") == 0) {
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
        }
        __ret_var.id = strdup(id);
        __ret_var.is_const = 0;
        if (strcmp(value->type, "STRING") == 0) {
            __ret_var.vtype = VAL_STRING;
            __ret_var.type = strdup("STRING");
            __ret_var.value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("OBJECT");
            __ret_var.value.object_value = (ObjectNode *)value->extra;
        } else if (strcmp(value->type, "FLOAT") == 0) {
            __ret_var.vtype = VAL_FLOAT;
            __ret_var.type = strdup("FLOAT");
            __ret_var.value.float_value = atof(value->str_value);
        } else {
            __ret_var.vtype = VAL_INT;
            __ret_var.type = strdup("INT");
            __ret_var.value.int_value = value->value;
        }
        __ret_var_active = 1;
        return;
    }

    Variable *var = find_variable_for(id);
    if (var) {        
        if (var->is_const) {
            fprintf(stderr, "Error: No se puede asignar a la variable constante '%s'.\n", id);
            exit(1);
        }
        free(var->type);
        var->type = strdup(value->type);
        if (var->vtype == VAL_STRING && var->value.string_value) free(var->value.string_value);
        if (strcmp(value->type, "STRING") == 0) {
            var->vtype = VAL_STRING;
            var->value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            var->vtype = VAL_OBJECT;
            var->value.object_value = (ObjectNode *)value->extra;
        } else {
            var->vtype = VAL_INT;
            var->value.int_value = value->value;
        }
    } else {
        if (var_count >= MAX_VARS) {
            printf("Error: Demasiadas variables declaradas.\n");
            return;
        }
        vars[var_count].id = strdup(id);
        vars[var_count].type = strdup(value->type);
        vars[var_count].is_const = 0;
        if (strcmp(value->type, "STRING") == 0) {
            vars[var_count].vtype = VAL_STRING;
            vars[var_count].value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            vars[var_count].vtype = VAL_OBJECT;
            vars[var_count].value.object_value = (ObjectNode *)value->extra;
        } else {
            vars[var_count].vtype = VAL_INT;
            vars[var_count].value.int_value = value->value;
        }
        var_count++;
    }
}

// ====================== CREACIÓN DE NODOS AST ======================

ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para ASTNode.\n");
        exit(1);
    }
    node->type = strdup(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    node->str_value = str_value ? strdup(str_value) : NULL;
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->type = strdup(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    node->str_value = str_value ? strdup(str_value) : NULL;
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

   if (strcmp(type, "ADD") == 0) {
        if ((left->type && strcmp(left->type, "STRING") == 0) ||
            (right->type && strcmp(right->type, "STRING") == 0)) {
            printf("Error: Suma de cadenas no permitida.\n");
            
            char *s1 = (left->type && strcmp(left->type, "STRING") == 0) ? 
                       left->str_value : int_to_string(left->value);
            char *s2 = (right->type && strcmp(right->type, "STRING") == 0) ? 
                       right->str_value : int_to_string(right->value);
            
            size_t len = strlen(s1) + strlen(s2) + 1;
            char *res = malloc(len);
            strcpy(res, s1);
            strcat(res, s2);
            
            if (s1 != left->str_value) free(s1);
            if (s2 != right->str_value) free(s2);
            
            return create_ast_leaf("STRING", 0, res, NULL);
        }
        node->value = left->value + right->value;
    } 
    else if (strcmp(type, "SUB") == 0) {
        node->value = left->value - right->value;
    } 
    else if (strcmp(type, "MUL") == 0) {
        node->value = left->value * right->value;
    } 
    else if (strcmp(type, "DIV") == 0) {
        if (right->value != 0) {
            node->value = left->value / right->value;
        } else {
            printf("Error: División por cero.\n");
            node->value = 0;
        }
    } 
    else {
        node->value = 0;
    }
    // ... (lógica de SUB, MUL, DIV sin cambios) ...
    
    return node;
}

ASTNode *create_int_node(int value) {
    return create_ast_leaf_number("INT", value, NULL, NULL);
}

ASTNode *create_float_node(int value) {
    return create_ast_leaf_number("FLOAT", value, NULL, NULL);
}

ASTNode *create_string_node(char *value) {
    return create_ast_leaf("STRING", 0, value, NULL);
}

ASTNode *create_identifier_node(const char* name) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = strdup("ID");
    node->id = strdup(name);
    node->left = NULL;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_var_decl_node(char *id, ASTNode *value) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para VAR_DECL node.\n");
        exit(1);
    }
    node->type = strdup("VAR_DECL");
    node->id = strdup(id);
    node->left = value;
    node->right = NULL;
    return node;
}

ASTNode *create_return_node(ASTNode *expr) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("RETURN");
    node->id = NULL;
    node->left = expr;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_function_call_node(const char *funcName, ASTNode *args) {
    ASTNode *n = malloc(sizeof(ASTNode));
    n->type = strdup("CALL_FUNC");
    n->id = strdup(funcName);
    n->left = args;
    n->right = NULL;
    n->str_value = NULL;
    n->value = 0;
    return n;
}

ASTNode *create_method_call_node(ASTNode *objectNode, const char *methodName, ASTNode *args) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("CALL_METHOD");
    node->id = strdup(methodName);
    node->left = objectNode;
    node->right = args;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_object_with_args(ClassNode *class, ASTNode *args) {
    if (!class) {
        printf("Error: Clase no encontrada.\n");
        return NULL;
    }

    ObjectNode *real_obj = create_object(class);
    ASTNode *obj = (ASTNode *)malloc(sizeof(ASTNode));
    obj->type = strdup("OBJECT");
    obj->left = args;
    obj->right = NULL;
    obj->id = strdup(class->name);
    // Store the actual ObjectNode pointer in 'extra' (used across the codebase)
    obj->extra = (struct ASTNode*)real_obj;
    obj->value = 0;
   
    return obj;
}

ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("FOR");
    node->id = var->id;
    node->left = init;
    node->right = condition;
    
    ASTNode *update_body = (ASTNode *)malloc(sizeof(ASTNode));
    update_body->type = strdup("FOR_BODY");
    update_body->left = update;
    update_body->right = body;
    
    node->right->right = update_body;
    return node;
}


ASTNode* create_layer_node(const char* layer_type, int units, const char* activation) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = strdup("LAYER");
    node->id = strdup(layer_type);
    node->value = units;
    node->str_value = strdup(activation);
    node->left = NULL;
    node->right = NULL;
    return node;
}

ASTNode* create_model_node(const char* name, ASTNode* layer_list) {
    ModelNode* m = malloc(sizeof(ModelNode));
    int count = 0;
    for (ASTNode* cur = layer_list; cur; cur = cur->right) count++;
    m->layer_count = count;
    m->layers = malloc(sizeof(LayerNode*) * count);

    ASTNode* cur = layer_list;
    for (int i = 0; i < count; i++, cur = cur->right) {
        LayerNode* ln = malloc(sizeof(LayerNode));
        ln->layer_type = strdup(cur->id);
        ln->units = cur->value;
        ln->activation = strdup(cur->str_value);
        m->layers[i] = ln;
    }

    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = strdup("OBJECT");
    node->id = strdup(name);
    node->value = (int)(intptr_t)m;
    node->left = NULL;
    node->right = NULL;
    return node;
}

ASTNode* create_dataset_node(const char* name, const char* path) {
    DatasetNode* ds = malloc(sizeof(DatasetNode));
    ds->count = 0;
    ds->inputs = NULL;
    ds->labels = NULL;

    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = strdup("DATASET");
    node->id = strdup(name);
    node->str_value = strdup(path);
    node->left = NULL;
    node->right = NULL;
    node->value = (int)(intptr_t)ds;
    return node;
}

ASTNode* create_train_node(const char* model_name,
    const char* data_name,
    ASTNode* options) {
    ASTNode* n = malloc(sizeof(ASTNode));
    n->type = strdup("TRAIN");
    n->id   = NULL;
    n->left = create_identifier_node(model_name);
    ASTNode* dataNode = create_identifier_node(data_name);
    dataNode->right = options;  
    n->right = dataNode;
    return n;
}


ASTNode* create_train_option_node(const char* key, int val) {
    ASTNode* n = malloc(sizeof(ASTNode));
    n->type = strdup("TRAIN_OPTION");
    n->id = strdup(key);
    n->value = val;
    n->left = n->right = NULL;
    return n;
}

ASTNode* create_predict_node(const char* model_name, const char* input_name) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = strdup("PREDICT");
    node->left = create_identifier_node(model_name);
    node->right = create_identifier_node(input_name);
    return node;
}

// ====================== CREACIÓN DE NODOS IF ======================

ASTNode* create_if_node(ASTNode* condition, ASTNode* if_branch, ASTNode* else_branch) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo if\n");
        exit(1);
    }
    node->type = strdup("IF");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = if_branch;
    node->next = else_branch;
    return node;
}

ASTNode* create_match_node(ASTNode* condition, ASTNode* case_list) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo match\n");
        exit(1);
    }
    node->type = strdup("MATCH");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = case_list;
    node->next = NULL;
    return node;
}

ASTNode* create_case_node(ASTNode* condition, ASTNode* body) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo case\n");
        exit(1);    }
    node->type = strdup("CASE");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = body;
    node->next = NULL;
    return node;
}

ASTNode* append_case_clause(ASTNode* list, ASTNode* case_clause) {
    if (!list) return case_clause;
    ASTNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = case_clause;
    return list;
}

// ====================== MANEJO DE LISTAS ======================

ASTNode *add_statement(ASTNode *list, ASTNode *stmt) {
    if (!list) return stmt;
    ASTNode *current = list;
    while (current->right) {
        current = current->right;
    }
    current->right = stmt;
    return list;
}

ASTNode *add_argument(ASTNode *list, ASTNode *expr) {
    if (!list) return expr;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = expr;
    return list;
}

ASTNode* append_layer_to_list(ASTNode* list, ASTNode* layer_node) {
    if (!list) return layer_node;
    ASTNode* cur = list;
    while (cur->right) cur = cur->right;
    cur->right = layer_node;
    return list;
}

ParameterNode *create_parameter_node(char *name, char *type) {
    ParameterNode *param = (ParameterNode *)malloc(sizeof(ParameterNode));
    param->name = strdup(name);
    param->type = strdup(type);
    param->next = NULL;
    return param;
}

ParameterNode *add_parameter(ParameterNode *list, char *name, char *type) {
    if (!list) return create_parameter_node(name, type);
    ParameterNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = create_parameter_node(name, type);
    return list;
}

// ====================== INTERPRETACIÓN DEL AST ======================

double evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    if (node->type && strcmp(node->type, "IDENTIFIER") == 0) {
        Variable *var = find_variable(node->id);
        if (var) {
            if (var->vtype == VAL_INT) {
                return var->value.int_value;
            } else if (var->vtype == VAL_FLOAT) {
                return var->value.float_value;
            } else if (var->vtype == VAL_STRING) {
                printf("Error: Variable '%s' es string, no se puede evaluar como número.\n", node->id);
                return 0;
            } else {
                printf("Error: Variable '%s' es un objeto, no se puede evaluar como número.\n", node->id);
                return 0;
            }
        } else {
            printf("Error: Variable '%s' no definida.\n", node->id);
            return 0;
        }
    }
    
    if (node->type && strcmp(node->type, "NUMBER") == 0) {
         return (double)node->value;
    }
    
    if (node->type && strcmp(node->type, "FLOAT") == 0) {
         return atof(node->str_value);
    }

    if (strcmp(node->type, "GT") == 0) {
        return evaluate_expression(node->left) > evaluate_expression(node->right);
    }
    if (strcmp(node->type, "LT") == 0) {
        return evaluate_expression(node->left) < evaluate_expression(node->right);
    }
    if (strcmp(node->type, "EQ") == 0) {
        return evaluate_expression(node->left) == evaluate_expression(node->right);
    }
    if (strcmp(node->type, "GT_EQ") == 0) {
        return evaluate_expression(node->left) <= evaluate_expression(node->right);
    }
    if (strcmp(node->type, "LT_EQ") == 0) {
        return evaluate_expression(node->left) >= evaluate_expression(node->right);
    }
    if (strcmp(node->type, "DIFF") == 0) {
        return evaluate_expression(node->left) != evaluate_expression(node->right);
    }
    
    if (strcmp(node->type, "ADD") == 0) {
        return evaluate_expression(node->left) + evaluate_expression(node->right);
    } else if (strcmp(node->type, "SUB") == 0) {
        return evaluate_expression(node->left) - evaluate_expression(node->right);
    } else if (strcmp(node->type, "MUL") == 0) {
        return evaluate_expression(node->left) * evaluate_expression(node->right);
    } else if (strcmp(node->type, "DIV") == 0) {
        double right = evaluate_expression(node->right);
        if (right == 0.0) {
            printf("Error: División por cero.\n");
            return 0;
        }
        return evaluate_expression(node->left) / right;
    }


    if (strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *objRef = node->left;
        ASTNode *attr   = node->right;
    
        Variable *v = find_variable(objRef->id);
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: '%s' no es un objeto válido para acceder atributo.\n", objRef->id);
            return 0;
        }
    
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, attr->id) == 0) {
                if(obj->attributes[i].vtype == VAL_INT)
                    return obj->attributes[i].value.int_value;
                if(obj->attributes[i].vtype == VAL_FLOAT)
                    return obj->attributes[i].value.float_value;
            }
        }
    
        printf("Error: Atributo '%s' no encontrado en objeto '%s'.\n", attr->id, objRef->id);
        return 0;
    }
    
    return (double)node->value;
}

void call_method(ObjectNode *obj, char *method) {
    /* debug print removed */
    ASTNode *thisNode = (ASTNode *)malloc(sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->extra = (struct ASTNode*)obj; // store ObjectNode pointer in 'extra' (consistent with create_object_with_args)
    thisNode->value = 0;
    add_or_update_variable("this", thisNode);

    MethodNode *m = obj->class->methods;
    while (m) {
        if (strcmp(m->name, method) == 0) {
            /* method invocation traces removed */
            interpret_ast(m->body);
            return;
        }
        m = m->next;
    }
    printf("Error: Método '%s' no encontrado en la clase '%s'.\n",
           method, obj->class->name);
}

void execute_predict(ASTNode* model_node, ASTNode* input_node) {
    if (!model_node || !input_node) {
        printf("Error: predict necesita dos argumentos.\n");
        return;
    }
    Variable* model_var = find_variable(model_node->id);
    if (!model_var || model_var->vtype != VAL_OBJECT) {
        printf("Error: Modelo '%s' no encontrado o no es válido.\n", model_node->id);
        return;
    }
    Variable* input_var = find_variable(input_node->id);
    if (!input_var) {
        printf("Error: Input '%s' no encontrado.\n", input_node->id);
        return;
    }
    /* predict debug logs removed */
    ASTNode *lit = create_ast_leaf_number("INT", 85, NULL, NULL);
    add_or_update_variable("__ret__", lit);
}

/* Prototipos de funciones auxiliares */
static void interpret_dataset(ASTNode *node);
static void interpret_for(ASTNode *node);
static void interpret_model_object(ASTNode *node);
static void interpret_train_node(ASTNode *node);
static void interpret_predict_node(ASTNode *node);
static void interpret_call_func(ASTNode *node);
static void interpret_return_node(ASTNode *node);
static void interpret_call_method(ASTNode *node);
static void interpret_var_decl(ASTNode *node);
static void interpret_assign_attr(ASTNode *node);
static void interpret_assign(ASTNode *node);
static void interpret_print(ASTNode *node);
static void interpret_println(ASTNode *node);
static void interpret_statement_list(ASTNode *node);
double evaluate_number(ASTNode *node);
static void interpret_match(ASTNode *node);
// void interpret_if(ASTNode *node); // Ya en ast.h
// int evaluate_condition(ASTNode* condition); // Ya en ast.h

/* ─── DATASET ─────────────────────────────────────────────────────────── */
static void interpret_dataset(ASTNode *node) {
    add_or_update_variable(node->id, node);
    /* dataset load debug log removed */
}

static void interpret_filter_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *list_expr = node->left;
    ASTNode *lambda = node->right;
    ASTNode *list_node = NULL;
    ASTNode *result_list_items = NULL;
    if (list_expr->type && (strcmp(list_expr->type, "ID") == 0 || strcmp(list_expr->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(list_expr->id);
        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            printf("Error: '%s' no es una lista válida para filter.\n", list_expr->id);
            return;
        }
        list_node = (ASTNode *)(intptr_t)v->value.object_value;
    } else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        list_node = list_expr;
    } else {
        printf("Error: Expresión no soportada para filter (tipo: %s).\n", list_expr->type);
        return;
    }
    if (!list_node || strcmp(list_node->type, "LIST") != 0) {
        printf("Error: El operando para filter no es una lista.\n");
        return;
    }
    ASTNode *current_item_node = list_node->left;
    while (current_item_node) {
        add_or_update_variable(lambda->id, current_item_node);
        if (evaluate_expression(lambda->left)) {
            ObjectNode *original_obj = (ObjectNode *)(intptr_t)current_item_node->value;
            ObjectNode *cloned_obj = clone_object(original_obj);
            result_list_items = append_to_list(result_list_items, create_object_node(cloned_obj));
        }
        current_item_node = current_item_node->next;
    }
    add_or_update_variable("__ret__", result_list_items ? result_list_items : create_list_node(NULL));
}

void interpret_list_func_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *listNode = node->left;
    ASTNode *lambda = node->right;
    const char *func = node->id;
    if (strcmp(func, "filter") == 0) {
        ASTNode *result = NULL;
        ASTNode *item = listNode->left;
        while (item) {
            add_or_update_variable(lambda->id, item);
            int r = evaluate_expression(lambda->left);
            if (r) {
                if (!result) result = item;
                else append_to_list(result, item);
            }
            item = item->right;
        }
        add_or_update_variable("__ret__", create_list_node(result));
    }
}

ASTNode* create_for_in_node(const char *var_name, ASTNode *list_expr, ASTNode *body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type      = strdup("FOR_IN");
    node->id        = strdup(var_name);
    node->left      = list_expr;
    node->right     = body;
    node->next      = NULL;
    return node;
}



static void interpret_for_in(ASTNode *node) {
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
            printf("Error: '%s' no es una lista válida.\n", list_expr->id);
            return;
        }
        listNode = (ASTNode *)(intptr_t)v->value.object_value;
    }
    else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        listNode = list_expr;
    }
    else {
        printf("Error: expresión de for-in no soportada (tipo: %s).\n", list_expr->type);
        return;
    }
    if (!listNode || strcmp(listNode->type, "LIST") != 0) {
        printf("Error: El nodo no es una lista válida.\n");
        return;
    }
    ASTNode *items = listNode->left;   
    for (ASTNode *item = items; item; item = item->next) {
        if (item->type && strcmp(item->type, "OBJECT") == 0) {
            ObjectNode *obj = (ObjectNode *)(intptr_t)item->value;
            if (strcmp(node->id, list_expr->id) != 0) {
                ASTNode *wrapper = malloc(sizeof(ASTNode));
                wrapper->type = strdup("OBJECT");
                wrapper->id = strdup(node->id);
                wrapper->left = wrapper->right = NULL;
                /* Store pointer in 'extra' to be consistent with create_object_with_args()/declare_variable */
                wrapper->extra = (struct ASTNode*)obj;
                wrapper->value = 0;
                /* debug print removed */
                add_or_update_variable(node->id, wrapper);
            } 
            /* debug print removed */
            interpret_ast(node->right);
        }
    }
}

ASTNode *create_object_node(ObjectNode *obj) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("OBJECT");
    node->value = (int)(intptr_t)obj;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

void interpret_bridge_decl(ASTNode *node) {
    char* bridge_name = node->id;
    ASTNode* call_node = node->left;
    if (call_node == NULL || strcmp(call_node->type, "CALL_METHOD") != 0) {
        fprintf(stderr, "Error: La declaración del bridge '%s' debe ser una llamada a método.\n", bridge_name);
        return;
    }
    char* lib_name = call_node->left->id;
    char* func_name = call_node->id;
    printf("[TypeEasy] Registrando Bridge (simulado): '%s'\n", bridge_name);
    printf("  -> Objetivo: Biblioteca '%s', Función '%s'\n", lib_name, func_name);

    // Create a generic class for all bridges if it doesn't exist
    /* bridge class search debug log removed */
    ClassNode* bridge_class = find_class("Bridge");
    
    // --- INICIO DE LA CORRECCIÓN (V10) ---
    // Si no se encuentra, la creamos "perezosamente"
    if (bridge_class == NULL) {
        /* bridge class not found — creation log removed */
        bridge_class = create_class("Bridge");
        add_class(bridge_class);
        
        // (Ya no salimos con error)
        // fprintf(stderr, "Error fatal de arquitectura: ...\n");
        // exit(1);
    }
    // --- FIN DE LA CORRECCIÓN ---

    // Create an object for this specific bridge instance
    ObjectNode* bridge_obj = create_object(bridge_class);

    // Create a temporary ASTNode to wrap the object for the symbol table
    ASTNode* obj_node = create_ast_leaf("OBJECT", 0, NULL, bridge_name);    
    obj_node->extra = (struct ASTNode*)bridge_obj;
    // Add the bridge object to the variables table  
    add_or_update_variable(bridge_name, obj_node);
    free_ast(obj_node);
   
}

ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para BRIDGE node.\n");
        exit(1);
    }
    node->type = strdup("BRIDGE_DECL");
    node->id = strdup(name);
    node->left = call_expr_node;
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

void interpret_agent(ASTNode *agent_node) {
    printf("[TypeEasy] Agente '%s' parseado (ignorado en modo script).\n", agent_node->id);
}

ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para ACCESS_EXPR node.\n");
        exit(1);
    }
    node->type = strdup("ACCESS_EXPR");
    node->left = base;
    node->right = index_expr;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    node->next = NULL;
    node->extra = NULL;
    return node;
}

ASTNode *create_object_literal_node(ASTNode *kv_list) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("OBJECT_LITERAL");
    node->left = kv_list;
    node->right = NULL;
    return node;
}

ASTNode *create_kv_pair_node(char *key, ASTNode *value) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("KV_PAIR");
    node->id = strdup(key);
    node->left = value;
    node->right = NULL;
    return node;
}

ASTNode *create_state_decl_node(char *name, ASTNode *value_expr) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("STATE_DECL");
    node->id = strdup(name);
    node->left = value_expr;
    node->right = NULL;
    return node;
}

ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair) {
    if (!list) return pair;
    ASTNode *current = list;
    while (current->right) {
        current = current->right;
    }
    current->right = pair;
    return list;
}

void interpret_ast(ASTNode *node) {    
    if (!node || return_flag) return; 
    /* debug print removed */

    if (strcmp(node->type, "STATE_DECL") == 0) {
        printf("[TypeEasy] 'state' '%s' tratado como 'var' en modo script.\n", node->id);
        interpret_var_decl(node);
    }
    else if (strcmp(node->type, "BRIDGE_DECL") == 0) {
        interpret_bridge_decl(node);
    }
    else if (strcmp(node->type, "AGENT") == 0) {
        interpret_agent(node);
    }
    else if (strcmp(node->type, "ACCESS_EXPR") == 0) { }
    else if (strcmp(node->type, "OBJECT_LITERAL") == 0) { }
    else if (strcmp(node->type, "KV_PAIR") == 0) { }
    else if (strcmp(node->type, "AGENT_LIST") == 0) {
        interpret_ast(node->left);
        interpret_ast(node->right);
    }
    else if (strcmp(node->type, "LISTENER") == 0) {
        // El 'main' puro ignora los listeners
    }
    else if (strcmp(node->type, "FOR") == 0)               {
        interpret_for(node);
    }
    else if (strcmp(node->type, "IF") == 0)            interpret_if(node);
    else if (strcmp(node->type, "MATCH") == 0)         interpret_match(node);
    else if (strcmp(node->type, "FOR_IN") == 0)        interpret_for_in(node);  
    else if (strcmp(node->type, "LIST_FUNC_CALL") == 0) interpret_list_func_call(node);
    else if (strcmp(node->type, "FILTER_CALL") == 0)    interpret_filter_call(node);
    else if (strcmp(node->type, "DATASET") == 0)        interpret_dataset(node);
    else if (strcmp(node->type, "MODEL") == 0 || strcmp(node->type, "OBJECT") == 0) interpret_model_object(node);
    else if (strcmp(node->type, "TRAIN") == 0)          interpret_train_node(node);
    else if (strcmp(node->type, "PREDICT") == 0)        interpret_predict_node(node);
    else if (strcmp(node->type, "CALL_FUNC") == 0)      interpret_call_func(node);
    else if (strcmp(node->type, "RETURN") == 0)         interpret_return_node(node);
    else if (strcmp(node->type, "CALL_METHOD") == 0)    interpret_call_method(node);
    else if (strcmp(node->type, "VAR_DECL") == 0)       interpret_var_decl(node);
    else if (strcmp(node->type, "ASSIGN_ATTR") == 0)    interpret_assign_attr(node);
    else if (strcmp(node->type, "ASSIGN") == 0)         interpret_assign(node);
    else if (strcmp(node->type, "PRINT") == 0)          interpret_print(node);
    else if (strcmp(node->type, "PRINTLN") == 0)        interpret_println(node);
    else if (strcmp(node->type, "STATEMENT_LIST") == 0) interpret_statement_list(node);
    else if (strcmp(node->type, "PLOT") == 0) {
    /* plot generation debug log removed */
        double values[100];
        int count = 0;
        ASTNode *child = node->left;
        while (child != NULL && count < 100) {
            values[count++] = evaluate_number(child);
            child = child->next;
        }
        generate_plot(values, count);
    }
}


/* ... (El resto de tu ast.c, desde evaluate_number hasta free_ast) ... */

double evaluate_number(ASTNode *node) {
    if (node == NULL) {
        fprintf(stderr, "Error: Nodo nulo en evaluate_number\n");
        return 0;
    }
    if (strcmp(node->type, "NUMBER") == 0) {
        return node->value;
    }
    if (strcmp(node->type, "FLOAT") == 0) {
        return atof(node->str_value);
    }
    if (strcmp(node->type, "EXPRESSION") == 0) {
        return evaluate_number(node->left); // simplificado
    }
    fprintf(stderr, "Error: Tipo no soportado en evaluate_number: %s\n", node->type);
    return 0;
}

void interpret_if(ASTNode *node) {
    if (!node || !node->left) return;
    int result = evaluate_condition(node->left);
    if (result) {
        interpret_ast(node->right);
    } else if (node->next) {
        interpret_ast(node->next);
    }
}

void interpret_match(ASTNode *node) {
    /* interpret_match debug logs removed */
    if (!node || !node->left || !node->right) return;
    char* match_value = get_node_string(node->left);

    ASTNode* case_node = node->right;
    while (case_node) {
        if (strcmp(case_node->type, "CASE") == 0) {
            char* case_value = get_node_string(case_node->left);
            if (strcmp(match_value, case_value) == 0) {
                free(case_value);
                interpret_ast(case_node->right);
                break;
            }
            free(case_value);
        }
        case_node = case_node->next;
    }
    free(match_value);
}



int evaluate_condition(ASTNode* condition) {
    if (!condition) return 0;
    if (strcmp(condition->type, "GT") == 0 ||
        strcmp(condition->type, "LT") == 0 ||
        strcmp(condition->type, "EQ") == 0) {
        int left_val = evaluate_expression(condition->left);
        int right_val = evaluate_expression(condition->right);
        if (strcmp(condition->type, "GT") == 0) return left_val > right_val;
        if (strcmp(condition->type, "LT") == 0) return left_val < right_val;
        if (strcmp(condition->type, "EQ") == 0) return left_val == right_val;
    }
    return evaluate_expression(condition);
}

void generate_plot(double *values, int count) {
    FILE *fp = fopen("plot_data.txt", "w");
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %f\n", i, values[i]);
    }
    fclose(fp);
    FILE *gnuplot = popen("gnuplot -persistent", "w");
    fprintf(gnuplot, "set title 'Gráfico generado por TypeEasy'\n");
    fprintf(gnuplot, "plot 'plot_data.txt' with linespoints\n");
    pclose(gnuplot);
}


static void interpret_for(ASTNode *node) {
    add_or_update_variable(node->id, node->left);
    Variable *var = find_variable(node->id);
    if (!var || var->vtype != VAL_INT) {
        printf("Error: Variable de control inválida en FOR.\n");
        return;
    }
    int incremento = node->right->right->left->value;
    int limite = node->right->value;
    ASTNode *body = node->right->right->right;
    if (!body) {
        printf("Advertencia: FOR sin cuerpo\n");
        return;
    }
    while (var->value.int_value < limite) {
        ASTNode *stmt = body;
        while (stmt) {
            if (var->value.int_value >= limite) break;
            interpret_ast(stmt);
            stmt = stmt->right;
        }
        var->value.int_value += incremento;
    }
}

static void interpret_model_object(ASTNode *node) {
    add_or_update_variable(node->id, node);
}

static void interpret_train_node(ASTNode *node) {
    int epochs = 1;
    ASTNode *opt = node->right->right;
    if (opt && strcmp(opt->id, "epochs") == 0) {
        epochs = opt->value;
    }   
    (void)epochs; // Suprime el warning de 'unused variable'
}


static void interpret_predict_node(ASTNode *node) {
    execute_predict(node->left, node->right);
}

static void interpret_call_func(ASTNode *node) {
    if (strcmp(node->id, "concat") != 0) return;

    char result_buffer[2048] = {0}; // Un búfer grande para construir el string
    char *s_temp = NULL;
    ASTNode *arg = node->left;

    while (arg) {
        // Usamos get_node_string para evaluar CADA argumento
        // (esto maneja "string", 123, y var_string, var_int)
        s_temp = get_node_string(arg); 
        
        // strncat es más seguro que strcat
        strncat(result_buffer, s_temp, sizeof(result_buffer) - strlen(result_buffer) - 1);
        
        free(s_temp); // Liberamos la memoria temporal
        arg = arg->right; // Avanzamos al siguiente argumento
    }

    // Devolvemos el resultado final
    ASTNode *lit = create_ast_leaf("STRING", 0, result_buffer, NULL); // create_ast_leaf hace strdup
    add_or_update_variable("__ret__", lit);
    free_ast(lit); // add_or_update_variable copia el valor
}

static void interpret_return_node(ASTNode *node) {
    return_node = node->left;
    return_flag = 1;
}

static void interpret_call_method(ASTNode *node) {
    ASTNode *objNode = node->left;
    Variable *v = find_variable(objNode->id);
    return_flag = 0;
    return_node = NULL;

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 0;
    }
    
    if (!v || v->vtype != VAL_OBJECT) {
        printf("Error: '%s' no es un objeto válido.\n", objNode->id);
        return;
    }
    ObjectNode *obj = v->value.object_value;

    // === FIX START: Handle Bridge method calls ===
    if (strcmp(obj->class->name, "Bridge") == 0) {
        printf("[TypeEasy] Llamada a Bridge Nativo: %s.%s\n", v->id, node->id);
        // Aquí es donde la magia ocurre. Delegamos al manejador específico del bridge.
        if (strcmp(v->id, "Chat") == 0) {
            if (g_bridge_handlers.handle_chat_bridge) g_bridge_handlers.handle_chat_bridge(node->id, node->right);
        } else if (strcmp(v->id, "NLU") == 0) {
            printf("[TypeEasy] Llamada a NLU Bridge\n");
            if (g_bridge_handlers.handle_nlu_bridge) {
                g_bridge_handlers.handle_nlu_bridge(node->id, node->right);
                // Diagnostic: did the bridge set __ret__? (only print when debug mode enabled)
                if (g_debug_mode) {
                    if (__ret_var_active) {
                        printf("[TypeEasy DEBUG] After NLU bridge: __ret__ active=1 type='%s'\n", __ret_var.type ? __ret_var.type : "(null)");
                    } else {
                        printf("[TypeEasy DEBUG] After NLU bridge: __ret__ active=0\n");
                    }
                }
            }
        } else if (strcmp(v->id, "API") == 0) {
            if (g_bridge_handlers.handle_api_bridge) g_bridge_handlers.handle_api_bridge(node->id, node->right);
        } else {
            printf("Advertencia: Bridge '%s' desconocido o no implementado en este ejecutable.\n", v->id);
        }
        // Los bridges pueden o no devolver un valor. Si lo hacen, lo ponen en __ret_var.
        // Por ahora, no necesitamos un valor de retorno falso.
        // ASTNode* dummy_return = create_ast_leaf("STRING", 0, "dummy_return_from_bridge", NULL);
        // add_or_update_variable("__ret__", dummy_return);
        // free_ast(dummy_return);
        return; // Skip normal method dispatch
    }
    // === FIX END ===

    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) m = m->next;
    if (!m) {
        printf("Error: Método '%s' no encontrado en clase '%s'.\n", node->id, obj->class->name);
        return;
    }
    ASTNode *thisNode = malloc(sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->extra = (struct ASTNode*)obj; /* store ObjectNode pointer in 'extra' for consistency */
    thisNode->value = 0;
    add_or_update_variable("this", thisNode);
    ParameterNode *p = m->params;
    ASTNode      *arg = node->right; // CORRECCIÓN
    while (p && arg) {
        ASTNode *vn = NULL;
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        } else if (arg->type && (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
            Variable *v_arg = find_variable(arg->id);
            if (!v_arg) {
                printf("Error: Variable '%s' no encontrada.\n", arg->id);
                return;
            }
            if (v_arg->vtype == VAL_STRING) {
                vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
            } else {
                vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
            }
        } else {
            int val = evaluate_expression(arg);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }
        add_or_update_variable(p->name, vn);
        p   = p->next;
        arg = arg->right;
    }

    interpret_ast(m->body);
    if (return_flag && return_node) {
        ASTNode *lit = NULL;
        if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
            lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
        } else if (return_node->id) {
            Variable *rv = find_variable(return_node->id);
            if (rv) {
                if (rv->vtype == VAL_STRING) {
                    lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                } else if (rv->vtype == VAL_FLOAT) {
                    lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                } else if (rv->vtype == VAL_INT) {
                    lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                }
            } else {
                printf("Error: variable '%s' not found in return statement.\n", return_node->id);
                return;
            }
        } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
            ASTNode *objN = return_node->left;
            ASTNode *attrN = return_node->right;
            Variable *ov = find_variable(objN->id);
            if (ov && ov->vtype == VAL_OBJECT) {
                ObjectNode *oobj = ov->value.object_value; int i = -1;
                int idx = -1;
                for (i=0; i<oobj->class->attr_count; i++) { // Inicializa 'i'
                    if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                }
                if (idx>=0) {
                    if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                        lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                    else if (strcmp(oobj->attributes[idx].type,"float")==0)
                        lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL); // Usar idx
                    else
                        lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                }
            }
        } else {
            double rv_double = evaluate_expression(return_node);
            if (rv_double == (int)rv_double) {
                lit = create_ast_leaf_number("INT", (int)rv_double, NULL, NULL);
            } else {
                lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
            }
        }
        if (lit) {
            add_or_update_variable("__ret__", lit);
        }
        return_flag = 0;
        return_node = NULL;
    }
}
ObjectNode* clone_object(ObjectNode *original) {
    if (!original || !original->class) {
        /* clone_object debug log removed */
        return NULL;
    }
    ObjectNode *clone = malloc(sizeof(ObjectNode));
    clone->class = original->class;
    clone->attributes = malloc(original->class->attr_count * sizeof(Variable));
    for (int i = 0; i < original->class->attr_count; i++) {
        clone->attributes[i].id = strdup(original->attributes[i].id);
        clone->attributes[i].type = strdup(original->attributes[i].type);
        clone->attributes[i].vtype = original->attributes[i].vtype;
        if (clone->attributes[i].vtype == VAL_STRING) {
            const char *src = original->attributes[i].value.string_value;
            clone->attributes[i].value.string_value = src ? strdup(src) : strdup("");
        } else if (clone->attributes[i].vtype == VAL_INT) {
            clone->attributes[i].value.int_value = original->attributes[i].value.int_value;
        } else {
            // (Manejar otros tipos como float si es necesario)
        }
    }
    return clone;
}

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        printf("Error: No se pudo abrir el archivo '%s'\n", filename);
        return NULL;
    }
    char line[512];
    fgets(line, sizeof(line), fp);
    ASTNode* first = NULL;
    ASTNode* last = NULL;
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        char* nombre = strtok(line, ",");
        char* precio_str = strtok(NULL, ",");
        if (!nombre || !precio_str) {
            continue;
        }
        char* nl = strchr(precio_str, '\n');
        if (nl) *nl = '\0';
        ASTNode* arg1 = create_string_node(nombre);
        ASTNode* arg2 = create_int_node(atoi(precio_str));
        ASTNode* args = add_argument(NULL, arg1);
        args = add_argument(args, arg2);
        ASTNode* obj = create_object_with_args(cls, args);
        obj->next = NULL;
        if (!first) {
            first = obj;
            last = obj;
        } else {
            last->next = obj;
            last = obj;
        }
        count++;
    }
    fclose(fp);
    /* debug print removed */
    ASTNode* listNode = malloc(sizeof(ASTNode));
    listNode->type = strdup("LIST");
    listNode->left = first;
    listNode->right = NULL;
    listNode->next = NULL;
    listNode->id = NULL;
    listNode->str_value = NULL;
    listNode->value = 0;
    return listNode;
}



static void interpret_var_decl(ASTNode *node) {
    /* trace removed */
    int is_const_flag = node->value;
    const char* declared_type = node->str_value;
    ASTNode* value_node = node->left;
    Variable *evaluated_value_var = NULL;

    // 1. Si el valor es una llamada a función, ejecútala primero
if (value_node && (strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0 || strcmp(value_node->type, "FILTER_CALL") == 0 || strcmp(value_node->type, "CALL_FUNC") == 0)) {        interpret_ast(value_node);
        evaluated_value_var = find_variable("__ret__");
        if (!evaluated_value_var) {
            fprintf(stderr, "Error: No se capturó valor de retorno de la expresión '%s'. __ret_var_active=%d\n", value_node->type ? value_node->type : "desconocido", __ret_var_active);
            exit(1);
        }
    }

    // 2. Comprobación de tipos (Tu lógica es correcta)
    const char* effective_value_type_str = NULL;
    if (evaluated_value_var != NULL) {
        effective_value_type_str = evaluated_value_var->type;
    } else if (value_node != NULL) {
        if (strcmp(value_node->type, "NUMBER") == 0) {
            effective_value_type_str = "INT";
        } else if (strcmp(value_node->type, "STRING_LITERAL") == 0 || strcmp(value_node->type, "STRING") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
            double result = evaluate_expression(value_node);
            if (result == (int)result) {
                effective_value_type_str = "INT";
            } else {
                effective_value_type_str = "FLOAT";
            }
        } else {
            effective_value_type_str = value_node->type;
        }
    }

    if (declared_type != NULL && effective_value_type_str != NULL) {
        if (strcmp(declared_type, effective_value_type_str) != 0) {
            int allow_int_to_float = (strcmp(declared_type, "FLOAT") == 0 && strcmp(effective_value_type_str, "INT") == 0);
            if (!allow_int_to_float) {
                fprintf(stderr, "Error de tipo: No se puede asignar un valor de tipo '%s' a una variable de tipo '%s'.\n", effective_value_type_str, declared_type);
                exit(1);
            }
        }
    }

    // 3. Asignación
    ASTNode *value_to_assign_node = NULL;
    if (evaluated_value_var != NULL) {
        // ---- Si el valor vino de una función (como __ret_var) ----
        
        // Crea un nuevo nodo AST persistente para almacenar el valor
        if (evaluated_value_var->vtype == VAL_STRING) {
            value_to_assign_node = create_ast_leaf("STRING", 0, strdup(evaluated_value_var->value.string_value), NULL);
        } else if (evaluated_value_var->vtype == VAL_INT) {
            value_to_assign_node = create_ast_leaf_number("INT", evaluated_value_var->value.int_value, NULL, NULL);
        } else if (evaluated_value_var->vtype == VAL_FLOAT) {
            value_to_assign_node = create_ast_leaf("FLOAT", 0, double_to_string(evaluated_value_var->value.float_value), NULL);
        } else if (evaluated_value_var->vtype == VAL_OBJECT) {
            value_to_assign_node = malloc(sizeof(ASTNode));
            value_to_assign_node->type = strdup(evaluated_value_var->type);
            value_to_assign_node->extra = (struct ASTNode*)evaluated_value_var->value.object_value; // <-- CORRECCIÓN
            value_to_assign_node->id = evaluated_value_var->id ? strdup(evaluated_value_var->id) : NULL;
            value_to_assign_node->str_value = NULL;
            if (strcmp(evaluated_value_var->type, "LIST") == 0) {
                value_to_assign_node->left = ((ASTNode*)evaluated_value_var->value.object_value)->left;
            } else {
                value_to_assign_node->left = NULL;
            }
            value_to_assign_node->right = NULL;
            value_to_assign_node->next = NULL;
           // value_to_assign_node->extra = NULL;
        } else {
            fprintf(stderr, "Error interno: Tipo de retorno desconocido para asignación de variable '%s'.\n", node->id);
            exit(1);
        }
        
    declare_variable(node->id, value_to_assign_node, is_const_flag);
        
    // NOTE: Do NOT free 'value_to_assign_node' here. The declared variable
    // stores pointers into the node (or copies them) and freeing it here
    // caused a use-after-free and intermittent segfaults (exit code 139).
    // free_ast(value_to_assign_node); // removed intentionally
        
        // Limpia la variable de retorno
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
            __ret_var_active = 0;
        }
        return_flag = 0;
        return_node = NULL;
        // ¡OJO! No pongas un 'return' aquí, el código del constructor debe ejecutarse
    
    } else {
        // ---- Si el valor es un literal (ej. let x = 10) ----
        declare_variable(node->id, value_node, is_const_flag);
    }

    // 4. Ejecutar el constructor (Tu lógica es correcta)
    // (Este código se ejecuta para *ambos* casos, lo cual es correcto)
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
                if (arg->type && strcmp(arg->type, "STRING") == 0) {
                    vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                } else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                    Variable *v = find_variable(arg->id);
                    if (!v) {
                        printf("Error: Variable '%s' no encontrada.\n", arg->id);
                        return;
                    }
                    if (v->vtype == VAL_STRING) {
                        vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                    } else {
                        vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                    }
                } else {
                    int val = evaluate_expression(arg);
                    vn = create_ast_leaf_number("INT", val, NULL, NULL);
                }
                add_or_update_variable(p->name, vn);
                p   = p->next;
                arg = arg->right;
            }
            call_method(var->value.object_value, "__constructor");
        }
    }
}

ASTNode *create_list_node(ASTNode *items) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("LIST");
    node->left = items; // 'left' apunta al primer item
    node->right = NULL;
    node->next = NULL;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    
    // Convertir la lista enlazada de 'right' a 'next'
    if(items) {
        ASTNode *cur = items;
        while(cur) {
            ASTNode* temp = cur->right; // Guardamos el siguiente item (de la regla expr_list)
            cur->right = NULL;         // Limpiamos 'right'
            cur->next = temp;          // Lo movemos a 'next'
            cur = temp;
        }
    }
    
    return node;
}

ASTNode *append_to_list_parser(ASTNode *list, ASTNode *item) {
    if (!list) return item;
    ASTNode *cur = list;
    while (cur->right) {
        cur = cur->right;
    }
    cur->right = item; // Usamos 'right' temporalmente
    item->right = NULL;
    return list;
}

ASTNode *append_argument_raw(ASTNode *list, ASTNode *arg) {
    if (!list) return arg;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = arg;
    return list;
}


ASTNode* append_to_list(ASTNode* list, ASTNode* item) {
    if (!item) return list;
    if (strcmp(item->type, "LIST") == 0) {
        printf("Error: No se puede insertar un nodo LIST dentro de una lista.\n");
        return list;
    }
    if (list == item) {
        printf("Error: Intento de insertar la misma lista dentro de sí misma.\n");
        return list;
    }
    if (!list) {
        if (strcmp(item->type, "LIST") == 0) {
            printf("Error: No se puede crear lista con nodo LIST como item.\n");
            return NULL;
        }
        item->next = NULL;
        ASTNode *listNode = malloc(sizeof(ASTNode));
        listNode->type = strdup("LIST");
        listNode->left = item;
        listNode->right = NULL;
        listNode->next = NULL;
        listNode->id = NULL;
        listNode->str_value = NULL;
        listNode->value = 0;
        return listNode;
    }
    if (strcmp(list->type, "LIST") != 0) {
        return NULL;
    }
    ASTNode* current = list->left;
    if (!current) {
        list->left = item;
    } else {
        int safety = 0;
        while (current->next) {
            if (++safety > 10000000) {
                printf("Error: Bucle infinito detectado en append_to_list()\n");
                break;
            }
            current = current->next;
        }     
        current->next = item;
    }
    item->next = NULL;
    return list;
}


ASTNode* create_list_function_call_node(ASTNode* list, const char* funcName, ASTNode* lambda) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("FILTER_CALL");
    node->id = strdup(funcName);
    node->left = list;
    node->right = lambda;
    node->next = NULL;
    return node;
}


ASTNode* create_lambda_node(const char* argName, ASTNode* body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(argName);
    node->left = body;
    node->right = NULL;
    node->next = NULL;
    return node;
}


static void interpret_assign_attr(ASTNode *node) {
    /* trace removed */
    ASTNode *access = node->left;
    /* access internals trace removed */
    ASTNode *value_node = node->right;
    Variable *var = find_variable(access->left->id);
    /* find_variable trace removed */
    if (!var || var->vtype!=VAL_OBJECT) {
        printf("Error: Objeto '%s' no definido o no es un objeto.\n", access->left->id); return;
    }
    ObjectNode *obj = var->value.object_value;
    const char *attr_name = access->right->id;
    int idx=-1;
    for(int i=0;i<obj->class->attr_count;i++){
        if(strcmp(obj->class->attributes[i].id,attr_name)==0){idx=i;break;}
    }
    if(idx<0){ printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", attr_name, obj->class->name); return; }
    const char *declared = obj->attributes[idx].type; 

    /* detailed attribute assignment trace removed */

    if (declared == NULL) {
        printf("Error: El atributo '%s' no tiene tipo definido.\n", attr_name);
        return;
    }    

    if (strcmp(declared, "string") == 0) {
        if (value_node->str_value) {
          obj->attributes[idx].value.string_value = strdup(value_node->str_value);
        }
        else if (value_node->id) {
          Variable *v2 = find_variable(value_node->id);
          if (!v2 || v2->vtype != VAL_STRING) {
            printf("Error: expresión no es una cadena válida.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
        }
        else if (strcmp(value_node->type, "CALL_FUNC") == 0) {
          interpret_ast(value_node);
          Variable *r = find_variable("__ret__");
          if (!r || r->vtype != VAL_STRING) {
            printf("Error: resultado de función no es una cadena.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(r->value.string_value);
        }
        obj->attributes[idx].vtype = VAL_STRING;
      } else {
        int val = evaluate_expression(value_node);
        obj->attributes[idx].value.int_value = val;
        obj->attributes[idx].vtype = VAL_INT;
    }
}

static void interpret_assign(ASTNode *node) {
    /* trace removed */
    ASTNode *var_node = node->left;
    ASTNode *value_node = node->right;
    if (!var_node || !var_node->id || !value_node) {
        printf("Error: Asignación inválida.\n"); 
        return; 
    }

    // --- INICIO DE LA CORRECCIÓN ---

    // ¿Es una llamada a función (como concat)?
    if (strcmp(value_node->type, "CALL_FUNC") == 0 || strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0) {
        
        // 1. Ejecutar la función (ej. concat)
        interpret_ast(value_node);
        
        // 2. Obtener el resultado de __ret_var
        Variable *ret_val = find_variable("__ret__");
        if (!ret_val) {
            printf("Error: Función en asignación no devolvió nada.\n");
            return;
        }

        // 3. Crear un nodo temporal para el valor
        ASTNode *temp_node = NULL;
        if (ret_val->vtype == VAL_STRING) {
            temp_node = create_ast_leaf("STRING", 0, strdup(ret_val->value.string_value), NULL);
        } else if (ret_val->vtype == VAL_INT) {
            temp_node = create_ast_leaf_number("INT", ret_val->value.int_value, NULL, NULL);
        } else if (ret_val->vtype == VAL_FLOAT) {
            temp_node = create_ast_leaf("FLOAT", 0, double_to_string(ret_val->value.float_value), NULL);
        } else if (ret_val->vtype == VAL_OBJECT) {
            temp_node = malloc(sizeof(ASTNode));
            memset(temp_node, 0, sizeof(ASTNode));
            temp_node->type = strdup(ret_val->type);
            temp_node->extra = (struct ASTNode*)ret_val->value.object_value;
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
            __ret_var_active = 0;
        }
        return_flag = 0;
        return_node = NULL;
    }
    // ¿Es un acceso a atributo (como intencion.item)?
    else if (strcmp(value_node->type, "ACCESS_ATTR") == 0) {
        // Esta lógica ya la escribimos para declare_variable, la usamos aquí
        ASTNode *o = value_node->left, *a = value_node->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
             printf("Error: Objeto '%s' no encontrado para asignación.\n", o->id);
             return;
        }
        
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
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
        printf("Error: Atributo '%s' no encontrado en '%s'.\n", a->id, o->id);
        return;
    }
    // ¿Es una expresión matemática?
    else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
        double result = evaluate_expression(value_node);
        char* str_res = double_to_string(result);
        ASTNode* temp_node = create_ast_leaf("FLOAT", 0, str_res, NULL);
        add_or_update_variable(var_node->id, temp_node);
        free_ast(temp_node);
        free(str_res);
    } 
    // Es un valor simple (literal, variable)
    else {
        add_or_update_variable(var_node->id, value_node);
    }
    // --- FIN DE LA CORRECCIÓN ---
}

static void interpret_print(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        printf("Error: print sin argumento\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        printf("%s", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
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
            printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            printf("%s", attr->value.string_value);
        else
            printf("%d", attr->value.int_value);
        return;
    }

    if (arg->id) { 
        Variable *v = find_variable(arg->id);
        if (!v) {
            printf("Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                printf("[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                printf("]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            printf("%s", v->value.string_value);
        else if (v->vtype == VAL_INT)
            printf("%d", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            printf("%f", v->value.float_value);
        else
            printf("Objeto de clase: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            printf("%d", (int)val);
        } else {
            printf("%f", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 0;
    }
}

static void interpret_println(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        printf("Error: print sin argumento\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        printf("%s\n", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
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
            printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            printf("%s\n", attr->value.string_value);
        else
            printf("%d\n", attr->value.int_value);
        return;
    }

    if (arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) {
            printf("Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                printf("[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                printf("]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            printf("%s\n", v->value.string_value);
        else if (v->vtype == VAL_INT)
            printf("%d\n", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            printf("%f\n", v->value.float_value);
        else
            printf("Objeto de clase: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            printf("%d\n", (int)val);
        } else {
            printf("%f\n", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 0;
    }
}


static void interpret_statement_list(ASTNode *node) {
    interpret_ast(node->left);
    interpret_ast(node->right);
}

// ====================== MANEJO DE ATRIBUTOS ======================

int get_attribute_value(ObjectNode *obj, const char *attr_name) {
    if (!obj) return 0;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (strcmp(obj->attributes[i].id, attr_name) == 0) {
            return obj->attributes[i].value.int_value;
        }
    }
    printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", attr_name, obj->class->name);
    return 0;
}

void set_attribute_value(ObjectNode *obj, const char *attr_name, int value) {
    if (!obj) return;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (strcmp(obj->attributes[i].id, attr_name) == 0) {
            obj->attributes[i].value.int_value = value;
            return;
        }
    }
    printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", attr_name, obj->class->name);
    exit(EXIT_FAILURE);
}

// ====================== LIBERACIÓN DE MEMORIA ======================

void free_ast(ASTNode *node) {
    if (!node) return;
    free(node->type);
    if (node->str_value) free(node->str_value);
    if (node->id) free(node->id);
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    free(node);
}