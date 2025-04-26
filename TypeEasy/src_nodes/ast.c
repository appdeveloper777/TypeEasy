#include "ast.h"
#include <stdint.h>



#define MAX_CLASSES 50
#define MAX_VARS 100

Variable vars[MAX_VARS];
ClassNode *classes[MAX_CLASSES];

int var_count = 0;
int class_count = 0;

// NUEVAS FUNCIONES PARA ACCEDER Y MODIFICAR ATRIBUTOS DE OBJETOS

#include "ast.h"
#include <string.h>

// Crea un ASTNode de tipo CALL_METHOD
ASTNode *create_method_call_node(ASTNode *objectNode, const char *methodName) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type      = strdup("CALL_METHOD");
    node->id        = strdup(methodName);    // nombre del método
    node->left      = objectNode;           // AST del objeto
    node->right     = NULL;
    node->str_value = NULL;
    node->value     = 0;
    return node;
}

// Acceso a atributo: obj.edad
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

// Asignación de atributo: obj.edad = 30;
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


ASTNode *create_object_with_args(ClassNode *class, ASTNode *args) {
    if (!class) {
        printf("Error: Clase no encontrada.\n");
        return NULL;
    }

    ObjectNode *real_obj = create_object(class);  // 🔹 Aquí sí se crea el objeto
    ASTNode *obj = (ASTNode *)malloc(sizeof(ASTNode));
    obj->type = strdup("OBJECT");
    obj->left = NULL;
    obj->right = args;
    obj->id = strdup(class->name);
    obj->value = (int)(intptr_t)real_obj;  // ✅ Pasa el puntero real como int

    return obj;
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

ClassNode *find_class(char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i]->name, name) == 0) {
            return classes[i];  // Clase encontrada
        }
    }
    printf("Error: Clase '%s' no encontrada.\n", name);
    return NULL;  // Clase no encontrada
}


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

void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type) {
    if (!class) return;
    
    class->attributes = realloc(class->attributes, (class->attr_count + 1) * sizeof(Variable));
    class->attributes[class->attr_count].id = strdup(attr_name);
    class->attributes[class->attr_count].type = strdup(attr_type);  // Guardamos el tipo
    class->attr_count++;
}

