#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bytecode.h"
// ====================== CONSTANTES Y ESTRUCTURAS GLOBALES ======================
#define MAX_CLASSES 50
#define MAX_VARS 100

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
static char* get_node_string(ASTNode *node) {
    if (!node) return strdup("");

    // Literal string
    if (strcmp(node->type, "STRING") == 0) {
        return strdup(node->str_value);
    }

    // Acceso a atributo
    if (strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left, *a = node->right;
        Variable *v = find_variable(o->id);
        if (v && v->vtype == VAL_OBJECT) {
            ObjectNode *obj = v->value.object_value;
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    if (strcmp(obj->class->attributes[i].type, "string") == 0) {
                        return strdup(obj->attributes[i].value.string_value);
                    } else {
                        return int_to_string(obj->attributes[i].value.int_value);
                    }
                }
            }
        }
        return strdup("");
    }

    // Nodo ADD mixto o anidado
    if (strcmp(node->type, "ADD") == 0) {
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

    // Expresión numérica    
    int v = evaluate_expression(node);
    return int_to_string(v);
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
        obj->attributes[i].vtype = VAL_INT;
        obj->attributes[i].value.int_value = 0;
    }
    
    return obj;
}

// ====================== MANEJO DE VARIABLES ======================

#include <stdio.h> // Asegúrate de tener esta línea al inicio de tu archivo
#include <string.h>

// Asumimos que la estructura Variable y el arreglo vars están definidos en otro lugar
// typedef struct {
//     char *id;
//     // ... otros campos
// } Variable;
//
// extern Variable vars[];
// extern int var_count;

#include <stdio.h> // Asegúrate de tener esta línea al inicio de tu archivo
#include <string.h>

// Asumimos que la estructura Variable y el arreglo vars están definidos en otro lugar
// typedef struct {
//     char *id;
//     // ... otros campos
// } Variable;
//
// extern Variable vars[];
// extern int var_count;

Variable *find_variable(char *id) {    
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    for (int i = 0; i < var_count; i++) {
        if (vars[i].id) {
            // Imprime el ID del elemento actual en el arreglo            
            if (strcmp(vars[i].id, id) == 0) {                
                return &vars[i];
            }
        }
    }
    printf("El ID '%s' no fue encontrado.\n", id); // Mensaje si no se encuentra
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
        // Para otros tipos como OBJECT, etc., que ya usan strdup en su creación.
        vars[my_index].type = strdup(value->type);
    }

    if (strcmp(value->type, "STRING") == 0) {
        vars[my_index].vtype = VAL_STRING;
        vars[my_index].value.string_value = strdup(value->str_value);
    } 
    else if (strcmp(value->type, "LIST") == 0) {
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (void *)(intptr_t)value;

        ASTNode *cur = value->left;
        while (cur) {
            if (strcmp(cur->type, "OBJECT") != 0) {
                return;
            }

            // 1. Clonar el objeto original
            ObjectNode *obj_original = (ObjectNode *)(intptr_t)cur->value;
            ObjectNode *obj_clonado = clone_object(obj_original);

            // 2. Obtener los argumentos antes de limpiar
            ASTNode *arg = cur->left;

            // 3. Reemplazar el objeto en el ASTNode
            cur->value = (int)(intptr_t)obj_clonado;

            // 4. Buscar el constructor
            MethodNode *m = obj_clonado->class->methods;
            while (m && strcmp(m->name, "__constructor") != 0) {
                m = m->next;
            }

            if (m) {
                ParameterNode *p = m->params;

                // 5. Enlazar los argumentos
                while (p && arg) {
                    ASTNode *vn = NULL;

                    if (arg->type && strcmp(arg->type, "STRING") == 0) {
                        vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    } 
                    else if (arg->type && 
                             (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
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

                // 6. Llamar el constructor sobre el clon
                call_method(obj_clonado, "__constructor");
               
            }

            // 7. Ahora que ya se usaron, limpiar los argumentos
            cur->left = NULL;

            cur = cur->next;
        }
    }
    else if (strcmp(value->type, "FLOAT") == 0) {
        vars[my_index].vtype = VAL_FLOAT;
        vars[my_index].value.float_value = atof(value->str_value);
    }
    else if (strcmp(value->type, "ADD") == 0 || strcmp(value->type, "SUB") == 0 || 
             strcmp(value->type, "MUL") == 0 || strcmp(value->type, "DIV") == 0) {
        // Evaluar la expresión
        double result = evaluate_expression(value);
        // Check if the result is an integer
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
        vars[my_index].value.object_value = (ObjectNode *)(intptr_t)value->value;
    }
    else {
        vars[my_index].vtype = VAL_INT;
        vars[my_index].value.int_value = value->value;
    }
    //printf("[DEBUG] Declared variable '%s' with type %s and vtype %d\n", id, vars[my_index].type, vars[my_index].vtype);
}


void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;

        // Special handling for __ret__
    if (strcmp(id, "__ret__") == 0) {
        // Clean up previous __ret__ value if active
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                free(__ret_var.value.string_value);
            }
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable)); // Clear the struct
        }

        __ret_var.id = strdup(id);
        __ret_var.is_const = 0; // __ret__ is never const

        if (strcmp(value->type, "STRING") == 0) {
            __ret_var.vtype = VAL_STRING;
            __ret_var.type = strdup("STRING");
            __ret_var.value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("OBJECT");
            __ret_var.value.object_value = (ObjectNode *)(intptr_t)value->value;
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
        return; // Handled __ret__
    }

    // Original logic for other variables
    Variable *var = find_variable_for(id);

    if (var) {        
        // Verificamos si la variable es constante antes de actualizarla.
        if (var->is_const) {
            // Imprimimos un error profesional y detenemos la ejecución.
            fprintf(stderr, "Error: No se puede asignar a la variable constante '%s'.\n", id);
            exit(1); // Detiene el programa con un código de error.
        }

        // Actualizar variable existente
        free(var->type);
        var->type = strdup(value->type);

        if (strcmp(value->type, "STRING") == 0) {
            var->vtype = VAL_STRING;
            var->value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            var->vtype = VAL_OBJECT;
            var->value.object_value = (ObjectNode *)(intptr_t)value->value;
        } else {
            var->vtype = VAL_INT;
            var->value.int_value = value->value;
        }
    } else {
        // Crear nueva variable
        if (var_count >= MAX_VARS) {
            printf("Error: Demasiadas variables declaradas.\n");
            return;
        }

        vars[var_count].id = strdup(id);
        vars[var_count].type = strdup(value->type);
        vars[var_count].is_const = 0; // Las variables creadas por asignación implícita no son const


        if (strcmp(value->type, "STRING") == 0) {
            vars[var_count].vtype = VAL_STRING;
            vars[var_count].value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            vars[var_count].vtype = VAL_OBJECT;
            vars[var_count].value.object_value = (ObjectNode *)(intptr_t)value->value;
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

    return node;
}

ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

    // Operaciones matemáticas
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
    obj->value = (int)(intptr_t)real_obj;
   
    return obj;
}

ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    // CORRECCIÓN: El tipo debe ser una copia dinámica para poder ser liberado.
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
    // Creamos un nodo para el dataset...
    ASTNode* dataNode = create_identifier_node(data_name);
    // ...y encadenamos ahí la lista de opciones (epochs)
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

    // Variable
    if (node->id) {
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
    
    // Operaciones matemáticas
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
                return obj->attributes[i].value.int_value;
            }
        }
    
        printf("Error: Atributo '%s' no encontrado en objeto '%s'.\n", attr->id, objRef->id);
        return 0;
    }
    

    // Número literal
    return (double)node->value;
}

