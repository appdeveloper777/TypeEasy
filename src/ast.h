#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Definiciones de tipos - MACHINE LEARNING */
typedef struct TensorNode {
    float* data;
    int* shape;
    int ndim;
} TensorNode;

typedef struct LayerNode {
    char* layer_type; // "Dense", "ReLU", etc.
    int units;
    char* activation;
} LayerNode;

typedef struct ModelNode {
    LayerNode** layers;
    int layer_count;
} ModelNode;

// END - MACHINE LEARNING
typedef struct DatasetNode {
    TensorNode** inputs;
    TensorNode** labels;
    int count;
} DatasetNode;


typedef struct ParameterNode ParameterNode;

typedef struct ASTNode {
    char *type;
    char *id;
    int value;
    char *str_value;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;    
    struct ASTNode *extra; 

} ASTNode;

typedef enum {
    VAL_INT,
    VAL_STRING,
    VAL_FLOAT,
    VAL_OBJECT
} ValueType; 

typedef struct ObjectNode ObjectNode;

typedef struct Variable {
    char *id;
    int is_const; // 1 si es constante, 0 si no
    ValueType vtype;
    char *type;
    union {
        int int_value;
        char *string_value;
        double float_value;
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

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls);

ASTNode *create_list_node(ASTNode *items);
ASTNode *append_to_list(ASTNode *list, ASTNode *item);
ASTNode *create_list_function_call_node(ASTNode *list, const char *funcName, ASTNode *lambda);
ASTNode *create_lambda_node(const char *argName, ASTNode *body);
// Nuevo en ast.h
ASTNode* create_for_in_node(const char *var_name, ASTNode *list_expr, ASTNode *body);


ASTNode* create_layer_node(const char*, int, const char*);
ASTNode* append_layer_to_list(ASTNode*, ASTNode*);
ASTNode* create_model_node(const char*, ASTNode*);

ASTNode* create_predict_node(const char* model_name, const char* input_name);
ASTNode* create_dataset_node(const char* name, const char* path);
ASTNode* create_identifier_node(const char* name);
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


ASTNode *create_function_call_node(const char *funcName, ASTNode *args);



ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_if_node(ASTNode* condition, ASTNode* if_branch, ASTNode* else_branch);
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body);
void interpret_ast(ASTNode *node);
Variable *find_variable_for(char *id);
void add_or_update_variable(char *id, ASTNode *value);
void add_variable_for(char *id, int value);
void store_variable(char *id, ASTNode *value);
Variable *find_variable(char *id);
void declare_variable(char *id, ASTNode *value, int is_const);
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

// Funciones que faltaban en las declaraciones
ASTNode *create_var_decl_node(char *id, ASTNode *value);
ASTNode *create_string_node(char *value);
ASTNode *create_int_node(int value);
ASTNode *create_float_node(int value);
ASTNode *create_train_node(const char *model_name, const char *dataset_name, ASTNode *options);
ASTNode *create_train_option_node(const char *option_name, int value);

// Funciones adicionales que faltan
double evaluate_expression(ASTNode *node);
void generate_plot(double *values, int count);

#endif