void add_method_to_class(ClassNode *class, char *method, ASTNode *body) {
    MethodNode *new_method = (MethodNode *)malloc(sizeof(MethodNode));
    new_method->name = strdup(method);
    new_method->body = body;
    new_method->next = class->methods;
    class->methods = new_method;
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

void call_method(ObjectNode *obj, char *method) {
    // 1) Crear un ASTNode que represente 'this'
    ASTNode *thisNode = (ASTNode *)malloc(sizeof(ASTNode));
    thisNode->type  = strdup("OBJECT");
    thisNode->id    = strdup("this");
    thisNode->left  = thisNode->right = NULL;
    thisNode->value = (int)(intptr_t)obj;

    // 2) Guardar/actualizar la variable 'this' en el entorno
    add_or_update_variable("this", thisNode);

    // 3) Buscar y ejecutar el método
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


ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

    // Calcular el resultado si el nodo es una suma
    if (strcmp(type, "ADD") == 0) {
        node->value = left->value + right->value;
    }
    /*  else  if (strcmp(type, "ASSIGN") == 0) {
       
            node->value = right->value;    
             printf("node->value %d = left->value %d + right->value %d;\n", node->value, left->value, right->value);

    } */
    else if (strcmp(type, "SUB") == 0) {
        node->value = left->value - right->value;
    } else if (strcmp(type, "MUL") == 0) {
        node->value = left->value * right->value;
    } else if (strcmp(type, "DIV") == 0) {
        if (right->value != 0) {
            node->value = left->value / right->value;
        } else {
            printf("Error: División por cero.\n");
            node->value = 0; // O manejar error
        }
    } else {
        node->value = 0;  // Si no es una operación matemática, deja el valor en 0.
    }

    return node;
}


ASTNode *create_int_node(int value) {    
    return create_ast_leaf_number("INT", value, NULL, NULL);
}

ASTNode *create_float_node(int value) {    
    return create_ast_leaf_number("FLOAT", value, NULL, NULL);
}

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
    if (node == NULL) {
        return NULL; // Manejo de error si malloc falla
    }

    node->type = strdup(type); // Duplicar el tipo
    node->left = NULL;
    node->right = NULL;

    node->value = value;

    // Duplicar str_value si no es NULL
    node->str_value = str_value ? strdup(str_value) : NULL;

    // Duplicar id si no es NULL
    node->id = id ? strdup(id) : NULL;

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

ASTNode *create_string_node(char *value) {
    return create_ast_leaf("STRING", 0, value, NULL);
}

ASTNode *add_statement(ASTNode *list, ASTNode *stmt) {
    if (!list) return stmt;
    
    ASTNode *current = list;
    while (current->right) {
        current = current->right;
    }
    current->right = stmt; // Conectar el siguiente statement correctamente

    return list;
}


void interpret_ast(ASTNode *node) {
    if (!node) return;

   // printf("[AST] Ejecutando nodo tipo: %s\n", node->type);


    if (strcmp(node->type, "FOR") == 0) {
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

        // ── Llamada a método: obj.metodo()
        else if (strcmp(node->type, "CALL_METHOD") == 0) {
            // 1) Obtener variable
            ASTNode *objNode = node->left;
            if (!objNode->id) {
                printf("Error: llamada inválida (no hay identificador de objeto).\n");
                return;
            }
            Variable *var = find_variable(objNode->id);
            if (!var || var->vtype != VAL_OBJECT) {
                printf("Error: '%s' no es un objeto.\n", objNode->id);
                return;
            }
            // 2) Ejecutar el método
            call_method(var->value.object_value, node->id);
            return;
        }
    

    else if (strcmp(node->type, "VAR_DECL") == 0) {
        //printf("[DEBUG] Declaring Variable: %s\n", node->id);
        declare_variable(node->id, node->left);

        /* 2) Si node->left es un objeto, invoco su constructor */
        if (node->left && strcmp(node->left->type, "OBJECT") == 0) {
            /* recupero la Variable recién creada */
            Variable *var = find_variable(node->id);
            /* ejecuto el constructor (__constructor) */
            call_method(var->value.object_value, "__constructor");
        }

    }

    else if (strcmp(node->type, "ASSIGN_ATTR") == 0) {
        ASTNode *access = node->left;
        ASTNode *value_node = node->right;
    
        if (!access || !value_node) {
            printf("Error: asignación de atributo inválida.\n");
            return;
        }
    
        Variable *var = find_variable(access->left->id);
        if (!var || var->vtype != VAL_OBJECT) {
            printf("Error: Objeto '%s' no definido o no es un objeto.\n", access->left->id);
            return;
        }
    
        int val = evaluate_expression(value_node);
        set_attribute_value(var->value.object_value, access->right->id, val);
        //printf("[DEBUG] ASSIGN_ATTR Asignación: %s.%s = %d\n", access->left->id, access->right->id, val);
    }
    
    

    else if (strcmp(node->type, "ASSIGN") == 0) {
        ASTNode *var_node = node->left;
        ASTNode *expr_node = node->right;

        if (!var_node || !expr_node) {
            printf("Error: Asignación inválida.\n");
            return;
        }

        int result = evaluate_expression(expr_node);
        ASTNode *value_node = create_ast_leaf("INT", result, NULL, NULL);
        add_or_update_variable(var_node->id, value_node);
        //printf("[DEBUG] Asignación: %s = %d\n", var_node->id, result);
    }

    else if (strcmp(node->type, "PRINT") == 0) {
       // printf("[DEBUG] Entró a PRINT\n");

       ASTNode *arg = node->left;
    if (!arg) {
        printf("Error: print sin argumento\n");
        return;
    }

    // 1) Cadena literal
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        printf("Output: %s\n", arg->str_value);
        return;
    }
    
        if (!node->left) {
            printf(" PRINT: node->left es NULL\n");
            return;
        }

       // printf("[DEBUG] PRINT: node->left: %s\n", node->left);
    
        if (node->left && node->left->type) {
            //printf("[DEBUG] PRINT: node->left->type: %s\n", node->left->type);
        } else if (node->left && node->left->id) {
          //  printf("[DEBUG] PRINT: node->left sin type pero con id: %s\n", node->left->id);
        } else {
          //  printf("[DEBUG] PRINT: node->left->type es NULL y no tiene id\n");
        }
    
        // 🔹 Manejo de print(obj.edad);
        if (node->left->type && strcmp(node->left->type, "ACCESS_ATTR") == 0) {
            ASTNode *objNode = node->left->left;
            ASTNode *attrNode = node->left->right;
    
            Variable *var = find_variable(objNode->id);
            if (!var || var->vtype != VAL_OBJECT) {
                printf("Error: Objeto '%s' no definido o no es un objeto.\n", objNode->id);
                return;
            }
    
            int val = get_attribute_value(var->value.object_value, attrNode->id);
            printf("Output Atributo: %s.%s = %d\n", objNode->id, attrNode->id, val);
            return;
        }
    
        if (!node->left->id) {
            printf(" PRINT: node->left->id es NULL\n");
            return;
        }
    
       // printf("[DEBUG] PRINT: Buscando variable con id: %s\n", node->left->id);
    
        Variable *var = find_variable(node->left->id);
        if (!var) {
            printf("Error: Variable '%s' no definida.\n", node->left->id);
            return;
        }
    
        switch (var->vtype) {
            case VAL_STRING:
                printf("Output: %s\n", var->value.string_value);
                break;
            case VAL_INT:
                printf("Output Numero: %d\n", var->value.int_value);
                break;
            case VAL_OBJECT:
                printf("Output Objeto de clase: %s\n", var->value.object_value->class->name);
                break;
            default:
                printf("Output: tipo desconocido\n");
        }
    }
    
    

    else if (strcmp(node->type, "STATEMENT_LIST") == 0) {
       // printf("[AST] Recorriendo lista: RIGHT ➜ LEFT\n");
        interpret_ast(node->right); // primero el más antiguo
        interpret_ast(node->left);  // luego el más reciente
    }
    
   

    else {
        //printf("[DEBUG] Nodo no manejado: %s\n", node->type);
    }
}

void add_constructor_to_class(ClassNode *class, ASTNode *body) {
    // Crea un MethodNode para el constructor
    MethodNode *ctor = malloc(sizeof(MethodNode));
    if (!ctor) exit(1);
    ctor->name = strdup("__constructor");  // o "constructor"
    ctor->body = body;
    // Enlaza al frente de la lista de métodos
    ctor->next = class->methods;
    class->methods = ctor;
}

int evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    //  Si es una variable, buscar su valor
    if (node->id) {
        Variable *var = find_variable(node->id);
        if (var && var->vtype == VAL_INT) {
            return var->value.int_value;
        } else {
            printf("Error: Variable '%s' no definida o no es un entero.\n", node->id);
            return 0;
        }
    }

    // 🚀 Si es una operación matemática, evaluar ambos lados
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

    // 🚀 Si es un número literal directo, devolverlo
    return node->value;
}