void call_method(ObjectNode *obj, char *method) {
    // Crear nodo 'this'
    ASTNode *thisNode = (ASTNode *)malloc(sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->value = (int)(intptr_t)obj;

    // Actualizar variable 'this'
    add_or_update_variable("this", thisNode);

    // Buscar y ejecutar método
    MethodNode *m = obj->class->methods;
    while (m) {
        if (strcmp(m->name, method) == 0) {
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

    printf("[DEBUG] Ejecutando predict(%s, %s)\n", model_node->id, input_node->id);
    printf("Predicción de '%s' sobre '%s': Resultado simulado 0.85\n", model_node->id, input_node->id);

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


/* ─── DATASET ─────────────────────────────────────────────────────────── */
static void interpret_dataset(ASTNode *node) {
    // Guardamos el ASTNode del dataset en una variable con nombre node->id
    add_or_update_variable(node->id, node);
    // Mensaje de depuración mostrando el origen
    printf("[DEBUG] Dataset '%s' cargado desde '%s'\n",
           node->id,
           node->str_value  /* asegúrate de que str_value almacena la ruta */);
}

static void interpret_filter_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;

    ASTNode *list_expr = node->left;
    ASTNode *lambda = node->right;
    ASTNode *list_node = NULL;
    ASTNode *result_list_items = NULL;

    // 1. Obtener la lista (ya sea una variable o una lista literal)
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

    // 2. Iterar y aplicar el filtro
    ASTNode *current_item_node = list_node->left;
    while (current_item_node) {
        // Crear un nodo temporal para el item actual y asignarlo a la variable de la lambda (ej. 'p')
        add_or_update_variable(lambda->id, current_item_node);

        // Evaluar la condición de la lambda
        if (evaluate_expression(lambda->left)) {
            // Si la condición es verdadera, clonamos el objeto y lo añadimos a la nueva lista
            ObjectNode *original_obj = (ObjectNode *)(intptr_t)current_item_node->value;
            ObjectNode *cloned_obj = clone_object(original_obj);
            result_list_items = append_to_list(result_list_items, create_object_node(cloned_obj));
        }
        current_item_node = current_item_node->next;
    }

    // 3. Guardar la nueva lista filtrada en __ret__
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
            // Simular p => p.precio > 2
            add_or_update_variable(lambda->id, item); // lambda->id es 'p'
            int r = evaluate_expression(lambda->left); // lambda->left es el cuerpo
            if (r) {
                if (!result) result = item;
                else append_to_list(result, item);
            }
            item = item->right;
        }
        add_or_update_variable("__ret__", create_list_node(result));
    }
}

// Nuevo en ast.h
ASTNode* create_for_in_node(const char *var_name, ASTNode *list_expr, ASTNode *body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type      = strdup("FOR_IN");
    node->id        = strdup(var_name);   // nombre de la variable de iteración
    node->left      = list_expr;          // la lista a recorrer
    node->right     = body;               // cuerpo del for
    node->next      = NULL;
    return node;
}



static void interpret_for_in(ASTNode *node) {
    if (!node->right) {
        printf("[DEBUG] El cuerpo del for-in está vacío (node->right == NULL)\n");
        return;
    }

    ASTNode *list_expr = node->left;
    ASTNode *listNode = NULL;

    // Caso 1: nombre de lista como variable (ID o IDENTIFIER)
    if (list_expr->type && (
        strcmp(list_expr->type, "ID") == 0 ||
        strcmp(list_expr->type, "IDENTIFIER") == 0)) {

        Variable *v = find_variable(list_expr->id);
       
        if (!v) {
            printf("[DEBUG] Variable '%s' no existe.\n", list_expr->id);
            return;
        }       

        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            printf("Error: '%s' no es una lista válida.\n", list_expr->id);

            return;
        }

        listNode = (ASTNode *)(intptr_t)v->value.object_value;
    }

    // Caso 2: lista literal
    else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        listNode = list_expr;
    }

    else {
        printf("Error: expresión de for-in no soportada (tipo: %s).\n", list_expr->type);
        return;
    }

    // Validación final
    if (!listNode || strcmp(listNode->type, "LIST") != 0) {
        printf("Error: El nodo no es una lista válida.\n");
        return;
    }

    // Iterar
    ASTNode *items = listNode->left;   

    for (ASTNode *item = items; item; item = item->next) {

        if (item->type && strcmp(item->type, "OBJECT") == 0) {
            ObjectNode *obj = (ObjectNode *)(intptr_t)item->value;

            //  NO sobrescribas la lista original si el nombre coincide
            if (strcmp(node->id, list_expr->id) != 0) {
                //printf("[DEBUG] Sobreescribiendo '%s'\n", node->id);
                ASTNode *wrapper = malloc(sizeof(ASTNode));
                wrapper->type = strdup("OBJECT");
                wrapper->id = strdup(node->id);
                wrapper->left = wrapper->right = NULL;
                wrapper->value = (int)(intptr_t)obj;
                add_or_update_variable(node->id, wrapper);
            } 
            /*else {
                printf("[DEBUG] Previniendo sobreescritura de '%s'\n", node->id);
            }*/
            

            // Interpretar el cuerpo
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
    char* bridge_name = node->id; // "Chat"
    ASTNode* call_node = node->left; // El nodo "CALL_METHOD"

    // Verificamos que sea una llamada a método
    if (call_node == NULL || strcmp(call_node->type, "CALL_METHOD") != 0) {
        fprintf(stderr, "Error: La declaración del bridge '%s' debe ser una llamada a método.\n", bridge_name);
        return;
    }

    // Extraemos la información del nodo CALL_METHOD
    // (Nota: tu 'CALL_METHOD' guarda el objeto en 'left' y el nombre en 'id')
    char* lib_name = call_node->left->id; // "WhatsApp"
    char* func_name = call_node->id;      // "connect"
    // (Aquí iterarías call_node->right para obtener los argumentos)

    printf("[TypeEasy Runtime] Registrando Bridge: '%s'\n", bridge_name);
    printf("  -> Objetivo: Biblioteca '%s', Función '%s'\n", lib_name, func_name);

    // LÓGICA REAL (Futuro):
    // 1. Cargar dinámicamente la librería (ej: libwhatsapp.so)
    // 2. Encontrar el puntero a la función (ej: "connect")
    // 3. Almacenar ese puntero en una tabla hash global bajo el nombre "Chat".
}

ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para BRIDGE node.\n");
        exit(1);
    }
    node->type = strdup("BRIDGE_DECL");
    node->id = strdup(name);    // El nombre del bridge, ej: "Chat"
    node->left = call_expr_node; // El nodo "CALL_METHOD" para WhatsApp.connect
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

