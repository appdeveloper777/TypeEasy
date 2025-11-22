// ...existing code...
// Prototipo nativo ORM debe ir después de ASTNode
// ...existing code...
// Serializa un objeto a JSON string dado su id y lo imprime
// Serializa un objeto a XML string dado su id y lo imprime
// Retorna un string XML serializado de un objeto dado su id
char* get_object_xml_by_id(const char* id);
void print_object_as_xml_by_id(const char* id);
void print_object_as_json_by_id(const char* id);
// Prototypes moved below ASTNode struct definition
#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct ParameterNode ParameterNode;
/*
    YA NO INCLUIMOS civetweb.h NI windows.h/unistd.h AQUÍ.
    Este archivo es ahora 100% independiente del servidor.
*/


/* --- 1. LA DEFINICIÓN DE ASTNode --- */
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
// Prototipo nativo ORM debe ir después de ASTNode
void native_orm_query(ASTNode* args);

/* --- 2. ELIMINAMOS RuntimeHost y ActiveBridge --- */
/* ... (Se han ido a servidor_agent.c) ... */


/* --- EL RESTO DE TUS DEFINICIONES (SIN CAMBIOS) --- */

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
    // New fields for dynamic endpoints
    char *route_path;   // e.g. "/api/users"
    char *http_method;  // e.g. "GET", "POST"
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
extern MethodNode *global_methods;
void runtime_save_initial_var_count();
void runtime_reset_vars_to_initial_state();

/* --- PROTOTIPOS DE TU "MOTOR" (PUROS) --- */

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls);

ASTNode *create_list_node(ASTNode *items);
ASTNode *append_to_list(ASTNode *list, ASTNode *item);
ASTNode *create_list_function_call_node(ASTNode *list, const char *funcName, ASTNode *lambda);
ASTNode *create_object_node(ObjectNode *obj);
ASTNode *create_lambda_node(const char *argName, ASTNode *body);
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
ASTNode *create_method_call_node(ASTNode *objectNode,
                                     const char *methodName,
                                     ASTNode *args);

ASTNode *add_argument(ASTNode *list, ASTNode *expr);
ASTNode *create_return_node(ASTNode *expr);

void add_constructor_to_class(ClassNode *class, ParameterNode *params, ASTNode *body);
ASTNode *create_function_call_node(const char *funcName, ASTNode *args);
ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_if_node(ASTNode* condition, ASTNode* if_branch, ASTNode* else_branch);
ASTNode *create_match_node(ASTNode* condition, ASTNode* case_list);
ASTNode *create_case_node(ASTNode* condition, ASTNode* body);
ASTNode *append_case_clause(ASTNode* list, ASTNode* case_clause);
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

// Prototipos para el Agente (¡SÓLO los nodos de sintaxis!)
ASTNode *create_agent_node(char *name, ASTNode *body);
ASTNode *create_listener_node(ASTNode *event_expr, ASTNode *body);
ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node);
ASTNode *create_state_decl_node(char *name, ASTNode *value_expr);

ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr);
ASTNode *create_object_literal_node(ASTNode *kv_list);
ASTNode *create_kv_pair_node(char *key, ASTNode *value);
ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair);
ASTNode *create_int_node(int value);
ASTNode *create_float_node(int value);
ASTNode *create_train_node(const char *model_name, const char *dataset_name, ASTNode *options);
ASTNode *create_train_option_node(const char *option_name, int value);

extern Variable __ret_var;
extern int __ret_var_active;

// Funciones adicionales que faltan
double evaluate_expression(ASTNode *node);
void generate_plot(double *values, int count);
void interpret_if(ASTNode *node);
ASTNode* create_call_node_return_json(const char* funcName, ASTNode* args);
ASTNode* create_call_node_return_xml(const char* funcName, ASTNode* args);
int evaluate_condition(ASTNode* condition);
ASTNode *append_to_list_parser(ASTNode *list, ASTNode *item);
void set_attribute_value_object(ObjectNode *obj, const char *attr_name, ObjectNode *value);

// Helper: Recursively evaluate arguments for native calls
void evaluate_native_args(ASTNode *arg);
// --- INICIO MEJORA: Desacoplamiento de Bridges ---
// Estructura para registrar los manejadores de bridges nativos.
// El motor (ast.c) llamará a estos punteros si no son NULL.
typedef struct BridgeHandlers {
    void (*handle_chat_bridge)(char* method_name, ASTNode* args);
    void (*handle_nlu_bridge)(char* method_name, ASTNode* args);
    void (*handle_api_bridge)(char* method_name, ASTNode* args);
} BridgeHandlers;

// Función para que el programa principal (servidor_agent o typeeasy_main)
// registre sus manejadores.
void runtime_register_bridge_handlers(BridgeHandlers handlers);
// --- FIN MEJORA ---

/* --- ELIMINADOS: Prototipos del Runtime (runtime_init, etc) --- */

#endif