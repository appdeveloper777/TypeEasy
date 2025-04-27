#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParameterNode ParameterNode;

typedef struct ASTNode {
    char *type;
    char *id;
    int value;
    char *str_value;
    struct ASTNode *left;
    struct ASTNode *right;
} ASTNode;

typedef enum {
    VAL_INT,
    VAL_STRING,
    VAL_OBJECT
} ValueType;

typedef struct ObjectNode ObjectNode;

typedef struct Variable {
    char *id;
    ValueType vtype;
    char *type;
    union {
        int int_value;
        char *string_value;
        ObjectNode *object_value;
    } value; // << este nombre ES IMPORTANTE
} Variable;


typedef struct MethodNode {
    char *name;
    ASTNode *body;
    ParameterNode *params; 
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
// ast.h, justo después de create_object_with_args
ASTNode *create_method_call_node(ASTNode *objectNode,
                                     const char *methodName,
                                     ASTNode *args);

ASTNode *add_argument(ASTNode *list, ASTNode *expr);
/* ast.h – justo tras tus otras creaciones de ASTNode */
ASTNode *create_return_node(ASTNode *expr);

// ast.h, justo después de add_method_to_class
void add_constructor_to_class(ClassNode *class, ParameterNode *params, ASTNode *body);





ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body);
void interpret_ast(ASTNode *node);
Variable *find_variable_for(char *id);
void add_or_update_variable(char *id, ASTNode *value);
void add_variable_for(char *id, int value);
void store_variable(char *id, ASTNode *value);
Variable *find_variable(char *id);
void declare_variable(char *id, ASTNode *value);
void free_ast(ASTNode *node);
int get_attribute_value(ObjectNode *obj, const char *attr_name);
void set_attribute_value(ObjectNode *obj, const char *attr_name, int value);

// Funciones para clases y objetos
ClassNode *create_class(char *name);
void add_class(ClassNode *class);
void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type);
void add_method_to_class(ClassNode *class,
                             char *method,
                             ParameterNode *params,
                             ASTNode *body);
ObjectNode *create_object(ClassNode *class);
void call_method(ObjectNode *obj, char *method);
ClassNode *find_class(char *name);

#endif