/* === CÓDIGO NUEVO COMPLEJO (Concepto) === */

// Esta función se llamaría desde interpret_ast
// cuando encuentra un nodo "AGENT".
void interpret_agent(ASTNode *agent_node) {
    printf("[TypeEasy Runtime] Iniciando Agente '%s'...\n", agent_node->id);

    // 1. EL PROBLEMA: Tu 'main' en parser.y termina.
    //    Un agente NO PUEDE TERMINAR. Necesita un bucle infinito
    //    para escuchar eventos (como un servidor web).
    //    Necesitarías cambiar tu 'main' para que NO llame a 'interpret_ast'
    //    directamente, sino que inicie un 'Runtime'.
    
    // 2. REGISTRAR LISTENERS:
    //    Tendrías que iterar sobre los listeners del agente (agent_node->left)
    ASTNode* listener = agent_node->left;
    while(listener) {
        if (listener && strcmp(listener->type, "LISTENER") == 0) {
            // 3. ANALIZAR EL "BRIDGE":
            //    Necesitas analizar 'listener->left' (la expresión 'Chat.onMessage')
            //    para saber que esto es un webhook de "Chat".
            //    Esto requiere un sistema de "Bridges" que aún no existe.

            printf("[TypeEasy Runtime] Registrando listener para un evento de 'Chat'...\n");

            // 4. REGISTRAR EL WEBHOOK:
            //    (Pseudo-código usando una librería C de servidor web como civetweb)
            //    my_server = server_start("8080");
            //    server_add_webhook(my_server, "/whatsapp_hook", &on_message_callback);
        }
        listener = listener->right; // Asumiendo que es una lista
    }

    // 5. INICIAR EL BUCLE DEL SERVIDOR:
    printf("[TypeEasy Runtime] Agente '%s' corriendo en puerto 8080. Esperando eventos...\n", agent_node->id);
    // server_run_forever(my_server); // Esto bloquearía el hilo principal.
}

