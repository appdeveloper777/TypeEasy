#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical interpreter variable-slot capacity.
 * SINGLE source of truth — every module that touches the global `vars[]`
 * array (ast.c, te_evloop.c, te_bytecode.c, debugger.c) MUST use this value.
 * Historically these were desynced (256/100/100) which silently truncated
 * variables in async fibers. Limit is per endpoint invocation: the var table
 * is reset after every request, so this is NOT a cumulative cap across
 * endpoints. NOTE: bytecode.c keeps its own unrelated `bc_vars[]` cap. */
#ifndef MAX_VARS
#define MAX_VARS 1024
#endif

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
    NK_MOD, NK_NEG,
    NK_BIT_AND, NK_BIT_OR, NK_BIT_XOR, NK_BIT_NOT, NK_SHL, NK_SHR,
    NK_IN,
    NK_GT, NK_LT, NK_EQ, NK_DIFF, NK_GT_EQ, NK_LT_EQ,
    NK_AND, NK_OR, NK_NOT, NK_NULL_COALESCE, NK_TERNARY,
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

/* Format a double as the shortest decimal string that round-trips back to the
 * same IEEE-754 value (Python/JS style). Integer-valued doubles print without
 * decimals ("2.0" -> "2"). Used for every float->string conversion so that
 * println/concat/interpolation/json all agree (no "%f" zero padding). */
void te_fmt_double(char *buf, size_t cap, double v);

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
    /* CSV reader (perf): nodo asignado en pool bump (no malloc individual).
     * free_ast NO debe llamar free(node) sobre estos nodos; los bloques del
     * pool viven hasta el exit del programa. type/id/str_value asociados
     * deben ser sentinels o interned. */
    int   from_pool;
    /* Debugger: source line where this node was parsed (1-based, 0 if unknown).
     * Set by create_ast_* constructors from yylineno. May be slightly off for
     * multi-line constructs (lookahead token); good enough for breakpoint match. */
    int   line;
    /* v0.0.13 (perf): columnar cache attached to LIST head when items are
     * homogeneous OBJECTs from a CSV load. NULL on all non-LIST nodes and on
     * LIST nodes that don't qualify. Owned by the LIST node; freed when the
     * list is freed (currently leaked at program-end which is fine). */
    void *col_cache;
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
        long long int_value;
        char *string_value;
        double float_value;
        struct ObjectNode *object_value;
    } value; // << este nombre ES IMPORTANTE
} Variable;

typedef struct ClassNode {
    char *name;
    Variable *attributes;
    int attr_count;
    /* Default-value expressions, parallel to attributes[] (NULL = none).
     * Set when a field is declared C#-style with an initializer, e.g.
     * `private int _total = 0;`. Applied in create_object() before the
     * constructor runs. AST nodes are owned by the parse tree (read-only). */
    struct ASTNode **attr_defaults;
    /* Access modifier per attribute, parallel to attributes[]:
     *   0 = public (default), 1 = private, 2 = protected.
     * `private` is enforced at runtime: a private field can only be read or
     * written through `this` (inside the class's own methods). */
    int *attr_access;
    MethodNode *methods;
    struct ClassNode *parent;   /* Phase E: superclass (NULL if none) */
    struct ClassNode *next;
} ClassNode;

typedef struct ObjectNode {
    ClassNode *class;
    Variable *attributes;
    /* v0.0.13 (perf) Back-pointer to the LIST ASTNode that owns this object
     * (set by te_colcache_build when a columnar mirror is built). NULL means
     * the object is not in any cached list. Used to invalidate the columnar
     * cache on attribute mutation. */
    void *owning_list;
} ObjectNode;