void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;

    // Detectar si existe
    Variable *var = find_variable_for(id);

    if (var) {
        // ACTUALIZAR
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
        // CREAR VARIABLE NUEVA SIN buscarla de nuevo
        if (var_count >= MAX_VARS) {
            printf("Error: Demasiadas variables declaradas.\n");
            return;
        }

        //printf("[DEBUG] AGREGANDO VARIABLE: id=%s, tipo=%s, tipo interno=%s\n",
          //     id, value->type, (strcmp(value->type, "OBJECT") == 0 ? "OBJETO" : "ESCALAR"));

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

        //printf("[DEBUG] Almacenando variable nueva: %s de tipo %s en posición [%d]\n", id, value->type, var_count);

        var_count++;
    }
}




void declare_variable(char *id, ASTNode *value) {
    if (var_count >= MAX_VARS) {
        printf("Error: Demasiadas variables declaradas.\n");
        return;
    }

    //printf(" [DEBUG] Declarando nueva variable: %s de tipo %s\n", id, value->type);

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

    //printf(" [DEBUG] Variable '%s' almacenada en posición [%d]\n", id, var_count);
    var_count++;
}

void add_variable(char *id, int value) {
    if (var_count >= MAX_VARS) {
        printf("Error: Too many variables declared.\n");
        return;
    }

    vars[var_count].id = strdup(id);
    vars[var_count].type = strdup("int");
    vars[var_count].vtype = VAL_INT;
    vars[var_count].value.int_value = value;
    var_count++;
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




Variable *find_variable(char *id) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}


void add_variable_for(char *id, int value) {
    if (var_count >= MAX_VARS) {
        printf("Error: Too many variables declared.\n");
        return;
    }

    vars[var_count].id = strdup(id);
    vars[var_count].type = strdup("int");
    vars[var_count].vtype = VAL_INT;
    vars[var_count].value.int_value = value;
    var_count++;
}



Variable *find_variable_for(char *id) {
    //printf("[DEBUG] Buscando variable: %s\n", id);
   /* for (int i = 0; i < var_count; i++) {
       // printf("[DEBUG]  - %s = ", vars[i].id);
        switch (vars[i].vtype) {
            case VAL_INT:
                printf("%d", vars[i].value.int_value);
                break;
            case VAL_STRING:
                printf("\"%s\"", vars[i].value.string_value);
                break;
            case VAL_OBJECT:
                if (vars[i].value.object_value && vars[i].value.object_value->class) {
                    //printf("[DEBUG] <objeto de clase %s>", vars[i].value.object_value->class->name);
                } else {
                    printf("<objeto nulo>");
                }
                break;
            default:
                printf("tipo desconocido");
        }
       // printf("[DEBUG] (Dirección: %p)\n", (void*)&vars[i]);
    }*/

    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            //printf("[DEBUG] Variable encontrada: %s (Dirección: %p)\n", vars[i].id, (void*)&vars[i]);
            return &vars[i];
        }
    }

    //printf("[DEBUG] Variable %s no encontrada\n", id);
    return NULL;
}




void free_ast(ASTNode *node) {
    if (!node) return;
    free(node->type);
    if (node->str_value) free(node->str_value);
    if (node->id) free(node->id);
    free_ast(node->left);
    free_ast(node->right);
    free(node);
}