/*
// 6. EL CALLBACK:
//    Tendrías que escribir esta función que el servidor C llamaría.
void on_message_callback(Request req, Response res) {
    // A. Parsear el JSON de 'req.body' para obtener el mensaje.
    // B. ¡CONECTAR A REDIS para obtener el 'state' de ese usuario!
    // C. Encontrar el nodo AST 'statement_list' del listener (listener->right)
    // D. ¡Finalmente, llamar a interpret_ast(listener->right) para ejecutar la lógica del chat!
}
*/
/* === FIN DEL CONCEPTO === */

ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para ACCESS_EXPR node.\n");
        exit(1);
    }
    node->type = strdup("ACCESS_EXPR");
    node->left = base;         // El objeto/variable (ej. intencion.entidad)
    node->right = index_expr;  // El índice (ej. "item")
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
    node->left = kv_list; // Lista de nodos KV_PAIR
    node->right = NULL;
    /* ... inicializa los otros campos ... */
    return node;
}

ASTNode *create_kv_pair_node(char *key, ASTNode *value) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("KV_PAIR");
    node->id = strdup(key); // Usamos 'id' para la clave
    node->left = value;     // Usamos 'left' para el valor
    node->right = NULL;     // Usaremos 'right' como 'next' en la lista
    /* ... inicializa los otros campos ... */
    return node;
}

ASTNode *create_state_decl_node(char *name, ASTNode *value_expr) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup("STATE_DECL");
    node->id = strdup(name);    // "sesion"
    node->left = value_expr; // El nodo "CALL_METHOD" para getSession
    node->right = NULL;
    /* ... inicializa los otros campos ... */
    return node;
}

ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair) {
    if (!list) return pair;
    ASTNode *current = list;
    while (current->right) { // Iteramos usando 'right'
        current = current->right;
    }
    current->right = pair; // Añadimos al final
    return list;
}

