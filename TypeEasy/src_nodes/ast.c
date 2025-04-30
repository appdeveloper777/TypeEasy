#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ====================== CONSTANTES Y ESTRUCTURAS GLOBALES ======================
#define MAX_CLASSES 50
#define MAX_VARS 100

// Variables globales
Variable vars[MAX_VARS];
ClassNode *classes[MAX_CLASSES];
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
        obj->attributes[i].id = strdup(class->attributes[i].id);
        obj->attributes[i].value = class->attributes[i].value;
    }
    return obj;
}

// ====================== MANEJO DE VARIABLES ======================

Variable *find_variable(char *id) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}

Variable *find_variable_for(char *id) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}

void declare_variable(char *id, ASTNode *value) {
    if (var_count >= MAX_VARS) {
        printf("Error: Demasiadas variables declaradas.\n");
        return;
    }

    vars[var_count].id = strdup(id);
    vars[var_count].type = strdup(value->type);

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

void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;

    Variable *var = find_variable_for(id);

    if (var) {
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
    obj->left = NULL;
    obj->right = args;
    obj->id = strdup(class->name);
    obj->value = (int)(intptr_t)real_obj;

    return obj;
}

ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup(type);
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

int evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    // Variable
    if (node->id) {
        Variable *var = find_variable(node->id);
        if (var) {
            if (var->vtype == VAL_INT) {
                return var->value.int_value;
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
    
    // Operaciones matemáticas
    if (strcmp(node->type, "ADD") == 0) {
        return evaluate_expression(node->left) + evaluate_expression(node->right);
    } else if (strcmp(node->type, "SUB") == 0) {
        return evaluate_expression(node->left) - evaluate_expression(node->right);
    } else if (strcmp(node->type, "MUL") == 0) {
        return evaluate_expression(node->left) * evaluate_expression(node->right);
    } else if (strcmp(node->type, "DIV") == 0) {
        int right = evaluate_expression(node->right);
        if (right == 0) {
            printf("Error: División por cero.\n");
            return 0;
        }
        return evaluate_expression(node->left) / right;
    }

    // Número literal
    return node->value;
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

/* Refactorización de interpret_ast en funciones auxiliares para mejorar legibilidad */

/* Refactorización de interpret_ast en funciones auxiliares para mejorar legibilidad */

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
static void interpret_statement_list(ASTNode *node);


/* ─── DATASET ─────────────────────────────────────────────────────────── */
static void interpret_dataset(ASTNode *node) {
    // Guardamos el ASTNode del dataset en una variable con nombre node->id
    add_or_update_variable(node->id, node);
    // Mensaje de depuración mostrando el origen
    printf("[DEBUG] Dataset '%s' cargado desde '%s'\n",
           node->id,
           node->str_value  /* asegúrate de que str_value almacena la ruta */);
}


void interpret_ast(ASTNode *node) {
    if (!node || return_flag) return;

    /* Despacho según tipo de nodo */
    if (strcmp(node->type, "FOR") == 0)                interpret_for(node);
    else if (strcmp(node->type, "DATASET") == 0)       interpret_dataset(node);
    else if (strcmp(node->type, "MODEL") == 0 || strcmp(node->type, "OBJECT") == 0) interpret_model_object(node);
    else if (strcmp(node->type, "TRAIN") == 0)         interpret_train_node(node);
    else if (strcmp(node->type, "PREDICT") == 0)       interpret_predict_node(node);
    else if (strcmp(node->type, "CALL_FUNC") == 0)     interpret_call_func(node);
    else if (strcmp(node->type, "RETURN") == 0)        interpret_return_node(node);
    else if (strcmp(node->type, "CALL_METHOD") == 0)   interpret_call_method(node);
    else if (strcmp(node->type, "VAR_DECL") == 0)      interpret_var_decl(node);
    else if (strcmp(node->type, "ASSIGN_ATTR") == 0)   interpret_assign_attr(node);
    else if (strcmp(node->type, "ASSIGN") == 0)        interpret_assign(node);
    else if (strcmp(node->type, "PRINT") == 0)         interpret_print(node);
    else if (strcmp(node->type, "STATEMENT_LIST") == 0)interpret_statement_list(node);
}

/* ────────────────────────────────────────────────────────────────────────── */

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

    printf("[DEBUG] Entrenando modelo '%s' con dataset '%s' durante %d epochs\n",
           node->left->id,
           node->right->id,
           epochs);
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
    ASTNode *arg = node->right;
    while (p && arg) {
        ASTNode *val_node;
        if (arg->type && strcmp(arg->type, "STRING") == 0)
            val_node = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        else {
            int iv = evaluate_expression(arg);
            val_node = create_ast_leaf_number("INT", iv, NULL, NULL);
        }
        add_or_update_variable(p->name, val_node);
        p = p->next;
        arg = arg->right;
    }
    /* Ejecutar cuerpo */
    interpret_ast(m->body);
    if (return_flag && return_node) {
        ASTNode *lit = NULL;
        /* Manejo de retorno */
        if (return_node->type && strcmp(return_node->type, "STRING") == 0)
            lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
        else if (return_node->id) {
            Variable *rv = find_variable(return_node->id);
            if (rv && rv->vtype == VAL_STRING)
                lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
            else if (rv && rv->vtype == VAL_INT)
                lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
        }
        else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
            ASTNode *objN = return_node->left;
            ASTNode *attrN = return_node->right;
            Variable *ov = find_variable(objN->id);
            if (ov && ov->vtype == VAL_OBJECT) {
                ObjectNode *oobj = ov->value.object_value;
                int idx = -1;
                for (int i=0; i<oobj->class->attr_count; i++) {
                    if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                }
                if (idx>=0) {
                    if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                        lit = create_ast_leaf("STRING",0,oobj->attributes[idx].value.string_value,NULL);
                    else
                        lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                }
            }
        }
        if (!lit) {
            int rv = evaluate_expression(return_node);
            lit = create_ast_leaf_number("INT", rv, NULL, NULL);
        }
        add_or_update_variable("__ret__", lit);
        return_flag = 0;
        return_node = NULL;
    }
}

static void interpret_var_decl(ASTNode *node) {
    if (node->left && (strcmp(node->left->type, "CALL_METHOD") == 0 || strcmp(node->left->type, "PREDICT") == 0)) {
        interpret_ast(node->left);
        Variable *r = find_variable("__ret__");
        if (!r) { printf("Error: No se capturó valor de retorno.\n"); return; }
        ASTNode *lit = r->vtype==VAL_STRING
            ? create_ast_leaf("STRING",0,r->value.string_value,NULL)
            : create_ast_leaf_number("INT",r->value.int_value,NULL,NULL);
        declare_variable(node->id, lit);
        /* Limpieza */
        Variable *cleanup = find_variable("__ret__");
        if (cleanup) {
            free(cleanup->id); free(cleanup->type);
            cleanup->id = strdup("__deleted__"); cleanup->type = strdup("deleted"); cleanup->vtype=VAL_INT; cleanup->value.int_value=0;
        }
        return_flag=0; return_node=NULL; return;
    }
    declare_variable(node->id, node->left);
    /* Llamada al constructor si existe */
    if (node->left && strcmp(node->left->type, "OBJECT")==0) {
        Variable *var = find_variable(node->id);
        if (!var || var->vtype!=VAL_OBJECT) return;
        MethodNode *m = var->value.object_value->class->methods;
        while (m && strcmp(m->name,"__constructor")!=0) m=m->next;
        if (m) {
            ParameterNode *p=m->params;
            ASTNode *arg=node->left->right;
            while(p && arg) {
                ASTNode *vn = (arg->type && strcmp(arg->type,"STRING")==0)
                    ? create_ast_leaf("STRING",0,arg->str_value,NULL)
                    : create_ast_leaf_number("INT",evaluate_expression(arg),NULL,NULL);
                add_or_update_variable(p->name,vn);
                p=p->next; arg=arg->right;
            }
            call_method(var->value.object_value,"__constructor");
        }
    }
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
    const char *declared = obj->class->attributes[idx].type;
    if(strcmp(declared,"string")==0) {
        if(value_node->str_value)
            obj->attributes[idx].value.string_value = strdup(value_node->str_value);
        else {
            Variable *v2=find_variable(value_node->id);
            if(!v2||v2->vtype!=VAL_STRING){ printf("Error: expresión no es una cadena válida.\n"); return; }
            obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
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
    ASTNode *expr_node = node->right;
    if (!var_node || !expr_node) { printf("Error: Asignación inválida.\n"); return; }
    int result = evaluate_expression(expr_node);
    ASTNode *value_node = create_ast_leaf("INT", result, NULL, NULL);
    add_or_update_variable(var_node->id, value_node);
}

static void interpret_print(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) { printf("Error: print sin argumento\n"); return; }
    if (arg->type && strcmp(arg->type,"STRING")==0) {
        printf("Output: %s\n", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type,"ACCESS_ATTR")==0) {
        ASTNode *o = arg->left; ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if(!v||v->vtype!=VAL_OBJECT){ printf("Error: Objeto '%s' no definido o no es un objeto.\n", o->id); return; }
        ObjectNode *obj = v->value.object_value;
        int idx=-1;
        for(int i=0;i<obj->class->attr_count;i++){ if(strcmp(obj->class->attributes[i].id,a->id)==0){idx=i;break;} }
        if(idx<0){ printf("Error: Atributo '%s' no encontrado en clase '%s'.\n", a->id, obj->class->name); return; }
        Variable *attr=&obj->attributes[idx];
        if(attr->vtype==VAL_STRING) printf("Output: %s\n", attr->value.string_value);
        else printf("Output: %d\n", attr->value.int_value);
        return;
    }
    if (!arg->id) { printf("Error: print sin identificador válido.\n"); return; }
    Variable *v = find_variable(arg->id);
    if (!v) { printf("Error: Variable '%s' no definida.\n", arg->id); return; }
    if (v->vtype==VAL_STRING) printf("Output: %s\n", v->value.string_value);
    else if (v->vtype==VAL_INT) printf("Output: %d\n", v->value.int_value);
    else printf("Output: Objeto de clase: %s\n", v->value.object_value->class->name);
}

static void interpret_statement_list(ASTNode *node) {
    interpret_ast(node->right);
    interpret_ast(node->left);
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