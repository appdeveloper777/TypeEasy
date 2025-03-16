#include "ast.h"



#define MAX_VARS 100

Variable vars[MAX_VARS];
int var_count = 0;


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

    //printf("Interpreting Node Type: %s\n", node->type);

    
    int contador = 0;
    if (strcmp(node->type, "FOR") == 0) {
        add_or_update_variable(node->id, node->left->value);
    
        int incremento = node->right->right->left->value;
        int limite = node->right->value;
    
        while (find_variable(node->id)->value < limite) {  // 🔹 Evita la iteración extra cuando `j == 3`
           
            contador = contador + 1;
            int valor_actual = find_variable(node->id)->value;
    
            ASTNode *body = node->right->right->right;
            if (!body) {
                printf("Advertencia: FOR sin cuerpo\n");
                break;
            }
    
            ASTNode *stmt = body;
            while (stmt) {
                int var_value = find_variable(node->id)->value;
    
                if (var_value >= limite) {  
                    break;  // 🔹 Asegurar que `FOR2` no se ejecuta cuando `j == 3`
                }
    
                interpret_ast(stmt);
                stmt = stmt->right;
            }
    
            find_variable(node->id)->value += incremento;
        }
    }
     
    else if (strcmp(node->type, "VAR_DECL") == 0) {
        //printf("Declaring Variable: %s\n", node->id);
        add_or_update_variable(node->id, node->left);  // Store the variable
    }
    else if (strcmp(node->type, "ASSIGN") == 0) {
        ASTNode *var_node = node->left;   // Variable que se asignará
        ASTNode *expr_node = node->right; // Expresión de la asignación

        if (!var_node || !expr_node) {
            printf("Error: Asignación inválida.\n");
            return;
        }

        // 🔹 Evaluar la expresión antes de asignarla
        int value = evaluate_expression(expr_node);

        add_or_update_variable(var_node->id, value);
        printf("Asignación: %s = %d\n", var_node->id, value);
    }

    
    else if (strcmp(node->type, "PRINT") == 0) {
        ASTNode *expr = node->left;
        
        if (!expr) {
            printf("Error: PRINT statement has no expression.\n");
            return;
        }

        // Look up variable if it's an identifier
        if (expr->id) {
            //("Looking for variable: %s\n", expr->id);
            ASTNode *value = find_variable(expr->id);
            if (!value) {
                printf("Error: Variable '%s' not defined.\n", expr->id);
                return;
            }
            expr = value;  // Replace with actual value
        }

        // Print value
        if (expr->str_value) {
            printf("Output: %s\n", expr->str_value);
        } else {
            printf("Output Numero: %d\n", expr->value);
        }
       // printf("Output Numero: %d\n", expr->value);
    }
    else if (strcmp(node->type, "STATEMENT_LIST") == 0) {
        interpret_ast(node->left);
        interpret_ast(node->right);
    }
}

int evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    // 🚀 Si es una variable, buscar su valor
    if (node->id) {
        ASTNode *var = find_variable(node->id);
        if (var) {
            return var->value;
        } else {
            printf("Error: Variable '%s' no definida.\n", node->id);
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

    // 🚀 Si es un número, devolverlo
    return node->value;
}


void add_or_update_variable(char *id, int value) {
    ASTNode *var = find_variable(id);
    if (var) {
       // printf("Actualizando variable: %s = %d\n", id, value);
        var->value = value;
    } else {
        if (var_count >= MAX_VARS) {
            printf("Error: Too many variables declared.\n");
            return;
        }
        vars[var_count].id = strdup(id);
        vars[var_count].value = value;
      //  printf("Variable agregada: %s = %d\n", id, value);
      //  printf("Dirección de memoria de nueva variable %s: %p\n", id, (void*)&vars[var_count]);
        var_count++;
    }
}



void add_variable(char *id, int value) {
    if (var_count >= MAX_VARS) {
        printf("Error: Too many variables declared.\n");
        return;
    }
    vars[var_count].id = strdup(id);
    vars[var_count].value = value;
  //  printf("Variable agregada: %s = %d\n", id, value);
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




ASTNode *find_variable(char *id) {
    for (int i = 0; i < var_count; i++) {
       // printf("strcmp(vars[i].id %s: |", vars[i].id);
        if (strcmp(vars[i].id, id) == 0) {
            return vars[i].value;
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
    vars[var_count].value = value;
    var_count++;
}


Variable *find_variable_for(char *id) { 
    printf("Buscando variable: %s\n", id);
    for (int i = 0; i < var_count; i++) {
        printf("  - %s = %d (Dirección: %p)\n", vars[i].id, vars[i].value, (void*)&vars[i]);
    }
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            printf("Variable encontrada: %s con valor %d (Dirección: %p)\n", vars[i].id, vars[i].value, (void*)&vars[i]);
            return &vars[i];
        }
    }
    printf("Variable %s no encontrada\n", id);
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