void interpret_ast(ASTNode *node) {    
    if (!node || return_flag) return; 

    if (strcmp(node->type, "STATE_DECL") == 0) {
        // Aquí iría tu lógica para 'getSession'
        // Por ahora, solo lo interpretamos como una var_decl normal
        printf("[TypeEasy Runtime] Obteniendo estado para: '%s'\n", node->id);
        interpret_var_decl(node); // Reutiliza la lógica de var_decl
    }
    else if (strcmp(node->type, "BRIDGE_DECL") == 0) {
        interpret_bridge_decl(node);
    }
    else if (strcmp(node->type, "AGENT") == 0) {
        interpret_agent(node); // ¡Tenemos que crear esta función!
    }
    else if (strcmp(node->type, "ACCESS_EXPR") == 0) {
        // Esta sintaxis es reconocida, pero aún no se "ejecuta".
        // La lógica real para 'evaluar' esto iría en
        // 'evaluate_expression' o en una nueva 'interpret_access_expr'.
        // Por ahora, con que el parser lo reconozca es suficiente
        // para que otras partes del código (como 'var_decl') lo usen.
    }
    else if (strcmp(node->type, "OBJECT_LITERAL") == 0) {
        // Reconocido, pero la lógica de 'evaluación'
        // se manejará dentro de 'CALL_METHOD' (sesion.pedido.add)
    }
    else if (strcmp(node->type, "KV_PAIR") == 0) {
        // No se interpreta directamente
    }
    else if (strcmp(node->type, "AGENT_LIST") == 0) {
        interpret_ast(node->left);  // Interpreta el agente/statement anterior
        interpret_ast(node->right); // Interpreta el agente/statement actual
    }
    else if (strcmp(node->type, "LISTENER") == 0) {
        // Normalmente, el 'interpret_agent' manejaría esto,
        // pero por ahora lo dejamos vacío.
    }
    else if (strcmp(node->type, "FOR") == 0)               {
        
        interpret_for(node);
        //printf("[DEBUG] Ejecutando FOR con BYTECODE\n");

        /*clear_bytecode();                // limpia el array de instrucciones
        compile(node);                   // compila ese FOR y sus FOR anidados
        emit(OP_HALT, 0, NULL, NULL);    // marca el fin del bytecode
        run_bytecode(); */
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
        printf("[DEBUG] Generando gráfico...\n");
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


/* ────────────────────────────────────────────────────────────────────────── */

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
        // Puedes extenderlo para +, -, *, /
        return evaluate_number(node->left); // simplificado
    }

    fprintf(stderr, "Error: Tipo no soportado en evaluate_number: %s\n", node->type);
    return 0;
}

void interpret_if(ASTNode *node) {
    if (!node || !node->left) return;  // condición es obligatoria
    
    int result = evaluate_condition(node->left); // Evaluar condición
    
    if (result) {
        interpret_ast(node->right);     // Si es verdadero, ejecutar rama if
    } else if (node->next) {           // Si hay else
        interpret_ast(node->next); // Ejecutar rama else
    }
}

void interpret_match(ASTNode *node) {
    if (!node || !node->left || !node->right) return;

    // For now, we only support string matching
    char* match_value = get_node_string(node->left);

    ASTNode* case_node = node->right;
    while (case_node) {
        if (strcmp(case_node->type, "CASE") == 0) {
            char* case_value = get_node_string(case_node->left);
            if (strcmp(match_value, case_value) == 0) {
                free(case_value);
                interpret_ast(case_node->right);
                break; // Exit after first match
            }
            free(case_value);
        }
        case_node = case_node->next;
    }
    free(match_value);
}



int evaluate_condition(ASTNode* condition) {
    if (!condition) return 0;

    // Si es una comparación
    if (strcmp(condition->type, "GT") == 0 ||
        strcmp(condition->type, "LT") == 0 ||
        strcmp(condition->type, "EQ") == 0) {
        
        int left_val = evaluate_expression(condition->left);
        int right_val = evaluate_expression(condition->right);
        
        if (strcmp(condition->type, "GT") == 0) return left_val > right_val;
        if (strcmp(condition->type, "LT") == 0) return left_val < right_val;
        if (strcmp(condition->type, "EQ") == 0) return left_val == right_val;
    }
    
    // Si es un valor booleano directo
    return evaluate_expression(condition);
}

void generate_plot(double *values, int count) {
    FILE *fp = fopen("plot_data.txt", "w");
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %f\n", i, values[i]);
    }
    fclose(fp);

    // Llama a gnuplot
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
    // Extraemos el número de epochs (por defecto 1)
    int epochs = 1;
    ASTNode *opt = node->right->right;  // aquí viene el train_option
    if (opt && strcmp(opt->id, "epochs") == 0) {
        epochs = opt->value;
    }   
}


static void interpret_predict_node(ASTNode *node) {
    execute_predict(node->left, node->right);
}