typedef struct MethodNode {
    char *name;
    ParameterNode *params;
    ASTNode *body;
    char *route_path;    // For endpoints
    char *http_method;   // For endpoints
    int cache_ttl;       // For endpoint cache decorator
    int requires_auth;   // For endpoint @auth decorator (Bearer JWT required)
    int is_async;        // 1 if declared with the C#/.NET-style `async` modifier
                         // (gates cooperative request overlap on await).
    char *return_type;   // "int" | "string" | "float" | "void" | "dynamic" | NULL (legacy/internal)
    /* Ola 4 (perf): cached bytecode for simple `return <numeric expr>;` bodies.
     * NULL = not yet attempted. (void*)0x1 = tried, not compilable. Else BCInfo*. */
    void *bc_body;
    struct MethodNode *next;
    /* v0.1.0 — WebSocket lifecycle. When ws_lifecycle != 0 the handler declares
     * on_open/on_message/on_close blocks; `body` holds the on_message body. For
     * legacy flat [WebSocket] handlers ws_lifecycle == 0 and `body` is the
     * single-shot connect handler (run once on ws_ready, unchanged behavior). */
    int      ws_lifecycle;   /* 1 if on_open/on_message/on_close blocks present */
    ASTNode *ws_on_open;     /* on_open body (NULL = none) */
    ASTNode *ws_on_close;    /* on_close body (NULL = none) */
    char    *ws_msg_param;   /* on_message(param) name bound to frame text (NULL) */
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
/* v0.1.0 — WebSocket lifecycle invocation (defined in typeeasy_api.c). */
int   typeeasy_ws_is_lifecycle(MethodNode *method);
char *typeeasy_ws_invoke_open(MethodNode *method);
char *typeeasy_ws_invoke_message(MethodNode *method);
char *typeeasy_ws_invoke_close(MethodNode *method);typedef struct ParameterNode {
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
void te_runtime_reset_flags(void);
/* Rebuild the variable name->index side-index from the live vars[0..var_count).
 * Used by the async event loop after it swaps the active variable set between
 * fibers (it rewrites vars[] in place). */
void te_runtime_rebuild_symtab(void);

/* --- Cooperative request yield (Option 1a): transparent multi-user concurrency.
 * te_reqstate_save/restore snapshot+restore a request's mutable interpreter
 * state so the global invoke lock can be released while parked in an await.
 * te_coop_yield_begin/end are the safe wrappers used by the async runtimes;
 * the API server registers its lock via te_coop_register_lock and toggles
 * g_te_lock_held while it holds the lock. In CLI builds nothing registers and
 * the yields are no-ops. --- */
void *te_reqstate_save(void);
void  te_reqstate_restore(void *st);
void  te_coop_register_lock(void (*acq)(void), void (*rel)(void));
void *te_coop_yield_begin(void);
void  te_coop_yield_end(void *st);
extern __thread int g_te_lock_held;
/* Set by the server to the current handler's MethodNode.is_async before running
 * its body. te_coop_yield_begin() only releases the invoke lock when this is
 * non-zero, so a plain (non-async) handler keeps the lock across an await
 * (blocking/serialised) while an `async` handler lets other requests overlap. */
extern __thread int g_current_handler_async;

/* --- PROTOTIPOS DE TU "MOTOR" (PUROS) --- */

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls);
ASTNode* from_csv_to_dataframe(const char* filename, ClassNode* cls);

ASTNode *create_list_node(ASTNode *items);
ASTNode *append_to_list(ASTNode *list, ASTNode *item);
/* Internal helpers exposed for module extraction (te_linq, etc.). */
void te_list_append(ASTNode *list, ASTNode *item);
ASTNode *build_item_from_value(ASTNode *value);
ASTNode *create_list_function_call_node(ASTNode *list, const char *funcName, ASTNode *lambda);
ASTNode *create_object_node(ObjectNode *obj);
ASTNode *create_lambda_node(const char *argName, ASTNode *body);
/* Fase B: lambda multi-param. paramsCsv = nombres separados por '\1'. */
ASTNode *create_lambda_multi_node(const char *paramsCsv, ASTNode *body);
/* Invoca un lambda con argumentos (lista enlazada por ->right). */
ASTNode *call_lambda(ASTNode *lambda, ASTNode *argsList);
/* Gotcha #2: llamada sobre el resultado de otra llamada — `make(10)(5)`.
 * callee = expresión que evalúa a un LAMBDA; args = lista de argumentos. */
ASTNode *create_call_on_expr_node(ASTNode *callee, ASTNode *args);
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
void inherit_from(ClassNode *child, char *parent_name);  /* Phase E */
void add_class(ClassNode *class);
void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type);
void set_last_attr_default(ClassNode *class, ASTNode *default_expr);
void set_last_attr_access(ClassNode *class, int access);
int te_attr_access_ok(ClassNode *cls, int idx, ASTNode *obj_ref);
void add_method_to_class(ClassNode *class,
                             char *method,
                             ParameterNode *params,
                             ASTNode *body,
                             char *return_type);
