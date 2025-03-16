#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ASTNode {
    char *type;
    char *id;
    int value;
    char *str_value;
    struct ASTNode *left;
    struct ASTNode *right;
} ASTNode;

typedef struct Variable {
    char *id;
    char *type;  // Agregar este campo para almacenar el tipo de dato
    int value;
} Variable;


typedef struct MethodNode {
    char *name;
    ASTNode *body;
    struct MethodNode *next;
} MethodNode;

typedef struct ClassNode {
    char *name;
    Variable *attributes;
    int attr_count;
    MethodNode *methods;
    struct ClassNode *next;
} ClassNode;

typedef struct ObjectNode {
    ClassNode *class;
    Variable *attributes;
} ObjectNode;

typedef struct ParameterNode {
    char *name;
    char *type;
    struct ParameterNode *next;
} ParameterNode;

ParameterNode *create_parameter_node(char *name, char *type);
ParameterNode *add_parameter(ParameterNode *list, char *name, char *type);
ASTNode *create_object_with_args(ClassNode *class, ASTNode *args);



ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body);
void interpret_ast(ASTNode *node);
Variable *find_variable_for(char *id);
void add_or_update_variable(char *id, int value);
void add_variable_for(char *id, int value);
void store_variable(char *id, ASTNode *value);
ASTNode *find_variable(char *id);
void free_ast(ASTNode *node);


// Funciones para clases y objetos
ClassNode *create_class(char *name);
void add_class(ClassNode *class);
void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type);
void add_method_to_class(ClassNode *class, char *method, ASTNode *body);
ObjectNode *create_object(ClassNode *class);
void call_method(ObjectNode *obj, char *method);
ClassNode *find_class(char *name);

#endif