static void interpret_call_func(ASTNode *node) {
    if (strcmp(node->id, "concat") != 0) return;
    ASTNode *arg1 = node->left;
    ASTNode *arg2 = arg1 ? arg1->right : NULL;
    char *s1 = get_node_string(arg1);
    char *s2 = get_node_string(arg2);
    size_t len = strlen(s1) + strlen(s2) + 1;
    char *res = malloc(len);
    strcpy(res, s1);
    strcat(res, s2);
    free(s1);
    free(s2);
    ASTNode *lit = create_ast_leaf("STRING", 0, res, NULL);
    add_or_update_variable("__ret__", lit);
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

    // Limpiar __ret_var antes de ejecutar un nuevo método
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
    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) m = m->next;
    if (!m) {
        printf("Error: Método '%s' no encontrado en clase '%s'.\n", node->id, obj->class->name);
        return;
    }
    /* Definir 'this' */
    ASTNode *thisNode = malloc(sizeof(ASTNode));
    thisNode->type = strdup("OBJECT"); thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->value = (int)(intptr_t)obj;
    add_or_update_variable("this", thisNode);
    /* Enlazar parámetros */
    ParameterNode *p = m->params;
    ASTNode      *arg = node->left->left;
    while (p && arg) {
        ASTNode *vn = NULL;

        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            // 1) Literal string
            vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);

        }else if (arg->type &&
                (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
           printf("[DEBUG-CONSTR] enlazando %s como %s\n",
                  arg->id,
                  (find_variable(arg->id)->vtype == VAL_STRING ? "STRING" : "INT"));
            // 2) Variable (ID o IDENTIFIER)
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
            // 3) Cualquier otra expresión numérica
            int val = evaluate_expression(arg);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }

        add_or_update_variable(p->name, vn);
        p   = p->next;
        arg = arg->right;
    }

    /* Ejecutar cuerpo */
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
                return; // Exit, do not fallback
            }
        } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
            ASTNode *objN = return_node->left;
            ASTNode *attrN = return_node->right;
            Variable *ov = find_variable(objN->id);
            if (ov && ov->vtype == VAL_OBJECT) {
                ObjectNode *oobj = ov->value.object_value; int i;
                int idx = -1;
                for (int i=0; i<oobj->class->attr_count; i++) {
                    if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                }
                if (idx>=0) {
                    if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                        lit = create_ast_leaf("STRING",0,oobj->attributes[idx].value.string_value,NULL);
                    else if (strcmp(oobj->attributes[i].type,"float")==0)
                        lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[i].value.float_value),NULL);
                    else
                        lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                }
            }
        } else { // Fallback for numeric expressions
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
        printf("[DEBUG] clone_object: objeto original inválido\n");
        return NULL;
    }

    ObjectNode *clone = malloc(sizeof(ObjectNode));
    clone->class = original->class;
    clone->attributes = malloc(original->class->attr_count * sizeof(Variable));

    for (int i = 0; i < original->class->attr_count; i++) {
        // Inicializa memoria de los metadatos
        clone->attributes[i].id = strdup(original->attributes[i].id);
        clone->attributes[i].type = strdup(original->attributes[i].type);
        clone->attributes[i].vtype = original->attributes[i].vtype;

        // Copia valores correctamente
        if (clone->attributes[i].vtype == VAL_STRING) {
            const char *src = original->attributes[i].value.string_value;
            clone->attributes[i].value.string_value = src ? strdup(src) : strdup("");
        } else if (clone->attributes[i].vtype == VAL_INT) {
            clone->attributes[i].value.int_value = original->attributes[i].value.int_value;
        } else {
            printf("[DEBUG] clone_object: tipo de dato no soportado para atributo '%s'\n", clone->attributes[i].id);
        }
    }

    return clone;
}