ObjectNode *create_object(ClassNode *class);
/* item #6 (leak audit): libera un ObjectNode malloc'd por create_object. */
void free_object_node(ObjectNode *obj);
/* Bulk arena allocation of N ObjectNodes (DataFrame.toList fast path).
 * Returns a pointer to N contiguous ObjectNodes; objects are arena-owned
 * and must NOT be passed to free_object(). Caller fills value fields. */
ObjectNode *create_objects_bulk(ClassNode *cls, int N);
void call_method(ObjectNode *obj, char *method);
ClassNode *find_class(char *name);
ASTNode *create_int_node(int value);
ASTNode *create_float_node(int value);
ASTNode *create_train_node(const char *model_name, const char *dataset_name, ASTNode *options);
ASTNode *create_train_option_node(const char *option_name, int value);

extern Variable __ret_var;
extern int __ret_var_active;

/* Ruta del script en ejecución (para mensajes de error). La asigna main(). */
extern const char *g_script_path;

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
ASTNode *create_kv_pair_node(char *key, ASTNode *value);
char* get_node_string(ASTNode *node);

/* ------------------------------------------------------------------
 * Prototipos faltantes usados por el parser (parser.y / parser.tab.c).
 *
 * CRITICO en Windows x64 (LLP64): sin estos prototipos, GCC asume
 * `int (*)()` por la regla de "implicit function declaration" y
 * trunca el puntero retornado a 32 bits → segfault al primer deref
 * del nodo retornado (ej. `d->value = 1` justo despues de
 * `create_var_decl_node`). Sintoma reportado en v0.0.1 del .exe Win64:
 * cualquier `let`/`var`/`const` top-level crasheaba en parse-time.
 * Mismo patron documentado previamente para `create_kv_pair_node`.
 * ------------------------------------------------------------------ */
ASTNode *create_var_decl_node(char *id, ASTNode *value);
ASTNode *create_function_call_node(const char *funcName, ASTNode *args);
ASTNode *create_string_node(char *value);
ASTNode *create_if_node(ASTNode *condition, ASTNode *if_branch, ASTNode *else_branch);
ASTNode *create_agent_node(char *name, ASTNode *body);
ASTNode *create_listener_node(ASTNode *event_expr, ASTNode *body);
ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node);
ASTNode *create_state_decl_node(char *name, ASTNode *value_expr);
ASTNode *create_object_literal_node(ASTNode *kv_list);
ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr);
ASTNode *create_method_call_node_alone(ASTNode *objectNode, const char *methodName, ASTNode *args);

/* Estos dos faltaban tambien y producian el MISMO crash LLP64 en
 * Win64 con OBJECT_LITERAL multi-key (`{ "a": 1, "b": 2 }`) y con
 * llamadas a funciones con multiples argumentos. Regla de la casa:
 * TODA funcion que retorna puntero y se llama desde parser.y / parser.l
 * DEBE tener prototipo declarado en ast.h. */
ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair);
ASTNode *append_argument_raw(ASTNode *list, ASTNode *arg);

/* Inicializar el pool global de threads CSV.
 * Llamar desde main() antes de ejecutar el script TE.
 * n = número total de workers deseados (incluyendo el main thread). */
void te_csv_pool_init(int n);

/* ---------- Fast-row primitives (compartidos CSV/MySQL ORM) ----------
 * Arena + AST pool thread-local. Todo lo asignado vive hasta exit (script-mode).
 * Reusable por cualquier productor que necesite construir LIST de ObjectNode
 * a alta velocidad (ORM streaming, CSV, etc).
 */
void  *te_orm_arena_alloc(size_t n);
char  *te_orm_arena_strdup(const char *s);
char  *te_orm_arena_dup(const char *s, size_t n);
ASTNode *te_orm_pool_alloc(void);
const char *te_orm_wrapper_obj_type(void);
/* 0=int, 1=string, 2=other (incluye float, dynamic, etc). */
int  te_orm_attr_kind(const char *type);
int  te_orm_attr_is_nullable(const char *type);

#endif
