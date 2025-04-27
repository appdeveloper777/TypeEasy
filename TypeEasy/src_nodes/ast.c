#include "ast.h"
#include <stdint.h>



#define MAX_CLASSES 50
#define MAX_VARS 100

Variable vars[MAX_VARS];
ClassNode *classes[MAX_CLASSES];

int var_count = 0;
int class_count = 0;


static int    return_flag  = 0;      // indica si hubo RETURN
static ASTNode *return_node = NULL;  // expr que devuelve


// NUEVAS FUNCIONES PARA ACCEDER Y MODIFICAR ATRIBUTOS DE OBJETOS

#include "ast.h"
#include <string.h>

// Crea un ASTNode de tipo CALL_METHOD
ASTNode *create_method_call_node(ASTNode *objectNode,
    const char *methodName,
    ASTNode *args) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type      = strdup("CALL_METHOD");
    node->id        = strdup(methodName);   // nombre del método
    node->left      = objectNode;           // AST del objeto
    node->right     = args;                 // AST de la lista de args (o NULL)
    node->str_value = NULL;
    node->value     = 0;
    return node;
}

ASTNode *add_argument(ASTNode *list, ASTNode *expr) {
    if (!list) return expr;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = expr;
    return list;
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

    ObjectNode *real_obj = create_object(class);  //  Aquí sí se crea el objeto
    ASTNode *obj = (ASTNode *)malloc(sizeof(ASTNode));
    obj->type = strdup("OBJECT");
    obj->left = NULL;
    obj->right = args;
    obj->id = strdup(class->name);
    obj->value = (int)(intptr_t)real_obj;  //  Pasa el puntero real como int

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

void add_method_to_class(ClassNode *cls,
    char *method,
    ParameterNode *params,
    ASTNode *body) {
    MethodNode *m = malloc(sizeof(MethodNode));
    if (!m) exit(1);
    m->name   = strdup(method);
    m->params = params;    // puede ser NULL
    m->body   = body;
    m->next   = cls->methods;
    cls->methods = m;
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

ASTNode *create_function_call_node(const char *funcName, ASTNode *args) {
    ASTNode *n = malloc(sizeof(ASTNode));
    n->type      = strdup("CALL_FUNC");
    n->id        = strdup(funcName);
    n->left      = args;    // lista de argumentos (arg1->right = arg2->right->…)
    n->right     = NULL;
    n->str_value = NULL;
    n->value     = 0;
    return n;
}


ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
   
   /* fprintf(stderr,
        "[DBG create_ast_node] type=%s, left=%s, right=%s\n",
        type,
        left  ? left->type  : "NULL",
        right ? right->type : "NULL"
    );*/

    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    node->type = strdup(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

    // Calcular el resultado si el nodo es una suma
    if (strcmp(type, "ADD") == 0 &&
    ((left->type && strcmp(left->type, "STRING") == 0) ||
     (right->type && strcmp(right->type, "STRING") == 0))) {

        printf("Error: Suma de cadenas no permitida.\n");

    // 1) Obtener s1
    char *s1;
    if (left->type && strcmp(left->type, "STRING") == 0) {
        s1 = left->str_value;
    } else {
        // es un entero, conviértelo
        char buf1[32];
        sprintf(buf1, "%d", left->value);
        s1 = buf1;
    }
    // 2) Obtener s2
    char *s2;
    if (right->type && strcmp(right->type, "STRING") == 0) {
        s2 = right->str_value;
    } else {
        char buf2[32];
        sprintf(buf2, "%d", right->value);
        s2 = buf2;
    }
    // 3) Alocar y concatenar
    size_t len = strlen(s1) + strlen(s2) + 1;
    char *res = malloc(len);
    strcpy(res, s1);
    strcat(res, s2);

    // 4) Devolver literal STRING
    return create_ast_leaf("STRING", 0, res, NULL);
} 

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

// Convierte un int a string dinámica
static char* int_to_string(int x) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", x);
    return strdup(buf);
}

// Obtiene la representación string de *cualquier* ASTNode
static char* get_node_string(ASTNode *node) {
    if (!node) return strdup("");
    // LITERAL
    if (strcmp(node->type, "STRING") == 0) {
        return strdup(node->str_value);
    }
    // ACCESO A ATRIBUTO
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
    // NODO ADD mixto o anidado
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
    // Cualquier otra expresión numérica
    int v = evaluate_expression(node);
    return int_to_string(v);
}


void interpret_ast(ASTNode *node) {
    if (!node) return;

    if (return_flag) return;  // si ya retornó, no ejecutar nada más

    
    // ── Bucle FOR ───────────────────────────────────────────────────────────
    if (strcmp(node->type, "FOR") == 0) {
        add_or_update_variable(node->id, node->left);
        Variable *var = find_variable(node->id);
        if (!var || var->vtype != VAL_INT) {
            printf("Error: Variable de control inválida en FOR.\n");
            return;
        }
        int incremento = node->right->right->left->value;
        int limite      = node->right->value;
        ASTNode *body   = node->right->right->right;
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
    else if (strcmp(node->type, "CALL_FUNC") == 0) {

        printf("concat(%s, %s)\n", get_node_string(node->left), get_node_string(node->right));

        if (strcmp(node->id, "concat") == 0) {

            // asumimos dos args
            ASTNode *arg1 = node->left;
            ASTNode *arg2 = arg1 ? arg1->right : NULL;
            char *s1 = get_node_string(arg1);
            char *s2 = get_node_string(arg2);
            size_t len = strlen(s1) + strlen(s2) + 1;
            char *res = malloc(len);
            strcpy(res, s1);
            strcat(res, s2);
            free(s1); free(s2);
            // guardamos en __ret__
            ASTNode *lit = create_ast_leaf("STRING", 0, res, NULL);
            add_or_update_variable("__ret__", lit);
            return;
        }
        // aquí podrías añadir más funciones nativas…
    }
    

   /* ── Manejo de RETURN dentro de un método ──────────────────────────── */
else if (strcmp(node->type, "RETURN") == 0) {
    // Al encontrar RETURN, guardamos la expresión y marcamos la bandera
    return_node = node->left;
    return_flag = 1;
    return;
}


else if (strcmp(node->type, "CALL_METHOD") == 0) {
    // 1) Obtener objeto y método
    ASTNode *objNode = node->left;
    Variable *v = find_variable(objNode->id);

    return_flag  = 0;
    return_node  = NULL;

    if (!v || v->vtype != VAL_OBJECT) {
        printf("Error: '%s' no es un objeto válido.\n", objNode->id);
        return;
    }
    ObjectNode *obj = v->value.object_value;

    // 2) Buscar MethodNode
    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) {
        m = m->next;
    }
    if (!m) {
        printf("Error: Método '%s' no encontrado en clase '%s'.\n",
               node->id, obj->class->name);
        return;
    }

    // 3) Define 'this'
    ASTNode *thisNode = malloc(sizeof(ASTNode));
    thisNode->type      = strdup("OBJECT");
    thisNode->id        = strdup("this");
    thisNode->left      = thisNode->right = NULL;
    thisNode->value     = (int)(intptr_t)obj;
    add_or_update_variable("this", thisNode);

    // 4) Enlazar parámetros con argumentos
    ParameterNode *p   = m->params;     // puede ser NULL
    ASTNode       *arg = node->right;   // lista de args (o NULL)
    while (p && arg) {
        ASTNode *val_node;
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            val_node = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        } else {
            int iv = evaluate_expression(arg);
            val_node = create_ast_leaf_number("INT", iv, NULL, NULL);
        }
        add_or_update_variable(p->name, val_node);
        p   = p->next;
        arg = arg->right;
    }

    // 5) ¡Aquí estaba el fallo! Ahora sí ejecutamos el cuerpo del método:
    interpret_ast(m->body);
        // … tras interpret_ast(m->body); …

        if (return_flag && return_node) {
            ASTNode *lit = NULL;
    
            // 1) LITERAL STRING puro
            if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
                lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
            }
                   else if (return_node->id) {
                            Variable *v = find_variable(return_node->id);
                            if (v && v->vtype == VAL_STRING) {
                                lit = create_ast_leaf("STRING", 0,
                                         strdup(v->value.string_value), NULL);
                            } else if (v && v->vtype == VAL_INT) {
                                lit = create_ast_leaf_number("INT",
                                         v->value.int_value, NULL, NULL);
                            } else {
                                // por si acaso otro tipo de dato u objeto
                                lit = create_ast_leaf("STRING", 0, "", NULL);
                            }
                        }
            // 2) ACCESO A ATRIBUTO (this.nombre u otro.obj)
            else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
                ASTNode *objNode  = return_node->left;
                ASTNode *attrNode = return_node->right;
                Variable *v = find_variable(objNode->id);
                if (v && v->vtype == VAL_OBJECT) {
                    ObjectNode *obj = v->value.object_value;
                    // busca índice del atributo
                    int idx = -1;
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        if (strcmp(obj->class->attributes[i].id, attrNode->id) == 0) {
                            idx = i;
                            break;
                        }
                    }
                    if (idx >= 0) {
                        // si el atributo es STRING
                        if (strcmp(obj->class->attributes[idx].type, "string") == 0) {
                            lit = create_ast_leaf(
                                "STRING",
                                0,
                                obj->attributes[idx].value.string_value,
                                NULL
                            );
                        } else {
                            // entero
                            lit = create_ast_leaf_number(
                                "INT",
                                obj->attributes[idx].value.int_value,
                                NULL,
                                NULL
                            );
                        }
                    }
                }
                // si algo falla, devolvemos cadena vacía
                if (!lit) {
                    lit = create_ast_leaf("STRING", 0, "", NULL);
                }
            }
            // 3) Cualquier otra expresión numérica
            else {
                int rv = evaluate_expression(return_node);
                lit = create_ast_leaf_number("INT", rv, NULL, NULL);
            }
    
            // guardamos en __ret__ y limpiamos
            add_or_update_variable("__ret__", lit);
            return_flag  = 0;
            return_node  = NULL;
        }
    

    return;
}

    // ── Declaración de variable y ejecución de constructor ────────────────
    else if (strcmp(node->type, "VAR_DECL") == 0) {
        // ── 1) Si node->left es una llamada a método, ejecútala y captura __ret__ ──
        if (node->left && strcmp(node->left->type, "CALL_METHOD") == 0) {
            // Ejecutar la llamada (puebla return_node + return_flag)
            interpret_ast(node->left);
    
            // Recuperar __ret__
            Variable *r = find_variable("__ret__");
            if (!r) {
                printf("Error: No se capturó valor de retorno.\n");
                return;
            }
    
            // Construir un ASTNode literal a partir de __ret__
            ASTNode *lit;
            if (r->vtype == VAL_STRING) {
                lit = create_ast_leaf("STRING", 0, r->value.string_value, NULL);
            } else {
                lit = create_ast_leaf_number("INT", r->value.int_value, NULL, NULL);
            }
    
            // Declarar la variable con ese literal
            declare_variable(node->id, lit);
    
            // Limpiar el estado de retorno
            return_flag  = 0;
            return_node  = NULL;
            return;
        }
    
        // ── 2) Flujo normal de declaración ───────────────────────────────────────
        // 2.1) Declara la variable con el AST que venga (literal, expresión, NEW, etc)
        declare_variable(node->id, node->left);
    
        // 2.2) Si node->left es NEW Clase(...), invocar constructor
        if (node->left && strcmp(node->left->type, "OBJECT") == 0) {
            Variable *var = find_variable(node->id);
            if (!var || var->vtype != VAL_OBJECT) return;
    
            // Buscar el MethodNode del constructor
            MethodNode *m = var->value.object_value->class->methods;
            while (m && strcmp(m->name, "__constructor") != 0) {
                m = m->next;
            }
            if (m) {
                // Enlazar parámetros con argumentos
                ParameterNode *p   = m->params;
                ASTNode       *arg = node->left->right;
                while (p && arg) {
                    ASTNode *val_node;
                    if (arg->type && strcmp(arg->type, "STRING") == 0) {
                        val_node = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    } else {
                        int v = evaluate_expression(arg);
                        val_node = create_ast_leaf_number("INT", v, NULL, NULL);
                    }
                    add_or_update_variable(p->name, val_node);
                    p   = p->next;
                    arg = arg->right;
                }
                // Ejecutar el constructor
                call_method(var->value.object_value, "__constructor");
            }
        }
    }
    
    

    // ── Asignación a atributo (int o string) ──────────────────────────────
    else if (strcmp(node->type, "ASSIGN_ATTR") == 0) {
        ASTNode *access     = node->left;
        ASTNode *value_node = node->right;

        // 1) Objeto base
        Variable *var = find_variable(access->left->id);
        if (!var || var->vtype != VAL_OBJECT) {
            printf("Error: Objeto '%s' no definido o no es un objeto.\n",
                   access->left->id);
            return;
        }
        ObjectNode *obj = var->value.object_value;

        // 2) Nombre del atributo
        const char *attr_name = access->right->id;

        // 3) Busca índice en la clase
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, attr_name) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            printf("Error: Atributo '%s' no encontrado en clase '%s'.\n",
                   attr_name, obj->class->name);
            return;
        }

        // 4) Asigna según tipo declarado
        const char *declared = obj->class->attributes[idx].type;
        if (strcmp(declared, "string") == 0) {
            // cadena literal o variable string
            if (value_node->str_value) {
                obj->attributes[idx].value.string_value = strdup(value_node->str_value);
            } else {
                Variable *v = find_variable(value_node->id);
                if (!v || v->vtype != VAL_STRING) {
                    printf("Error: expresión no es una cadena válida.\n");
                    return;
                }
                obj->attributes[idx].value.string_value = strdup(v->value.string_value);
            }
            obj->attributes[idx].vtype = VAL_STRING;
        } else {
            // entero u otras expresiones
            int val = evaluate_expression(value_node);
            obj->attributes[idx].value.int_value = val;
            obj->attributes[idx].vtype = VAL_INT;
        }
    }

    // ── Asignación simple: x = expr ────────────────────────────────────────
    else if (strcmp(node->type, "ASSIGN") == 0) {
        ASTNode *var_node  = node->left;
        ASTNode *expr_node = node->right;
        if (!var_node || !expr_node) {
            printf("Error: Asignación inválida.\n");
            return;
        }
        int result = evaluate_expression(expr_node);
        ASTNode *value_node = create_ast_leaf("INT", result, NULL, NULL);
        add_or_update_variable(var_node->id, value_node);
    }

    // ── PRINT, con soporte para literales, variables y atributos ─────────
    else if (strcmp(node->type, "PRINT") == 0) {
        ASTNode *arg = node->left;
        if (!arg) {
            printf("Error: print sin argumento\n");
            return;
        }
        // 1) literal string directo
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            printf("Output: %s\n", arg->str_value);
            return;
        }
        // 2) acceso a atributo
        if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
            ASTNode *o = arg->left;
            ASTNode *a = arg->right;
            Variable *v = find_variable(o->id);
            if (!v || v->vtype != VAL_OBJECT) {
                printf("Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
                return;
            }
            ObjectNode *obj = v->value.object_value;
            // busca índice
            int idx = -1;
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                printf("Error: Atributo '%s' no encontrado en clase '%s'.\n",
                       a->id, obj->class->name);
                return;
            }
            Variable *attr = &obj->attributes[idx];
            if (attr->vtype == VAL_STRING) {
                printf("Output: %s\n", attr->value.string_value);
            } else {
                printf("Output: %d\n", attr->value.int_value);
            }
            return;
        }
        // 3) variable simple
        if (!arg->id) {
            printf("Error: print sin identificador válido.\n");
            return;
        }
        Variable *v = find_variable(arg->id);
        if (!v) {
            printf("Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_STRING) {
            printf("Output: %s\n", v->value.string_value);
        } else if (v->vtype == VAL_INT) {
            printf("Output: %d\n", v->value.int_value);
        } else {
            printf("Output: Objeto de clase: %s\n", v->value.object_value->class->name);
        }
    }

    // ── Lista de sentencias: recórrela ─────────────────────────────────────
    else if (strcmp(node->type, "STATEMENT_LIST") == 0) {
        interpret_ast(node->right);
        interpret_ast(node->left);
    }

    // ── Nodo no reconocido ────────────────────────────────────────────────
    else {
        // Nada que hacer
    }
}

void add_constructor_to_class(ClassNode *class,
    ParameterNode *params,
    ASTNode *body) {
    // Crea un MethodNode para el constructor
    MethodNode *ctor = malloc(sizeof(MethodNode));
    if (!ctor) exit(1);
    ctor->name = strdup("__constructor");  // o "constructor"
    ctor->params = params;
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

/* ast.c */
ASTNode *create_return_node(ASTNode *expr) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type      = strdup("RETURN");
    node->id        = NULL;
    node->left      = expr;    // la expresión a retornar
    node->right     = NULL;
    node->str_value = NULL;
    node->value     = 0;
    return node;
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