#include <stdio.h>
#include <string.h>

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        printf("Error: No se pudo abrir el archivo '%s'\n", filename);
        return NULL;
    }

    //printf("[DEBUG] Leyendo archivo CSV: %s\n", filename);

    char line[512];
    fgets(line, sizeof(line), fp); // omitir encabezado

    ASTNode* first = NULL;
    ASTNode* last = NULL;
    int count = 0;

    while (fgets(line, sizeof(line), fp)) {
        char* nombre = strtok(line, ",");
        char* precio_str = strtok(NULL, ",");

        if (!nombre || !precio_str) {
            printf("[DEBUG] Línea inválida en CSV.\n");
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

    int is_const_flag = node->value; // Recuperamos el flag de const
    //printf("[DEBUG] interpret_var_decl para '%s'\n", node->id);
    const char* declared_type = node->str_value; // El tipo declarado, ej: "INT"
    ASTNode* value_node = node->left;

    Variable *evaluated_value_var = NULL; // Almacenará el resultado de llamadas a método/predict/filter

    // --- Paso 1: Ejecutar método/predict/filter si value_node es uno ---
    if (value_node && (strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0 || strcmp(value_node->type, "FILTER_CALL") == 0)) {
        interpret_ast(value_node); // Ejecutar la llamada
        evaluated_value_var = find_variable("__ret__");
        if (!evaluated_value_var) {
            fprintf(stderr, "Error: No se capturó valor de retorno de la expresión '%s'.\n", value_node->type ? value_node->type : "desconocido");
            exit(1);
        }
    }

    // --- Paso 2: Determinar el tipo efectivo del valor a asignar ---
    const char* effective_value_type_str = NULL;
    if (evaluated_value_var != NULL) {
        effective_value_type_str = evaluated_value_var->type;
    } else if (value_node != NULL) {
        if (strcmp(value_node->type, "NUMBER") == 0) {
            effective_value_type_str = "INT";
        } else if (strcmp(value_node->type, "STRING_LITERAL") == 0 || strcmp(value_node->type, "STRING") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
            // --- CORRECCIÓN ---
            // Evaluar la expresión para determinar si el resultado es INT o FLOAT.
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

    // --- Paso 3: Realizar la validación de tipos si se declaró un tipo ---
    if (declared_type != NULL && effective_value_type_str != NULL) {
        if (strcmp(declared_type, effective_value_type_str) != 0) {
            // EXCEPCIÓN: Permitir asignar un INT a un FLOAT.
            int allow_int_to_float = (strcmp(declared_type, "FLOAT") == 0 && strcmp(effective_value_type_str, "INT") == 0);

            if (!allow_int_to_float) {
                fprintf(stderr, "Error de tipo: No se puede asignar un valor de tipo '%s' a una variable de tipo '%s'.\n", effective_value_type_str, declared_type);
                exit(1); // Detenemos la ejecución por el error de tipo.
            }
        }
    }

    // --- Paso 4: Crear un ASTNode temporal para el valor y asignarlo ---
    ASTNode *value_to_assign_node = NULL;
    if (evaluated_value_var != NULL) {
        // Usar el valor de retorno almacenado de la llamada a método/predict/filter
        if (evaluated_value_var->vtype == VAL_STRING) {
            value_to_assign_node = create_ast_leaf("STRING", 0, strdup(evaluated_value_var->value.string_value), NULL);
        } else if (evaluated_value_var->vtype == VAL_INT) {
            value_to_assign_node = create_ast_leaf_number("INT", evaluated_value_var->value.int_value, NULL, NULL);
        } else if (evaluated_value_var->vtype == VAL_FLOAT) {
            char float_str[64];
            snprintf(float_str, sizeof(float_str), "%f", evaluated_value_var->value.float_value);
            value_to_assign_node = create_ast_leaf("FLOAT", 0, strdup(float_str), NULL);
        } else if (evaluated_value_var->vtype == VAL_OBJECT) {
            // Crear un ASTNode temporal de tipo OBJECT/LIST que apunte al object_value real.
            value_to_assign_node = malloc(sizeof(ASTNode));
            value_to_assign_node->type = strdup(evaluated_value_var->type); // Usar el tipo específico (ej. "LIST", "OBJECT")
            value_to_assign_node->value = (int)(intptr_t)evaluated_value_var->value.object_value;
            value_to_assign_node->id = evaluated_value_var->id ? strdup(evaluated_value_var->id) : NULL;
            value_to_assign_node->str_value = evaluated_value_var->value.string_value ? strdup(evaluated_value_var->value.string_value) : NULL;
            value_to_assign_node->left = NULL;
            value_to_assign_node->right = NULL;
            value_to_assign_node->next = NULL;
            value_to_assign_node->extra = NULL;
        } else {
            fprintf(stderr, "Error interno: Tipo de retorno desconocido para asignación de variable '%s'.\n", node->id);
            exit(1);
        }
        declare_variable(node->id, value_to_assign_node, is_const_flag);
        free_ast(value_to_assign_node); // Liberar el ASTNode temporal

        // Limpiar __ret__ para la siguiente llamada
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                free(__ret_var.value.string_value);
            }
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable)); // Clear the struct
            __ret_var_active = 0;
        }

        return_flag = 0;
        return_node = NULL;
    } else {
        // No es una llamada a método/predict/filter, asignar el value_node original
        declare_variable(node->id, value_node, is_const_flag);
    }

    /* Llamada al constructor si existe */
    if (node->left && strcmp(node->left->type, "OBJECT")==0) {
        Variable *var = find_variable(node->id);
        if (!var || var->vtype!=VAL_OBJECT) return;
        MethodNode *m = var->value.object_value->class->methods;
        while (m && strcmp(m->name,"__constructor")!=0) m=m->next;
        if (m) {
            ParameterNode *p = m->params;
            ASTNode      *arg = node->left->left;
            while (p && arg) {
                ASTNode *vn = NULL;

                if (arg->type && strcmp(arg->type, "STRING") == 0) {
                    // 1) literal string
                    vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);

                } else if (arg->type && (
                        strcmp(arg->type, "ID") == 0 ||
                        strcmp(arg->type, "IDENTIFIER") == 0)) {
                    // 2) variable (ID o IDENTIFIER)
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
                    // 3) cualquier otra expresión numérica
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
   
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("LIST");
    node->left = items;
    node->right = NULL;
    node->next = NULL;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;

    // VALIDACIÓN:
    
    ASTNode *cur = items;
    int index = 0;
    if (!cur) {
       // printf("Advertencia: Lista vacía pasada a create_list_node.\n");
        return node;  // Permitir listas vacías si quieres
    }    
   
    while (cur) {
       
        cur = cur->next;
        index++;
    }
    

    return node;
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

    // Protección contra LIST anidados
    if (strcmp(item->type, "LIST") == 0) {
        printf("Error: No se puede insertar un nodo LIST dentro de una lista.\n");
        return list;
    }

    // Protección contra self-insertion
    if (list == item) {
        printf("Error: Intento de insertar la misma lista dentro de sí misma.\n");
        return list;
    }

    // Si la lista está vacía, crea una nueva
    if (!list) {
        // Validar que item no sea tipo LIST
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
    

    // Validar que es una lista real
    if (strcmp(list->type, "LIST") != 0) {
        //printf("Error: Se esperaba una lista de tipo LIST.\n");
        return NULL;
    }

    // Encadenar correctamente
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
    node->type = strdup("FILTER_CALL");  // o "MAP_CALL" si luego extiendes
    node->id = strdup(funcName);         // "filter"
    node->left = list;
    node->right = lambda;
    node->next = NULL;
    return node;
}


ASTNode* create_lambda_node(const char* argName, ASTNode* body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(argName);  // el nombre de la variable (ej. p)
    node->left = body;           // cuerpo
    node->right = NULL;
    node->next = NULL;
    return node;
}


static void interpret_assign_attr(ASTNode *node) {
    ASTNode *access = node->left;
    ASTNode *value_node = node->right;
    Variable *var = find_variable(access->left->id);
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

    if (declared == NULL) {
        printf("Error: El atributo '%s' no tiene tipo definido.\n", attr_name);
        return;
    }    

    if (strcmp(declared, "string") == 0) {
        // si viene literal STRING (por parseo de "hola")
        if (value_node->str_value) {
          obj->attributes[idx].value.string_value = strdup(value_node->str_value);
        }
        // si viene cualquier referencia a variable (ID o IDENTIFIER)
        else if (value_node->id) {
          Variable *v2 = find_variable(value_node->id);
          if (!v2 || v2->vtype != VAL_STRING) {
            printf("Error: expresión no es una cadena válida.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
        }
        // si viene una llamada a función que devuelve STRING
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
    ASTNode *var_node = node->left;
    ASTNode *value_node = node->right; // El valor a asignar

    if (!var_node || !var_node->id || !value_node) {
        printf("Error: Asignación inválida.\n"); 
        return; 
    }

    // Si el valor a asignar es una expresión matemática (ej: 5 + 3),
    // la evaluamos primero para obtener el resultado final.
    if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
        double result = evaluate_expression(value_node);
        // Creamos un nodo temporal con el resultado para poder asignarlo.
        // Usamos FLOAT para mantener la consistencia con el resultado de evaluate_expression.
        value_node = create_ast_leaf("FLOAT", 0, int_to_string(result), NULL);
    }

    // Esta función se encarga de crear la variable si no existe, o de actualizarla si ya existe.
    // ¡Es exactamente la lógica que querías!
    add_or_update_variable(var_node->id, value_node);
}

static void interpret_print(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        printf("Error: print sin argumento\n");
        return;
    }

    // Literal string directo
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        printf("%s", arg->str_value);
        return;
    }

    // Acceso a atributo: objeto.atributo
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

    // Identificador (nombre de variable)
    if (!arg->id) {
        printf("Error: print sin identificador válido.\n");
        return;
    }

    Variable *v = find_variable(arg->id);
    if (!v) {
        printf("Error: Variable '%s' no definida.\n", arg->id);
        return;
    }

    // Verifica si es una lista (guardada como VAL_OBJECT + type == "LIST")
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
                cur = cur->right;
            }
            printf("]\n");
            return;
        }
    }

    // Escalares
    if (v->vtype == VAL_STRING)
        printf("%s", v->value.string_value);
    else if (v->vtype == VAL_INT)
        printf("%d", v->value.int_value);
    else if (v->vtype == VAL_FLOAT)
        printf("%f", v->value.float_value);
    else
        printf("Objeto de clase: %s\n", v->value.object_value->class->name);

    // Limpiar __ret__ si println lo usó indirectamente
    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
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

    // Literal string directo
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        printf("%s\n", arg->str_value);
        return;
    }

    // Acceso a atributo: objeto.atributo
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

    // Identificador (nombre de variable)
    if (!arg->id) {
        printf("Error: print sin identificador válido.\n");
        return;
    }

    Variable *v = find_variable(arg->id);
    if (!v) {
        printf("Error: Variable '%s' no definida.\n", arg->id);
        return;
    }

    // Verifica si es una lista (guardada como VAL_OBJECT + type == "LIST")
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
                cur = cur->right;
            }
            printf("]\n");
            return;
        }
    }
    
    // Escalares
    if (v->vtype == VAL_STRING)
        printf("%s\n", v->value.string_value);
    else if (v->vtype == VAL_INT)
        printf("%d\n", v->value.int_value);
    else if (v->vtype == VAL_FLOAT)
        printf("%f\n", v->value.float_value);
    else
        printf("Objeto de clase: %s\n", v->value.object_value->class->name);

    // Limpiar __ret__ si println lo usó indirectamente
    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 0;
    }
}


static void interpret_statement_list(ASTNode *node) {
    interpret_ast(node->left);
    interpret_ast(node->right);

    /*ASTNode *current = node;
    while (current) {
        interpret_ast(current->right);
        current = current->left;
    }*/

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
    free(node);
}