#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MethodNode MethodNode;
typedef struct ParameterNode ParameterNode;

/* --- Fase 1: NodeKind enum (perf optimization) ---
 * Cached enum for AST node types. Set automatically by create_ast_node*
 * from the string `type`. Allows hot dispatch via switch instead of strcmp.
 * NK_UNKNOWN = 0 = node type string did not match a known kind (still works
 * via strcmp fallback in non-hot paths).
 */
typedef enum {
    NK_UNKNOWN = 0,
    NK_ADD, NK_SUB, NK_MUL, NK_DIV,
    NK_GT, NK_LT, NK_EQ, NK_DIFF, NK_GT_EQ, NK_LT_EQ,
    NK_AND, NK_OR, NK_NOT, NK_NULL_COALESCE,
    NK_NUMBER, NK_INT, NK_FLOAT, NK_STRING, NK_STRING_LITERAL, NK_STRING_INTERP,
    NK_NULL, NK_IDENTIFIER, NK_ID,
    NK_ACCESS_ATTR, NK_ACCESS_EXPR, NK_ASSIGN, NK_ASSIGN_ATTR, NK_INDEX_ASSIGN,
    NK_VAR_DECL, NK_STATE_DECL, NK_BRIDGE_DECL,
    NK_IF, NK_MATCH, NK_FOR, NK_FOR_IN, NK_WHILE, NK_BREAK, NK_CONTINUE,
    NK_RETURN, NK_THROW, NK_TRY_CATCH,
    NK_PRINT, NK_PRINTLN, NK_FPRINT, NK_FPRINTLN,
    NK_CALL_FUNC, NK_CALL_METHOD, NK_METHOD_CALL_ALONE,
    NK_LIST, NK_LIST_FUNC_CALL, NK_FILTER_CALL,
    NK_OBJECT, NK_OBJECT_LITERAL, NK_KV_PAIR,
    NK_AGENT, NK_AGENT_LIST, NK_LISTENER,
    NK_DATASET, NK_MODEL, NK_TRAIN, NK_PREDICT, NK_PLOT,
    NK_RETURN_JSON, NK_RETURN_XML,
    NK_EXPRESSION, NK_STATEMENT_LIST
} NodeKind;

/* Resolve a type-string to its NodeKind (call once at node creation). */
NodeKind nk_from_str(const char *type);

/* --- 1. LA DEFINICIÓN DE ASTNode --- */
typedef struct ASTNode {
    char *type;
    NodeKind kind;        /* Fase 1: cached enum kind */
    char *id;
    int value;
    char *str_value;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;    
    struct ASTNode *extra;
    /* Fase 2 (perf): cached Variable* for NK_IDENTIFIER nodes.
     * vars[] entries are append-only and never relocated, so the pointer
     * stays valid for the program lifetime once resolved. */
    void *cached_var;
    /* Fase 3 (perf): bytecode cache for compilable numeric expressions.
     *   NULL          -> not compiled yet (try on first evaluation)
     *   (void*)0x1    -> tried and failed (do not retry; fall back to AST)
     *   other         -> heap-allocated BCInfo*: a tiny stack-VM program */
    void *bc;
    /* Ola 2 (perf): inline cache for NK_ACCESS_ATTR (obj.attr).
     * On first access we store the ClassNode* of the object and the
     * resolved attribute index. Next accesses validate the class pointer
     * and skip the strcmp loop entirely. */
    void *cached_class;
    int   cached_attr_idx;
    /* Ola 3 Fase A (perf): str_value points into the global intern table
     * (immortal, deduplicated). When set, free_ast must NOT free str_value,
     * and string equality may compare pointers directly. */
    int   str_interned;
    /* Ola 15 (perf): same flag for `id` slot. Used by KV_PAIR keys so that
     * map lookups can short-circuit with pointer-eq when the lookup key is
     * also interned. */
    int   id_interned;
    /* Debugger: source line where this node was parsed (1-based, 0 if unknown).
     * Set by create_ast_* constructors from yylineno. May be slightly off for
     * multi-line constructs (lookahead token); good enough for breakpoint match. */
    int   line;
} ASTNode;

// Prototipo nativo ORM debe ir después de ASTNode
void native_orm_query(ASTNode* args);

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
        struct ObjectNode *object_value;
    } value; // << este nombre ES IMPORTANTE
} Variable;

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

typedef struct MethodNode {
    char *name;
    ParameterNode *params;
    ASTNode *body;
    char *route_path;    // For endpoints
    char *http_method;   // For endpoints
    int cache_ttl;       // For endpoint cache decorator
    char *return_type;   // "int" | "string" | "float" | "void" | "dynamic" | NULL (legacy/internal)
    /* Ola 4 (perf): cached bytecode for simple `return <numeric expr>;` bodies.
     * NULL = not yet attempted. (void*)0x1 = tried, not compilable. Else BCInfo*. */
    void *bc_body;
    struct MethodNode *next;
} MethodNode;

// --- Endpoint Cache Support ---
typedef struct CachedResponse {
    MethodNode *method;
    ASTNode *args;         // Arguments used for cache key
    ASTNode *result;       // Cached result
    time_t timestamp;      // When cached
    int ttl;               // Time-to-live in seconds
    struct CachedResponse *next;
} CachedResponse;

CachedResponse *get_cached_response(MethodNode *method, ASTNode *args);
void set_cached_response(MethodNode *method, ASTNode *args, ASTNode *result);
void invalidate_cache(MethodNode *method);

typedef struct ParameterNode {
    char *name;
    char *type;
    /* Ola 3 Fase B (perf): cached pointer to the Variable* used by this
     * parameter slot. Resolved on the first call; subsequent calls write
     * the value directly into that Variable, skipping
     * create_ast_leaf*+add_or_update_variable allocations. */
    void *cached_var;
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

ClassNode *create_class(char *name);
void add_class(ClassNode *class);
void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type);
void add_method_to_class(ClassNode *class,
                             char *method,
                             ParameterNode *params,
                             ASTNode *body,
                             char *return_type);
ObjectNode *create_object(ClassNode *class);
void call_method(ObjectNode *obj, char *method);
ClassNode *find_class(char *name);
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
    void (*handle_gemini_bridge)(char* method_name, ASTNode* args);
} BridgeHandlers;

// Función para que el programa principal (servidor_agent o typeeasy_main)
// registre sus manejadores.
void runtime_register_bridge_handlers(BridgeHandlers handlers);
// --- FIN MEJORA ---

// Missing declarations
void interpret_ast(ASTNode *node);
void free_ast(ASTNode *node);
ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body);
Variable *find_variable_for(char *id);
Variable *find_variable(char *id);
void add_or_update_variable(char *id, ASTNode *value);
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_return_node(ASTNode *expr);
ASTNode *create_call_node(const char *funcName, ASTNode *args);
char* get_object_xml_by_id(const char* id);
void print_object_as_xml_by_id(const char* id);
void print_object_as_json_by_id(const char* id);
ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_case_node(ASTNode* condition, ASTNode* body);
ASTNode *append_case_clause(ASTNode* list, ASTNode* case_clause);
ASTNode *create_match_node(ASTNode* condition, ASTNode* case_list);
ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
char* get_node_string(ASTNode *node);

#endif
