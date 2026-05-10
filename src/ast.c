
#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "bytecode.h"
#include "mysql_bridge.h"
#include "debugger.h"

/* Ola 10: JIT availability — must be defined BEFORE any use further down. */
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <pthread.h>
#define TE_JIT_AVAILABLE 1
#define TE_HAS_MMAP 1
#define TE_HAS_PTHREAD 1
#else
#define TE_JIT_AVAILABLE 0
#define TE_HAS_MMAP 0
#define TE_HAS_PTHREAD 0
#endif

#if defined(__AVX2__)
#include <immintrin.h>
#define TE_HAS_AVX2 1
#else
#define TE_HAS_AVX2 0
#endif

/* Forward decls used in helpers below */
char* expand_interp_string(const char *raw);
int is_string_type(struct ASTNode *node);
extern int g_debug_mode;

/* Debugger: lexer line counter (from flex). Used to stamp ASTNode->line at
 * creation time so the runtime debugger can match breakpoints. */
extern int yylineno;

// Helper: Recursively evaluate arguments for native calls
void evaluate_native_args(ASTNode *arg) {
    ASTNode *curr = arg;
    int idx = 0;
    while (curr) {
        if (curr->type && (strcmp(curr->type, "IDENTIFIER") == 0 || strcmp(curr->type, "ID") == 0)) {
            Variable *v = find_variable(curr->id);
            if (v) {
                if (v->vtype == VAL_STRING) {
                    curr->type = strdup("STRING");
                    curr->str_value = strdup(v->value.string_value);
                } else if (v->vtype == VAL_INT) {
                    curr->type = strdup("NUMBER");
                    curr->value = v->value.int_value;
                }
            }
        }
        curr = curr->right;
        idx++;
    }
}

// Global buffer for capturing println output
char *g_stdout_buffer = NULL;
size_t g_stdout_size = 0;

void append_to_stdout(const char *str) {
    if (!str) return;
    /* NOTE: emission to the VS Code Debug Console is handled by dbg_printf
     * (so that we don't double-emit when call sites do both dbg_printf AND
     * append_to_stdout for json() capture). */
    size_t len = strlen(str);
    if (!g_stdout_buffer) {
        g_stdout_size = len + 1024;
        g_stdout_buffer = malloc(g_stdout_size);
        if (g_stdout_buffer) {
            strcpy(g_stdout_buffer, str);
        }
    } else {
        size_t current_len = strlen(g_stdout_buffer);
        if (current_len + len + 1 > g_stdout_size) {
            g_stdout_size = current_len + len + 1024;
            g_stdout_buffer = realloc(g_stdout_buffer, g_stdout_size);
        }
        if (g_stdout_buffer) {
            strcat(g_stdout_buffer, str);
        }
    }
}

void native_json(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_json called\n"); fflush(stderr); }
    
    // Handle empty argument (return json())
    if (!arg) {
        if (g_stdout_buffer) {
            ASTNode *result_node = create_ast_leaf("STRING", 0, g_stdout_buffer, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        } else {
            ASTNode *result_node = create_ast_leaf("STRING", 0, "{}", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        }
        return;
    }

    const char *var_id = NULL;
    if (arg->id) {
        var_id = arg->id;
    } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
        var_id = arg->str_value ? arg->str_value : arg->id;
    } else if (arg->left && arg->left->id) {
        var_id = arg->left->id;
    }
    
    if (!var_id) { printf("[DIAG] native_json: var_id is NULL\n"); return; }
    Variable *v = find_variable((char*)var_id);
    if (!v) { printf("[DIAG] native_json: variable '%s' not found\n", var_id); return; }
    
    // Handle STRING
    if (v->vtype == VAL_STRING) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, v->value.string_value, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle LIST
    if (v->type && strcmp(v->type, "LIST") == 0) {
        ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
        if (!listNode) {
             ASTNode *empty = create_ast_leaf("STRING", 0, "[]", NULL);
             add_or_update_variable("__ret__", empty);
             free_ast(empty);
             return;
        }
        
        int estimated_size = 4096; 
        char *json_buffer = malloc(estimated_size);
        if(!json_buffer) return;
        strcpy(json_buffer, "[");
        
        ASTNode *cur = listNode->left;
        int first = 1;
        
        while (cur) {
            if (!first) strcat(json_buffer, ", ");
            first = 0;
            
            if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                ObjectNode *obj = NULL;
                if (cur->extra) obj = (ObjectNode *)cur->extra;
                else obj = (ObjectNode *)(intptr_t)cur->value;
                
                if (obj && obj->class) {
                    if (strlen(json_buffer) + 1024 > estimated_size) {
                        estimated_size *= 2;
                        json_buffer = realloc(json_buffer, estimated_size);
                    }
                    
                    strcat(json_buffer, "{");
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "\"%s\": ", obj->class->attributes[i].id);
                        strcat(json_buffer, temp);
                        
                        Variable *attr = &obj->attributes[i];
                        if (attr->vtype == VAL_STRING) {
                            strcat(json_buffer, "\"");
                            strcat(json_buffer, attr->value.string_value ? attr->value.string_value : "");
                            strcat(json_buffer, "\"");
                        } else if (attr->vtype == VAL_FLOAT) {
                            snprintf(temp, sizeof(temp), "%f", attr->value.float_value);
                            strcat(json_buffer, temp);
                        } else {
                            snprintf(temp, sizeof(temp), "%d", attr->value.int_value);
                            strcat(json_buffer, temp);
                        }
                        if (i < obj->class->attr_count - 1) strcat(json_buffer, ", ");
                    }
                    strcat(json_buffer, "}");
                }
            }
            cur = cur->next;
        }
        strcat(json_buffer, "]");
        
        ASTNode *result_node = create_ast_leaf("STRING", 0, json_buffer, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(json_buffer);
        return;
    }

    // Handle single OBJECT
    if (v->vtype != VAL_OBJECT) { printf("[DIAG] native_json: variable is not VAL_OBJECT, vtype=%d\n", v->vtype); return; }
    ObjectNode *obj = v->value.object_value;
    if (!obj || !obj->class) return;
    
    char *json_buffer = malloc(4096);
    strcpy(json_buffer, "{");
    for (int i = 0; i < obj->class->attr_count; i++) {
        char temp[512];
        snprintf(temp, sizeof(temp), "\"%s\": ", obj->class->attributes[i].id);
        strcat(json_buffer, temp);
        if (obj->attributes[i].vtype == VAL_STRING) {
            strcat(json_buffer, "\"");
            strcat(json_buffer, obj->attributes[i].value.string_value ? obj->attributes[i].value.string_value : "");
            strcat(json_buffer, "\"");
        } else if (obj->attributes[i].vtype == VAL_INT) {
            snprintf(temp, sizeof(temp), "%d", obj->attributes[i].value.int_value);
            strcat(json_buffer, temp);
        } else if (obj->attributes[i].vtype == VAL_FLOAT) {
            snprintf(temp, sizeof(temp), "%f", obj->attributes[i].value.float_value);
            strcat(json_buffer, temp);
        } else {
            strcat(json_buffer, "null");
        }
        if (i < obj->class->attr_count - 1) strcat(json_buffer, ", ");
    }
    strcat(json_buffer, "}");
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, json_buffer, NULL);
    add_or_update_variable("__ret__", result_node);
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_json: __ret__ set to %s\n", json_buffer); fflush(stderr); }
    free_ast(result_node);
    free(json_buffer);
}

// --- Hook para funciones nativas ---

void native_xml(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_xml called\n"); fflush(stderr); }
    
    // Handle empty argument (return xml())
    if (!arg) {
        if (g_stdout_buffer) {
            ASTNode *result_node = create_ast_leaf("STRING", 0, g_stdout_buffer, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        } else {
            ASTNode *result_node = create_ast_leaf("STRING", 0, "<root></root>", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        }
        return;
    }

    const char *var_id = NULL;
    if (arg->id) {
        var_id = arg->id;
    } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
        var_id = arg->str_value ? arg->str_value : arg->id;
    } else if (arg->left && arg->left->id) {
        var_id = arg->left->id;
    }
    
    if (var_id) {
        print_object_as_xml_by_id(var_id);
        return;
    }
    
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
         ASTNode *result_node = create_ast_leaf("STRING", 0, arg->str_value, NULL);
         add_or_update_variable("__ret__", result_node);
         free_ast(result_node);
         return;
    }
}


// Helper function to get string representation of any node
char* get_node_string(ASTNode* node) {
    if (!node) return strdup("");
    
    char temp[64];
    
    if (node->type && (strcmp(node->type, "STRING") == 0 || strcmp(node->type, "STRING_LITERAL") == 0)) {
        return node->str_value ? strdup(node->str_value) : strdup("");
    }

    if (node->type && strcmp(node->type, "STRING_INTERP") == 0) {
        return expand_interp_string(node->str_value ? node->str_value : "");
    }

    if (node->type && strcmp(node->type, "NULL") == 0) {
        return strdup("null");
    }

    if (node->type && strcmp(node->type, "ADD") == 0) {
        char *l = get_node_string(node->left);
        char *r = get_node_string(node->right);
        size_t n = strlen(l) + strlen(r) + 1;
        char *out = malloc(n);
        snprintf(out, n, "%s%s", l, r);
        free(l); free(r);
        return out;
    }
    
    if (node->type && (strcmp(node->type, "NUMBER") == 0 || strcmp(node->type, "INT") == 0)) {
        snprintf(temp, sizeof(temp), "%d", node->value);
        return strdup(temp);
    }
    
    if (node->type && strcmp(node->type, "FLOAT") == 0) {
        snprintf(temp, sizeof(temp), "%f", node->str_value ? atof(node->str_value) : 0.0);
        return strdup(temp);
    }

    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (v) {
            if (v->vtype == VAL_STRING) return strdup(v->value.string_value ? v->value.string_value : "");
            if (v->vtype == VAL_INT) {
                snprintf(temp, sizeof(temp), "%d", v->value.int_value);
                return strdup(temp);
            }
            if (v->vtype == VAL_FLOAT) {
                snprintf(temp, sizeof(temp), "%f", v->value.float_value);
                return strdup(temp);
            }
        }
        return strdup(""); // Variable not found or unknown type
    }
    
    if (node->type && strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left;
        ASTNode *a = node->right;
        Variable *v = find_variable(o->id);
        if (v && v->vtype == VAL_OBJECT) {
            ObjectNode *obj = v->value.object_value;
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    Variable *attr = &obj->attributes[i];
                    if (attr->vtype == VAL_STRING) return strdup(attr->value.string_value ? attr->value.string_value : "");
                    if (attr->vtype == VAL_INT) {
                        snprintf(temp, sizeof(temp), "%d", attr->value.int_value);
                        return strdup(temp);
                    }
                    if (attr->vtype == VAL_FLOAT) {
                        snprintf(temp, sizeof(temp), "%f", attr->value.float_value);
                        return strdup(temp);
                    }
                }
            }
        }
        return strdup(""); // Attribute not found
    }

    return strdup(""); // Fallback
}

void native_concat(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_concat called\n"); fflush(stderr); }
    
    // Start with a reasonable buffer size
    size_t buffer_size = 1024;
    size_t current_len = 0;
    char *result = malloc(buffer_size);
    result[0] = '\0';
    
    ASTNode *curr = arg;
    while (curr) {
        char *s_temp = get_node_string(curr);
        if (s_temp) {
            if (g_debug_mode) fprintf(stderr, "[DEBUG] concat appending: '%s'\n", s_temp);
            size_t len = strlen(s_temp);
            if (current_len + len >= buffer_size) {
                buffer_size = (current_len + len) * 2;
                result = realloc(result, buffer_size);
            }
            strcat(result, s_temp);
            current_len += len;
            free(s_temp);
        }
        curr = curr->right; // Next argument
    }
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, result, NULL);
    add_or_update_variable("__ret__", result_node);
    free_ast(result_node);
    free(result);
}

int call_native_function(const char *name, ASTNode *arg) {
    if (strcmp(name, "orm_query") == 0) {
        native_orm_query(arg);
        return 1;
    }
    // Siempre evaluar argumentos antes de llamada nativa
    if (arg) evaluate_native_args(arg);
    if (strcmp(name, "json") == 0) {
        native_json(arg);
        return 1;
    }
    if (strcmp(name, "xml") == 0) {
        native_xml(arg);
        return 1;
    }
    if (strcmp(name, "concat") == 0) {
        native_concat(arg);
        return 1;
    }
    if (strcmp(name, "mysql_connect") == 0) {
        native_mysql_connect(arg);
        return 1;
    }
    if (strcmp(name, "mysql_query") == 0) {
        native_mysql_query(arg);
        return 1;
    }
    if (strcmp(name, "mysql_close") == 0) {
        native_mysql_close(arg);
        return 1;
    }
    return 0;
}

#include <stdarg.h>

/* Wrapper that does printf to real stdout AND mirrors the formatted text
 * to append_to_stdout (which also forwards to the VS Code Debug Console
 * via debugger_emit_output when the debugger is attached).
 * Used by interpret_print/println/fprint/fprintln so EVERY output path
 * shows up in the Debug Console — not just the few paths that historically
 * called append_to_stdout. Returns the number of chars formatted (or -1). */
static int dbg_printf(const char *fmt, ...) {
    char stackbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof(stackbuf)) {
        fputs(stackbuf, stdout);
        append_to_stdout(stackbuf);
        if (g_debug_enabled) debugger_emit_output("stdout", stackbuf);
        va_end(ap2);
        return n;
    }
    /* Output too big for stack buffer: allocate. */
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { va_end(ap2); return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    fputs(buf, stdout);
    append_to_stdout(buf);
    if (g_debug_enabled) debugger_emit_output("stdout", buf);
    free(buf);
    return n;
}

/* Same as dbg_printf, but writes to stderr (used by fprint/fprintln). */
static int dbg_eprintf(const char *fmt, ...) {
    char stackbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof(stackbuf)) {
        fputs(stackbuf, stderr);
        if (g_debug_enabled) debugger_emit_output("stderr", stackbuf);
        va_end(ap2);
        return n;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { va_end(ap2); return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    fputs(buf, stderr);
    if (g_debug_enabled) debugger_emit_output("stderr", buf);
    free(buf);
    return n;
}

MethodNode* global_methods = NULL;

ASTNode* create_call_node(const char* funcName, ASTNode* args) {
    //printf("[DEBUG] Entering create_call_node: %s\n", funcName); fflush(stdout);
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) { printf("[DEBUG] malloc failed in create_call_node\n"); fflush(stdout); return NULL; }
    //printf("[DEBUG] malloc success\n"); fflush(stdout);
    node->type = strdup("CALL_FUNC");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
   // printf("[DEBUG] create_call_node success\n"); fflush(stdout);
   // if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
   // } else {
   //     printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

ASTNode* create_call_node_return_json(const char* funcName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("RETURN_JSON");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    //if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
    //} else {
     //   printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

ASTNode* create_call_node_return_xml(const char* funcName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("RETURN_XML");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    //if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
    //} else {
     //   printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

ASTNode* create_method_call_node_alone(ASTNode* objectNode, const char* methodName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("METHOD_CALL_ALONE");
    node->left = objectNode;
    node->right = args;
    node->str_value = NULL;
    node->value = 0;
    if (methodName) {
        node->id = strdup(methodName);
    } else {
        node->id = NULL;
    }
    return node;
}

/* Small helper to standardize TypeEasy logs inside ast.c */
static void te_log_ast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char *green = "\x1b[32m";
    const char *reset = "\x1b[0m";
    printf("%sTypeEasy Agent: ", green);
    vprintf(fmt, ap);
    printf("%s\n", reset);
    va_end(ap);
}

/* Allow runtime debug mode controlled by TYPEEASY_DEBUG env var (set in main) */
extern int g_debug_mode;

/* --- ELIMINADOS: g_runtime y los prototipos del servidor --- */

// ====================== CONSTANTES Y ESTRUCTURAS GLOBALES ======================
#define MAX_CLASSES 50
#define MAX_VARS 100
static int g_initial_var_count = 0;
// Variables globales
Variable vars[MAX_VARS];
ClassNode *classes[MAX_CLASSES];
Variable __ret_var; // Global variable for return values
int __ret_var_active = 0; // Flag to indicate if __ret_var holds a valid value
int var_count = 0;
int class_count = 0;

// Estado de retorno
int return_flag = 0;

static ASTNode *return_node = NULL;

/* Fase 2: throw/try-catch */
int throw_flag = 0;
static char *throw_message = NULL;

const char *get_throw_message(void) { return throw_message; }

/* Fase 4: break/continue */
static int break_flag = 0;
static int continue_flag = 0;

/* Fase 3a: forward decl */
char* expand_interp_string(const char *raw);

/* Ola 16: forward decl (defined in symtab block below). */
static void te_sym_reset_to(int initial_count);

// --- INICIO MEJORA: Punteros a los manejadores de bridges ---
static BridgeHandlers g_bridge_handlers = {NULL, NULL, NULL};
void runtime_save_initial_var_count() {
    g_initial_var_count = var_count;
    if (g_debug_mode) te_log_ast("Initial state saved. %d global variables retained.", g_initial_var_count);
}

void runtime_reset_vars_to_initial_state() {
    // Libera la memoria de todas las variables CREADAS DURANTE LA ÚLTIMA EJECUCIÓN
    // (es decir, todas las variables DESPUÉS de los bridges)
    for (int i = g_initial_var_count; i < var_count; i++) {
        if (vars[i].id) free(vars[i].id);
        if (vars[i].type) free(vars[i].type);
        if (vars[i].vtype == VAL_STRING && vars[i].value.string_value) {
            free(vars[i].value.string_value);
        }
        
        // ¡Importante! Si la variable es un Objeto (como 'intencion')
        // debemos liberar el objeto en sí (que está en 'extra')
        // PERO 'declare_variable'  y 'add_or_update_variable' 
        // copian el puntero, y los 'free_ast'  ya liberan los nodos.
        // No necesitamos liberar 'extra' aquí, solo el contenedor de la variable.
    }

    // Resetea el contador de variables a su estado "limpio"
    var_count = g_initial_var_count;
    /* Ola 16: drop hash entries beyond initial state. */
    te_sym_reset_to(g_initial_var_count);

    // También limpia la variable de retorno global
    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: No desactivar para permitir uso en requests subsiguientes
    }

    // Limpiar buffer de stdout
    if (g_stdout_buffer) {
        free(g_stdout_buffer);
        g_stdout_buffer = NULL;
        g_stdout_size = 0;
    }
}
void runtime_register_bridge_handlers(BridgeHandlers handlers) {
    g_bridge_handlers.handle_chat_bridge = handlers.handle_chat_bridge;
    g_bridge_handlers.handle_nlu_bridge = handlers.handle_nlu_bridge;
    g_bridge_handlers.handle_api_bridge = handlers.handle_api_bridge;
    g_bridge_handlers.handle_gemini_bridge = handlers.handle_gemini_bridge;
}
// --- FIN MEJORA ---


/* --- ELIMINADAS: Las 5 funciones del Agente (webhook_handler, runtime_start_bridges, etc.) --- */
/* --- (Ahora viven en servidor_agent.c) --- */


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
 * Convierte un double a string dinámico
 */
static char* double_to_string(double x) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%f", x);
    return strdup(buf);
}

/**
 * Obtiene representación string de cualquier ASTNode
 */
/* --- ELIMINADA LA PALABRA 'static' --- */


// ====================== MANEJO DE CLASES Y OBJETOS ======================
// ====================== ENDPOINT CACHE SUPPORT ======================
#include <time.h>
static CachedResponse *cache_head = NULL;

// Helper: compare args for cache key (simple pointer equality for now)
static int args_equal(ASTNode *a, ASTNode *b) {
    return a == b; // TODO: Deep compare if needed
}

CachedResponse *get_cached_response(MethodNode *method, ASTNode *args) {
    time_t now = time(NULL);
    CachedResponse *cur = cache_head;
    while (cur) {
        if (cur->method == method && args_equal(cur->args, args)) {
            if (cur->ttl > 0 && (now - cur->timestamp) < cur->ttl) {
                return cur;
            }
        }
        cur = cur->next;
    }
    return NULL;
}

void set_cached_response(MethodNode *method, ASTNode *args, ASTNode *result) {
    CachedResponse *entry = get_cached_response(method, args);
    if (entry) {
        entry->result = result;
        entry->timestamp = time(NULL);
        entry->ttl = method->cache_ttl;
        return;
    }
    entry = malloc(sizeof(CachedResponse));
    entry->method = method;
    entry->args = args;
    entry->result = result;
    entry->timestamp = time(NULL);
    entry->ttl = method->cache_ttl;
    entry->next = cache_head;
    cache_head = entry;
}

void invalidate_cache(MethodNode *method) {
    CachedResponse *cur = cache_head, *prev = NULL;
    while (cur) {
        if (cur->method == method) {
            if (prev) prev->next = cur->next;
            else cache_head = cur->next;
            free(cur);
            if (prev) cur = prev->next;
            else cur = cache_head;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
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

ClassNode *find_class(char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i]->name, name) == 0) {
            return classes[i];
        }
    }
    printf("Error: class '%s' not found.\n", name);
    return NULL;
}

void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type) {
    if (!class) return;
    
    class->attributes = realloc(class->attributes, (class->attr_count + 1) * sizeof(Variable));
    class->attributes[class->attr_count].id = strdup(attr_name);
    class->attributes[class->attr_count].type = strdup(attr_type);
    class->attr_count++;
}

void add_method_to_class(ClassNode *cls, char *method, ParameterNode *params, ASTNode *body, char *return_type) {
    MethodNode *m = malloc(sizeof(MethodNode));
    if (!m) exit(1);
    m->name = strdup(method);
    m->params = params;
    m->body = body;
    m->route_path = NULL;
    m->http_method = NULL;
    m->cache_ttl = 0;
    m->return_type = return_type ? strdup(return_type) : NULL;
    m->bc_body = NULL; /* Ola 4: lazy bytecode cache */
    m->next = cls->methods;
    cls->methods = m;
}
ObjectNode* clone_object(ObjectNode *original);

void add_constructor_to_class(ClassNode *class, ParameterNode *params, ASTNode *body) {
    MethodNode *ctor = malloc(sizeof(MethodNode));
    if (!ctor) exit(1);
    ctor->name = strdup("__constructor");
    ctor->params = params;
    ctor->body = body;
    ctor->route_path = NULL;
    ctor->http_method = NULL;
    ctor->cache_ttl = 0;
    ctor->return_type = strdup("void");
    ctor->bc_body = NULL; /* Ola 4: lazy bytecode cache */
    ctor->next = class->methods;
    class->methods = ctor;
}

ObjectNode *create_object(ClassNode *class) {
    ObjectNode *obj = (ObjectNode *)malloc(sizeof(ObjectNode));
    obj->class = class;
    obj->attributes = malloc(class->attr_count * sizeof(Variable));
    for (int i = 0; i < class->attr_count; i++) {
        obj->attributes[i].id    = strdup(class->attributes[i].id);
        obj->attributes[i].type  = strdup(class->attributes[i].type);
        // Inicialización por defecto (ej. int 0)
        if (strcmp(class->attributes[i].type, "string") == 0) {
            obj->attributes[i].vtype = VAL_STRING;
            obj->attributes[i].value.string_value = strdup("");
        } else if (strcmp(class->attributes[i].type, "float") == 0) {
            obj->attributes[i].vtype = VAL_FLOAT;
            obj->attributes[i].value.float_value = 0.0;
        } else {
            obj->attributes[i].vtype = VAL_INT;
            obj->attributes[i].value.int_value = 0;
        }
    }
    
    return obj;
}

// ====================== MANEJO DE VARIABLES ======================

/* Forward decl: defined below in Ola 14 helpers block. */
static uint64_t te_str_hash(const char *s);

/* ============================================================
 * Ola 16 — Hash-indexed symbol table.
 *   Side-index over `vars[]` mapping (FNV-1a hash, key) -> index.
 *   Variable identifier strings are NOT moved; we just index them.
 *   When a string is interned, the hash slot's `key` will point to
 *   the interned copy (immortal), enabling pointer-eq fast paths.
 * ============================================================*/
typedef struct TESymSlot {
    uint64_t hash;
    const char *key;  /* alias to vars[idx].id */
    int idx;
} TESymSlot;

#define TE_SYM_CAP 256  /* MAX_VARS=100 → cap 256 keeps load < 0.5 */
static TESymSlot g_sym_slots[TE_SYM_CAP];
static int g_sym_init = 0;

static inline void te_sym_clear(void) {
    for (int i = 0; i < TE_SYM_CAP; i++) {
        g_sym_slots[i].key = NULL;
        g_sym_slots[i].hash = 0;
        g_sym_slots[i].idx = -1;
    }
    g_sym_init = 1;
}

static inline int te_sym_lookup(const char *id) {
    if (!g_sym_init) return -1;
    if (!id) return -1;
    uint64_t h = te_str_hash(id);
    int mask = TE_SYM_CAP - 1;
    int i = (int)(h & (uint64_t)mask);
    for (;;) {
        const char *sk = g_sym_slots[i].key;
        if (sk == NULL) return -1;
        if (sk == id) return g_sym_slots[i].idx;          /* ptr-eq */
        if (g_sym_slots[i].hash == h && strcmp(sk, id) == 0) return g_sym_slots[i].idx;
        i = (i + 1) & mask;
    }
}

static inline void te_sym_insert(const char *id, int idx) {
    if (!g_sym_init) te_sym_clear();
    if (!id) return;
    uint64_t h = te_str_hash(id);
    int mask = TE_SYM_CAP - 1;
    int i = (int)(h & (uint64_t)mask);
    for (;;) {
        const char *sk = g_sym_slots[i].key;
        if (sk == NULL) {
            g_sym_slots[i].key = id;
            g_sym_slots[i].hash = h;
            g_sym_slots[i].idx = idx;
            return;
        }
        if (sk == id || (g_sym_slots[i].hash == h && strcmp(sk, id) == 0)) {
            g_sym_slots[i].idx = idx;  /* update */
            g_sym_slots[i].key = id;
            return;
        }
        i = (i + 1) & mask;
    }
}

/* Drop all entries whose idx >= initial_count and rebuild from the
 * remaining vars[]. Called from runtime_reset_vars_to_initial_state. */
static void te_sym_reset_to(int initial_count) {
    te_sym_clear();
    for (int i = 0; i < initial_count && i < MAX_VARS; i++) {
        if (vars[i].id) te_sym_insert(vars[i].id, i);
    }
}

Variable *find_variable(char *id) {    
    if (!id) return NULL;  /* Bug fix: caller paths sometimes pass NULL (e.g. arr[i].attr access where 'o' is ACCESS_EXPR with NULL id). */
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    /* Ola 16: hash side-index. */
    int idx = te_sym_lookup(id);
    if (idx >= 0 && idx < var_count) {
        return &vars[idx];
    }
    /* Fallback: linear scan (also primes the hash if absent). */
    for (int i = 0; i < var_count; i++) {
        if (vars[i].id) {           
            if (strcmp(vars[i].id, id) == 0) {                
                te_sym_insert(vars[i].id, i);
                return &vars[i];
            }
        }
    }
    return NULL;
}

ASTNode *create_agent_node(char *name, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para AGENT node.\n");
        exit(1);
    }
    node->type = strdup("AGENT");
    node->id = strdup(name);
    node->left = body; // 'body' es la lista de listeners
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_listener_node(ASTNode *event_expr, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para LISTENER node.\n");
        exit(1);
    }
    node->type = strdup("LISTENER");
    node->left = event_expr; // La expresión p.ej. Chat.onMessage(mensaje)
    node->right = body;     // El bloque de código
    node->id = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

Variable *find_variable_for(char *id) {
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    /* Ola 16: hash side-index. */
    int idx = te_sym_lookup(id);
    if (idx >= 0 && idx < var_count) {
        return &vars[idx];
    }
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].id, id) == 0) {
            te_sym_insert(vars[i].id, i);
            return &vars[i];
        }
    }
    return NULL;
}

void declare_variable(char *id, ASTNode *value, int is_const) {
    if (var_count >= MAX_VARS) {
        printf("Error: too many declared variables.\n");
        return;
    }

    int my_index = var_count;
    var_count++;

    vars[my_index].id = strdup(id);
    vars[my_index].is_const = is_const;
    /* Ola 16: index in symtab. */
    te_sym_insert(vars[my_index].id, my_index);

    // Aseguramos que el tipo siempre sea una copia dinámica para poder liberarlo después
    if (strcmp(value->type, "NUMBER") == 0) {
        vars[my_index].type = strdup("INT");
    } else if (strcmp(value->type, "STRING_LITERAL") == 0) {
        vars[my_index].type = strdup("STRING");
    } else if (strcmp(value->type, "FLOAT") == 0) {
        vars[my_index].type = strdup("FLOAT");
    } else {
        vars[my_index].type = strdup(value->type);
    }

   if (strcmp(value->type, "STRING") == 0 || strcmp(value->type, "STRING_LITERAL") == 0) {
        vars[my_index].vtype = VAL_STRING;
        vars[my_index].value.string_value = strdup(value->str_value);
    }
    else if (strcmp(value->type, "STRING_INTERP") == 0) {
        /* Fase 3a: expandir interpolación */
        free(vars[my_index].type);
        vars[my_index].type = strdup("STRING");
        vars[my_index].vtype = VAL_STRING;
        vars[my_index].value.string_value = expand_interp_string(value->str_value);
    }
    else if (strcmp(value->type, "LIST") == 0) {
        /* debug print removed */
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (void *)(intptr_t)value;

        /* Fast-path: if listNode->value == 1, every OBJECT child already has
         * its `extra` populated with a fully-initialized ObjectNode (from
         * `from_csv_to_list` Phase 1). Skip the per-item clone + constructor
         * pass entirely — that's what made CSV loads ~65× slower than Python.
         */
        if (value->value == 1) {
            return;
        }

        ASTNode *cur = value->left;
        int list_count = 0;
        while (cur) {
            list_count++;
            /* debug print removed */
            if (strcmp(cur->type, "OBJECT") != 0) {
                return;
            }

            /* Read the original Object pointer from either 'extra' (preferred)
               or 'value' (legacy). Then clone and store the cloned object in
               both fields so future code can find it. */
            ObjectNode *obj_original = NULL;
            if (cur->extra) obj_original = (ObjectNode *)(cur->extra);
            else obj_original = (ObjectNode *)(intptr_t)cur->value;
            ObjectNode *obj_clonado = clone_object(obj_original);
            ASTNode *arg = cur->left;
            cur->value = (int)(intptr_t)obj_clonado;
            cur->extra = (struct ASTNode*)obj_clonado;
            MethodNode *m = obj_clonado->class->methods;
            while (m && strcmp(m->name, "__constructor") != 0) {
                m = m->next;
            }

            if (m) {
                ParameterNode *p = m->params;
                while (p && arg) {
                    ASTNode *vn = NULL;
                    if (arg->type && strcmp(arg->type, "STRING") == 0) {
                        vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    } 
                    else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                        Variable *v = find_variable(arg->id);
                        if (!v) {
                            printf("Error: variable '%s' not found.\n", arg->id);
                            return;
                        }
                        if (v->vtype == VAL_STRING) {
                            vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                        } else {
                            vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                        }
                    } 
                    else {
                        int val = evaluate_expression(arg);
                        vn = create_ast_leaf_number("INT", val, NULL, NULL);
                    }
                    add_or_update_variable(p->name, vn);
                    p = p->next;
                    arg = arg->right;
                }
                call_method(obj_clonado, "__constructor");
                /* Ensure constructor side-effects (like return_flag) don't block later AST execution */
                return_flag = 0;
                return_node = NULL;
                /* debug print removed */
            }
            cur->left = NULL;
            /* debug print removed */
            cur = cur->next;
        }
    /* debug print removed */
    }
    else if (strcmp(value->type, "OBJECT_LITERAL") == 0) {
        /* Fase 1c: Maps — almacenado como tipo "MAP", lista enlazada de KV_PAIR en value->left */
        free(vars[my_index].type);
        vars[my_index].type = strdup("MAP");
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (void *)(intptr_t)value;
    }
    else if (strcmp(value->type, "NULL") == 0) {
        /* Fase 1d: null */
        free(vars[my_index].type);
        vars[my_index].type = strdup("NULL");
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = NULL;
    }
    else if (strcmp(value->type, "FLOAT") == 0) {
        vars[my_index].vtype = VAL_FLOAT;
        vars[my_index].value.float_value = atof(value->str_value);
    }
    else if (strcmp(value->type, "ADD") == 0 || strcmp(value->type, "SUB") == 0 || 
             strcmp(value->type, "MUL") == 0 || strcmp(value->type, "DIV") == 0) {
        /* Fase 6: string concatenation if ADD with strings */
        if (strcmp(value->type, "ADD") == 0 && is_string_type(value)) {
            char *s = get_node_string(value);
            free(vars[my_index].type);
            vars[my_index].type = strdup("STRING");
            vars[my_index].vtype = VAL_STRING;
            vars[my_index].value.string_value = s;
            return;
        }
        double result = evaluate_expression(value);
        if (result == (int)result) {
            vars[my_index].vtype = VAL_INT;
            vars[my_index].value.int_value = (int)result;
            vars[my_index].type = strdup("INT");
        } else {
            vars[my_index].vtype = VAL_FLOAT;
            vars[my_index].value.float_value = result;
            vars[my_index].type = strdup("FLOAT");
        }
    }
    else if (strcmp(value->type, "OBJECT") == 0) {
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = (ObjectNode *)value->extra;

        //if (obj_dbg && obj_dbg->class && obj_dbg->class->name) {
       //     printf("[DIAG] declare_variable: objeto '%s' de clase '%s' declarado\n", id, obj_dbg->class->name);
       // } else {
        //    printf("[DIAG] declare_variable: objeto '%s' declarado pero clase no encontrada\n", id);
       // }
    }
    else if (strcmp(value->type, "ACCESS_ATTR") == 0) {
        // Esto maneja: let item = intencion.item;
        ASTNode *o = value->left, *a = value->right;
        Variable *v = find_variable(o->id);
        
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: object '%s' not found for assignment.\n", o->id);
            vars[my_index].vtype = VAL_INT;
            vars[my_index].value.int_value = 0; // Valor de error
            return;
        }
        
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
                // Encontramos el atributo. Copiamos su valor.
                if (obj->attributes[i].vtype == VAL_STRING) {
                    vars[my_index].vtype = VAL_STRING;
                    vars[my_index].value.string_value = strdup(obj->attributes[i].value.string_value);
                } else if (obj->attributes[i].vtype == VAL_INT) {
                    vars[my_index].vtype = VAL_INT;
                    vars[my_index].value.int_value = obj->attributes[i].value.int_value;
                } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                    vars[my_index].vtype = VAL_FLOAT;
                    vars[my_index].value.float_value = obj->attributes[i].value.float_value;
                }
                return; // ¡Asignación completada!
            }
        }
        
        printf("Error: Atributo '%s' no encontrado en '%s'.\n", a->id, o->id);
        vars[my_index].vtype = VAL_INT;
        vars[my_index].value.int_value = 0; // Valor de error
        return;
    }
    else {
        vars[my_index].vtype = VAL_INT;
        vars[my_index].value.int_value = value->value;
    }
}


void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;
    /* Fase 3a: si es STRING_INTERP, expandir a STRING antes de almacenar */
    ASTNode *expanded_holder = NULL;
    if (value->type && strcmp(value->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(value->str_value);
        expanded_holder = create_ast_leaf("STRING", 0, s, NULL);
        free(s);
        value = expanded_holder;
    }
    // printf("[DEBUG] add_or_update_variable: id=%s, type=%s, value=%d, str_value=%s\n", id, value->type ? value->type : "NULL", value->value, value->str_value ? value->str_value : "NULL");

    if (strcmp(id, "__ret__") == 0) {
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
        }
        __ret_var.id = strdup(id);
        __ret_var.is_const = 0;
        if (strcmp(value->type, "STRING") == 0) {
            __ret_var.vtype = VAL_STRING;
            __ret_var.type = strdup("STRING");
            __ret_var.value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("OBJECT");
            __ret_var.value.object_value = (ObjectNode *)value->extra;
        } else if (strcmp(value->type, "LIST") == 0) {
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("LIST");
            __ret_var.value.object_value = (ObjectNode *)value;
        } else if (strcmp(value->type, "FLOAT") == 0) {
            __ret_var.vtype = VAL_FLOAT;
            __ret_var.type = strdup("FLOAT");
            __ret_var.value.float_value = atof(value->str_value);
        } else {
            __ret_var.vtype = VAL_INT;
            __ret_var.type = strdup("INT");
            __ret_var.value.int_value = value->value;
        }
        __ret_var_active = 1;
        return;
    }

    Variable *var = find_variable_for(id);
    if (var) {        
        if (var->is_const) {
            fprintf(stderr, "Error: cannot assign to constant variable '%s'.\n", id);
            exit(1);
        }
        free(var->type);
        var->type = strdup(value->type);
        if (var->vtype == VAL_STRING && var->value.string_value) free(var->value.string_value);
        if (strcmp(value->type, "STRING") == 0) {
            var->vtype = VAL_STRING;
            var->value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            var->vtype = VAL_OBJECT;
            var->value.object_value = (ObjectNode *)value->extra;
        } else if (strcmp(value->type, "FLOAT") == 0) {
            var->vtype = VAL_FLOAT;
            var->value.float_value = value->str_value ? atof(value->str_value) : 0.0;
        } else {
            var->vtype = VAL_INT;
            var->value.int_value = value->value;
        }
    } else {
        if (var_count >= MAX_VARS) {
            printf("Error: too many declared variables.\n");
            return;
        }
        vars[var_count].id = strdup(id);
        vars[var_count].type = strdup(value->type);
        vars[var_count].is_const = 0;
        /* Ola 16 */
        te_sym_insert(vars[var_count].id, var_count);
        if (strcmp(value->type, "STRING") == 0) {
            vars[var_count].vtype = VAL_STRING;
            vars[var_count].value.string_value = strdup(value->str_value);
        } else if (strcmp(value->type, "OBJECT") == 0) {
            vars[var_count].vtype = VAL_OBJECT;
            vars[var_count].value.object_value = (ObjectNode *)value->extra;
        } else {
            vars[var_count].vtype = VAL_INT;
            vars[var_count].value.int_value = value->value;
        }
        var_count++;
    }
}

// ====================== CREACIÓN DE NODOS AST ======================

/* Fase 1 (perf): lazy NodeKind resolver. Looks up node->kind, computes
 * from node->type if still NK_UNKNOWN, and caches the result on the node.
 * Use this in hot dispatch paths instead of strcmp(node->type, "X"). */
static inline NodeKind nk_of(ASTNode *n) {
    if (!n) return NK_UNKNOWN;
    if (n->kind != NK_UNKNOWN) return n->kind;
    if (n->type) n->kind = nk_from_str(n->type);
    return n->kind;
}

/* Fase 1 (perf): map type-string -> NodeKind. Called once per node creation.
 * Uses first-character switch to keep dispatch O(1) on the common path. */
NodeKind nk_from_str(const char *t) {
    if (!t) return NK_UNKNOWN;
    switch (t[0]) {
        case 'A':
            if (!strcmp(t, "ADD")) return NK_ADD;
            if (!strcmp(t, "AND")) return NK_AND;
            if (!strcmp(t, "ASSIGN")) return NK_ASSIGN;
            if (!strcmp(t, "ASSIGN_ATTR")) return NK_ASSIGN_ATTR;
            if (!strcmp(t, "ACCESS_ATTR")) return NK_ACCESS_ATTR;
            if (!strcmp(t, "ACCESS_EXPR")) return NK_ACCESS_EXPR;
            if (!strcmp(t, "AGENT")) return NK_AGENT;
            if (!strcmp(t, "AGENT_LIST")) return NK_AGENT_LIST;
            break;
        case 'B':
            if (!strcmp(t, "BREAK")) return NK_BREAK;
            if (!strcmp(t, "BRIDGE_DECL")) return NK_BRIDGE_DECL;
            break;
        case 'C':
            if (!strcmp(t, "CALL_FUNC")) return NK_CALL_FUNC;
            if (!strcmp(t, "CALL_METHOD")) return NK_CALL_METHOD;
            if (!strcmp(t, "CONTINUE")) return NK_CONTINUE;
            break;
        case 'D':
            if (!strcmp(t, "DIV")) return NK_DIV;
            if (!strcmp(t, "DIFF")) return NK_DIFF;
            if (!strcmp(t, "DATASET")) return NK_DATASET;
            break;
        case 'E':
            if (!strcmp(t, "EQ")) return NK_EQ;
            if (!strcmp(t, "EXPRESSION")) return NK_EXPRESSION;
            break;
        case 'F':
            if (!strcmp(t, "FOR")) return NK_FOR;
            if (!strcmp(t, "FOR_IN")) return NK_FOR_IN;
            if (!strcmp(t, "FLOAT")) return NK_FLOAT;
            if (!strcmp(t, "FILTER_CALL")) return NK_FILTER_CALL;
            if (!strcmp(t, "FPRINT")) return NK_FPRINT;
            if (!strcmp(t, "FPRINTLN")) return NK_FPRINTLN;
            break;
        case 'G':
            if (!strcmp(t, "GT")) return NK_GT;
            if (!strcmp(t, "GT_EQ")) return NK_GT_EQ;
            break;
        case 'I':
            if (!strcmp(t, "IDENTIFIER")) return NK_IDENTIFIER;
            if (!strcmp(t, "ID")) return NK_ID;
            if (!strcmp(t, "INT")) return NK_INT;
            if (!strcmp(t, "IF")) return NK_IF;
            if (!strcmp(t, "INDEX_ASSIGN")) return NK_INDEX_ASSIGN;
            break;
        case 'K':
            if (!strcmp(t, "KV_PAIR")) return NK_KV_PAIR;
            break;
        case 'L':
            if (!strcmp(t, "LT")) return NK_LT;
            if (!strcmp(t, "LT_EQ")) return NK_LT_EQ;
            if (!strcmp(t, "LIST")) return NK_LIST;
            if (!strcmp(t, "LIST_FUNC_CALL")) return NK_LIST_FUNC_CALL;
            if (!strcmp(t, "LISTENER")) return NK_LISTENER;
            break;
        case 'M':
            if (!strcmp(t, "MUL")) return NK_MUL;
            if (!strcmp(t, "MATCH")) return NK_MATCH;
            if (!strcmp(t, "MODEL")) return NK_MODEL;
            if (!strcmp(t, "METHOD_CALL_ALONE")) return NK_METHOD_CALL_ALONE;
            break;
        case 'N':
            if (!strcmp(t, "NUMBER")) return NK_NUMBER;
            if (!strcmp(t, "NULL")) return NK_NULL;
            if (!strcmp(t, "NULL_COALESCE")) return NK_NULL_COALESCE;
            if (!strcmp(t, "NOT")) return NK_NOT;
            break;
        case 'O':
            if (!strcmp(t, "OR")) return NK_OR;
            if (!strcmp(t, "OBJECT")) return NK_OBJECT;
            if (!strcmp(t, "OBJECT_LITERAL")) return NK_OBJECT_LITERAL;
            break;
        case 'P':
            if (!strcmp(t, "PRINT")) return NK_PRINT;
            if (!strcmp(t, "PRINTLN")) return NK_PRINTLN;
            if (!strcmp(t, "PREDICT")) return NK_PREDICT;
            if (!strcmp(t, "PLOT")) return NK_PLOT;
            break;
        case 'R':
            if (!strcmp(t, "RETURN")) return NK_RETURN;
            if (!strcmp(t, "RETURN_JSON")) return NK_RETURN_JSON;
            if (!strcmp(t, "RETURN_XML")) return NK_RETURN_XML;
            break;
        case 'S':
            if (!strcmp(t, "SUB")) return NK_SUB;
            if (!strcmp(t, "STRING")) return NK_STRING;
            if (!strcmp(t, "STRING_LITERAL")) return NK_STRING_LITERAL;
            if (!strcmp(t, "STRING_INTERP")) return NK_STRING_INTERP;
            if (!strcmp(t, "STATE_DECL")) return NK_STATE_DECL;
            if (!strcmp(t, "STATEMENT_LIST")) return NK_STATEMENT_LIST;
            break;
        case 'T':
            if (!strcmp(t, "THROW")) return NK_THROW;
            if (!strcmp(t, "TRAIN")) return NK_TRAIN;
            if (!strcmp(t, "TRY_CATCH")) return NK_TRY_CATCH;
            break;
        case 'V':
            if (!strcmp(t, "VAR_DECL")) return NK_VAR_DECL;
            break;
        case 'W':
            if (!strcmp(t, "WHILE")) return NK_WHILE;
            break;
    }
    return NK_UNKNOWN;
}

/* ====================================================================
 * Ola 3 Fase A: STRING INTERNING TABLE
 * --------------------------------------------------------------------
 * Global hash set of immortal const char*. Calling tee_intern("hola")
 * always returns the same pointer for the same content. Strings are
 * never freed (Lua/Python style). Used for STRING literals to enable
 * O(1) pointer-equality in `==` / `!=` comparisons.
 *
 * Switch: TYPEEASY_NO_INTERN=1 disables interning.
 * ==================================================================== */
typedef struct InternEntry {
    char *str;
    size_t len;
    uint32_t hash;
    struct InternEntry *next;
} InternEntry;

#define INTERN_BUCKETS 1024
static InternEntry *g_intern_table[INTERN_BUCKETS];
static int g_intern_init = 0;
static int g_intern_enabled = 1;

static inline uint32_t intern_hash(const char *s, size_t len) {
    /* FNV-1a */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

const char *tee_intern(const char *s) {
    if (!s) return NULL;
    if (!g_intern_init) {
        const char *e = getenv("TYPEEASY_NO_INTERN");
        if (e && e[0] && e[0] != '0') g_intern_enabled = 0;
        g_intern_init = 1;
    }
    if (!g_intern_enabled) return s; /* caller must still own the memory */

    size_t len = strlen(s);
    uint32_t h = intern_hash(s, len);
    InternEntry *e = g_intern_table[h % INTERN_BUCKETS];
    while (e) {
        if (e->hash == h && e->len == len && memcmp(e->str, s, len) == 0)
            return e->str;
        e = e->next;
    }
    /* Not found -> insert. We strdup so the caller's memory is independent. */
    InternEntry *ne = (InternEntry*)malloc(sizeof(InternEntry));
    ne->str  = strdup(s);
    ne->len  = len;
    ne->hash = h;
    ne->next = g_intern_table[h % INTERN_BUCKETS];
    g_intern_table[h % INTERN_BUCKETS] = ne;
    return ne->str;
}

ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para ASTNode.\n");
        exit(1);
    }
    node->line = yylineno;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    /* Ola 3 Fase A: intern STRING literals so equality can be pointer-compared. */
    if (str_value) {
        if (node->kind == NK_STRING) {
            node->str_value    = (char*)tee_intern(str_value);
            node->str_interned = g_intern_enabled ? 1 : 0;
        } else {
            node->str_value = strdup(str_value);
        }
    } else {
        node->str_value = NULL;
    }
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;
    node->line = yylineno;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    node->str_value = str_value ? strdup(str_value) : NULL;
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->line = yylineno;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

   if (strcmp(type, "ADD") == 0) {
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
            printf("Error: division by zero.\n");
            node->value = 0;
        }
    } 
    else {
        node->value = 0;
    }
    // ... (lógica de SUB, MUL, DIV sin cambios) ...
    
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
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("ID");
    node->kind = NK_ID;
    node->id = strdup(name);
    node->left = NULL;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_var_decl_node(char *id, ASTNode *value) {
    //printf("[DEBUG] Entering create_var_decl_node: %s\n", id); fflush(stdout);
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para VAR_DECL node.\n");
        exit(1);
    }
    node->type = strdup("VAR_DECL");
    node->id = strdup(id);
    node->left = value;
    node->right = NULL;
    node->str_value = NULL; // Fix: Initialize to NULL to avoid garbage access
    node->line = yylineno;
    //printf("[DEBUG] create_var_decl_node success\n"); fflush(stdout);
    return node;
}

ASTNode *create_return_node(ASTNode *expr) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (node) node->line = yylineno;
    node->type = strdup("RETURN");
    node->id = NULL;
    node->left = expr;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_function_call_node(const char *funcName, ASTNode *args) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (n) n->line = yylineno;
    n->type = strdup("CALL_FUNC");
    n->id = strdup(funcName);
    n->left = args;
    n->right = NULL;
    n->str_value = NULL;
    n->value = 0;
    return n;
}

ASTNode *create_method_call_node(ASTNode *objectNode, const char *methodName, ASTNode *args) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (node) node->line = yylineno;
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
    ASTNode *obj = (ASTNode *)calloc(1, sizeof(ASTNode));
    obj->type = strdup("OBJECT");
    obj->left = args;
    obj->right = NULL;
    obj->id = strdup(class->name);
    // Store the actual ObjectNode pointer in 'extra' (used across the codebase)
    obj->extra = (struct ASTNode*)real_obj;
    obj->value = 0;
   
    return obj;
}

ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (node) node->line = yylineno;
    node->type = strdup("FOR");
    node->id = var->id;
    node->left = init;
    node->right = condition;
    
    ASTNode *update_body = (ASTNode *)calloc(1, sizeof(ASTNode));
    update_body->type = strdup("FOR_BODY");
    update_body->left = update;
    update_body->right = body;
    
    node->right->right = update_body;
    return node;
}


ASTNode* create_layer_node(const char* layer_type, int units, const char* activation) {
    ASTNode* node = calloc(1, sizeof(ASTNode));
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

    ASTNode* node = calloc(1, sizeof(ASTNode));
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

    ASTNode* node = calloc(1, sizeof(ASTNode));
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
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = strdup("TRAIN");
    n->id   = NULL;
    n->left = create_identifier_node(model_name);
    ASTNode* dataNode = create_identifier_node(data_name);
    dataNode->right = options;  
    n->right = dataNode;
    return n;
}


ASTNode* create_train_option_node(const char* key, int val) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = strdup("TRAIN_OPTION");
    n->id = strdup(key);
    n->value = val;
    n->left = n->right = NULL;
    return n;
}

ASTNode* create_predict_node(const char* model_name, const char* input_name) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("PREDICT");
    node->left = create_identifier_node(model_name);
    node->right = create_identifier_node(input_name);
    return node;
}

// ====================== CREACIÓN DE NODOS IF ======================

ASTNode* create_if_node(ASTNode* condition, ASTNode* if_branch, ASTNode* else_branch) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo if\n");
        exit(1);
    }
    node->type = strdup("IF");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = if_branch;
    node->next = else_branch;
    return node;
}

ASTNode* create_match_node(ASTNode* condition, ASTNode* case_list) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo match\n");
        exit(1);
    }
    node->type = strdup("MATCH");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = case_list;
    node->next = NULL;
    return node;
}

ASTNode* create_case_node(ASTNode* condition, ASTNode* body) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo case\n");
        exit(1);    }
    node->type = strdup("CASE");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = body;
    node->next = NULL;
    return node;
}

ASTNode* append_case_clause(ASTNode* list, ASTNode* case_clause) {
    if (!list) return case_clause;
    ASTNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = case_clause;
    return list;
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
    param->cached_var = NULL;
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

// Helper to check if node resolves to a string
int is_string_type(ASTNode *node) {
    if (!node) return 0;
    if (node->type && (strcmp(node->type, "STRING") == 0 || strcmp(node->type, "STRING_LITERAL") == 0 || strcmp(node->type, "STRING_INTERP") == 0)) return 1;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (v && v->vtype == VAL_STRING) return 1;
    }
    if (node->type && strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left;
        ASTNode *a = node->right;
        /* Bug fix: si o->id es NULL (p.ej. arr[i].attr donde o es ACCESS_EXPR),
         * no podemos buscar variable; tratar como no-string. */
        if (!o || !o->id) return 0;
        Variable *v = find_variable(o->id);
        if (v && v->vtype == VAL_OBJECT) {
            ObjectNode *obj = v->value.object_value;
             if (!obj) {
                ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
                if (wrapper && wrapper->extra) obj = (ObjectNode *)wrapper->extra;
            }
            if (obj) {
                for (int i = 0; i < obj->class->attr_count; i++) {
                    if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                        if (obj->attributes[i].vtype == VAL_STRING) return 1;
                    }
                }
            }
        }
    }
    if (node->type && strcmp(node->type, "ADD") == 0) {
        if (is_string_type(node->left) || is_string_type(node->right)) return 1;
    }
    if (node->type && strcmp(node->type, "STRING_INTERP") == 0) return 1;
    return 0;
}

/* --- Helpers para LIST (Fase 1a: Arrays) --- */

/* ============================================================
 * Ola 14 \u2014 Side-cache O(1) para LIST y MAP.
 *   Estructura paralela almacenada en `node->extra` del root
 *   (LIST root o OBJECT_LITERAL root). Construida lazy en la
 *   primera lectura post-mutaci\u00f3n. Invalidada (extra=NULL +
 *   free) al mutar la estructura subyacente.
 * ============================================================*/
typedef struct TEListIdx {
    int len;
    int cap;
    ASTNode **items;
} TEListIdx;

typedef struct TEMapSlot {
    uint64_t hash;
    const char *key;   /* alias to pair->id, do NOT free */
    ASTNode *pair;
} TEMapSlot;

typedef struct TEMapHash {
    int cap;          /* power of 2 */
    int count;
    TEMapSlot *slots;
} TEMapHash;

static uint64_t te_str_hash(const char *s) {
    /* FNV-1a 64 */
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static void te_list_idx_free(TEListIdx *ix) {
    if (!ix) return;
    if (ix->items) free(ix->items);
    free(ix);
}

static void te_map_hash_free(TEMapHash *h) {
    if (!h) return;
    if (h->slots) free(h->slots);
    free(h);
}

/* Invalidate side cache on `root` (LIST or OBJECT_LITERAL). Safe to call
 * even when extra is NULL or root is NULL. Distinguishes by node->type. */
static void te_invalidate_list_cache(ASTNode *root) {
    if (!root || !root->extra) return;
    te_list_idx_free((TEListIdx*)root->extra);
    root->extra = NULL;
}

/* Forward decl needed because te_list_get_idx is defined further below. */
static TEListIdx* te_list_get_idx(ASTNode *list);

/* Ola 14b: append `item` to a list AND keep the side-cache in sync.
 * Brings push from O(n) (walk to tail + invalidate cache) to O(1) amortized. */
static void te_list_append(ASTNode *list, ASTNode *item) {
    if (!list || !item) return;
    item->next = NULL;
    TEListIdx *ix = (TEListIdx*)list->extra;
    if (ix) {
        /* Grow if needed (geometric). */
        if (ix->len >= ix->cap) {
            int newcap = ix->cap * 2;
            if (newcap < 8) newcap = 8;
            ASTNode **nb = (ASTNode**)realloc(ix->items, (size_t)newcap * sizeof(ASTNode*));
            if (!nb) {
                /* OOM: drop cache, fall back to walk. */
                te_invalidate_list_cache(list);
                ASTNode *cur = list->left;
                if (!cur) list->left = item;
                else { while (cur->next) cur = cur->next; cur->next = item; }
                return;
            }
            ix->items = nb;
            ix->cap = newcap;
        }
        if (ix->len == 0) {
            list->left = item;
        } else {
            ASTNode *tail = ix->items[ix->len - 1];
            tail->next = item;
        }
        ix->items[ix->len++] = item;
    } else {
        /* Sin caché: build it from scratch (one O(n) walk amortized over
         * future appends), then append. */
        ASTNode *cur = list->left;
        if (!cur) {
            list->left = item;
        } else {
            while (cur->next) cur = cur->next;
            cur->next = item;
        }
        /* Construir caché perezosamente para que próximos push sean O(1). */
        (void)te_list_get_idx(list);
    }
}
static void te_invalidate_map_cache(ASTNode *root) {
    if (!root || !root->extra) return;
    te_map_hash_free((TEMapHash*)root->extra);
    root->extra = NULL;
}

/* Build / get list index. Returns NULL on OOM. Idempotent. */
static TEListIdx* te_list_get_idx(ASTNode *list) {
    if (!list) return NULL;
    if (list->extra) return (TEListIdx*)list->extra;
    /* count */
    int n = 0;
    ASTNode *cur = list->left;
    while (cur) { n++; cur = cur->next; }
    int cap = n < 8 ? 8 : n;
    TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
    if (!ix) return NULL;
    ix->len = n;
    ix->cap = cap;
    ix->items = (ASTNode**)calloc((size_t)cap, sizeof(ASTNode*));
    if (!ix->items) { free(ix); return NULL; }
    cur = list->left;
    for (int i = 0; i < n; i++) { ix->items[i] = cur; cur = cur->next; }
    list->extra = (struct ASTNode*)ix;
    return ix;
}

/* Insert key->pair into hash table; assumes capacity available. */
static void te_map_hash_insert(TEMapHash *h, uint64_t hh, const char *k, ASTNode *p) {
    int mask = h->cap - 1;
    int i = (int)(hh & (uint64_t)mask);
    while (h->slots[i].key != NULL) {
        i = (i + 1) & mask;
    }
    h->slots[i].hash = hh;
    h->slots[i].key  = k;
    h->slots[i].pair = p;
    h->count++;
}

/* Build / get map hash. Returns NULL on OOM. */
static TEMapHash* te_map_get_hash(ASTNode *map) {
    if (!map) return NULL;
    if (map->extra) return (TEMapHash*)map->extra;
    /* count entries */
    int n = 0;
    ASTNode *cur = map->left;
    while (cur) { n++; cur = cur->right; }
    int cap = 16;
    while (cap < n * 2) cap <<= 1;
    TEMapHash *h = (TEMapHash*)calloc(1, sizeof(TEMapHash));
    if (!h) return NULL;
    h->cap = cap;
    h->count = 0;
    h->slots = (TEMapSlot*)calloc((size_t)cap, sizeof(TEMapSlot));
    if (!h->slots) { free(h); return NULL; }
    cur = map->left;
    while (cur) {
        if (cur->id) {
            uint64_t hh = te_str_hash(cur->id);
            te_map_hash_insert(h, hh, cur->id, cur);
        }
        cur = cur->right;
    }
    map->extra = (struct ASTNode*)h;
    return h;
}

static ASTNode* list_get_item(ASTNode *list, int idx) {
    if (!list || !list->type || strcmp(list->type, "LIST") != 0) return NULL;
    if (idx < 0) return NULL;
    /* Ola 14: O(1) via side-cache index. */
    TEListIdx *ix = te_list_get_idx(list);
    if (ix) {
        if (idx >= ix->len) return NULL;
        return ix->items[idx];
    }
    /* Fallback (OOM): legacy O(n) walk. */
    ASTNode *cur = list->left;
    int i = 0;
    while (cur && i < idx) { cur = cur->next; i++; }
    return cur;
}

static int list_length(ASTNode *list) {
    if (!list || !list->type || strcmp(list->type, "LIST") != 0) return 0;
    /* Ola 14: O(1) via side-cache index. */
    TEListIdx *ix = te_list_get_idx(list);
    if (ix) return ix->len;
    /* Fallback. */
    int n = 0;
    ASTNode *cur = list->left;
    while (cur) { n++; cur = cur->next; }
    return n;
}

/* Resolve an expression node to the underlying LIST ASTNode, or NULL. */
static ASTNode* resolve_to_list(ASTNode *node) {
    if (!node) return NULL;
    if (node->type && strcmp(node->type, "LIST") == 0) return node;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (!v || !v->type || strcmp(v->type, "LIST") != 0) return NULL;
        return (ASTNode*)(intptr_t)v->value.object_value;
    }
    return NULL;
}

/* --- Helpers para MAP (Fase 1c) --- */
static ASTNode* resolve_to_map(ASTNode *node) {
    if (!node) return NULL;
    if (node->type && strcmp(node->type, "OBJECT_LITERAL") == 0) return node;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (!v || !v->type || strcmp(v->type, "MAP") != 0) return NULL;
        return (ASTNode*)(intptr_t)v->value.object_value;
    }
    return NULL;
}

static int map_length(ASTNode *map) {
    if (!map) return 0;
    /* Ola 14: O(1) via side-cache hash. */
    TEMapHash *h = te_map_get_hash(map);
    if (h) return h->count;
    int n = 0;
    ASTNode *cur = map->left;
    while (cur) { n++; cur = cur->right; }
    return n;
}

/* Find a KV_PAIR by key string. Returns the pair node, or NULL. */
static ASTNode* map_find_pair(ASTNode *map, const char *key) {
    if (!map || !key) return NULL;
    /* Ola 14: O(1) via side-cache hash. */
    TEMapHash *h = te_map_get_hash(map);
    if (h && h->cap > 0) {
        uint64_t hh = te_str_hash(key);
        int mask = h->cap - 1;
        int i = (int)(hh & (uint64_t)mask);
        for (;;) {
            const char *sk = h->slots[i].key;
            if (sk == NULL) return NULL;
            /* Ola 15: pointer-eq fast-path (interned keys). */
            if (sk == key) return h->slots[i].pair;
            if (h->slots[i].hash == hh && strcmp(sk, key) == 0) return h->slots[i].pair;
            i = (i + 1) & mask;
        }
    }
    /* Fallback O(n). */
    ASTNode *cur = map->left;
    while (cur) {
        if (cur->id) {
            if (cur->id == key) return cur;  /* Ola 15: ptr-eq */
            if (strcmp(cur->id, key) == 0) return cur;
        }
        cur = cur->right;
    }
    return NULL;
}

/* Fase 3a: Expand string interpolation. Input contains {var} placeholders.
   Special chars: \1 = literal '{', \2 = literal '}'. Returns malloc'd string. */
char* expand_interp_string(const char *raw) {
    if (!raw) return strdup("");
    size_t cap = strlen(raw) + 64;
    char *out = (char*)malloc(cap);
    size_t len = 0;
    const char *p = raw;
    while (*p) {
        if (*p == '\1') {
            if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '{'; p++;
            continue;
        }
        if (*p == '\2') {
            if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '}'; p++;
            continue;
        }
        if (*p == '{') {
            const char *end = strchr(p, '}');
            if (!end) {
                if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
                continue;
            }
            char name[128]; size_t nlen = end - p - 1;
            if (nlen >= sizeof(name)) nlen = sizeof(name)-1;
            memcpy(name, p+1, nlen); name[nlen] = '\0';
            char *s = name; while (*s == ' ') s++;
            char *e = s + strlen(s); while (e > s && (e[-1]==' ')) { e--; *e='\0'; }
            char buf[256]; const char *valstr = "";
            Variable *v = find_variable(s);
            if (v) {
                if (v->type && strcmp(v->type, "NULL") == 0) valstr = "null";
                else if (v->vtype == VAL_STRING) valstr = v->value.string_value ? v->value.string_value : "";
                else if (v->vtype == VAL_INT) { snprintf(buf,256,"%d",v->value.int_value); valstr = buf; }
                else if (v->vtype == VAL_FLOAT) { snprintf(buf,256,"%g",v->value.float_value); valstr = buf; }
                else valstr = "";
            }
            size_t vl = strlen(valstr);
            while (len + vl + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + len, valstr, vl); len += vl;
            p = end + 1;
            continue;
        }
        if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = *p++;
    }
    out[len] = '\0';
    return out;
}

/* Build a fresh ASTNode item from a runtime value (used for index assign / push). */
static ASTNode* build_item_from_value(ASTNode *value) {
    ASTNode *new_item = (ASTNode*)calloc(1, sizeof(ASTNode));
    memset(new_item, 0, sizeof(ASTNode));
    if (!value) { new_item->type = strdup("NUMBER"); return new_item; }
    if (value->type && strcmp(value->type, "STRING") == 0) {
        new_item->type = strdup("STRING");
        new_item->str_value = strdup(value->str_value ? value->str_value : "");
    } else if (value->type && (strcmp(value->type, "IDENTIFIER") == 0 || strcmp(value->type, "ID") == 0)) {
        Variable *vv = find_variable(value->id);
        if (vv && vv->vtype == VAL_STRING) {
            new_item->type = strdup("STRING");
            new_item->str_value = strdup(vv->value.string_value ? vv->value.string_value : "");
        } else if (vv && vv->vtype == VAL_FLOAT) {
            new_item->type = strdup("FLOAT");
            char buf[64]; snprintf(buf, 64, "%f", vv->value.float_value);
            new_item->str_value = strdup(buf);
        } else if (vv) {
            new_item->type = strdup("NUMBER");
            new_item->value = vv->value.int_value;
        } else {
            new_item->type = strdup("NUMBER");
        }
    } else {
        double v_ = evaluate_expression(value);
        if (v_ == (int)v_) {
            new_item->type = strdup("NUMBER");
            new_item->value = (int)v_;
        } else {
            new_item->type = strdup("FLOAT");
            char buf[64]; snprintf(buf, 64, "%f", v_);
            new_item->str_value = strdup(buf);
        }
    }
    return new_item;
}

/* ===================== Fase 3: bytecode mini-VM (numeric expressions) =====================
 * Goal: speed up hot numeric expressions in loops by lazily compiling them
 * into a flat opcode stream and executing via computed-goto dispatch.
 * Eligible: ADD/SUB/MUL/DIV/comparisons over numeric variables and constants
 * (no strings, no NULL, no method calls, no attribute access). Anything else
 * sets bc=BC_NOT_COMPILABLE on the node and falls back to the AST walker. */

typedef enum {
    BC_HALT = 0,
    BC_LOAD_CONST,
    BC_LOAD_VAR,        /* numeric var (INT/FLOAT) — pointer cached at compile time */
    BC_ADD, BC_SUB, BC_MUL, BC_DIV,
    BC_LT, BC_GT, BC_LE, BC_GE, BC_EQ, BC_NEQ,
    BC_AND, BC_OR, BC_NOT,
    BC_NEG,             /* unary minus (compiled from SUB(0, x)) — currently unused */
    /* Fase 4: full-statement bytecode (assign / while / if / blocks). */
    BC_STORE_VAR,       /* pop top, write to numeric Variable* (auto INT/FLOAT) */
    BC_JUMP,            /* ip += offset (relative, signed) */
    BC_JUMP_IF_FALSE,   /* pop; if 0, ip += offset; else fall through */
    BC_POP,             /* discard top of stack */
    /* Ola 4: load this.attr where attr slot is known at compile time. */
    BC_LOAD_THIS_ATTR,  /* operand=int slot index in obj->attributes */
    /* Ola 5: inline call to a method whose body is already bytecode. */
    BC_CALL_METHOD,     /* operand=MethodCallSite* (precomputed, recursive bc_exec) */
    /* Ola 5b: inline-expanded method call (body opcodes copied in caller). */
    BC_SET_THIS,        /* operand=Variable* holding the object; pushes saved this */
    BC_RESTORE_THIS,    /* pops saved this back into g_bc_this */
    /* Ola 14d: arr[idx].attr fastpath. operand=ListItemAttrSite*.
     * Pops idx from stack, pushes the (numeric) attribute value. */
    BC_LIST_ITEM_ATTR
} BCOp;

/* Ola 5: precomputed call site for a `obj.method(args)` callable from
 * inside another bytecode program. The compiler resolves obj's Variable*,
 * the target MethodNode, its compiled body BCInfo*, and the parameter
 * Variable* slots — all at compile time. At runtime BC_CALL_METHOD pops
 * args from stack, writes them into param slots, sets g_bc_this, runs
 * bc_exec on the body, and pushes the result. No interpret_call_method,
 * no AST walker. */
typedef struct MethodCallSite {
    struct Variable   *obj_var;       /* var holding ObjectNode* */
    struct MethodNode *method;        /* target method (informational) */
    void              *body_bc;       /* BCInfo* (compiled return expression) */
    int                n_params;      /* number of params */
    struct Variable   *param_vars[8]; /* cached param Variable*s */
} MethodCallSite;

/* Ola 14d: precomputed call site for `list_var[idx].attr` (numeric attr).
 * Resolved at compile time when we can prove (a) list_var is a LIST,
 * (b) items[0] is an OBJECT of a known class, (c) the attribute exists
 * with int/float type. At runtime BC_LIST_ITEM_ATTR pops idx, reads
 * items[idx] from TEListIdx, validates class match (cheap pointer cmp),
 * returns the attr value. Class mismatch → push 0 (homogeneous list
 * assumption). */
typedef struct ListItemAttrSite {
    struct Variable  *list_var;        /* v->type=="LIST", v->value.object_value=(ASTNode*) LIST root */
    struct ClassNode *expected_class;  /* class of items[0] at compile time */
    int               attr_slot;       /* index in obj->attributes[] */
} ListItemAttrSite;

typedef struct {
    uint8_t op;
    union {
        Variable *var;      /* for BC_LOAD_VAR / BC_STORE_VAR */
        double    constant; /* for BC_LOAD_CONST */
        int32_t   offset;   /* for BC_JUMP / BC_JUMP_IF_FALSE (signed, relative to next ip) */
        int32_t   slot;     /* for BC_LOAD_THIS_ATTR */
        MethodCallSite *site; /* for BC_CALL_METHOD */
        ListItemAttrSite *lia_site; /* for BC_LIST_ITEM_ATTR (Ola 14d) */
    } u;
} Instr;

typedef struct BCInfo {
    Instr *code;
    int    len;
} BCInfo;

#define BC_NOT_COMPILABLE ((void*)0x1)

/* Ola 4: per-call current `this` for BC_LOAD_THIS_ATTR. Set by
 * interpret_call_method before invoking bc_exec on a compiled method body. */
static ObjectNode *g_bc_this = NULL;
/* Ola 5b: small save/restore stack for inline-expanded method calls. */
static ObjectNode *g_bc_this_stack[16];
static int         g_bc_this_sp = 0;
/* Ola 4: class context for compiling method bodies — used to resolve
 * `this.attr` to a fixed slot index at compile time. */
static ClassNode *g_bc_compile_class = NULL;

/* Forward decl */
static int bc_compile(ASTNode *node, Instr *out, int *pos, int max);
typedef struct BCInfo BCInfo;
static BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls);

/* Try to compile `node` as a numeric expression. Returns 1 on success,
 * appending instructions to out[*pos..]. On failure returns 0 and *pos may
 * be partially advanced — caller discards the buffer. */
static int bc_compile(ASTNode *node, Instr *out, int *pos, int max) {
    if (!node || *pos >= max - 1) return 0;
    NodeKind k = nk_of(node);
    switch (k) {
    case NK_NUMBER:
    case NK_INT:
        out[*pos].op = BC_LOAD_CONST;
        out[*pos].u.constant = (double)node->value;
        (*pos)++;
        return 1;
    case NK_FLOAT:
        out[*pos].op = BC_LOAD_CONST;
        out[*pos].u.constant = node->str_value ? atof(node->str_value) : 0.0;
        (*pos)++;
        return 1;
    case NK_IDENTIFIER: {
        Variable *v = (Variable *)node->cached_var;
        if (!v) {
            v = find_variable(node->id);
            if (v) node->cached_var = v;
        }
        if (!v) return 0;
        if (v->vtype != VAL_INT && v->vtype != VAL_FLOAT) return 0;
        out[*pos].op = BC_LOAD_VAR;
        out[*pos].u.var = v;
        (*pos)++;
        return 1;
    }
    case NK_ADD: case NK_SUB: case NK_MUL: case NK_DIV:
    case NK_LT:  case NK_GT:
    case NK_LT_EQ: case NK_GT_EQ:
    case NK_EQ:  case NK_DIFF:
    case NK_AND: case NK_OR: {
        /* String concat must NOT use bytecode path (handled by AST walker). */
        if (k == NK_ADD && is_string_type(node)) return 0;
        int saved = *pos;
        if (!bc_compile(node->left,  out, pos, max)) return 0;
        if (!bc_compile(node->right, out, pos, max)) return 0;

        /* Constant folding (#3): if both operands are LOAD_CONST, evaluate
         * at compile time and replace with a single LOAD_CONST. */
        if (*pos >= saved + 2 &&
            out[*pos - 2].op == BC_LOAD_CONST &&
            out[*pos - 1].op == BC_LOAD_CONST) {
            double a = out[*pos - 2].u.constant;
            double b = out[*pos - 1].u.constant;
            double r = 0;
            int folded = 1;
            switch (k) {
                case NK_ADD:   r = a + b; break;
                case NK_SUB:   r = a - b; break;
                case NK_MUL:   r = a * b; break;
                case NK_DIV:   if (b == 0) { folded = 0; } else r = a / b; break;
                case NK_LT:    r = (a <  b); break;
                case NK_GT:    r = (a >  b); break;
                /* NK_GT_EQ / NK_LT_EQ have inverted semantics in TypeEasy
                 * (GT_EQ is evaluated as <=, LT_EQ as >=). Mirror that. */
                case NK_GT_EQ: r = (a <= b); break;
                case NK_LT_EQ: r = (a >= b); break;
                case NK_EQ:    r = (a == b); break;
                case NK_DIFF:  r = (a != b); break;
                case NK_AND:   r = (a && b) ? 1 : 0; break;
                case NK_OR:    r = (a || b) ? 1 : 0; break;
                default:       folded = 0; break;
            }
            if (folded) {
                *pos -= 2;
                out[*pos].op = BC_LOAD_CONST;
                out[*pos].u.constant = r;
                (*pos)++;
                return 1;
            }
        }

        BCOp op;
        switch (k) {
            case NK_ADD:   op = BC_ADD; break;
            case NK_SUB:   op = BC_SUB; break;
            case NK_MUL:   op = BC_MUL; break;
            case NK_DIV:   op = BC_DIV; break;
            case NK_LT:    op = BC_LT;  break;
            case NK_GT:    op = BC_GT;  break;
            /* Inverted: GT_EQ in TypeEasy means <=, LT_EQ means >=. */
            case NK_GT_EQ: op = BC_LE;  break;
            case NK_LT_EQ: op = BC_GE;  break;
            case NK_EQ:    op = BC_EQ;  break;
            case NK_DIFF:  op = BC_NEQ; break;
            case NK_AND:   op = BC_AND; break;
            case NK_OR:    op = BC_OR;  break;
            default:       return 0;
        }
        out[*pos].op = op;
        (*pos)++;
        return 1;
    }
    case NK_NOT: {
        if (!node->left) return 0;
        int saved = *pos;
        if (!bc_compile(node->left, out, pos, max)) return 0;
        /* Constant fold: !const → folded */
        if (*pos == saved + 1 && out[saved].op == BC_LOAD_CONST) {
            out[saved].u.constant = (out[saved].u.constant == 0.0) ? 1 : 0;
            return 1;
        }
        if (*pos >= max) return 0;
        out[*pos].op = BC_NOT;
        (*pos)++;
        return 1;
    }
    case NK_ACCESS_ATTR: {
        ASTNode *objRef = node->left;
        ASTNode *attr   = node->right;
        if (!objRef || !attr || !attr->id) return 0;

        /* Ola 14d: arr[idx_expr].attr fastpath. Detect when objRef is an
         * ACCESS_EXPR whose left is an IDENTIFIER bound to a non-empty
         * LIST whose items[0] is an OBJECT of a known class with a
         * numeric attribute matching attr->id. Compile idx_expr onto the
         * stack, then emit BC_LIST_ITEM_ATTR. */
        if (nk_of(objRef) == NK_ACCESS_EXPR) {
            ASTNode *list_id = objRef->left;
            ASTNode *idx_exp = objRef->right;
            if (!list_id || !idx_exp) return 0;
            if (nk_of(list_id) != NK_IDENTIFIER && nk_of(list_id) != NK_ID) return 0;
            Variable *lv = find_variable(list_id->id);
            if (!lv || !lv->type || strcmp(lv->type, "LIST") != 0) return 0;
            ASTNode *list = (ASTNode*)(intptr_t)lv->value.object_value;
            if (!list) return 0;
            TEListIdx *ix = (TEListIdx*)list->extra;
            if (!ix || ix->len <= 0) return 0;
            ASTNode *first = ix->items[0];
            if (!first || !first->type || strcmp(first->type, "OBJECT") != 0) return 0;
            ObjectNode *fobj = first->extra ? (ObjectNode*)first->extra
                                            : (ObjectNode*)(intptr_t)first->value;
            if (!fobj || !fobj->class) return 0;
            int slot = -1;
            for (int i = 0; i < fobj->class->attr_count; i++) {
                if (strcmp(fobj->class->attributes[i].id, attr->id) == 0) {
                    const char *t = fobj->class->attributes[i].type;
                    if (!t || (strcmp(t, "int") != 0 && strcmp(t, "float") != 0
                            && strcmp(t, "INT") != 0 && strcmp(t, "FLOAT") != 0))
                        return 0;
                    slot = i;
                    break;
                }
            }
            if (slot < 0) return 0;
            /* Compile idx expression onto stack. */
            if (!bc_compile(idx_exp, out, pos, max)) return 0;
            if (*pos >= max) return 0;
            ListItemAttrSite *s = (ListItemAttrSite*)calloc(1, sizeof(ListItemAttrSite));
            if (!s) return 0;
            s->list_var = lv;
            s->expected_class = fobj->class;
            s->attr_slot = slot;
            out[*pos].op = BC_LIST_ITEM_ATTR;
            out[*pos].u.lia_site = s;
            (*pos)++;
            return 1;
        }

        /* Ola 4: support `this.attr` in method bodies. We compile only
         * when:
         *  - left is the identifier "this"
         *  - g_bc_compile_class is set (we know which class the method
         *    belongs to)
         *  - the attribute exists in the class and is INT or FLOAT
         * The attr slot index is baked into the instruction. At runtime
         * BC_LOAD_THIS_ATTR reads from g_bc_this->attributes[slot]. */
        if (!objRef->id) return 0;
        if (strcmp(objRef->id, "this") != 0) return 0;
        if (!g_bc_compile_class) return 0;
        int slot = -1;
        for (int i = 0; i < g_bc_compile_class->attr_count; i++) {
            if (strcmp(g_bc_compile_class->attributes[i].id, attr->id) == 0) {
                /* Only numeric attrs supported. */
                const char *t = g_bc_compile_class->attributes[i].type;
                if (!t || (strcmp(t, "int") != 0 && strcmp(t, "float") != 0
                        && strcmp(t, "INT") != 0 && strcmp(t, "FLOAT") != 0))
                    return 0;
                slot = i;
                break;
            }
        }
        if (slot < 0) return 0;
        out[*pos].op = BC_LOAD_THIS_ATTR;
        out[*pos].u.slot = slot;
        (*pos)++;
        return 1;
    }
    case NK_CALL_METHOD: {
        /* Ola 5: inline method call. We compile to BC_CALL_METHOD when:
         *  - left side is an identifier resolving to an object Variable*
         *  - the method exists in that class with int/float return
         *  - its body is bytecode-compilable via bc_get_or_compile_method
         *  - all params are int/float
         *  - all args are leaf nodes (NUMBER/INT/FLOAT/IDENTIFIER) since
         *    args are linked via ->right which makes complex expressions
         *    in multi-arg lists ambiguous in TypeEasy's AST.
         * Switch: TYPEEASY_NO_BCCALL=1 disables. */
        if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] entering NK_CALL_METHOD case\n");
        static int bcc_init = 0;
        static int bcc_enabled = 1;
        if (!bcc_init) {
            const char *e = getenv("TYPEEASY_NO_BCCALL");
            if (e && e[0] && e[0] != '0') bcc_enabled = 0;
            bcc_init = 1;
        }
        if (!bcc_enabled) return 0;

        ASTNode *objRef = node->left;
        if (!objRef || !objRef->id || !node->id) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: no obj or method id\n");
            return 0;
        }
        Variable *ov = find_variable(objRef->id);
        if (!ov || ov->vtype != VAL_OBJECT) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: obj '%s' not VAL_OBJECT (ov=%p vtype=%d)\n", objRef->id, (void*)ov, ov?ov->vtype:-1);
            return 0;
        }
        ObjectNode *obj = ov->value.object_value;
        if (!obj || !obj->class) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: no class\n");
            return 0;
        }

        MethodNode *mm = NULL;
        for (MethodNode *it = obj->class->methods; it; it = it->next) {
            if (it->name && strcmp(it->name, node->id) == 0) { mm = it; break; }
        }
        if (!mm) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: method '%s' not found\n", node->id);
            return 0;
        }
        if (!mm->return_type
            || (strcmp(mm->return_type, "int")   != 0
             && strcmp(mm->return_type, "float") != 0)) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: return_type=%s\n", mm->return_type?mm->return_type:"(null)");
            return 0;
        }

        BCInfo *body = bc_get_or_compile_method(mm, obj->class);
        if (!body) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: body bc_compile failed\n");
            return 0;
        }

        if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] body compiled, len=%d\n", body->len);

        /* Validate params.
         * NOTE: TypeEasy's lexer does NOT set yylval.sval for INT/FLOAT
         * tokens, so p->type may end up holding the param NAME instead of
         * "int"/"float". We therefore skip the type-string check and
         * rely on BC_STORE_VAR's runtime int/float detection. We just need
         * each param to have a name and a slot of some numeric kind. */
        int n_params = 0;
        for (ParameterNode *p = mm->params; p; p = p->next) {
            if (n_params >= 8) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: too many params\n"); return 0; }
            if (!p->name) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: param no name\n"); return 0; }
            n_params++;
        }

        /* Count args */
        int n_args = 0;
        ASTNode *a = node->right;
        while (a) { n_args++; a = a->right; }
        if (n_args != n_params) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr, "[OLA5] fail: n_args=%d n_params=%d\n", n_args, n_params); return 0; }

        /* Emit args (leaves only) */
        a = node->right;
        while (a) {
            NodeKind ak = nk_of(a);
            if (*pos >= max - 2) return 0;
            if (ak == NK_NUMBER || ak == NK_INT) {
                out[*pos].op = BC_LOAD_CONST;
                out[*pos].u.constant = (double)a->value;
                (*pos)++;
            } else if (ak == NK_FLOAT) {
                out[*pos].op = BC_LOAD_CONST;
                out[*pos].u.constant = a->str_value ? atof(a->str_value) : 0.0;
                (*pos)++;
            } else if (ak == NK_IDENTIFIER || ak == NK_ID) {
                Variable *v = (Variable *)a->cached_var;
                if (!v) { v = find_variable(a->id); if (v) a->cached_var = v; }
                if (!v) return 0;
                if (v->vtype != VAL_INT && v->vtype != VAL_FLOAT) return 0;
                out[*pos].op = BC_LOAD_VAR;
                out[*pos].u.var = v;
                (*pos)++;
            } else {
                return 0;
            }
            a = a->right;
        }

        /* Build site (cached param Variable*s) */
        MethodCallSite *site = (MethodCallSite *)calloc(1, sizeof(MethodCallSite));
        site->obj_var  = ov;
        site->method   = mm;
        site->body_bc  = body;
        site->n_params = n_params;
        int idx = 0;
        for (ParameterNode *p = mm->params; p; p = p->next) {
            Variable *pv = (Variable *)p->cached_var;
            if (!pv) {
                pv = find_variable_for(p->name);
                if (!pv) {
                    if (var_count < MAX_VARS) {
                        int is_float = (p->type
                                      && (strcmp(p->type, "float") == 0
                                       || strcmp(p->type, "FLOAT") == 0));
                        vars[var_count].id       = strdup(p->name);
                        vars[var_count].type     = strdup(is_float ? "FLOAT" : "INT");
                        vars[var_count].is_const = 0;
                        vars[var_count].vtype    = is_float ? VAL_FLOAT : VAL_INT;
                        if (is_float) vars[var_count].value.float_value = 0.0;
                        else          vars[var_count].value.int_value   = 0;
                        pv = &vars[var_count];
                        var_count++;
                    }
                }
                p->cached_var = pv;
            }
            if (!pv) { free(site); return 0; }
            site->param_vars[idx++] = pv;
        }

        /* Ola 5b: INLINE EXPANSION instead of recursive bc_exec.
         * After args are on stack we:
         *   1) STORE_VAR each arg into its param slot (in reverse so last
         *      pushed = last param).
         *   2) BC_SET_THIS (pushes saved g_bc_this on this-stack, sets new).
         *   3) Inline body opcodes (excluding the trailing HALT).
         *   4) BC_RESTORE_THIS (pops this-stack back into g_bc_this).
         * Stack net: -n_params (consumed) +1 (return value) = +1.
         */
        int body_len_no_halt = body->len - 1; /* drop trailing HALT */
        if (body_len_no_halt < 0) body_len_no_halt = 0;
        if (*pos + n_params + 2 + body_len_no_halt >= max) {
            free(site);
            return 0;
        }
        for (int i = n_params - 1; i >= 0; i--) {
            out[*pos].op    = BC_STORE_VAR;
            out[*pos].u.var = site->param_vars[i];
            (*pos)++;
        }
        out[*pos].op    = BC_SET_THIS;
        out[*pos].u.var = ov;
        (*pos)++;
        if (body_len_no_halt > 0) {
            memcpy(&out[*pos], body->code, body_len_no_halt * sizeof(Instr));
            *pos += body_len_no_halt;
        }
        out[*pos].op = BC_RESTORE_THIS;
        (*pos)++;
        /* site is no longer needed (we inlined); free it. */
        free(site);
        {
            static int reported = 0;
            if (!reported && getenv("TYPEEASY_BCDEBUG")) {
                fprintf(stderr, "[OLA5] inlined call %s.%s n_params=%d body_len=%d\n",
                        objRef->id, mm->name, n_params, body_len_no_halt);
                reported = 1;
            }
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ============================================================ */
/* Ola 6: hot-path profiler (gateado por TYPEEASY_PROFILE=1).    */
/* ============================================================ */
/* Conteo de regiones calientes — entradas a bc_exec (loops/methods)
 * y backward jumps (loop back-edges). Usamos open-addressing con
 * lineal probing sobre el puntero del code/target (clave). El overhead
 * es cero cuando g_profile_on == 0 (un único if por evento profilable).
 *
 * Sin esto no hay JIT posible: un Tracing JIT necesita saber QUÉ
 * recompilar antes de generar código. Esta Ola sólo mide; no compila. */
#define HOT_TABLE_SZ 1024  /* potencia de 2 */
typedef struct {
    Instr   *key;
    uint64_t count;
    uint8_t  kind; /* 0=unused, 1=bc_exec entry, 2=backward jump target */
} HotEntry;
static HotEntry g_hot[HOT_TABLE_SZ];
static int      g_profile_on = -1; /* -1 = no inicializado, 0=off, 1=on */

static void profile_dump(void); /* fwd */

static void profile_init_once(void) {
    if (g_profile_on != -1) return;
    g_profile_on = (getenv("TYPEEASY_PROFILE") != NULL) ? 1 : 0;
    if (g_profile_on) {
        memset(g_hot, 0, sizeof(g_hot));
        atexit(profile_dump);
    }
}

static inline void profile_bump(Instr *key, uint8_t kind) {
    /* Open-addressing por puntero. Cae al primer slot libre o al match. */
    uintptr_t h = (uintptr_t)key;
    h ^= (h >> 16);
    h *= 0x9E3779B1u;
    int idx = (int)(h & (HOT_TABLE_SZ - 1));
    for (int probe = 0; probe < HOT_TABLE_SZ; probe++) {
        HotEntry *e = &g_hot[(idx + probe) & (HOT_TABLE_SZ - 1)];
        if (e->key == key) { e->count++; return; }
        if (e->key == NULL) {
            e->key = key; e->count = 1; e->kind = kind; return;
        }
    }
    /* tabla llena — silenciosamente ignora (sólo profiling) */
}

static int hot_cmp(const void *a, const void *b) {
    const HotEntry *ea = (const HotEntry *)a;
    const HotEntry *eb = (const HotEntry *)b;
    if (eb->count > ea->count) return 1;
    if (eb->count < ea->count) return -1;
    return 0;
}

static void profile_dump(void) {
    if (!g_profile_on) return;
    HotEntry sorted[HOT_TABLE_SZ];
    memcpy(sorted, g_hot, sizeof(g_hot));
    qsort(sorted, HOT_TABLE_SZ, sizeof(HotEntry), hot_cmp);
    fprintf(stderr, "\n=== TypeEasy Ola 6 — hot regions (top 20) ===\n");
    fprintf(stderr, "%-6s %-18s %-18s %s\n", "rank", "kind", "key(Instr*)", "hits");
    int shown = 0;
    for (int i = 0; i < HOT_TABLE_SZ && shown < 20; i++) {
        if (!sorted[i].key || sorted[i].count == 0) continue;
        const char *k = (sorted[i].kind == 1) ? "bc_exec entry"
                       : (sorted[i].kind == 2) ? "backward jump"
                       : "?";
        fprintf(stderr, "  #%-3d %-18s %-18p %llu\n",
                shown + 1, k, (void*)sorted[i].key,
                (unsigned long long)sorted[i].count);
        shown++;
    }
    if (shown == 0)
        fprintf(stderr, "  (no hot regions recorded)\n");
    fprintf(stderr, "=============================================\n");
}

/* ============================================================ */
/* Ola 7: IR SSA tipado + buffer de trazas (infraestructura).   */
/* ============================================================ */
/* Esta Ola NO graba ni ejecuta trazas todavía — sólo define la
 * estructura de datos y el trigger por threshold. Ola 8 conectará
 * los hooks de cada opcode handler para emitir IR.
 *
 * Diseño SSA: cada IR op tiene un id (índice en buffer), un tipo
 * (T_INT/T_FLOAT/T_BOOL/T_OBJ/T_UNK), un opcode y hasta 2 refs a
 * otros IR ops por id. Las constantes y vars se materializan como
 * ops también para mantener forma SSA. */
typedef enum {
    T_UNK = 0,
    T_INT,
    T_FLOAT,
    T_BOOL,
    T_OBJ,
} IRType;

typedef enum {
    IR_NOP = 0,
    /* Loads (sin operandos) */
    IR_LOAD_CONST,      /* aux=double constant value */
    IR_LOAD_VAR,        /* aux=Variable* */
    IR_LOAD_THIS_ATTR,  /* aux=int slot */
    /* Guards (1 ref): si el tipo dinámico no coincide, deopt */
    IR_GUARD_INT,       /* a=ref a checkear */
    IR_GUARD_FLOAT,
    IR_GUARD_OBJ,       /* aux=ClassNode* esperado */
    /* Ola 11: guards de control (deopt si la cond no coincide). */
    IR_GUARD_TRUE,      /* a=valor; deopt si valor==0  (loop continues iff true) */
    IR_GUARD_FALSE,     /* a=valor; deopt si valor!=0 */
    /* Aritmética tipada (2 refs) */
    IR_ADD_INT, IR_SUB_INT, IR_MUL_INT, IR_DIV_INT,
    IR_ADD_FLOAT, IR_SUB_FLOAT, IR_MUL_FLOAT, IR_DIV_FLOAT,
    /* Comparaciones */
    IR_LT, IR_GT, IR_LE, IR_GE, IR_EQ, IR_NEQ,
    /* Side effects */
    IR_STORE_VAR,       /* aux=Variable*, a=value ref */
    IR_RETURN,          /* a=value ref */
    /* Ola 14e: list item attribute load (a = idx ref).
     * aux=ListItemAttrSite*. type=T_INT. Has guards (deopt on bounds,
     * null, class mismatch), so it's a side-effect op. Codegen inlines
     * the full lookup in asm; bytecode handler also emits this IR. */
    IR_LIST_ITEM_ATTR,
    /* Loop terminators */
    IR_LOOP_BACK,       /* close trace: jump al inicio si guards OK */
} IROp;

typedef struct {
    uint8_t op;        /* IROp */
    uint8_t type;      /* IRType — tipo del valor producido */
    uint16_t a;        /* ref a IR id (o 0 si no usa) */
    uint16_t b;        /* ref a IR id (o 0 si no usa) */
    union {
        double      cst;     /* IR_LOAD_CONST */
        Variable   *var;     /* IR_LOAD_VAR / IR_STORE_VAR */
        int32_t     slot;    /* IR_LOAD_THIS_ATTR */
        ClassNode  *cls;     /* IR_GUARD_OBJ */
        ListItemAttrSite *lia; /* IR_LIST_ITEM_ATTR (Ola 14e) */
    } aux;
    /* Ola 9: flags por op (bit 0 = loop-invariant). */
    uint8_t flags;
    uint8_t _pad;
} IRInst;

#define TRACE_MAX 256
typedef struct {
    Instr   *anchor;            /* puntero al `code` que disparó la traza */
    IRInst   ops[TRACE_MAX];
    int      len;
    int      complete;          /* 1 si terminó con LOOP_BACK o RETURN */
    /* Ola 10b: codegen results. compiled==NULL && !compile_failed → not tried.
     * compiled!=NULL → ready to call (signature: double (*)(void)).
     * compile_failed==1 → trace doesn't fit JIT, never retry. */
    void    *compiled;
    int      compile_failed;
} Trace;

/* Tabla de trazas por anchor (open-addressing). Pequeña: 64 slots. */
#define TRACE_TBL_SZ 64
typedef struct {
    Instr  *key;       /* anchor; NULL = libre */
    Trace  *trace;
    int     attempts;  /* veces que intentamos grabar (para evitar loop infinito) */
} TraceSlot;
static TraceSlot g_traces[TRACE_TBL_SZ];

/* Estado de recording — UN solo trace activo a la vez (linear tracing). */
static int     g_record_on = 0;       /* 1 = recording activo */
static Trace  *g_cur_trace = NULL;
static int     g_trace_threshold = 50;  /* hits antes de iniciar recording */
static int     g_trace_dump = 0;        /* TYPEEASY_TRACE_DUMP=1 imprime trazas */
static int     g_trace_opt_dump = 0;    /* Ola 9: TYPEEASY_TRACE_OPT_DUMP=1 */
static int     g_trace_init_done = 0;
/* Ola 11: estado de inlining durante recording. */
static int     g_inline_depth = 0;       /* 0 = top-level, >0 = inlined call */
static uint16_t g_inline_ret_id = 0;     /* ret value's IR id (set by inner do_halt) */
static uint8_t  g_inline_ret_type = T_UNK;
/* Ola 11: para identificar el "loop top" en la traza. Si la traza fue
 * iniciada en un backward jump, anchor IS el loop top y el primer op
 * (id=1) es el header. */

static void trace_dump(Trace *t); /* fwd */
static void trace_optimize(Trace *t); /* fwd (Ola 9) */
static void *jit_compile_trace(Trace *t); /* fwd (Ola 10b) */
/* Ola 10: JIT globals declared here so trace_optimize (above 10a section)
 * can reference them. Real definitions still live in the Ola 10a section. */
static int    g_jit_on        = -1;
static int    g_jit_smoke_done = 0;
static void  *g_jit_slab      = NULL;
static size_t g_jit_slab_sz   = 0;
static size_t g_jit_slab_used = 0;
static int    g_jit_dump      = 0;

static void trace_init_once(void) {
    if (g_trace_init_done) return;
    g_trace_init_done = 1;
    const char *th = getenv("TYPEEASY_TRACE_THRESHOLD");
    if (th) g_trace_threshold = atoi(th);
    if (g_trace_threshold <= 0) g_trace_threshold = 50;
    g_trace_dump = (getenv("TYPEEASY_TRACE_DUMP") != NULL) ? 1 : 0;
    g_trace_opt_dump = (getenv("TYPEEASY_TRACE_OPT_DUMP") != NULL) ? 1 : 0;
    memset(g_traces, 0, sizeof(g_traces));
}

static TraceSlot *trace_slot_for(Instr *anchor) {
    uintptr_t h = (uintptr_t)anchor;
    h ^= (h >> 16);
    h *= 0x9E3779B1u;
    int idx = (int)(h & (TRACE_TBL_SZ - 1));
    for (int probe = 0; probe < TRACE_TBL_SZ; probe++) {
        TraceSlot *s = &g_traces[(idx + probe) & (TRACE_TBL_SZ - 1)];
        if (s->key == anchor || s->key == NULL) return s;
    }
    return NULL;
}

/* API que Ola 8 usará desde los opcode handlers para emitir IR.
 * Devuelve el id de la op recién emitida (o 0 si recording fuera). */
static uint16_t ir_emit(IROp op, IRType ty, uint16_t a, uint16_t b) {
    if (!g_record_on || !g_cur_trace) return 0;
    if (g_cur_trace->len >= TRACE_MAX) {
        /* trace overflow — abortar grabación */
        g_record_on = 0;
        return 0;
    }
    int id = g_cur_trace->len++;
    IRInst *ins = &g_cur_trace->ops[id];
    ins->op = op; ins->type = ty; ins->a = a; ins->b = b;
    memset(&ins->aux, 0, sizeof(ins->aux));
    return (uint16_t)id;
}

/* Iniciar recording para un anchor que ya superó el threshold. */
static void trace_begin(Instr *anchor) {
    TraceSlot *s = trace_slot_for(anchor);
    if (!s) return;
    if (s->trace && s->trace->complete) return;        /* ya grabada */
    if (s->attempts >= 3) return;                       /* abortó 3 veces */
    if (!s->trace) s->trace = (Trace *)calloc(1, sizeof(Trace));
    s->key = anchor;
    s->attempts++;
    s->trace->anchor = anchor;
    s->trace->len = 0;
    s->trace->complete = 0;
    /* Ola 9 fix: reserve id 0 as a NOP sentinel so that "0" can mean
     * "no operand" in IRInst.a/b without colliding with a real id. */
    s->trace->ops[0].op = IR_NOP;
    s->trace->ops[0].type = T_UNK;
    s->trace->ops[0].a = 0;
    s->trace->ops[0].b = 0;
    s->trace->ops[0].flags = 0;
    s->trace->len = 1;
    g_cur_trace = s->trace;
    g_record_on = 1;
}

/* Cerrar recording. complete=1 si la traza es válida y reusable. */
static void trace_end(int complete) {
    if (!g_record_on || !g_cur_trace) return;
    g_cur_trace->complete = complete;
    if (g_trace_dump) trace_dump(g_cur_trace);
    /* Ola 9: si la traza es válida, optimizarla. */
    if (complete) trace_optimize(g_cur_trace);
    g_record_on = 0;
    g_cur_trace = NULL;
}

static const char *ir_op_name(uint8_t op) {
    switch (op) {
    case IR_NOP: return "nop";
    case IR_LOAD_CONST: return "load_const";
    case IR_LOAD_VAR: return "load_var";
    case IR_LOAD_THIS_ATTR: return "load_this_attr";
    case IR_GUARD_INT: return "guard_int";
    case IR_GUARD_FLOAT: return "guard_float";
    case IR_GUARD_OBJ: return "guard_obj";
    case IR_GUARD_TRUE: return "guard_true";
    case IR_GUARD_FALSE: return "guard_false";
    case IR_ADD_INT: return "add_int";
    case IR_SUB_INT: return "sub_int";
    case IR_MUL_INT: return "mul_int";
    case IR_DIV_INT: return "div_int";
    case IR_ADD_FLOAT: return "add_float";
    case IR_SUB_FLOAT: return "sub_float";
    case IR_MUL_FLOAT: return "mul_float";
    case IR_DIV_FLOAT: return "div_float";
    case IR_LT: return "lt"; case IR_GT: return "gt";
    case IR_LE: return "le"; case IR_GE: return "ge";
    case IR_EQ: return "eq"; case IR_NEQ: return "neq";
    case IR_STORE_VAR: return "store_var";
    case IR_RETURN: return "return";
    case IR_LIST_ITEM_ATTR: return "list_item_attr";
    case IR_LOOP_BACK: return "loop_back";
    default: return "??";
    }
}

static const char *ir_type_name(uint8_t ty) {
    switch (ty) {
    case T_UNK: return "?";
    case T_INT: return "i";
    case T_FLOAT: return "f";
    case T_BOOL: return "b";
    case T_OBJ: return "o";
    default: return "?";
    }
}

static void trace_dump(Trace *t) {
    fprintf(stderr, "\n=== TypeEasy Ola 7 — trace dump (anchor=%p, len=%d, complete=%d) ===\n",
            (void*)t->anchor, t->len, t->complete);
    for (int i = 1; i < t->len; i++) {  /* skip id 0 (NOP sentinel) */
        IRInst *ins = &t->ops[i];
        fprintf(stderr, "  %3d: %-15s %-3s",
                i, ir_op_name(ins->op), ir_type_name(ins->type));
        if (ins->op == IR_LOAD_CONST) {
            fprintf(stderr, "  cst=%g", ins->aux.cst);
        } else if (ins->op == IR_LOAD_VAR || ins->op == IR_STORE_VAR) {
            fprintf(stderr, "  var=%s", ins->aux.var ? ins->aux.var->id : "?");
        } else if (ins->op == IR_LOAD_THIS_ATTR) {
            fprintf(stderr, "  slot=%d", ins->aux.slot);
        } else {
            if (ins->a) fprintf(stderr, "  a=%%%d", ins->a);
            if (ins->b) fprintf(stderr, "  b=%%%d", ins->b);
        }
        /* Ola 9: flags */
        if (ins->flags & 0x01) fprintf(stderr, "  [INV]");   /* loop-invariant */
        if (ins->flags & 0x02) fprintf(stderr, "  +guard_int");
        if (ins->flags & 0x04) fprintf(stderr, "  +guard_float");
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "===========================================================\n");
}

/* ============================================================ */
/* Ola 9: optimizador del IR (passes sobre Trace ya grabada).   */
/* ============================================================ */
/* Pases en orden:
 *   1. Constant folding   — ops aritméticas con dos LOAD_CONST → LOAD_CONST
 *   2. Guard marking      — bit en flags de cada LOAD_VAR/LOAD_THIS_ATTR
 *                           tipado (T_INT/T_FLOAT). Codegen lo emitirá
 *                           como test+jne deopt en Ola 10.
 *   3. LICM marking       — bit INV en ops invariantes (LOAD_CONST,
 *                           LOAD_VAR cuyo Variable* no se store-modifica
 *                           en la traza, LOAD_THIS_ATTR, y aritmética
 *                           cuyas operandos sean ambas invariantes).
 *   4. DCE                — ops sin usuarios pasan a IR_NOP (mantiene
 *                           IDs estables para no romper refs).
 *
 * No se reordena nada — los IDs sobreviven. Optimizaciones más
 * agresivas (re-empaquetado, allocation removal) → Ola posterior. */

#define IR_FLAG_INV         0x01
#define IR_FLAG_GUARD_INT   0x02
#define IR_FLAG_GUARD_FLOAT 0x04

static int ir_is_arith(uint8_t op) {
    return op == IR_ADD_INT || op == IR_SUB_INT || op == IR_MUL_INT || op == IR_DIV_INT
        || op == IR_ADD_FLOAT || op == IR_SUB_FLOAT || op == IR_MUL_FLOAT || op == IR_DIV_FLOAT
        || op == IR_LT || op == IR_GT || op == IR_LE || op == IR_GE
        || op == IR_EQ || op == IR_NEQ;
}

static int ir_has_side_effect(uint8_t op) {
    return op == IR_STORE_VAR || op == IR_RETURN || op == IR_LOOP_BACK
        || op == IR_GUARD_INT || op == IR_GUARD_FLOAT || op == IR_GUARD_OBJ
        || op == IR_GUARD_TRUE || op == IR_GUARD_FALSE
        || op == IR_LIST_ITEM_ATTR;  /* deopt on guards => side effect */
}

static void opt_constant_fold(Trace *t, int *folded) {
    *folded = 0;
    for (int i = 0; i < t->len; i++) {
        IRInst *ins = &t->ops[i];
        if (!ir_is_arith(ins->op)) continue;
        IRInst *A = &t->ops[ins->a];
        IRInst *B = &t->ops[ins->b];
        if (A->op != IR_LOAD_CONST || B->op != IR_LOAD_CONST) continue;
        double r = 0; int ok = 1;
        switch (ins->op) {
        case IR_ADD_INT: case IR_ADD_FLOAT: r = A->aux.cst + B->aux.cst; break;
        case IR_SUB_INT: case IR_SUB_FLOAT: r = A->aux.cst - B->aux.cst; break;
        case IR_MUL_INT: case IR_MUL_FLOAT: r = A->aux.cst * B->aux.cst; break;
        case IR_DIV_INT: case IR_DIV_FLOAT:
            if (B->aux.cst == 0.0) { ok = 0; break; }
            r = A->aux.cst / B->aux.cst; break;
        case IR_LT:  r = (A->aux.cst <  B->aux.cst); break;
        case IR_GT:  r = (A->aux.cst >  B->aux.cst); break;
        case IR_LE:  r = (A->aux.cst <= B->aux.cst); break;
        case IR_GE:  r = (A->aux.cst >= B->aux.cst); break;
        case IR_EQ:  r = (A->aux.cst == B->aux.cst); break;
        case IR_NEQ: r = (A->aux.cst != B->aux.cst); break;
        default: ok = 0; break;
        }
        if (!ok) continue;
        ins->op = IR_LOAD_CONST;
        ins->a = 0; ins->b = 0;
        ins->aux.cst = r;
        (*folded)++;
    }
}

static void opt_guard_mark(Trace *t, int *guards) {
    *guards = 0;
    for (int i = 0; i < t->len; i++) {
        IRInst *ins = &t->ops[i];
        if (ins->op == IR_LOAD_VAR || ins->op == IR_LOAD_THIS_ATTR) {
            if (ins->type == T_INT) {
                ins->flags |= IR_FLAG_GUARD_INT;
                (*guards)++;
            } else if (ins->type == T_FLOAT) {
                ins->flags |= IR_FLAG_GUARD_FLOAT;
                (*guards)++;
            }
        }
    }
}

static void opt_licm_mark(Trace *t, int *invariants) {
    *invariants = 0;
    /* Set de Variable* que se store-modifica en la traza. */
    Variable *stored[64]; int nstored = 0;
    for (int i = 0; i < t->len; i++) {
        IRInst *ins = &t->ops[i];
        if (ins->op == IR_STORE_VAR && nstored < 64)
            stored[nstored++] = ins->aux.var;
    }
    /* Pasada de propagación. Como las ops están en orden topológico
     * (SSA append-only), una sola pasada basta. */
    for (int i = 0; i < t->len; i++) {
        IRInst *ins = &t->ops[i];
        int inv = 0;
        switch (ins->op) {
        case IR_LOAD_CONST:
            inv = 1; break;
        case IR_LOAD_THIS_ATTR:
            inv = 1; break;  /* no soportamos store_this_attr todavía */
        case IR_LOAD_VAR: {
            inv = 1;
            for (int k = 0; k < nstored; k++)
                if (stored[k] == ins->aux.var) { inv = 0; break; }
            break;
        }
        default:
            if (ir_is_arith(ins->op)) {
                int ai = (t->ops[ins->a].flags & IR_FLAG_INV) ? 1 : 0;
                int bi = (t->ops[ins->b].flags & IR_FLAG_INV) ? 1 : 0;
                inv = (ai && bi);
            }
            break;
        }
        if (inv) {
            ins->flags |= IR_FLAG_INV;
            (*invariants)++;
        }
    }
}

static void opt_dce(Trace *t, int *killed) {
    *killed = 0;
    /* Mark phase: live = side-effect ops + sus operandos transitivos. */
    uint8_t live[TRACE_MAX] = {0};
    for (int i = 0; i < t->len; i++) {
        if (ir_has_side_effect(t->ops[i].op))
            live[i] = 1;
    }
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = t->len - 1; i >= 0; i--) {
            if (!live[i]) continue;
            IRInst *ins = &t->ops[i];
            if (ins->a && !live[ins->a]) { live[ins->a] = 1; changed = 1; }
            if (ins->b && !live[ins->b]) { live[ins->b] = 1; changed = 1; }
        }
    }
    /* Sweep: ops no live → IR_NOP. Mantienen ID. */
    for (int i = 0; i < t->len; i++) {
        if (!live[i] && t->ops[i].op != IR_NOP) {
            t->ops[i].op = IR_NOP;
            t->ops[i].a = 0; t->ops[i].b = 0;
            (*killed)++;
        }
    }
}

static void trace_optimize(Trace *t) {
    if (!t || !t->complete || t->len == 0) return;
    int folded = 0, guards = 0, invs = 0, killed = 0;
    opt_constant_fold(t, &folded);
    opt_guard_mark    (t, &guards);
    opt_licm_mark     (t, &invs);
    opt_dce           (t, &killed);
    if (g_trace_opt_dump) {
        fprintf(stderr, "[OLA9] optimized trace anchor=%p: folded=%d guards=%d invariants=%d killed=%d\n",
                (void*)t->anchor, folded, guards, invs, killed);
        trace_dump(t);
    }
#if TE_JIT_AVAILABLE
    /* Ola 10b: intentar compilar la traza. Sólo si JIT está activo y
     * la traza encaja con el patrón soportado. */
    if (g_jit_on == 1 && !t->compiled && !t->compile_failed) {
        void *code = jit_compile_trace(t);
        if (code) t->compiled = code;
        else      t->compile_failed = 1;
    }
#endif
}

/* ===========================================================================
 * Ola 10a — JIT infrastructure (executable slab + raw x86_64 byte emitter)
 *
 * No DynASM yet. Direct byte emission, just enough to allocate an executable
 * page, write a tiny function (mov rax,imm64; ret), and jump to it from C.
 * Triggered only when TYPEEASY_JIT=1 is set; otherwise zero impact.
 * Linux/x86_64 only (which matches the Docker build).
 * =========================================================================*/
/* TE_JIT_AVAILABLE and JIT-related includes are at the top of this file
 * (must precede any earlier `#if TE_JIT_AVAILABLE`). */
/* g_jit_on / g_jit_slab / g_jit_dump declared earlier (above trace_optimize). */

static void jit_init_once(void) {
    if (g_jit_on != -1) return;
    const char *e = getenv("TYPEEASY_JIT");
    g_jit_on = (e && *e && *e != '0') ? 1 : 0;
    {
        const char *d = getenv("TYPEEASY_JIT_DUMP");
        g_jit_dump = (d && *d && *d != '0') ? 1 : 0;
    }
#if TE_JIT_AVAILABLE
    if (g_jit_on) {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        g_jit_slab_sz = (size_t)ps * 16;  /* 64 KiB */
        g_jit_slab = mmap(NULL, g_jit_slab_sz,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_jit_slab == MAP_FAILED) {
            fprintf(stderr, "[OLA10] mmap PROT_EXEC failed; JIT disabled\n");
            g_jit_slab = NULL;
            g_jit_on = 0;
        } else {
            fprintf(stderr, "[OLA10] JIT slab=%p size=%zu (PROT_EXEC ok)\n",
                    g_jit_slab, g_jit_slab_sz);
        }
    }
#else
    if (g_jit_on) {
        fprintf(stderr, "[OLA10] platform not supported; JIT disabled\n");
        g_jit_on = 0;
    }
#endif
}

#if TE_JIT_AVAILABLE
/* Bump-allocate `n` bytes from the executable slab and return a writable+
 * executable pointer to it. Returns NULL on overflow. */
static uint8_t *jit_alloc(size_t n) {
    if (!g_jit_slab) return NULL;
    if (g_jit_slab_used + n > g_jit_slab_sz) return NULL;
    uint8_t *p = (uint8_t*)g_jit_slab + g_jit_slab_used;
    g_jit_slab_used += (n + 15) & ~(size_t)15;  /* keep 16-byte aligned */
    return p;
}

/* Tiny x86_64 byte emitter helpers. We only need a handful of opcodes
 * for the smoke test in 10a; 10b will extend with mov [mem], add, ret. */
static inline void e8(uint8_t **p, uint8_t v)  { *(*p)++ = v; }
static inline void e64(uint8_t **p, uint64_t v) {
    for (int i = 0; i < 8; i++) e8(p, (uint8_t)(v >> (i*8)));
}

/* Emit `mov rax, imm64`  (REX.W + B8+rd io)  — 10 bytes. */
static void emit_mov_rax_imm64(uint8_t **p, uint64_t imm) {
    e8(p, 0x48); e8(p, 0xB8); e64(p, imm);
}

/* Emit `ret` — 1 byte. */
static void emit_ret(uint8_t **p) { e8(p, 0xC3); }

/* Smoke test: build a function `int64_t (*)(void)` that returns 42 and
 * call it. Proves slab is mapped PROT_EXEC and our bytes are valid. */
static int jit_smoke_test(void) {
    if (g_jit_smoke_done) return 0;
    g_jit_smoke_done = 1;
    uint8_t *code = jit_alloc(16);
    if (!code) {
        fprintf(stderr, "[OLA10] smoke: jit_alloc failed\n");
        return -1;
    }
    uint8_t *p = code;
    emit_mov_rax_imm64(&p, 42);
    emit_ret(&p);
    typedef int64_t (*fn_t)(void);
    fn_t fn = (fn_t)(uintptr_t)code;
    int64_t got = fn();
    fprintf(stderr, "[OLA10] smoke: compiled fn @ %p returned %lld (expected 42) %s\n",
            (void*)code, (long long)got, got == 42 ? "OK" : "FAIL");
    return (got == 42) ? 0 : -1;
}
#endif  /* TE_JIT_AVAILABLE (Ola 10a) */

/* ===========================================================================
 * Ola 10b — codegen real x86_64 para una traza optimizada.
 *
 * Soporta exclusivamente trazas con la siguiente forma (que es justo lo
 * que produce `bench_method_call.te` y, en general, métodos como
 * `return self.attr + arg;`):
 *
 *   ops[1..N-2]: secuencia de IR_LOAD_VAR / IR_LOAD_THIS_ATTR (todos T_INT)
 *                e IR_ADD_INT / IR_SUB_INT / IR_MUL_INT (T_INT)
 *   ops[N-1]:    IR_RETURN T_INT, a=<ref>
 *
 * Sin guards (Ola 10c los añadirá): si el tipo cambia en runtime el
 * resultado es indefinido. Para mitigar, sólo activamos compiled traces
 * cuando los `IR_LOAD_VAR.aux.var->vtype == VAL_INT` se mantiene; el
 * trampolín en bc_exec hace una verificación barata antes de saltar.
 *
 * Convención de llamada del código generado:
 *   double fn(void);                    // SysV: result en xmm0
 * Lee `g_bc_this` y los `Variable*` directamente desde direcciones
 * absolutas embebidas en el código (mov rax, imm64).
 *
 * Frame: push rbp; mov rbp,rsp; sub rsp,256.  Cada IR id usa 8 bytes
 * en [rsp + id*8]. Soporta hasta 32 IDs (TRACE_MAX más grande aborta).
 * =========================================================================*/

/* Compile-time guarantees sobre layout. Si esto falla, los offsets
 * hardcodeados de abajo están equivocados. */
#if TE_JIT_AVAILABLE
_Static_assert(sizeof(Variable) == 32,           "JIT assumes sizeof(Variable)==32");
_Static_assert(offsetof(Variable, value) == 24,  "JIT assumes Variable.value @ offset 24");
_Static_assert(offsetof(ObjectNode, attributes) == 8, "JIT assumes ObjectNode.attributes @ offset 8");
/* Ola 14e: extra-field offset of ASTNode used by IR_LIST_ITEM_ATTR codegen.
 * We do NOT hardcode the value; it's read at compile time via offsetof(). */
#define TE_AST_EXTRA_OFF   ((int32_t)offsetof(ASTNode, extra))
#define TE_LISTIDX_LEN_OFF   ((int32_t)offsetof(TEListIdx, len))
#define TE_LISTIDX_ITEMS_OFF ((int32_t)offsetof(TEListIdx, items))
_Static_assert(offsetof(TEListIdx, len)   == 0,  "JIT assumes TEListIdx.len @ offset 0");
_Static_assert(offsetof(TEListIdx, items) == 8,  "JIT assumes TEListIdx.items @ offset 8");
_Static_assert(offsetof(ObjectNode, class) == 0, "JIT assumes ObjectNode.class @ offset 0");

#define TE_JIT_FRAME_BYTES   256       /* multiple of 16 */
#define TE_JIT_MAX_IDS       32        /* 32 * 8 = 256 */

/* g_jit_dump declared above (outside #if so init code can write it) */

/* --- micro-emisor --- */
static inline void e32(uint8_t **p, uint32_t v) {
    for (int i = 0; i < 4; i++) e8(p, (uint8_t)(v >> (i*8)));
}

/* mov rax, imm64                           48 B8 ib...  (10 bytes) */
/* (already defined above as emit_mov_rax_imm64) */

/* push rbp; mov rbp,rsp; sub rsp, imm32    55 48 89 E5 48 81 EC ii ii ii ii */
static void emit_prologue(uint8_t **p, uint32_t frame) {
    e8(p, 0x55);
    e8(p, 0x48); e8(p, 0x89); e8(p, 0xE5);
    e8(p, 0x48); e8(p, 0x81); e8(p, 0xEC); e32(p, frame);
}
/* mov rsp,rbp; pop rbp; ret                48 89 EC 5D C3 */
static void emit_epilogue(uint8_t **p) {
    e8(p, 0x48); e8(p, 0x89); e8(p, 0xEC);
    e8(p, 0x5D); e8(p, 0xC3);
}

/* mov rax, [rax + disp32]                  48 8B 80 dd dd dd dd  (7 bytes) */
static void emit_mov_rax_mem_rax_disp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x8B); e8(p, 0x80); e32(p, (uint32_t)disp);
}
/* mov rax, [rax]                           48 8B 00              (3 bytes) */
static void emit_mov_rax_mem_rax(uint8_t **p) {
    e8(p, 0x48); e8(p, 0x8B); e8(p, 0x00);
}
/* mov [rsp + disp32], rax                  48 89 84 24 dd dd dd dd  (8 bytes) */
static void emit_mov_rspdisp_rax(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x89); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* mov rax, [rsp + disp32]                  48 8B 84 24 dd dd dd dd  (8 bytes) */
static void emit_mov_rax_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x8B); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* add rax, [rsp + disp32]                  48 03 84 24 dd dd dd dd  (8 bytes) */
static void emit_add_rax_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x03); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* sub rax, [rsp + disp32]                  48 2B 84 24 dd dd dd dd  (8 bytes) */
static void emit_sub_rax_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x2B); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* imul rax, [rsp + disp32]                 48 0F AF 84 24 dd dd dd dd  (9 bytes) */
static void emit_imul_rax_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x0F); e8(p, 0xAF); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* cvtsi2sd xmm0, rax                       F2 48 0F 2A C0  (5 bytes) */
static void emit_cvtsi2sd_xmm0_rax(uint8_t **p) {
    e8(p, 0xF2); e8(p, 0x48); e8(p, 0x0F); e8(p, 0x2A); e8(p, 0xC0);
}

/* === Ola 11 helpers === */
/* xor edx, edx                             31 D2                 (2 bytes) */
static void emit_xor_edx_edx(uint8_t **p) { e8(p, 0x31); e8(p, 0xD2); }
/* cmp rax, [rsp + disp32]                  48 3B 84 24 dd dd dd dd (8 bytes) */
static void emit_cmp_rax_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x3B); e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* setl dl                                  0F 9C C2              (3 bytes) */
static void emit_setl_dl(uint8_t **p) { e8(p, 0x0F); e8(p, 0x9C); e8(p, 0xC2); }
/* mov [rsp + disp32], rdx                  48 89 94 24 dd dd dd dd (8 bytes) */
static void emit_mov_rspdisp_rdx(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x89); e8(p, 0x94); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* test rax, rax                            48 85 C0              (3 bytes) */
static void emit_test_rax_rax(uint8_t **p) { e8(p, 0x48); e8(p, 0x85); e8(p, 0xC0); }
/* je rel32 (forward, backpatch). Returns offset to disp32 field. */
static size_t emit_je_rel32(uint8_t **p, uint8_t *base) {
    e8(p, 0x0F); e8(p, 0x84);
    size_t at = (size_t)(*p - base);
    e32(p, 0); /* placeholder */
    return at;
}
/* jmp rel32 (forward or backward). Returns offset to disp32 field. */
static size_t emit_jmp_rel32(uint8_t **p, uint8_t *base) {
    e8(p, 0xE9);
    size_t at = (size_t)(*p - base);
    e32(p, 0);
    return at;
}
/* Patch a rel32 at base+at so it points to base+target (relative to end of disp). */
static void patch_rel32(uint8_t *base, size_t at, size_t target) {
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(at + 4));
    base[at + 0] = (uint8_t)(rel & 0xFF);
    base[at + 1] = (uint8_t)((rel >> 8)  & 0xFF);
    base[at + 2] = (uint8_t)((rel >> 16) & 0xFF);
    base[at + 3] = (uint8_t)((rel >> 24) & 0xFF);
}
/* mov rdx, imm64                           48 BA ib...           (10 bytes) */
static void emit_mov_rdx_imm64(uint8_t **p, uint64_t imm) {
    e8(p, 0x48); e8(p, 0xBA);
    for (int i = 0; i < 8; i++) e8(p, (uint8_t)(imm >> (i*8)));
}
/* mov [rdx], rax                           48 89 02              (3 bytes) */
static void emit_mov_memrdx_rax(uint8_t **p) { e8(p, 0x48); e8(p, 0x89); e8(p, 0x02); }
/* pxor xmm0, xmm0                          66 0F EF C0           (4 bytes) */
static void emit_pxor_xmm0_xmm0(uint8_t **p) {
    e8(p, 0x66); e8(p, 0x0F); e8(p, 0xEF); e8(p, 0xC0);
}

/* === Ola 12 — Register allocation helpers ===
 * Una "lreg" (loop-carried reg) toma valores 0..4, mapeando a:
 *   0 → rbx,  1 → r12,  2 → r13,  3 → r14,  4 → r15  (todos callee-saved).
 * Estos cinco registros son los únicos que asumimos preservados a través
 * de cualquier subrutina de C (aquí no hacemos calls, pero los guardamos
 * en push/pop en el prólogo/epílogo para mantener el ABI de SysV). */
#define TE_JIT_MAX_LREGS 5

static void emit_push_lreg(uint8_t **p, int lreg) {
    if (lreg == 0) e8(p, 0x53);                       /* push rbx */
    else { e8(p, 0x41); e8(p, 0x54 + (lreg - 1)); }   /* push r12..r15 */
}
static void emit_pop_lreg(uint8_t **p, int lreg) {
    if (lreg == 0) e8(p, 0x5B);                       /* pop rbx */
    else { e8(p, 0x41); e8(p, 0x5C + (lreg - 1)); }   /* pop r12..r15 */
}
/* mov LREG, [rax]                          REX 8B ModRM */
static void emit_mov_lreg_memrax(uint8_t **p, int lreg) {
    uint8_t rex, modrm;
    if (lreg == 0) { rex = 0x48; modrm = 0x18; }      /* rbx: reg=011 → 00 011 000 */
    else { rex = 0x4C; modrm = 0x20 + ((lreg - 1) << 3); }  /* r12..r15 */
    e8(p, rex); e8(p, 0x8B); e8(p, modrm);
}
/* mov [rax], LREG                          REX 89 ModRM */
static void emit_mov_memrax_lreg(uint8_t **p, int lreg) {
    uint8_t rex, modrm;
    if (lreg == 0) { rex = 0x48; modrm = 0x18; }
    else { rex = 0x4C; modrm = 0x20 + ((lreg - 1) << 3); }
    e8(p, rex); e8(p, 0x89); e8(p, modrm);
}
/* mov [rsp+disp32], LREG */
static void emit_mov_rspdisp_lreg(uint8_t **p, int32_t disp, int lreg) {
    uint8_t rex, modrm;
    if (lreg == 0) { rex = 0x48; modrm = 0x9C; }      /* rbx: mod=10 reg=011 rm=100 */
    else { rex = 0x4C; modrm = 0xA4 + ((lreg - 1) << 3); }
    e8(p, rex); e8(p, 0x89); e8(p, modrm); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* mov LREG, [rsp+disp32] */
static void emit_mov_lreg_rspdisp(uint8_t **p, int lreg, int32_t disp) {
    uint8_t rex, modrm;
    if (lreg == 0) { rex = 0x48; modrm = 0x9C; }
    else { rex = 0x4C; modrm = 0xA4 + ((lreg - 1) << 3); }
    e8(p, rex); e8(p, 0x8B); e8(p, modrm); e8(p, 0x24); e32(p, (uint32_t)disp);
}

/* g_bc_this dirección (la conoce el linker, capturamos &g_bc_this). */
static ObjectNode **jit_p_g_bc_this(void) { return &g_bc_this; }

/* Emite "carga value.int_value en rax para la id `id` y guárdalo en
 * el slot del frame [rsp + id*8]". */
static int jit_emit_load_var(uint8_t **p, Variable *v, int id) {
    if (!v) return -1;
    /* mov rax, &v->value.int_value  (literal address) */
    emit_mov_rax_imm64(p, (uint64_t)(uintptr_t)&v->value.int_value);
    /* mov rax, [rax]                                          */
    emit_mov_rax_mem_rax(p);
    /* mov [rsp + id*8], rax                                   */
    emit_mov_rspdisp_rax(p, id * 8);
    return 0;
}
static int jit_emit_load_this_attr(uint8_t **p, int32_t slot, int id) {
    /* mov rax, &g_bc_this           */
    emit_mov_rax_imm64(p, (uint64_t)(uintptr_t)jit_p_g_bc_this());
    /* mov rax, [rax]   ; rax = g_bc_this           */
    emit_mov_rax_mem_rax(p);
    /* mov rax, [rax+8] ; rax = g_bc_this->attributes */
    emit_mov_rax_mem_rax_disp(p, 8);
    /* mov rax, [rax + slot*32 + 24] ; rax = attributes[slot].value.int_value */
    emit_mov_rax_mem_rax_disp(p, slot * 32 + 24);
    emit_mov_rspdisp_rax(p, id * 8);
    return 0;
}

/* === Ola 17 — Float fast-path codegen helpers ===
 * Doubles viven en los slots del frame [rsp+id*8] como bits raw (8 bytes).
 * xmm0 es scratch para todas las ops aritméticas; xmm1 no se usa.
 * Sin register allocation para floats: cada op carga, opera, almacena. */

/* movsd xmm0, [rsp + disp32]               F2 0F 10 84 24 dd dd dd dd  (9) */
static void emit_movsd_xmm0_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x10);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* movsd [rsp + disp32], xmm0               F2 0F 11 84 24 dd dd dd dd  (9) */
static void emit_movsd_rspdisp_xmm0(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x11);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* movsd xmm0, [rax]                        F2 0F 10 00              (4 bytes) */
static void emit_movsd_xmm0_memrax(uint8_t **p) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x10); e8(p, 0x00);
}
/* movsd [rax], xmm0                        F2 0F 11 00              (4 bytes) */
static void emit_movsd_memrax_xmm0(uint8_t **p) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x11); e8(p, 0x00);
}
/* addsd xmm0, [rsp + disp32]               F2 0F 58 84 24 ...       (9 bytes) */
static void emit_addsd_xmm0_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x58);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* subsd xmm0, [rsp + disp32]               F2 0F 5C 84 24 ...       (9 bytes) */
static void emit_subsd_xmm0_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x5C);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* mulsd xmm0, [rsp + disp32]               F2 0F 59 84 24 ...       (9 bytes) */
static void emit_mulsd_xmm0_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x59);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}
/* divsd xmm0, [rsp + disp32]               F2 0F 5E 84 24 ...       (9 bytes) */
static void emit_divsd_xmm0_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0xF2); e8(p, 0x0F); e8(p, 0x5E);
    e8(p, 0x84); e8(p, 0x24); e32(p, (uint32_t)disp);
}

/* Load Variable.value.float_value (offset 24 in Variable; same address as
 * value.int_value via the union) into slot [rsp+id*8] as raw bits. */
static int jit_emit_load_var_float(uint8_t **p, Variable *v, int id) {
    if (!v) return -1;
    emit_mov_rax_imm64(p, (uint64_t)(uintptr_t)&v->value.float_value);
    emit_movsd_xmm0_memrax(p);
    emit_movsd_rspdisp_xmm0(p, id * 8);
    return 0;
}
/* Store slot [rsp+a*8] (raw double bits) into v->value.float_value. */
static int jit_emit_store_var_float(uint8_t **p, Variable *v, int a) {
    if (!v) return -1;
    emit_movsd_xmm0_rspdisp(p, a * 8);
    emit_mov_rax_imm64(p, (uint64_t)(uintptr_t)&v->value.float_value);
    emit_movsd_memrax_xmm0(p);
    return 0;
}

/* === Ola 14e — emit helpers para IR_LIST_ITEM_ATTR (asm puro) ===
 * Nuevos opcodes x86_64 que no existían antes:
 *   - mov rdx, [rsp+disp32]            48 8B 94 24 dd dd dd dd     (8 bytes)
 *   - cmp edx, [rax]                   3B 10                       (2 bytes)
 *   - jae rel32 (forward)              0F 83 dd dd dd dd           (6 bytes)
 *   - jne rel32 (forward)              0F 85 dd dd dd dd           (6 bytes)
 *   - mov rax, [rax + rdx*8]           48 8B 04 D0                 (4 bytes)
 *   - cmp rdx, [rax]                   48 3B 10                    (3 bytes)
 */
static void emit_mov_rdx_rspdisp(uint8_t **p, int32_t disp) {
    e8(p, 0x48); e8(p, 0x8B); e8(p, 0x94); e8(p, 0x24); e32(p, (uint32_t)disp);
}
static void emit_cmp_edx_memrax(uint8_t **p) {
    e8(p, 0x3B); e8(p, 0x10);
}
static size_t emit_jae_rel32(uint8_t **p, uint8_t *base) {
    e8(p, 0x0F); e8(p, 0x83);
    size_t at = (size_t)(*p - base);
    e32(p, 0);
    return at;
}
static size_t emit_jne_rel32(uint8_t **p, uint8_t *base) {
    e8(p, 0x0F); e8(p, 0x85);
    size_t at = (size_t)(*p - base);
    e32(p, 0);
    return at;
}
/* mov rax, [rax + rdx*8]              48 8B 04 D0
 * REX.W=48, op=8B (MOV r64,r/m64), ModRM=04 (mod=00 reg=000(rax) rm=100=SIB),
 * SIB=D0 (scale=11=8, index=010=rdx, base=000=rax). */
static void emit_mov_rax_memrax_rdx8(uint8_t **p) {
    e8(p, 0x48); e8(p, 0x8B); e8(p, 0x04); e8(p, 0xD0);
}
/* cmp rdx, [rax]                      48 3B 10
 * REX.W=48, op=3B (CMP r64,r/m64), ModRM=10 (mod=00 reg=010(rdx) rm=000(rax)). */
static void emit_cmp_rdx_memrax(uint8_t **p) {
    e8(p, 0x48); e8(p, 0x3B); e8(p, 0x10);
}

/* Emit el lookup completo de `arr[idx].attr` (T_INT) inline en asm.
 *   - idx_disp:    desplazamiento en frame del slot que contiene idx (int).
 *   - dst_disp:    desplazamiento en frame donde escribir el resultado.
 *   - site:        ListItemAttrSite con list_var, expected_class, attr_slot.
 *   - guard_patches/n_patches: arr donde acumular jumps a deopt_label.
 * Retorna 0 OK, -1 si overflow de patches. */
static int jit_emit_list_item_attr(uint8_t **p, uint8_t *base,
                                   int32_t idx_disp, int32_t dst_disp,
                                   ListItemAttrSite *site,
                                   size_t *guard_patches, int *n_patches,
                                   int max_patches) {
    if (!site || !site->list_var || !site->expected_class) return -1;
    int slot = site->attr_slot;
    if (slot < 0) return -1;

    /* 1) rax = list_var->value.object_value (que aquí es ASTNode*) */
    emit_mov_rax_imm64(p, (uint64_t)(uintptr_t)&site->list_var->value.object_value);
    emit_mov_rax_mem_rax(p);
    emit_test_rax_rax(p);
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_je_rel32(p, base);

    /* 2) rax = list->extra (TEListIdx*) */
    emit_mov_rax_mem_rax_disp(p, TE_AST_EXTRA_OFF);
    emit_test_rax_rax(p);
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_je_rel32(p, base);

    /* 3) rdx = idx (lo cargamos como qword; usamos edx para bounds check) */
    emit_mov_rdx_rspdisp(p, idx_disp);

    /* 4) bounds check: cmp edx, [rax + len_off]; jae deopt
     *    (unsigned compare cubre idx<0 e idx>=len en una sola rama) */
    if (TE_LISTIDX_LEN_OFF == 0) {
        emit_cmp_edx_memrax(p);
    } else {
        /* mov ecx,[rax+off]; cmp edx,ecx — no soportamos offset != 0 hoy */
        return -1;
    }
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_jae_rel32(p, base);

    /* 5) rax = ix->items (puntero @ items_off) */
    emit_mov_rax_mem_rax_disp(p, TE_LISTIDX_ITEMS_OFF);

    /* 6) rax = items[idx] (ASTNode*) */
    emit_mov_rax_memrax_rdx8(p);
    emit_test_rax_rax(p);
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_je_rel32(p, base);

    /* 7) rax = item->extra (ObjectNode*) */
    emit_mov_rax_mem_rax_disp(p, TE_AST_EXTRA_OFF);
    emit_test_rax_rax(p);
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_je_rel32(p, base);

    /* 8) class guard: cmp rdx, [rax] (ObjectNode.class @ offset 0) */
    emit_mov_rdx_imm64(p, (uint64_t)(uintptr_t)site->expected_class);
    emit_cmp_rdx_memrax(p);
    if (*n_patches >= max_patches) return -1;
    guard_patches[(*n_patches)++] = emit_jne_rel32(p, base);

    /* 9) rax = obj->attributes (Variable* @ offset 8) */
    emit_mov_rax_mem_rax_disp(p, (int32_t)offsetof(ObjectNode, attributes));

    /* 10) rax = attributes[slot].value.int_value
     *     offset = slot * sizeof(Variable) + offsetof(Variable, value) */
    {
        int32_t attr_off = (int32_t)(slot * sizeof(Variable)
                                   + offsetof(Variable, value));
        emit_mov_rax_mem_rax_disp(p, attr_off);
    }

    /* 11) store al slot del frame */
    emit_mov_rspdisp_rax(p, dst_disp);
    return 0;
}

/* Intenta compilar la traza. Devuelve puntero a fn o NULL. */
static void *jit_compile_trace(Trace *t) {
    if (!g_jit_on || !g_jit_slab || !t || !t->complete || t->len < 2) return NULL;
    if (t->len > TE_JIT_MAX_IDS) return NULL;

    /* Detectar tipo de traza por op final. */
    int last_idx = t->len - 1;
    int is_loop = (t->ops[last_idx].op == IR_LOOP_BACK);
    int is_ret  = (t->ops[last_idx].op == IR_RETURN);
    if (!is_loop && !is_ret) return NULL;
    /* Ola 17: aceptamos return T_INT o T_FLOAT. */
    if (is_ret && t->ops[last_idx].type != T_INT && t->ops[last_idx].type != T_FLOAT) return NULL;

    /* Validación de ops permitidas. */
    for (int i = 1; i < last_idx; i++) {
        IRInst *ins = &t->ops[i];
        switch (ins->op) {
        case IR_NOP: break;
        case IR_LOAD_CONST:
            /* Ola 17: T_INT o T_FLOAT. */
            if (ins->type != T_INT && ins->type != T_FLOAT) return NULL;
            break;
        case IR_LOAD_VAR:
            if (ins->type == T_INT) {
                if (!ins->aux.var || ins->aux.var->vtype != VAL_INT) return NULL;
            } else if (ins->type == T_FLOAT) {
                /* Ola 17: aceptamos VAL_FLOAT (y VAL_INT con auto-promote
                 * NO — sería write inconsistente). Estricto: VAL_FLOAT. */
                if (!ins->aux.var || ins->aux.var->vtype != VAL_FLOAT) return NULL;
            } else {
                return NULL;
            }
            break;
        case IR_LOAD_THIS_ATTR:
            if (ins->type != T_INT)              return NULL;
            if (ins->aux.slot < 0)               return NULL;
            break;
        case IR_ADD_INT:
        case IR_SUB_INT:
        case IR_MUL_INT:
        case IR_LT:
            if (ins->a >= t->len || ins->b >= t->len) return NULL;
            break;
        /* Ola 17: float arithmetic. */
        case IR_ADD_FLOAT:
        case IR_SUB_FLOAT:
        case IR_MUL_FLOAT:
        case IR_DIV_FLOAT:
            if (ins->a >= t->len || ins->b >= t->len) return NULL;
            break;
        case IR_GUARD_TRUE:
            if (ins->a >= t->len) return NULL;
            break;
        case IR_LIST_ITEM_ATTR:
            /* Ola 14e: T_INT only por ahora. */
            if (ins->type != T_INT)              return NULL;
            if (ins->a >= t->len)                return NULL;
            if (!ins->aux.lia)                   return NULL;
            if (!ins->aux.lia->list_var)         return NULL;
            if (!ins->aux.lia->expected_class)   return NULL;
            if (ins->aux.lia->attr_slot < 0)     return NULL;
            break;
        case IR_STORE_VAR:
            if (!ins->aux.var)                   return NULL;
            if (ins->type == T_INT) {
                if (ins->aux.var->vtype != VAL_INT) return NULL;
            } else if (ins->type == T_FLOAT) {
                if (ins->aux.var->vtype != VAL_FLOAT) return NULL;
            } else {
                return NULL;
            }
            if (ins->a >= t->len)                return NULL;
            break;
        default:
            return NULL;
        }
    }

    /* Generosa cota superior: 64 bytes/op para ops + prologo/epilogo. */
    size_t max_bytes = 128 + 64 * t->len;
    uint8_t *base = jit_alloc(max_bytes);
    if (!base) return NULL;
    uint8_t *p = base;

    /* === Ola 12 — register allocation para loop-carried vars ===
     * Una "loop-carried var" es una Variable* que aparece como destino de
     * algún IR_STORE_VAR dentro de la traza. La cacheamos en un registro
     * callee-saved durante todo el loop:
     *   - pre-loop: cargamos su valor desde memoria al registro UNA vez.
     *   - body: IR_LOAD_VAR(v) → mov [rsp+id*8], reg_v   (sin tocar memoria)
     *           IR_STORE_VAR(v=src) → mov reg_v, [rsp+src*8] (sin escribir mem)
     *   - deopt: writeback reg_v → memoria, restauramos callee-saved, ret. */
    struct LoopVar { Variable *var; int lreg; } lvars[TE_JIT_MAX_LREGS];
    int n_lvars = 0;
    if (is_loop) {
        for (int i = 1; i < last_idx; i++) {
            if (t->ops[i].op != IR_STORE_VAR) continue;
            /* Ola 17: float vars no participan en lreg cache. */
            if (t->ops[i].type == T_FLOAT) continue;
            Variable *v = t->ops[i].aux.var;
            int found = 0;
            for (int k = 0; k < n_lvars; k++) if (lvars[k].var == v) { found = 1; break; }
            if (!found && n_lvars < TE_JIT_MAX_LREGS) {
                lvars[n_lvars].var = v;
                lvars[n_lvars].lreg = n_lvars;  /* asigna 0..4 → rbx, r12..r15 */
                n_lvars++;
            }
        }
    }
    /* Helper: ¿está esta Variable* cacheada? Devuelve lreg o -1. */
    #define LREG_OF(V) ({ int _r = -1; \
        for (int _k = 0; _k < n_lvars; _k++) if (lvars[_k].var == (V)) { _r = lvars[_k].lreg; break; } \
        _r; })

    /* === Prólogo === */
    /* push rbp; mov rbp,rsp; <push callee-saved>; sub rsp, frame */
    e8(&p, 0x55);                                              /* push rbp */
    e8(&p, 0x48); e8(&p, 0x89); e8(&p, 0xE5);                  /* mov rbp, rsp */
    for (int k = 0; k < n_lvars; k++) emit_push_lreg(&p, lvars[k].lreg);
    e8(&p, 0x48); e8(&p, 0x81); e8(&p, 0xEC); e32(&p, TE_JIT_FRAME_BYTES);  /* sub rsp,frame */

    /* === Pre-loop: cargar cada loop-carried var en su registro === */
    for (int k = 0; k < n_lvars; k++) {
        emit_mov_rax_imm64(&p, (uint64_t)(uintptr_t)&lvars[k].var->value.int_value);
        emit_mov_lreg_memrax(&p, lvars[k].lreg);
    }

    /* Para loop traces: marcar loop_top después de los pre-loads + LICM hoist.
     * Recolectar offsets de cada `je deopt` para backpatch. */
    size_t guard_patches[TE_JIT_MAX_IDS];
    int n_patches = 0;

    /* Ola 11: HOIST de invariants. Si es loop trace, primero emitimos los
     * ops marcados [INV] (LICM) UNA vez antes del loop_top; en pass 2 se
     * skipean. Para ops con efectos de control (guard/store/return/loop_back)
     * NO hoist nunca. */
    for (int hoist_pass = (is_loop ? 0 : 1); hoist_pass < 2; hoist_pass++) {
        if (hoist_pass == 1) break;  /* dummy: arrancamos pass 2 abajo */

        for (int i = 1; i < t->len; i++) {
            IRInst *ins = &t->ops[i];
            if (!(ins->flags & IR_FLAG_INV)) continue;
            if (ins->op == IR_GUARD_TRUE || ins->op == IR_GUARD_FALSE
             || ins->op == IR_STORE_VAR  || ins->op == IR_RETURN
             || ins->op == IR_LOOP_BACK) continue;
            switch (ins->op) {
            case IR_NOP: break;
            case IR_LOAD_CONST: {
                uint64_t imm;
                if (ins->type == T_FLOAT) {
                    /* Ola 17: raw bits del double. */
                    double d = ins->aux.cst;
                    memcpy(&imm, &d, 8);
                } else {
                    imm = (uint64_t)(int64_t)ins->aux.cst;
                }
                emit_mov_rax_imm64(&p, imm);
                emit_mov_rspdisp_rax(&p, i * 8);
                break;
            }
            case IR_LOAD_VAR:
                if (ins->type == T_FLOAT) {
                    if (jit_emit_load_var_float(&p, ins->aux.var, i) < 0) goto fail;
                } else {
                    if (jit_emit_load_var(&p, ins->aux.var, i) < 0) goto fail;
                }
                break;
            case IR_LOAD_THIS_ATTR:
                if (jit_emit_load_this_attr(&p, ins->aux.slot, i) < 0) goto fail;
                break;
            case IR_ADD_INT:
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_add_rax_rspdisp(&p, ins->b * 8);
                emit_mov_rspdisp_rax(&p, i * 8);
                break;
            case IR_SUB_INT:
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_sub_rax_rspdisp(&p, ins->b * 8);
                emit_mov_rspdisp_rax(&p, i * 8);
                break;
            case IR_MUL_INT:
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_imul_rax_rspdisp(&p, ins->b * 8);
                emit_mov_rspdisp_rax(&p, i * 8);
                break;
            case IR_ADD_FLOAT:
                emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
                emit_addsd_xmm0_rspdisp(&p, ins->b * 8);
                emit_movsd_rspdisp_xmm0(&p, i * 8);
                break;
            case IR_SUB_FLOAT:
                emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
                emit_subsd_xmm0_rspdisp(&p, ins->b * 8);
                emit_movsd_rspdisp_xmm0(&p, i * 8);
                break;
            case IR_MUL_FLOAT:
                emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
                emit_mulsd_xmm0_rspdisp(&p, ins->b * 8);
                emit_movsd_rspdisp_xmm0(&p, i * 8);
                break;
            case IR_DIV_FLOAT:
                emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
                emit_divsd_xmm0_rspdisp(&p, ins->b * 8);
                emit_movsd_rspdisp_xmm0(&p, i * 8);
                break;
            case IR_LT:
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_xor_edx_edx(&p);
                emit_cmp_rax_rspdisp(&p, ins->b * 8);
                emit_setl_dl(&p);
                emit_mov_rspdisp_rdx(&p, i * 8);
                break;
            default: goto fail;
            }
        }
    }

    /* Marcar loop_top después de los hoisted invariants (o justo después
     * del prologue si no hay hoisting). */
    size_t loop_top_off = (size_t)(p - base);

    for (int i = 1; i < t->len; i++) {
        IRInst *ins = &t->ops[i];
        /* Skip ops ya emitidos en el hoist (sólo loop traces). */
        if (is_loop && (ins->flags & IR_FLAG_INV)
            && ins->op != IR_GUARD_TRUE && ins->op != IR_GUARD_FALSE
            && ins->op != IR_STORE_VAR  && ins->op != IR_RETURN
            && ins->op != IR_LOOP_BACK) continue;
        switch (ins->op) {
        case IR_NOP: break;
        case IR_LOAD_CONST: {
            uint64_t imm;
            if (ins->type == T_FLOAT) {
                double d = ins->aux.cst;
                memcpy(&imm, &d, 8);
            } else {
                imm = (uint64_t)(int64_t)ins->aux.cst;
            }
            emit_mov_rax_imm64(&p, imm);
            emit_mov_rspdisp_rax(&p, i * 8);
            break;
        }
        case IR_LOAD_VAR: {
            if (ins->type == T_FLOAT) {
                /* Ola 17: float vars no participan en lreg cache (Ola 12). */
                if (jit_emit_load_var_float(&p, ins->aux.var, i) < 0) goto fail;
                break;
            }
            int lr = LREG_OF(ins->aux.var);
            if (lr >= 0) {
                /* Cached: el valor live está en reg, sólo spillear al slot
                 * para que el consumidor lo lea desde [rsp+id*8]. */
                emit_mov_rspdisp_lreg(&p, i * 8, lr);
            } else {
                if (jit_emit_load_var(&p, ins->aux.var, i) < 0) goto fail;
            }
            break;
        }
        case IR_LOAD_THIS_ATTR:
            if (jit_emit_load_this_attr(&p, ins->aux.slot, i) < 0) goto fail;
            break;
        case IR_ADD_INT:
            emit_mov_rax_rspdisp(&p, ins->a * 8);
            emit_add_rax_rspdisp(&p, ins->b * 8);
            emit_mov_rspdisp_rax(&p, i * 8);
            break;
        case IR_SUB_INT:
            emit_mov_rax_rspdisp(&p, ins->a * 8);
            emit_sub_rax_rspdisp(&p, ins->b * 8);
            emit_mov_rspdisp_rax(&p, i * 8);
            break;
        case IR_MUL_INT:
            emit_mov_rax_rspdisp(&p, ins->a * 8);
            emit_imul_rax_rspdisp(&p, ins->b * 8);
            emit_mov_rspdisp_rax(&p, i * 8);
            break;
        case IR_ADD_FLOAT:
            emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
            emit_addsd_xmm0_rspdisp(&p, ins->b * 8);
            emit_movsd_rspdisp_xmm0(&p, i * 8);
            break;
        case IR_SUB_FLOAT:
            emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
            emit_subsd_xmm0_rspdisp(&p, ins->b * 8);
            emit_movsd_rspdisp_xmm0(&p, i * 8);
            break;
        case IR_MUL_FLOAT:
            emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
            emit_mulsd_xmm0_rspdisp(&p, ins->b * 8);
            emit_movsd_rspdisp_xmm0(&p, i * 8);
            break;
        case IR_DIV_FLOAT:
            emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
            emit_divsd_xmm0_rspdisp(&p, ins->b * 8);
            emit_movsd_rspdisp_xmm0(&p, i * 8);
            break;
        case IR_LT:
            emit_mov_rax_rspdisp(&p, ins->a * 8);
            emit_xor_edx_edx(&p);
            emit_cmp_rax_rspdisp(&p, ins->b * 8);
            emit_setl_dl(&p);
            emit_mov_rspdisp_rdx(&p, i * 8);
            break;
        case IR_LIST_ITEM_ATTR: {
            /* Ola 14e: inline asm para arr[idx].attr (T_INT).
             * idx_ref = ins->a (slot del frame con el idx int).
             * dst slot = i*8.  Genera 6 guards que saltan a deopt label. */
            if (jit_emit_list_item_attr(&p, base,
                    ins->a * 8, i * 8,
                    ins->aux.lia,
                    guard_patches, &n_patches,
                    TE_JIT_MAX_IDS) < 0) goto fail;
            break;
        }
        case IR_GUARD_TRUE: {
            emit_mov_rax_rspdisp(&p, ins->a * 8);
            emit_test_rax_rax(&p);
            size_t at = emit_je_rel32(&p, base);
            if (n_patches >= TE_JIT_MAX_IDS) goto fail;
            guard_patches[n_patches++] = at;
            break;
        }
        case IR_STORE_VAR: {
            if (ins->type == T_FLOAT) {
                /* Ola 17: write directo a memoria, sin participar en lreg. */
                if (jit_emit_store_var_float(&p, ins->aux.var, ins->a) < 0) goto fail;
                break;
            }
            int lr = LREG_OF(ins->aux.var);
            if (lr >= 0) {
                /* Cached: actualizar reg desde slot. NO escribir a memoria;
                 * el writeback se hace en el deopt label. */
                emit_mov_lreg_rspdisp(&p, lr, ins->a * 8);
            } else {
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_mov_rdx_imm64(&p, (uint64_t)(uintptr_t)&ins->aux.var->value.int_value);
                emit_mov_memrdx_rax(&p);
            }
            break;
        }
        case IR_RETURN:
            if (ins->type == T_FLOAT) {
                /* Ola 17: ya está en bits raw double en el slot. */
                emit_movsd_xmm0_rspdisp(&p, ins->a * 8);
            } else {
                emit_mov_rax_rspdisp(&p, ins->a * 8);
                emit_cvtsi2sd_xmm0_rax(&p);
            }
            /* RET trace: n_lvars==0, así que el epílogo simple basta. */
            emit_epilogue(&p);
            break;
        case IR_LOOP_BACK: {
            size_t at = emit_jmp_rel32(&p, base);
            patch_rel32(base, at, loop_top_off);
            break;
        }
        default: goto fail;
        }
    }

    /* Para loop traces: deopt label + writeback de cached regs + epílogo. */
    if (is_loop) {
        size_t deopt_off = (size_t)(p - base);
        /* Writeback: por cada cached var, mov rax,&v.int ; mov [rax],reg. */
        for (int k = 0; k < n_lvars; k++) {
            emit_mov_rax_imm64(&p, (uint64_t)(uintptr_t)&lvars[k].var->value.int_value);
            emit_mov_memrax_lreg(&p, lvars[k].lreg);
        }
        /* xor xmm0 (return value irrelevante) */
        emit_pxor_xmm0_xmm0(&p);
        /* add rsp, frame                       48 81 C4 ii ii ii ii */
        e8(&p, 0x48); e8(&p, 0x81); e8(&p, 0xC4); e32(&p, TE_JIT_FRAME_BYTES);
        /* pop callee-saved en orden inverso */
        for (int k = n_lvars - 1; k >= 0; k--) emit_pop_lreg(&p, lvars[k].lreg);
        e8(&p, 0x5D);                                          /* pop rbp */
        e8(&p, 0xC3);                                          /* ret */
        for (int k = 0; k < n_patches; k++) patch_rel32(base, guard_patches[k], deopt_off);
    }

    if (g_jit_dump) {
        fprintf(stderr, "[OLA12] compiled %s trace anchor=%p code=%p bytes=%zd lvars=%d\n",
                is_loop ? "LOOP" : "RET",
                (void*)t->anchor, (void*)base, (ptrdiff_t)(p - base), n_lvars);
    }
    return (void*)base;

fail:
    return NULL;
}

/* Trampolín: dado un anchor (Instr*), busca en g_traces y devuelve la
 * fn compilada (o NULL). Usa el mismo hashing que trace_slot_for. */
static void *trace_lookup_compiled(Instr *anchor) {
    uintptr_t h = (uintptr_t)anchor;
    h ^= (h >> 16); h *= 0x9E3779B1u;
    int idx = (int)(h & (TRACE_TBL_SZ - 1));
    for (int probe = 0; probe < TRACE_TBL_SZ; probe++) {
        TraceSlot *s = &g_traces[(idx + probe) & (TRACE_TBL_SZ - 1)];
        if (s->key == anchor) {
            Trace *tr = s->trace;
            return (tr && tr->complete) ? tr->compiled : NULL;
        }
        if (s->key == NULL) return NULL;
    }
    return NULL;
}
#endif  /* TE_JIT_AVAILABLE */

/* Stack-based VM with computed goto dispatch (GCC/Clang extension). */
static double bc_exec(Instr *code) {
    /* Ola 6: cuenta entrada a bc_exec (loop body o method body). */
    if (g_profile_on == -1) profile_init_once();
    if (!g_trace_init_done) trace_init_once();
#if TE_JIT_AVAILABLE
    if (g_jit_on == -1) {
        jit_init_once();
        if (g_jit_on) jit_smoke_test();
    }
#else
    if (g_jit_on == -1) jit_init_once();
#endif
#if TE_JIT_AVAILABLE
    /* Ola 10b: trampolín. Si hay una traza compilada para este anchor,
     * saltar al código nativo y devolver su resultado. NOTA: esto
     * cortocircuita el dispatch del bytecode. Sin guards (Ola 10c los
     * añadirá): si los tipos cambian, el resultado es indefinido. */
    if (g_jit_on && !g_record_on) {
        void *fn = trace_lookup_compiled(code);
        if (fn) {
            return ((double (*)(void))(uintptr_t)fn)();
        }
    }
#endif
    if (g_profile_on > 0) {
        profile_bump(code, 1);
        /* Ola 7: si superamos el threshold y aún no estamos grabando
         * y no hay traza completa, iniciar recording. */
        if (!g_record_on) {
            HotEntry *e = NULL;
            uintptr_t h = (uintptr_t)code;
            h ^= (h >> 16); h *= 0x9E3779B1u;
            int idx = (int)(h & (HOT_TABLE_SZ - 1));
            for (int probe = 0; probe < HOT_TABLE_SZ; probe++) {
                HotEntry *cand = &g_hot[(idx + probe) & (HOT_TABLE_SZ - 1)];
                if (cand->key == code) { e = cand; break; }
                if (cand->key == NULL) break;
            }
            if (e && e->count >= (uint64_t)g_trace_threshold) {
                TraceSlot *s = trace_slot_for(code);
                if (s && (!s->trace || (!s->trace->complete && s->attempts < 3))) {
                    trace_begin(code);
                }
            }
        }
    }
    static void *table[] = {
        [BC_HALT]           = &&do_halt,
        [BC_LOAD_CONST]     = &&do_const,
        [BC_LOAD_VAR]       = &&do_var,
        [BC_ADD]            = &&do_add,
        [BC_SUB]            = &&do_sub,
        [BC_MUL]            = &&do_mul,
        [BC_DIV]            = &&do_div,
        [BC_LT]             = &&do_lt,
        [BC_GT]             = &&do_gt,
        [BC_LE]             = &&do_le,
        [BC_GE]             = &&do_ge,
        [BC_EQ]             = &&do_eq,
        [BC_NEQ]            = &&do_neq,
        [BC_NEG]            = &&do_neg,
        [BC_AND]            = &&do_and,
        [BC_OR]             = &&do_or,
        [BC_NOT]            = &&do_not,
        [BC_STORE_VAR]      = &&do_store,
        [BC_JUMP]           = &&do_jump,
        [BC_JUMP_IF_FALSE]  = &&do_jump_if_false,
        [BC_POP]            = &&do_pop,
        [BC_LOAD_THIS_ATTR] = &&do_this_attr,
        [BC_CALL_METHOD]    = &&do_call_method,
        [BC_SET_THIS]       = &&do_set_this,
        [BC_RESTORE_THIS]   = &&do_restore_this,
        [BC_LIST_ITEM_ATTR] = &&do_list_item_attr,
    };
    double stack[64];
    int sp = 0;
    Instr *ip = code;

    /* Ola 8: shadow stack de refs SSA paralelo a stack[].
     * rstack[i] = id del IR op que produjo stack[i].
     * rtype[i]  = tipo IR de stack[i] (T_INT/T_FLOAT/T_BOOL).
     * Sólo se mantiene mientras g_record_on != 0. */
    uint16_t rstack[64];
    uint8_t  rtype[64];
    /* Helpers locales: detección de tipo desde valor runtime. */
    #define TY_OF(v) (((v) == (double)(int)(v)) ? T_INT : T_FLOAT)
    /* Aborto de la traza desde cualquier handler no soportado. */
    #define TRACE_ABORT() do { if (g_record_on) trace_end(0); } while (0)

    #define DISPATCH() goto *table[ip->op]
    DISPATCH();

do_const: {
    double c = ip->u.constant;
    stack[sp] = c;
    if (g_record_on) {
        IRType ty = TY_OF(c);
        uint16_t id = ir_emit(IR_LOAD_CONST, ty, 0, 0);
        if (id) g_cur_trace->ops[id].aux.cst = c;
        rstack[sp] = id; rtype[sp] = ty;
    }
    sp++; ip++; DISPATCH();
}
do_var: {
    Variable *v = ip->u.var;
    double val = (v->vtype == VAL_INT) ? (double)v->value.int_value
                                       : v->value.float_value;
    stack[sp] = val;
    if (g_record_on) {
        IRType ty = (v->vtype == VAL_INT) ? T_INT : T_FLOAT;
        uint16_t id = ir_emit(IR_LOAD_VAR, ty, 0, 0);
        if (id) g_cur_trace->ops[id].aux.var = v;
        rstack[sp] = id; rtype[sp] = ty;
    }
    sp++; ip++; DISPATCH();
}
do_add:
    stack[sp-2] = stack[sp-2] + stack[sp-1]; sp--;
    if (g_record_on) {
        IRType ta = rtype[sp-1] /* old sp-2 */, tb = rtype[sp];
        IRType ty = (ta == T_INT && tb == T_INT) ? T_INT : T_FLOAT;
        IROp op = (ty == T_INT) ? IR_ADD_INT : IR_ADD_FLOAT;
        uint16_t id = ir_emit(op, ty, rstack[sp-1], rstack[sp]);
        rstack[sp-1] = id; rtype[sp-1] = ty;
    }
    ip++; DISPATCH();
do_sub:
    stack[sp-2] = stack[sp-2] - stack[sp-1]; sp--;
    if (g_record_on) {
        IRType ta = rtype[sp-1], tb = rtype[sp];
        IRType ty = (ta == T_INT && tb == T_INT) ? T_INT : T_FLOAT;
        IROp op = (ty == T_INT) ? IR_SUB_INT : IR_SUB_FLOAT;
        uint16_t id = ir_emit(op, ty, rstack[sp-1], rstack[sp]);
        rstack[sp-1] = id; rtype[sp-1] = ty;
    }
    ip++; DISPATCH();
do_mul:
    stack[sp-2] = stack[sp-2] * stack[sp-1]; sp--;
    if (g_record_on) {
        IRType ta = rtype[sp-1], tb = rtype[sp];
        IRType ty = (ta == T_INT && tb == T_INT) ? T_INT : T_FLOAT;
        IROp op = (ty == T_INT) ? IR_MUL_INT : IR_MUL_FLOAT;
        uint16_t id = ir_emit(op, ty, rstack[sp-1], rstack[sp]);
        rstack[sp-1] = id; rtype[sp-1] = ty;
    }
    ip++; DISPATCH();
do_div:
    if (stack[sp-1] == 0.0) {
        printf("Error: division by zero.\n");
        stack[sp-2] = 0;
        TRACE_ABORT();
    } else {
        stack[sp-2] = stack[sp-2] / stack[sp-1];
        if (g_record_on) {
            IRType ta = rtype[sp-2], tb = rtype[sp-1];
            IRType ty = (ta == T_INT && tb == T_INT) ? T_INT : T_FLOAT;
            IROp op = (ty == T_INT) ? IR_DIV_INT : IR_DIV_FLOAT;
            uint16_t id = ir_emit(op, ty, rstack[sp-2], rstack[sp-1]);
            rstack[sp-2] = id; rtype[sp-2] = ty;
        }
    }
    sp--; ip++; DISPATCH();
do_lt:  stack[sp-2] = (stack[sp-2] <  stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_LT, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_gt:  stack[sp-2] = (stack[sp-2] >  stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_GT, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_le:  stack[sp-2] = (stack[sp-2] <= stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_LE, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_ge:  stack[sp-2] = (stack[sp-2] >= stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_GE, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_eq:  stack[sp-2] = (stack[sp-2] == stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_EQ, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_neq: stack[sp-2] = (stack[sp-2] != stack[sp-1]); sp--;
        if (g_record_on) { uint16_t id = ir_emit(IR_NEQ, T_BOOL, rstack[sp-1], rstack[sp]); rstack[sp-1]=id; rtype[sp-1]=T_BOOL; }
        ip++; DISPATCH();
do_neg:
    stack[sp-1] = -stack[sp-1];
    TRACE_ABORT();   /* unary neg no instrumentado en Ola 8 */
    ip++; DISPATCH();
do_and:
    stack[sp-2] = (stack[sp-2] != 0.0 && stack[sp-1] != 0.0) ? 1 : 0; sp--;
    TRACE_ABORT();
    ip++; DISPATCH();
do_or:
    stack[sp-2] = (stack[sp-2] != 0.0 || stack[sp-1] != 0.0) ? 1 : 0; sp--;
    TRACE_ABORT();
    ip++; DISPATCH();
do_not:
    stack[sp-1] = (stack[sp-1] == 0.0) ? 1 : 0;
    TRACE_ABORT();
    ip++; DISPATCH();
do_store: {
    /* Pop top, write to var. Auto-detect INT vs FLOAT to keep parity with
     * the Fase 2 fast-path: integral doubles stay as VAL_INT. */
    Variable *v = ip->u.var;
    double r = stack[--sp];
    if (r == (double)(int)r) {
        v->vtype = VAL_INT;
        v->value.int_value = (int)r;
    } else {
        v->vtype = VAL_FLOAT;
        v->value.float_value = r;
    }
    if (g_record_on) {
        IRType ty = (v->vtype == VAL_INT) ? T_INT : T_FLOAT;
        uint16_t id = ir_emit(IR_STORE_VAR, ty, rstack[sp], 0);
        if (id) g_cur_trace->ops[id].aux.var = v;
    }
    ip++; DISPATCH();
}
do_jump:
    /* Ola 6: si es backward jump, cuenta el target como hot. */
    if (g_profile_on > 0 && ip->u.offset < 0)
        profile_bump(ip + 1 + ip->u.offset, 2);
    /* Ola 8: backward jump al anchor cierra la traza con LOOP_BACK. */
    if (g_record_on) {
        Instr *target = ip + 1 + ip->u.offset;
        if (target == g_cur_trace->anchor) {
            ir_emit(IR_LOOP_BACK, T_UNK, 0, 0);
            trace_end(1);
        } else {
            TRACE_ABORT();
        }
    }
    /* Ola 11: trampolín / start-of-trace en backward jump.
     * El target del backward jump es el "loop top" — el lugar perfecto para
     * (a) ejecutar nativo si ya hay traza compilada, o (b) iniciar recording. */
    if (ip->u.offset < 0 && !g_record_on) {
        Instr *target = ip + 1 + ip->u.offset;
#if TE_JIT_AVAILABLE
        if (g_jit_on) {
            void *fn = trace_lookup_compiled(target);
            if (fn) {
                /* Native loop ejecuta hasta deopt natural (i==limit).
                 * Estado vive en Variable*s; nada que restaurar.
                 * NO tomamos el backward jump: caemos al instr siguiente
                 * (HALT en el FOR canon), que retornará de bc_exec. */
                ((double (*)(void))(uintptr_t)fn)();
                ip++;
                DISPATCH();
            }
        }
#endif
        /* Hot-loop detection: si el target es hot, iniciar recording allí. */
        if (g_profile_on > 0) {
            uintptr_t h = (uintptr_t)target;
            h ^= (h >> 16); h *= 0x9E3779B1u;
            int idx = (int)(h & (HOT_TABLE_SZ - 1));
            HotEntry *e = NULL;
            for (int probe = 0; probe < HOT_TABLE_SZ; probe++) {
                HotEntry *cand = &g_hot[(idx + probe) & (HOT_TABLE_SZ - 1)];
                if (cand->key == target) { e = cand; break; }
                if (cand->key == NULL) break;
            }
            if (e && e->count >= (uint64_t)g_trace_threshold) {
                TraceSlot *s = trace_slot_for(target);
                if (s && (!s->trace || (!s->trace->complete && s->attempts < 3))) {
                    trace_begin(target);
                }
            }
        }
    }
    ip += 1 + ip->u.offset;
    DISPATCH();
do_jump_if_false: {
    /* Ola 11: capturar IR id ANTES del decremento de sp. */
    uint16_t cond_id = g_record_on ? rstack[sp-1] : 0;
    double v = stack[--sp];
    /* Ola 11: emitir GUARD_TRUE (deopt si cond es falsa).
     * Si observamos v==0 (loop exit), abortamos limpio: la traza
     * sólo cubre el path "stay in loop". */
    if (g_record_on) {
        if (v == 0.0) {
            TRACE_ABORT();
        } else {
            ir_emit(IR_GUARD_TRUE, T_INT, cond_id, 0);
        }
    }
    if (v == 0.0) {
        if (g_profile_on > 0 && ip->u.offset < 0)
            profile_bump(ip + 1 + ip->u.offset, 2);
        ip += 1 + ip->u.offset;
    } else {
        ip++;
    }
    DISPATCH();
}
do_pop:
    sp--;
    if (g_record_on) TRACE_ABORT();
    ip++; DISPATCH();
do_this_attr: {
    /* Ola 4: read this.attribute[slot] (numeric) onto stack. */
    int slot = ip->u.slot;
    Variable *a = &g_bc_this->attributes[slot];
    double val = (a->vtype == VAL_INT) ? (double)a->value.int_value
                                       : a->value.float_value;
    stack[sp] = val;
    if (g_record_on) {
        /* Ola 11: emitir IR_LOAD_VAR con la Variable* del atributo
         * (resuelve g_bc_this en compile-time). Esto hace que el codegen
         * trate atributos exactamente como variables, y elimina la
         * dependencia del compiled trace en g_bc_this — clave para
         * que SET_THIS / RESTORE_THIS no necesiten emitir IR. */
        IRType ty = (a->vtype == VAL_INT) ? T_INT : T_FLOAT;
        uint16_t id = ir_emit(IR_LOAD_VAR, ty, 0, 0);
        if (id) g_cur_trace->ops[id].aux.var = a;
        rstack[sp] = id; rtype[sp] = ty;
    }
    sp++; ip++; DISPATCH();
}
do_call_method: {
    /* Ola 5: inline-call a method whose body is precompiled bytecode.
     *
     * Stack layout on entry: ... arg0 arg1 ... arg(n-1)  (top)
     * 1) Move args into the cached param Variable* slots.
     * 2) Save/swap g_bc_this to the call's object.
     * 3) Recursively run bc_exec on the method body.
     * 4) Restore g_bc_this and push the return value.
     *
     * Ola 11: durante recording, INLINE el cuerpo del método en la traza
     * actual. No abortar: emitir IR_STORE_VAR para cada arg → param Variable*,
     * luego recurrir bc_exec con g_inline_depth>0 para que do_halt no cierre
     * la traza, y empujar el ret id capturado en outer rstack. */
    MethodCallSite *site = ip->u.site;
    int n = site->n_params;
    int recording = g_record_on;
    /* Capturar IR ids de los args ANTES de mover (rstack[sp-n+i] son los args). */
    uint16_t arg_ids[8] = {0};
    if (recording && n <= 8) {
        for (int i = 0; i < n; i++) arg_ids[i] = rstack[sp - n + i];
    } else if (recording && n > 8) {
        TRACE_ABORT();  /* >8 args: no soportado en inlining */
    }
    for (int i = 0; i < n; i++) {
        Variable *pv = site->param_vars[i];
        double v = stack[sp - n + i];
        if (pv->vtype == VAL_FLOAT) {
            pv->value.float_value = v;
        } else {
            pv->vtype = VAL_INT;
            pv->value.int_value = (int)v;
        }
        if (recording) {
            IRType ty = (pv->vtype == VAL_INT) ? T_INT : T_FLOAT;
            uint16_t sid = ir_emit(IR_STORE_VAR, ty, arg_ids[i], 0);
            if (sid) g_cur_trace->ops[sid].aux.var = pv;
        }
    }
    sp -= n;
    ObjectNode *saved_this = g_bc_this;
    g_bc_this = site->obj_var->value.object_value;
    double rv;
    if (recording) {
        /* Inlined recording: profundizar, evitar que el inner do_halt cierre. */
        g_inline_depth++;
        uint16_t saved_ret_id = g_inline_ret_id;
        uint8_t saved_ret_ty  = g_inline_ret_type;
        g_inline_ret_id = 0;
        g_inline_ret_type = T_UNK;
        rv = bc_exec(((BCInfo *)site->body_bc)->code);
        /* push ret id en outer rstack ANTES de restaurar globals */
        if (g_record_on) {
            /* puede ser que la inline grabación abortara; sólo empujar si seguimos */
            rstack[sp] = g_inline_ret_id;
            rtype[sp]  = g_inline_ret_type;
        }
        g_inline_ret_id = saved_ret_id;
        g_inline_ret_type = saved_ret_ty;
        g_inline_depth--;
    } else {
        rv = bc_exec(((BCInfo *)site->body_bc)->code);
    }
    g_bc_this = saved_this;
    stack[sp++] = rv;
    ip++; DISPATCH();
}
do_set_this: {
    /* Ola 5b: push current g_bc_this on the this-stack and set new from var.
     * Ola 11: NO abortar durante recording. Las cargas de atributos durante
     * recording se materializan como IR_LOAD_VAR con la Variable* concreta
     * (do_this_attr resolvió g_bc_this), así que el compiled trace no depende
     * del estado de g_bc_this. Aquí sólo hay que mantener el push/pop para
     * cuando volvemos a interpretación. */
    g_bc_this_stack[g_bc_this_sp++] = g_bc_this;
    g_bc_this = ip->u.var->value.object_value;
    ip++; DISPATCH();
}
do_restore_this: {
    g_bc_this = g_bc_this_stack[--g_bc_this_sp];
    ip++; DISPATCH();
}
do_list_item_attr: {
    /* Ola 14d: hot path for `arr[idx].attr` over a homogeneous list of
     * objects. Pops idx, pushes the (numeric) attr value.
     * Ola 14e: when recording a trace, emit IR_LIST_ITEM_ATTR (instead
     * of aborting) so the JIT can inline the lookup in asm. */
    ListItemAttrSite *s = ip->u.lia_site;
    int idx = (int)stack[sp - 1];
    /* Resolve list (Variable holds intptr_t to LIST ASTNode). */
    ASTNode *list = (ASTNode*)(intptr_t)s->list_var->value.object_value;
    TEListIdx *ix = list ? (TEListIdx*)list->extra : NULL;
    double val = 0.0;
    int matched = 0;          /* did the lookup succeed cleanly? */
    int matched_int = 0;      /* and was the attr a VAL_INT? */
    if (ix && idx >= 0 && idx < ix->len) {
        ASTNode *item = ix->items[idx];
        ObjectNode *obj = NULL;
        if (item) {
            if (item->extra) obj = (ObjectNode*)item->extra;
            else             obj = (ObjectNode*)(intptr_t)item->value;
        }
        /* Cheap class match check; on mismatch fall back to 0 (caller can
         * disable this opcode by setting TYPEEASY_NO_BC=1). */
        if (obj && obj->class == s->expected_class &&
            s->attr_slot >= 0 && s->attr_slot < obj->class->attr_count) {
            Variable *attr = &obj->attributes[s->attr_slot];
            matched = 1;
            matched_int = (attr->vtype == VAL_INT);
            val = matched_int ? (double)attr->value.int_value
                              : attr->value.float_value;
        }
    }
    stack[sp - 1] = val;
    if (g_record_on) {
        /* Only emit IR if the lookup matched cleanly AND the attr is
         * VAL_INT (the asm path returns int64). Otherwise abort. */
        if (matched && matched_int) {
            uint16_t id = ir_emit(IR_LIST_ITEM_ATTR, T_INT,
                                  rstack[sp - 1] /* idx ref */, 0);
            if (id) g_cur_trace->ops[id].aux.lia = s;
            rstack[sp - 1] = id;
            rtype[sp - 1]  = T_INT;
        } else {
            TRACE_ABORT();
        }
    }
    ip++; DISPATCH();
}
do_halt:
    /* Ola 8: si recording activo, cerrar la traza con IR_RETURN del top.
     * Ola 11: si estamos inlined (depth>0), NO cerrar; sólo capturar el ret id. */
    if (g_record_on) {
        if (g_inline_depth > 0) {
            if (sp > 0) { g_inline_ret_id = rstack[sp-1]; g_inline_ret_type = rtype[sp-1]; }
            else        { g_inline_ret_id = 0; g_inline_ret_type = T_UNK; }
        } else {
            if (sp > 0) ir_emit(IR_RETURN, rtype[sp-1], rstack[sp-1], 0);
            else        ir_emit(IR_RETURN, T_UNK, 0, 0);
            trace_end(1);
        }
    }
    return sp > 0 ? stack[sp-1] : 0;
    #undef DISPATCH
    #undef TY_OF
    #undef TRACE_ABORT
}

/* Try to fetch (or build on first call) a compiled bytecode for `node`.
 * Returns NULL if the node isn't compilable. Once a node is marked
 * BC_NOT_COMPILABLE we never retry. */
static BCInfo *bc_get_or_compile(ASTNode *node) {
    if (!node) return NULL;
    void *p = node->bc;
    if (p == BC_NOT_COMPILABLE) return NULL;
    if (p) return (BCInfo *)p;

    /* Only attempt compilation for nodes likely to win: arithmetic and
     * comparisons. Plain identifiers/numbers don't benefit (one VM step
     * vs one switch case). NOTE: do NOT mark non-worth nodes as
     * BC_NOT_COMPILABLE here — Fase 4 statement compiler also uses node->bc
     * as cache and may want to compile WHILE/IF/STATEMENT_LIST nodes. */
    NodeKind k = nk_of(node);
    int worth = (k == NK_ADD || k == NK_SUB || k == NK_MUL || k == NK_DIV ||
                 k == NK_LT  || k == NK_GT  ||
                 k == NK_LT_EQ || k == NK_GT_EQ ||
                 k == NK_EQ  || k == NK_DIFF ||
                 k == NK_AND || k == NK_OR  || k == NK_NOT);
    if (!worth) {
        return NULL;
    }

    Instr buf[64];
    int pos = 0;
    if (!bc_compile(node, buf, &pos, 64)) {
        node->bc = BC_NOT_COMPILABLE;
        return NULL;
    }
    /* Append HALT */
    if (pos >= 64) { node->bc = BC_NOT_COMPILABLE; return NULL; }
    buf[pos].op = BC_HALT;
    pos++;

    BCInfo *info = (BCInfo *)malloc(sizeof(BCInfo));
    info->code = (Instr *)malloc(sizeof(Instr) * pos);
    memcpy(info->code, buf, sizeof(Instr) * pos);
    info->len = pos;
    node->bc = info;
    return info;
}

/* ===================== Ola 4: method-body bytecode =========================
 * Compile method bodies of the shape `{ return <numeric expr>; }` to a flat
 * bytecode program. The expression may reference parameters (resolved via
 * find_variable on identifier name — the same Variable* slot is reused
 * across calls thanks to Ola 3 Fase B) and `this.attr` (resolved to a
 * fixed slot index at compile time using the class context).
 *
 * Result is cached on MethodNode.bc_body. NULL = not yet attempted.
 * BC_NOT_COMPILABLE = tried, gave up. Otherwise BCInfo*.
 *
 * Switch: TYPEEASY_NO_BCMETHOD=1 disables this path entirely. */

/* Walk down the body looking for a single RETURN node, skipping
 * STATEMENT_LIST wrappers that contain only one statement. Returns the
 * RETURN node or NULL if the shape doesn't match. */
static ASTNode *bc_find_single_return(ASTNode *body) {
    while (body) {
        NodeKind k = nk_of(body);
        if (k == NK_RETURN) return body;
        if (k == NK_STATEMENT_LIST) {
            /* Two-child list: if right side is empty, descend into left.
             * If left is empty, descend into right. Otherwise: more than
             * one statement → not eligible. */
            if (body->left && !body->right) { body = body->left; continue; }
            if (body->right && !body->left) { body = body->right; continue; }
            if (body->left && body->right) {
                /* Allow the case where one side is itself empty STATEMENT_LIST. */
                NodeKind lk = nk_of(body->left);
                NodeKind rk = nk_of(body->right);
                if (lk == NK_STATEMENT_LIST && !body->left->left && !body->left->right) {
                    body = body->right; continue;
                }
                if (rk == NK_STATEMENT_LIST && !body->right->left && !body->right->right) {
                    body = body->left; continue;
                }
                return NULL;
            }
            return NULL;
        }
        return NULL;
    }
    return NULL;
}

static BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls) {
    if (!m) return NULL;
    if (m->bc_body == BC_NOT_COMPILABLE) return NULL;
    if (m->bc_body) return (BCInfo *)m->bc_body;

    /* Body must be a single `return <expr>;`. */
    ASTNode *ret = bc_find_single_return(m->body);
    if (!ret || !ret->left) { m->bc_body = BC_NOT_COMPILABLE; return NULL; }

    /* Ola 5: pre-allocate Variable* slots for all params so that the
     * NK_IDENTIFIER lookups inside the body compile succeed even if the
     * method has never been invoked (and Fase B's fast-call hasn't yet
     * created the slots). */
    for (ParameterNode *p = m->params; p; p = p->next) {
        if (!p->name) continue;
        Variable *pv = (Variable *)p->cached_var;
        if (!pv) pv = find_variable_for(p->name);
        if (!pv && var_count < MAX_VARS) {
            /* TypeEasy's lexer doesn't set yylval for INT/FLOAT tokens,
             * so p->type can be garbage. Default to INT; BC_STORE_VAR
             * will switch the slot to FLOAT at runtime if needed. */
            int is_float = (p->type
                          && (strcmp(p->type, "float") == 0
                           || strcmp(p->type, "FLOAT") == 0));
            vars[var_count].id       = strdup(p->name);
            vars[var_count].type     = strdup(is_float ? "FLOAT" : "INT");
            vars[var_count].is_const = 0;
            vars[var_count].vtype    = is_float ? VAL_FLOAT : VAL_INT;
            if (is_float) vars[var_count].value.float_value = 0.0;
            else          vars[var_count].value.int_value   = 0;
            pv = &vars[var_count];
            var_count++;
        }
        if (pv) p->cached_var = pv;
    }

    Instr buf[64];
    int pos = 0;
    ClassNode *saved_cls = g_bc_compile_class;
    g_bc_compile_class = cls;
    int ok = bc_compile(ret->left, buf, &pos, 63);
    g_bc_compile_class = saved_cls;
    if (!ok) { m->bc_body = BC_NOT_COMPILABLE; return NULL; }

    buf[pos].op = BC_HALT;
    pos++;

    BCInfo *info = (BCInfo *)malloc(sizeof(BCInfo));
    info->code = (Instr *)malloc(sizeof(Instr) * pos);
    memcpy(info->code, buf, sizeof(Instr) * pos);
    info->len = pos;
    m->bc_body = info;
    return info;
}

/* ===================== Fase 4: full-statement bytecode =====================
 * Compile entire ASSIGN / WHILE / IF / STATEMENT_LIST trees so a hot loop
 * runs without ever returning to the AST walker. Eligible bodies must contain
 * only: numeric ASSIGN to known-numeric vars, nested compilable IF/WHILE, and
 * statement lists thereof. Anything else (PRINT, CALL, BREAK, RETURN, THROW,
 * string ops, FOR, etc.) → fail compilation, fall back to AST walker. */

#define BC_STMT_MAX 1024  /* max instructions per compiled statement tree */

static int bc_compile_stmt(ASTNode *node, Instr *out, int *pos, int max);
static int bc_compile_for(ASTNode *node, Instr *out, int *pos, int max);

/* Compile an assignment "var = expr" where var must be an existing numeric
 * Variable* and expr must be bytecode-compilable. */
static int bc_compile_assign(ASTNode *node, Instr *out, int *pos, int max) {
    ASTNode *var_node   = node->left;
    ASTNode *value_node = node->right;
    if (!var_node || !var_node->id || !value_node) return 0;

    /* Resolve & cache the destination Variable*. Must be numeric and not const. */
    Variable *fv = (Variable *)var_node->cached_var;
    if (!fv) {
        fv = find_variable_for(var_node->id);
        if (fv) var_node->cached_var = fv;
    }
    if (!fv || fv->is_const) return 0;
    if (fv->vtype != VAL_INT && fv->vtype != VAL_FLOAT) return 0;

    /* Disallow string-typed ADD (concat) — handled by AST walker. */
    NodeKind vk = nk_of(value_node);
    if (vk == NK_ADD && is_string_type(value_node)) return 0;

    /* Compile RHS expression onto stack. Reuse bc_compile from Fase 3. */
    if (!bc_compile(value_node, out, pos, max)) return 0;

    if (*pos >= max) return 0;
    out[*pos].op = BC_STORE_VAR;
    out[*pos].u.var = fv;
    (*pos)++;
    return 1;
}

/* Compile WHILE: emit cond → JUMP_IF_FALSE end → body → JUMP start → end:
 * Backpatches the two jump offsets (relative). */
static int bc_compile_while(ASTNode *node, Instr *out, int *pos, int max) {
    if (!node->left || !node->right) return 0;
    int start = *pos;
    if (!bc_compile(node->left, out, pos, max)) return 0;  /* condition */
    if (*pos >= max) return 0;
    int jif_pos = (*pos)++;
    out[jif_pos].op = BC_JUMP_IF_FALSE;
    out[jif_pos].u.offset = 0;  /* backpatch later */

    if (!bc_compile_stmt(node->right, out, pos, max)) return 0;  /* body */

    if (*pos >= max) return 0;
    int back_pos = (*pos)++;
    out[back_pos].op = BC_JUMP;
    out[back_pos].u.offset = start - (back_pos + 1);  /* relative to next ip */

    /* Now backpatch JUMP_IF_FALSE to land at *pos (after the back-jump). */
    out[jif_pos].u.offset = (*pos) - (jif_pos + 1);
    return 1;
}

/* Compile IF: cond → JUMP_IF_FALSE else_or_end → then → [JUMP end → else] → end */
static int bc_compile_if(ASTNode *node, Instr *out, int *pos, int max) {
    if (!node->left) return 0;
    if (!bc_compile(node->left, out, pos, max)) return 0;

    if (*pos >= max) return 0;
    int jif_pos = (*pos)++;
    out[jif_pos].op = BC_JUMP_IF_FALSE;
    out[jif_pos].u.offset = 0;

    /* then-branch */
    if (node->right) {
        if (!bc_compile_stmt(node->right, out, pos, max)) return 0;
    }

    if (node->next) {
        /* else-branch present: emit JUMP-over after then */
        if (*pos >= max) return 0;
        int jmp_pos = (*pos)++;
        out[jmp_pos].op = BC_JUMP;
        out[jmp_pos].u.offset = 0;

        out[jif_pos].u.offset = (*pos) - (jif_pos + 1);

        if (!bc_compile_stmt(node->next, out, pos, max)) return 0;

        out[jmp_pos].u.offset = (*pos) - (jmp_pos + 1);
    } else {
        out[jif_pos].u.offset = (*pos) - (jif_pos + 1);
    }
    return 1;
}

/* Compile FOR loop. TypeEasy syntax: `for(i = INIT; LIMIT; STEP) { body }`,
 * where LIMIT and STEP are stored as NUMBER literals (.value). The body is
 * a linked list traversed via ->right (peculiar to interpret_for). We emit:
 *     STORE init → top: LOAD var; LOAD limit; LT; JUMP_IF_FALSE end;
 *     body... ; LOAD var; LOAD step; ADD; STORE var; JUMP top; end:
 * If anything in the body is not bytecode-compilable, fail and let the AST
 * walker run the loop. */
static int bc_compile_for(ASTNode *node, Instr *out, int *pos, int max) {
    if (!node || !node->id || !node->left || !node->right) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: shape\n"); return 0; }
    ASTNode *update_body = node->right->right;
    if (!update_body || !update_body->left) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: update_body\n"); return 0; }

    /* Resolve / create the control variable as INT. We can't allocate a new
     * variable from inside the bytecode hot-path (find_variable_for is fine
     * but we need it to exist). Try lookup first; if not present, fail and
     * let the AST walker create it (it will be cached on subsequent calls). */
    Variable *fv = find_variable_for(node->id);
    if (!fv && var_count < MAX_VARS) {
        /* Ola 5: pre-allocate as INT so we can compile straight away. */
        vars[var_count].id       = strdup(node->id);
        vars[var_count].type     = strdup("INT");
        vars[var_count].is_const = 0;
        vars[var_count].vtype    = VAL_INT;
        vars[var_count].value.int_value = 0;
        fv = &vars[var_count];
        var_count++;
    }
    if (!fv || fv->is_const) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: fv const/null\n"); return 0; }
    if (fv->vtype != VAL_INT) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: fv not VAL_INT (vtype=%d)\n", fv->vtype); return 0; }

    int init_val  = node->left->value;
    int limit_val = node->right->value;
    int step_val  = update_body->left->value;
    if (step_val == 0) return 0;  /* would be infinite loop */

    /* STORE init: push const, store to var. */
    if (*pos + 2 >= max) return 0;
    out[*pos].op = BC_LOAD_CONST; out[*pos].u.constant = (double)init_val; (*pos)++;
    out[*pos].op = BC_STORE_VAR;  out[*pos].u.var      = fv;               (*pos)++;

    /* top: */
    int top = *pos;
    /* LOAD var; LOAD limit; LT */
    if (*pos + 4 >= max) return 0;
    out[*pos].op = BC_LOAD_VAR;   out[*pos].u.var      = fv;                (*pos)++;
    out[*pos].op = BC_LOAD_CONST; out[*pos].u.constant = (double)limit_val; (*pos)++;
    out[*pos].op = BC_LT;                                                   (*pos)++;
    int jif_pos = (*pos)++;
    out[jif_pos].op = BC_JUMP_IF_FALSE;
    out[jif_pos].u.offset = 0;  /* backpatch */

    /* Compile body. body es node->right->right->right; puede ser NK_STATEMENT_LIST
     * o un statement suelto. bc_compile_stmt maneja ambos casos recursivamente. */
    ASTNode *body = update_body->right;
    if (body && !bc_compile_stmt(body, out, pos, max)) { if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: body nk=%d\n", nk_of(body)); return 0; }

    /* var = var + step */
    if (*pos + 4 >= max) return 0;
    out[*pos].op = BC_LOAD_VAR;   out[*pos].u.var      = fv;               (*pos)++;
    out[*pos].op = BC_LOAD_CONST; out[*pos].u.constant = (double)step_val; (*pos)++;
    out[*pos].op = BC_ADD;                                                 (*pos)++;
    out[*pos].op = BC_STORE_VAR;  out[*pos].u.var      = fv;               (*pos)++;

    /* JUMP top */
    if (*pos >= max) return 0;
    int back_pos = (*pos)++;
    out[back_pos].op = BC_JUMP;
    out[back_pos].u.offset = top - (back_pos + 1);

    /* Backpatch JUMP_IF_FALSE to here. */
    out[jif_pos].u.offset = (*pos) - (jif_pos + 1);
    return 1;
}

/* Compile a statement (or statement list). Statements push nothing net. */
static int bc_compile_stmt(ASTNode *node, Instr *out, int *pos, int max) {
    if (!node) return 1;  /* empty body is OK */
    NodeKind k = nk_of(node);
    switch (k) {
    case NK_STATEMENT_LIST:
        if (!bc_compile_stmt(node->left,  out, pos, max)) return 0;
        if (!bc_compile_stmt(node->right, out, pos, max)) return 0;
        return 1;
    case NK_ASSIGN:
        return bc_compile_assign(node, out, pos, max);
    case NK_WHILE:
        return bc_compile_while(node, out, pos, max);
    case NK_IF:
        return bc_compile_if(node, out, pos, max);
    case NK_FOR:
        return bc_compile_for(node, out, pos, max);
    default:
        /* Anything else (PRINT, CALL, BREAK, RETURN, THROW, VAR_DECL,
         * INDEX_ASSIGN, etc.) is not compilable. */
        return 0;
    }
}

/* Lazy compile-on-first-call for whole statement subtrees. */
static BCInfo *bc_get_or_compile_stmt(ASTNode *node) {
    if (!node) return NULL;
    void *p = node->bc;
    if (p == BC_NOT_COMPILABLE) return NULL;
    if (p) return (BCInfo *)p;

    /* Only worth attempting on WHILE/IF/FOR (huge win). Plain single ASSIGN
     * already has a fast-path in interpret_assign. */
    NodeKind k = nk_of(node);
    if (k != NK_WHILE && k != NK_IF && k != NK_FOR) {
        node->bc = BC_NOT_COMPILABLE;
        return NULL;
    }

    Instr buf[BC_STMT_MAX];
    int pos = 0;
    if (k == NK_WHILE) {
        if (!bc_compile_while(node, buf, &pos, BC_STMT_MAX)) {
            node->bc = BC_NOT_COMPILABLE;
            return NULL;
        }
    } else if (k == NK_FOR) {
        if (!bc_compile_for(node, buf, &pos, BC_STMT_MAX)) {
            node->bc = BC_NOT_COMPILABLE;
            return NULL;
        }
    } else {
        if (!bc_compile_if(node, buf, &pos, BC_STMT_MAX)) {
            node->bc = BC_NOT_COMPILABLE;
            return NULL;
        }
    }
    if (pos >= BC_STMT_MAX) { node->bc = BC_NOT_COMPILABLE; return NULL; }
    buf[pos].op = BC_HALT;
    pos++;

    BCInfo *info = (BCInfo *)malloc(sizeof(BCInfo));
    info->code = (Instr *)malloc(sizeof(Instr) * pos);
    memcpy(info->code, buf, sizeof(Instr) * pos);
    info->len = pos;
    node->bc = info;
    return info;
}

double evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    /* Fase 3 (perf): bytecode fast-path. Compile & cache on first hit;
     * on subsequent calls (loop bodies, conditions) skip the AST walker
     * entirely and run the flat opcode stream via computed goto. */
    {
        static int bc_init = 0;
        static int bc_enabled = 1;
        if (!bc_init) {
            const char *e = getenv("TYPEEASY_NO_BC");
            if (e && e[0] && e[0] != '0') bc_enabled = 0;
            bc_init = 1;
        }
        if (bc_enabled) {
            BCInfo *info = bc_get_or_compile(node);
            if (info) return bc_exec(info->code);
        }
    }

    /* Fase 1 (perf): single dispatch via cached NodeKind enum. */
    NodeKind k = nk_of(node);

    switch (k) {
    case NK_NULL:
        return 0; /* null como número = 0 */

    case NK_IDENTIFIER: {
        /* Fase 2 (perf): cache Variable* on first lookup.
         * vars[] is append-only with stable pointers, so this is safe. */
        Variable *var = (Variable *)node->cached_var;
        if (!var) {
            var = find_variable(node->id);
            node->cached_var = var;
        }
        if (var) {
            if (var->type && strcmp(var->type, "NULL") == 0) {
                return 0;
            }
            if (var->vtype == VAL_INT) {
                return var->value.int_value;
            } else if (var->vtype == VAL_FLOAT) {
                return var->value.float_value;
            } else if (var->vtype == VAL_STRING) {
                printf("Error: variable '%s' is a string, cannot be evaluated as a number.\n", node->id);
                return 0;
            } else {
                printf("Error: variable '%s' is an object, cannot be evaluated as a number.\n", node->id);
                return 0;
            }
        } else {
            printf("Error: variable '%s' is not defined.\n", node->id);
            return 0;
        }
    }

    case NK_NUMBER:
    case NK_INT:
        return (double)node->value;

    case NK_FLOAT:
        return atof(node->str_value);

    case NK_GT:
        return evaluate_expression(node->left) > evaluate_expression(node->right);
    case NK_LT:
        return evaluate_expression(node->left) < evaluate_expression(node->right);
    case NK_GT_EQ:
        return evaluate_expression(node->left) <= evaluate_expression(node->right);
    case NK_LT_EQ:
        return evaluate_expression(node->left) >= evaluate_expression(node->right);

    case NK_EQ: {
        int left_null = 0, right_null = 0;
        if (node->left  && nk_of(node->left)  == NK_NULL) left_null = 1;
        if (node->right && nk_of(node->right) == NK_NULL) right_null = 1;
        if (node->left  && nk_of(node->left)  == NK_IDENTIFIER) {
            Variable *v = find_variable(node->left->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) left_null = 1;
        }
        if (node->right && nk_of(node->right) == NK_IDENTIFIER) {
            Variable *v = find_variable(node->right->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) right_null = 1;
        }
        if (left_null || right_null) return (double)(left_null && right_null);
        if (is_string_type(node->left) || is_string_type(node->right)) {
             /* Ola 3 Fase A: zero-alloc string compare.
              * Resolve both sides to const char* without strdup. If at least
              * one side is an interned literal, pointer-equality is enough
              * for the positive case; otherwise fall back to strcmp. */
             const char *s1 = NULL, *s2 = NULL;
             int s1_interned = 0, s2_interned = 0;
             if (node->left) {
                 NodeKind lk = nk_of(node->left);
                 if (lk == NK_STRING) { s1 = node->left->str_value; s1_interned = node->left->str_interned; }
                 else if (lk == NK_IDENTIFIER || lk == NK_ID) {
                     Variable *vv = find_variable(node->left->id);
                     if (vv && vv->vtype == VAL_STRING) s1 = vv->value.string_value;
                 }
             }
             if (node->right) {
                 NodeKind rk = nk_of(node->right);
                 if (rk == NK_STRING) { s2 = node->right->str_value; s2_interned = node->right->str_interned; }
                 else if (rk == NK_IDENTIFIER || rk == NK_ID) {
                     Variable *vv = find_variable(node->right->id);
                     if (vv && vv->vtype == VAL_STRING) s2 = vv->value.string_value;
                 }
             }
             if (s1 && s2) {
                 if (s1 == s2) return 1.0;
                 /* If both are interned and pointers differ, contents differ. */
                 if (s1_interned && s2_interned) return 0.0;
                 return (strcmp(s1, s2) == 0) ? 1.0 : 0.0;
             }
             /* Fallback for the rare cases (e.g. CALL on a side). */
             char *fs1 = get_node_string(node->left);
             char *fs2 = get_node_string(node->right);
             int res = (strcmp(fs1, fs2) == 0);
             free(fs1); free(fs2);
             return (double)res;
        }
        return evaluate_expression(node->left) == evaluate_expression(node->right);
    }

    case NK_DIFF: {
        int left_null = 0, right_null = 0;
        if (node->left  && nk_of(node->left)  == NK_NULL) left_null = 1;
        if (node->right && nk_of(node->right) == NK_NULL) right_null = 1;
        if (node->left  && nk_of(node->left)  == NK_IDENTIFIER) {
            Variable *v = find_variable(node->left->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) left_null = 1;
        }
        if (node->right && nk_of(node->right) == NK_IDENTIFIER) {
            Variable *v = find_variable(node->right->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) right_null = 1;
        }
        if (left_null || right_null) return (double)!(left_null && right_null);
        if (is_string_type(node->left) || is_string_type(node->right)) {
             /* Ola 3 Fase A: zero-alloc string compare (mirror of NK_EQ). */
             const char *s1 = NULL, *s2 = NULL;
             int s1_interned = 0, s2_interned = 0;
             if (node->left) {
                 NodeKind lk = nk_of(node->left);
                 if (lk == NK_STRING) { s1 = node->left->str_value; s1_interned = node->left->str_interned; }
                 else if (lk == NK_IDENTIFIER || lk == NK_ID) {
                     Variable *vv = find_variable(node->left->id);
                     if (vv && vv->vtype == VAL_STRING) s1 = vv->value.string_value;
                 }
             }
             if (node->right) {
                 NodeKind rk = nk_of(node->right);
                 if (rk == NK_STRING) { s2 = node->right->str_value; s2_interned = node->right->str_interned; }
                 else if (rk == NK_IDENTIFIER || rk == NK_ID) {
                     Variable *vv = find_variable(node->right->id);
                     if (vv && vv->vtype == VAL_STRING) s2 = vv->value.string_value;
                 }
             }
             if (s1 && s2) {
                 if (s1 == s2) return 0.0;
                 if (s1_interned && s2_interned) return 1.0;
                 return (strcmp(s1, s2) != 0) ? 1.0 : 0.0;
             }
             char *fs1 = get_node_string(node->left);
             char *fs2 = get_node_string(node->right);
             int res = (strcmp(fs1, fs2) != 0);
             free(fs1); free(fs2);
             return (double)res;
        }
        return evaluate_expression(node->left) != evaluate_expression(node->right);
    }

    case NK_AND:
        if (!evaluate_expression(node->left)) return 0;
        return evaluate_expression(node->right) ? 1 : 0;
    case NK_OR:
        if (evaluate_expression(node->left)) return 1;
        return evaluate_expression(node->right) ? 1 : 0;
    case NK_NOT:
        return evaluate_expression(node->left) ? 0 : 1;

    case NK_NULL_COALESCE: {
        ASTNode *l = node->left;
        int is_null = 0;
        if (l && nk_of(l) == NK_NULL) is_null = 1;
        else if (l && nk_of(l) == NK_IDENTIFIER) {
            Variable *v = find_variable(l->id);
            if (!v || (v->type && strcmp(v->type, "NULL") == 0)) is_null = 1;
        }
        if (is_null) return evaluate_expression(node->right);
        return evaluate_expression(l);
    }

    case NK_ADD:
        return evaluate_expression(node->left) + evaluate_expression(node->right);
    case NK_SUB:
        return evaluate_expression(node->left) - evaluate_expression(node->right);
    case NK_MUL:
        return evaluate_expression(node->left) * evaluate_expression(node->right);
    case NK_DIV: {
        double right = evaluate_expression(node->right);
        if (right == 0.0) {
            printf("Error: division by zero.\n");
            return 0;
        }
        return evaluate_expression(node->left) / right;
    }

    case NK_ACCESS_ATTR: {
        ASTNode *objRef = node->left;
        ASTNode *attr   = node->right;

        /* Fase 7: null-safe ?. — return 0 (null-as-number) if obj is null */
        if (node->value == 1 && objRef && nk_of(objRef) == NK_IDENTIFIER) {
            Variable *vv = find_variable(objRef->id);
            if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) return 0;
        }

        /* Fase 1a: arr.length sobre listas */
        if (attr && attr->id && strcmp(attr->id, "length") == 0) {
            ASTNode *list = resolve_to_list(objRef);
            if (list) return (double)list_length(list);
            ASTNode *map = resolve_to_map(objRef);
            if (map) return (double)map_length(map);
        }

        /* Bug fix: arr[i].attr — objRef es un ACCESS_EXPR (indexing),
         * no un identificador. Resolvemos el item de la lista a su
         * ObjectNode y saltamos directo al lookup de atributo.
         * Esto debe ir ANTES de find_variable porque o->id es NULL
         * en este caso (find_variable hace strcmp con NULL → crash). */
        if (objRef && objRef->type && nk_of(objRef) == NK_ACCESS_EXPR) {
            ASTNode *list = resolve_to_list(objRef->left);
            if (list && objRef->right) {
                int idx = (int)evaluate_expression(objRef->right);
                int len = list_length(list);
                if (idx < 0 || idx >= len) {
                    printf("Error: index %d out of range (length=%d).\n", idx, len);
                    return 0;
                }
                ASTNode *item = list_get_item(list, idx);
                ObjectNode *iobj = NULL;
                if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                    if (item->extra) iobj = (ObjectNode*)item->extra;
                    else iobj = (ObjectNode*)(intptr_t)item->value;
                }
                if (iobj && iobj->class) {
                    for (int i = 0; i < iobj->class->attr_count; i++) {
                        if (strcmp(iobj->class->attributes[i].id, attr->id) == 0) {
                            if (iobj->attributes[i].vtype == VAL_INT)
                                return iobj->attributes[i].value.int_value;
                            if (iobj->attributes[i].vtype == VAL_FLOAT)
                                return iobj->attributes[i].value.float_value;
                            return 0;
                        }
                    }
                    printf("Error: attribute '%s' not found in indexed object.\n", attr->id);
                    return 0;
                }
            }
            printf("Error: cannot resolve indexed expression for attribute access.\n");
            return 0;
        }
        Variable *v = find_variable(objRef->id);
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: '%s' is not a valid object to access an attribute.\n",
                   (objRef && objRef->id) ? objRef->id : "<expr>");
            return 0;
        }

        ObjectNode *obj = v->value.object_value;
        if (!obj) {
            ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
            if (wrapper && wrapper->extra) {
                obj = (ObjectNode *)wrapper->extra;
            }
        }

        if (!obj) {
            printf("Error: could not get the object to access the attribute.\n");
            return 0;
        }
        /* Ola 2: inline cache. If the cached class matches, jump directly to
         * the cached attribute index without scanning the attributes array. */
        {
            static int ic_init = 0;
            static int ic_enabled = 1;
            if (!ic_init) {
                const char *e = getenv("TYPEEASY_NO_IC");
                if (e && e[0] && e[0] != '0') ic_enabled = 0;
                ic_init = 1;
            }
            if (ic_enabled && node->cached_class == (void*)obj->class) {
                int idx = node->cached_attr_idx;
                if (idx >= 0 && idx < obj->class->attr_count) {
                    if (obj->attributes[idx].vtype == VAL_INT)
                        return obj->attributes[idx].value.int_value;
                    if (obj->attributes[idx].vtype == VAL_FLOAT)
                        return obj->attributes[idx].value.float_value;
                }
            }
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, attr->id) == 0) {
                    /* Cache the resolved (class, idx) for next time. */
                    if (ic_enabled) {
                        node->cached_class    = (void*)obj->class;
                        node->cached_attr_idx = i;
                    }
                    if(obj->attributes[i].vtype == VAL_INT)
                        return obj->attributes[i].value.int_value;
                    if(obj->attributes[i].vtype == VAL_FLOAT)
                        return obj->attributes[i].value.float_value;
                }
            }
        }

        printf("Error: attribute '%s' not found in object '%s'.\n", attr->id, objRef->id);
        return 0;
    }

    case NK_ACCESS_EXPR: {
        /* Map indexing m["key"] */
        ASTNode *map = resolve_to_map(node->left);
        if (map) {
            const char *key = NULL;
            if (node->right && nk_of(node->right) == NK_STRING) key = node->right->str_value;
            else if (node->right && (nk_of(node->right) == NK_IDENTIFIER || nk_of(node->right) == NK_ID)) {
                Variable *kv = find_variable(node->right->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            if (!key) {
                fprintf(stderr, "Error: Map key must be a string.\n");
                return 0;
            }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) {
                fprintf(stderr, "Error: key '%s' not found in Map.\n", key);
                return 0;
            }
            ASTNode *val = pair->left;
            if (val && nk_of(val) == NK_STRING) {
                fprintf(stderr, "Error: value at Map['%s'] is a string; use print/println.\n", key);
                return 0;
            }
            return evaluate_expression(val);
        }

        ASTNode *list = resolve_to_list(node->left);
        if (!list) {
            fprintf(stderr, "Error: indexed object is neither a list nor a Map.\n");
            return 0;
        }
        int idx = (int)evaluate_expression(node->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            fprintf(stderr, "Error: index %d out of range (length=%d).\n", idx, len);
            return 0;
        }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return 0;
        if (item->type && nk_of(item) == NK_STRING) {
            fprintf(stderr, "Error: item is a string; use print/println to display it.\n");
            return 0;
        }
        return evaluate_expression(item);
    }

    default:
        return (double)node->value;
    }
}

void call_method(ObjectNode *obj, char *method) {
    /* debug print removed */
    ASTNode *thisNode = (ASTNode *)calloc(1, sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->extra = (struct ASTNode*)obj; // store ObjectNode pointer in 'extra' (consistent with create_object_with_args)
    thisNode->value = 0;
    add_or_update_variable("this", thisNode);

    MethodNode *m = obj->class->methods;
    while (m) {
        if (strcmp(m->name, method) == 0) {
            /* method invocation traces removed */
            debugger_push_frame(m->name, NULL);
            interpret_ast(m->body);
            debugger_pop_frame();
            return;
        }
        m = m->next;
    }
    printf("Error: method '%s' not found in class '%s'.\n",
           method, obj->class->name);
}

void execute_predict(ASTNode* model_node, ASTNode* input_node) {
    if (!model_node || !input_node) {
        printf("Error: predict requires two arguments.\n");
        return;
    }
    Variable* model_var = find_variable(model_node->id);
    if (!model_var || model_var->vtype != VAL_OBJECT) {
        printf("Error: model '%s' not found or invalid.\n", model_node->id);
        return;
    }
    Variable* input_var = find_variable(input_node->id);
    if (!input_var) {
        printf("Error: input '%s' not found.\n", input_node->id);
        return;
    }
    /* predict debug logs removed */
    ASTNode *lit = create_ast_leaf_number("INT", 85, NULL, NULL);
    add_or_update_variable("__ret__", lit);
}

/* Prototipos de funciones auxiliares */
static void interpret_dataset(ASTNode *node);
static void interpret_for(ASTNode *node);
static void interpret_model_object(ASTNode *node);
static void interpret_train_node(ASTNode *node);
static void interpret_predict_node(ASTNode *node);
static void interpret_call_func(ASTNode *node);
static void interpret_return_node(ASTNode *node);
static void interpret_call_method(ASTNode *node);
static void interpret_call_method_alone(ASTNode *node);
static void interpret_var_decl(ASTNode *node);
static void interpret_assign_attr(ASTNode *node);
static void interpret_assign(ASTNode *node);
static void interpret_print(ASTNode *node);
static void interpret_println(ASTNode *node);
static void interpret_fprint(ASTNode *node);
static void interpret_fprintln(ASTNode *node);
static void interpret_statement_list(ASTNode *node);
double evaluate_number(ASTNode *node);
static void interpret_match(ASTNode *node);
// void interpret_if(ASTNode *node); // Ya en ast.h
// int evaluate_condition(ASTNode* condition); // Ya en ast.h

/* ─── DATASET ─────────────────────────────────────────────────────────── */
static void interpret_dataset(ASTNode *node) {
    add_or_update_variable(node->id, node);
    /* dataset load debug log removed */
}

static void interpret_filter_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *list_expr = node->left;
    ASTNode *lambda = node->right;
    ASTNode *list_node = NULL;
    ASTNode *result_list_items = NULL;
    if (list_expr->type && (strcmp(list_expr->type, "ID") == 0 || strcmp(list_expr->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(list_expr->id);
        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            printf("Error: '%s' is not a valid list for filter.\n", list_expr->id);
            return;
        }
        list_node = (ASTNode *)(intptr_t)v->value.object_value;
    } else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        list_node = list_expr;
    } else {
        printf("Error: unsupported expression for filter (type: %s).\n", list_expr->type);
        return;
    }
    if (!list_node || strcmp(list_node->type, "LIST") != 0) {
        printf("Error: El operando para filter no es una lista.\n");
        return;
    }
    ASTNode *current_item_node = list_node->left;
    while (current_item_node) {
        add_or_update_variable(lambda->id, current_item_node);
        if (evaluate_expression(lambda->left)) {
            /* Usar ->extra para el puntero ObjectNode completo en 64 bits.
             * ->value es int (32-bit truncation) y falla con heaps >2GB. */
            ObjectNode *original_obj = current_item_node->extra
                                       ? (ObjectNode *)current_item_node->extra
                                       : (ObjectNode *)(intptr_t)current_item_node->value;
            ObjectNode *cloned_obj = clone_object(original_obj);
            result_list_items = append_to_list(result_list_items, create_object_node(cloned_obj));
        }
        current_item_node = current_item_node->next;
    }
    add_or_update_variable("__ret__", result_list_items ? result_list_items : create_list_node(NULL));
}

void interpret_list_func_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *listNode = node->left;
    ASTNode *lambda = node->right;
    const char *func = node->id;
    if (strcmp(func, "filter") == 0) {
        ASTNode *result = NULL;
        ASTNode *item = listNode->left;
        while (item) {
            add_or_update_variable(lambda->id, item);
            int r = evaluate_expression(lambda->left);
            if (r) {
                if (!result) result = item;
                else append_to_list(result, item);
            }
            item = item->right;
        }
        add_or_update_variable("__ret__", create_list_node(result));
    }
}

ASTNode* create_for_in_node(const char *var_name, ASTNode *list_expr, ASTNode *body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type      = strdup("FOR_IN");
    node->id        = strdup(var_name);
    node->left      = list_expr;
    node->right     = body;
    node->next      = NULL;
    return node;
}



static void interpret_for_in(ASTNode *node) {
    if (!node->right) {
        /* for-in empty body (debug log removed) */
        return;
    }
    /* debug print removed */
    ASTNode *list_expr = node->left;
    ASTNode *listNode = NULL;
    if (list_expr->type && (
        strcmp(list_expr->type, "ID") == 0 ||
        strcmp(list_expr->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(list_expr->id);
        if (!v) {
            /* variable not found (debug log removed) */
            return;
        }       
        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            printf("Error: '%s' is not a valid list.\n", list_expr->id);
            return;
        }
        listNode = (ASTNode *)(intptr_t)v->value.object_value;
    }
    else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        listNode = list_expr;
    }
    else {
        printf("Error: unsupported for-in expression (type: %s).\n", list_expr->type);
        return;
    }
    if (!listNode || strcmp(listNode->type, "LIST") != 0) {
        printf("Error: node is not a valid list.\n");
        return;
    }
    ASTNode *items = listNode->left;   
    for (ASTNode *item = items; item; item = item->next) {
        if (item->type && strcmp(item->type, "OBJECT") == 0) {
            // Get ObjectNode from extra field (where create_object_with_args stores it)
            // For backward compatibility, also check value field for objects created differently
            ObjectNode *obj = (ObjectNode *)item->extra;
            if (!obj) {
                // Fallback for objects that might store pointer in value field
                obj = (ObjectNode *)(intptr_t)item->value;
            }
            if (strcmp(node->id, list_expr->id) != 0) {
                ASTNode *wrapper = calloc(1, sizeof(ASTNode));
                wrapper->type = strdup("OBJECT");
                wrapper->id = strdup(node->id);
                wrapper->left = wrapper->right = NULL;
                /* Store pointer in 'extra' to be consistent with create_object_with_args()/declare_variable */
                wrapper->extra = (struct ASTNode*)obj;
                wrapper->value = 0;
                /* debug print removed */
                add_or_update_variable(node->id, wrapper);
            } 
            /* debug print removed */
            interpret_ast(node->right);
        }
        if (break_flag) { break_flag = 0; break; }
        if (continue_flag) { continue_flag = 0; continue; }
        if (throw_flag || return_flag) break;
    }
}

ASTNode *create_object_node(ObjectNode *obj) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("OBJECT");
    node->value = (int)(intptr_t)obj; /* compat legacy: kept for scalar reads */
    node->extra = (struct ASTNode*)obj; /* full 64-bit pointer for 64-bit systems */
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

void interpret_bridge_decl(ASTNode *node) {
    char* bridge_name = node->id;
    ASTNode* call_node = node->left;
    if (call_node == NULL || strcmp(call_node->type, "CALL_METHOD") != 0) {
        fprintf(stderr, "Error: bridge declaration '%s' must be a method call.\n", bridge_name);
        return;
    }
    char* lib_name = call_node->left->id;
    char* func_name = call_node->id;
    if (g_debug_mode) {
        te_log_ast("Registering Bridge (simulated): '%s'", bridge_name);
        te_log_ast(" -> Target: Library '%s', Function '%s'", lib_name, func_name);
    }

    // Create a generic class for all bridges if it doesn't exist
    /* bridge class search debug log removed */
    ClassNode* bridge_class = find_class("Bridge");
    
    // --- INICIO DE LA CORRECCIÓN (V10) ---
    // Si no se encuentra, la creamos "perezosamente"
    if (bridge_class == NULL) {
        /* bridge class not found — creation log removed */
        bridge_class = create_class("Bridge");
        add_class(bridge_class);
        
        // (Ya no salimos con error)
        // fprintf(stderr, "Error fatal de arquitectura: ...\n");
        // exit(1);
    }
    // --- FIN DE LA CORRECCIÓN ---

    // Create an object for this specific bridge instance
    ObjectNode* bridge_obj = create_object(bridge_class);

    // Create a temporary ASTNode to wrap the object for the symbol table
    ASTNode* obj_node = create_ast_leaf("OBJECT", 0, NULL, bridge_name);    
    obj_node->extra = (struct ASTNode*)bridge_obj;
    // Add the bridge object to the variables table  
    add_or_update_variable(bridge_name, obj_node);
    free_ast(obj_node);
   
}

ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para BRIDGE node.\n");
        exit(1);
    }
    node->type = strdup("BRIDGE_DECL");
    node->id = strdup(name);
    node->left = call_expr_node;
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

void interpret_agent(ASTNode *agent_node) {
    if (g_debug_mode) te_log_ast("Agent parsed: %s (ignored in script mode)", agent_node->id);
}

ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error fatal: No se pudo asignar memoria para ACCESS_EXPR node.\n");
        exit(1);
    }
    node->type = strdup("ACCESS_EXPR");
    node->left = base;
    node->right = index_expr;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    node->next = NULL;
    node->extra = NULL;
    return node;
}

ASTNode *create_object_literal_node(ASTNode *kv_list) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("OBJECT_LITERAL");
    node->left = kv_list;
    node->right = NULL;
    return node;
}

ASTNode *create_kv_pair_node(char *key, ASTNode *value) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("KV_PAIR");
    /* Ola 15: intern map key for pointer-eq lookup fast-path. */
    if (key && g_intern_enabled) {
        node->id = (char*)tee_intern(key);
        node->id_interned = 1;
    } else {
        node->id = key ? strdup(key) : NULL;
        node->id_interned = 0;
    }
    node->left = value;
    node->right = NULL;
    return node;
}

ASTNode *create_state_decl_node(char *name, ASTNode *value_expr) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("STATE_DECL");
    node->id = strdup(name);
    node->left = value_expr;
    node->right = NULL;
    return node;
}

ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair) {
    if (!list) return pair;
    ASTNode *current = list;
    while (current->right) {
        current = current->right;
    }
    current->right = pair;
    return list;
}


// Forward declarations
static void interpret_return_node(ASTNode *node);
void print_object_as_xml_by_id(const char* id);
void print_object_as_json_by_id(const char* id);

void interpret_ast(ASTNode *node) {
    if (!node) return;
    if (return_flag) return;
    if (throw_flag) return;

    /* Debugger hook: only stop on "stoppable" statement-level nodes.
     * Cheap when g_debug_enabled == 0 (single load+test). */
    if (g_debug_enabled) {
        switch (nk_of(node)) {
            case NK_VAR_DECL:
            case NK_ASSIGN: case NK_ASSIGN_ATTR: case NK_INDEX_ASSIGN:
            case NK_IF: case NK_MATCH:
            case NK_FOR: case NK_FOR_IN: case NK_WHILE:
            case NK_BREAK: case NK_CONTINUE:
            case NK_RETURN: case NK_THROW: case NK_TRY_CATCH:
            case NK_PRINT: case NK_PRINTLN:
            case NK_FPRINT: case NK_FPRINTLN:
            case NK_CALL_FUNC: case NK_CALL_METHOD: case NK_METHOD_CALL_ALONE:
            case NK_RETURN_JSON: case NK_RETURN_XML:
                debugger_on_statement(node);
                break;
            default:
                break;
        }
    }

    /* Fase 1 (perf): single dispatch via cached NodeKind enum. */
    switch (nk_of(node)) {
    case NK_STATE_DECL:
        printf("[TypeEasy] 'state' '%s' tratado como 'var' en modo script.\n", node->id);
        interpret_var_decl(node);
        break;

    case NK_BRIDGE_DECL:
        interpret_bridge_decl(node);
        break;

    case NK_AGENT:
        interpret_agent(node);
        break;

    case NK_ACCESS_EXPR:
    case NK_OBJECT_LITERAL:
    case NK_KV_PAIR:
        /* expression-only nodes; nothing to do at statement level */
        break;

    case NK_AGENT_LIST:
        interpret_ast(node->left);
        interpret_ast(node->right);
        break;

    case NK_LISTENER:
        /* main puro ignora los listeners */
        break;

    case NK_FOR:        interpret_for(node); break;
    case NK_IF:         interpret_if(node); break;
    case NK_MATCH:      interpret_match(node); break;
    case NK_FOR_IN:     interpret_for_in(node); break;
    case NK_LIST_FUNC_CALL: interpret_list_func_call(node); break;
    case NK_FILTER_CALL:    interpret_filter_call(node); break;
    case NK_DATASET:    interpret_dataset(node); break;
    case NK_MODEL:
    case NK_OBJECT:
        interpret_model_object(node);
        break;
    case NK_TRAIN:      interpret_train_node(node); break;
    case NK_PREDICT:    interpret_predict_node(node); break;
    case NK_RETURN_JSON: native_json(node->left); break;

    case NK_RETURN_XML:
        if (node->left && node->left->id) {
            print_object_as_xml_by_id(node->left->id);
        } else {
            if (g_stdout_buffer) {
                ASTNode *result_node = create_ast_leaf("STRING", 0, g_stdout_buffer, NULL);
                add_or_update_variable("__ret__", result_node);
                free_ast(result_node);
            } else {
                ASTNode *result_node = create_ast_leaf("STRING", 0, "", NULL);
                add_or_update_variable("__ret__", result_node);
                free_ast(result_node);
            }
        }
        break;

    case NK_CALL_FUNC:    interpret_call_func(node); break;
    case NK_RETURN:       interpret_return_node(node); break;
    case NK_CALL_METHOD:  interpret_call_method(node); break;

    case NK_METHOD_CALL_ALONE: {
        MethodNode *m = global_methods;
        while (m) {
            if (strcmp(m->name, node->id) == 0) {
                debugger_push_frame(m->name, node);
                interpret_ast(m->body);
                debugger_pop_frame();
                break;
            }
            m = m->next;
        }
        break;
    }

    case NK_VAR_DECL:    interpret_var_decl(node); break;
    case NK_ASSIGN_ATTR: interpret_assign_attr(node); break;
    case NK_ASSIGN:      interpret_assign(node); break;

    case NK_INDEX_ASSIGN: {
        /* Fase 1b: arr[i] = x   |   Fase 1c: m["k"] = x */
        ASTNode *access = node->left;
        ASTNode *value  = node->right;
        if (!access || !access->left) break;

        ASTNode *map = resolve_to_map(access->left);
        if (map) {
            const char *key = NULL;
            if (access->right && nk_of(access->right) == NK_STRING) key = access->right->str_value;
            else if (access->right && (nk_of(access->right) == NK_IDENTIFIER || nk_of(access->right) == NK_ID)) {
                Variable *kv = find_variable(access->right->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            if (!key) { fprintf(stderr, "Error: Map key must be a string.\n"); break; }
            ASTNode *new_val = build_item_from_value(value);
            ASTNode *pair = map_find_pair(map, key);
            if (pair) {
                pair->left = new_val;  /* same key, value changed: hash entry still valid */
            } else {
                ASTNode *new_pair = create_kv_pair_node((char*)key, new_val);
                if (!map->left) {
                    map->left = new_pair;
                } else {
                    ASTNode *cur = map->left;
                    while (cur->right) cur = cur->right;
                    cur->right = new_pair;
                }
                te_invalidate_map_cache(map);  /* Ola 14: new key */
            }
            break;
        }

        ASTNode *list = resolve_to_list(access->left);
        if (!list) {
            fprintf(stderr, "Error: variable is neither a list nor a Map.\n");
            break;
        }
        int idx = (int)evaluate_expression(access->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            fprintf(stderr, "Error: index %d out of range (length=%d).\n", idx, len);
            break;
        }
        ASTNode *new_item = build_item_from_value(value);
        ASTNode *cur = list->left;
        ASTNode *prev = NULL;
        for (int k = 0; k < idx && cur; k++) { prev = cur; cur = cur->next; }
        new_item->next = cur ? cur->next : NULL;
        if (prev) prev->next = new_item; else list->left = new_item;
        te_invalidate_list_cache(list);  /* Ola 14: item replaced */
        break;
    }

    case NK_PRINT:    interpret_print(node); break;
    case NK_PRINTLN:  interpret_println(node); break;
    case NK_FPRINT:   interpret_fprint(node); break;
    case NK_FPRINTLN: interpret_fprintln(node); break;
    case NK_STATEMENT_LIST: interpret_statement_list(node); break;

    case NK_THROW: {
        ASTNode *e = node->left;
        char *msg = NULL;
        if (e && nk_of(e) == NK_STRING) msg = strdup(e->str_value ? e->str_value : "");
        else if (e && nk_of(e) == NK_IDENTIFIER) {
            Variable *v = find_variable(e->id);
            if (v && v->vtype == VAL_STRING) msg = strdup(v->value.string_value ? v->value.string_value : "");
            else if (v && v->vtype == VAL_INT) { char b[32]; snprintf(b,32,"%d",v->value.int_value); msg = strdup(b); }
            else msg = strdup("");
        } else if (e) {
            double d = evaluate_expression(e);
            char b[64]; if (d == (int)d) snprintf(b,64,"%d",(int)d); else snprintf(b,64,"%f",d);
            msg = strdup(b);
        } else msg = strdup("");
        if (throw_message) free(throw_message);
        throw_message = msg;
        throw_flag = 1;
        break;
    }

    case NK_TRY_CATCH: {
        ASTNode *try_body = node->left;
        ASTNode *catch_body = node->right;
        ASTNode *finally_body = node->extra;
        const char *err_var_name = node->id;

        interpret_ast(try_body);
        if (throw_flag && catch_body) {
            char *msg = throw_message ? strdup(throw_message) : strdup("");
            throw_flag = 0;
            if (throw_message) { free(throw_message); throw_message = NULL; }
            if (err_var_name) {
                ASTNode *lit = create_ast_leaf("STRING", 0, msg, NULL);
                add_or_update_variable((char*)err_var_name, lit);
            }
            free(msg);
            interpret_ast(catch_body);
        }
        if (finally_body) {
            int saved_throw = throw_flag;
            char *saved_msg = throw_message; throw_message = NULL; throw_flag = 0;
            interpret_ast(finally_body);
            if (!throw_flag && saved_throw) { throw_flag = 1; throw_message = saved_msg; }
            else if (saved_msg) free(saved_msg);
        }
        break;
    }

    case NK_WHILE:
        /* Fase 4: try compiled-bytecode loop. Only succeeds if the entire
         * body is numeric (assigns/ifs/whiles). Falls back to AST walker
         * for any non-trivial body. */
        {
            static int bc4_init = 0;
            static int bc4_enabled = 1;
            if (!bc4_init) {
                const char *e = getenv("TYPEEASY_NO_BC");
                if (e && e[0] && e[0] != '0') bc4_enabled = 0;
                bc4_init = 1;
            }
            if (bc4_enabled) {
                BCInfo *info = bc_get_or_compile_stmt(node);
                if (info) { bc_exec(info->code); break; }
            }
        }
        while (1) {
            if (throw_flag || return_flag) break;
            int cond = evaluate_condition(node->left);
            if (!cond) break;
            interpret_ast(node->right);
            if (break_flag) { break_flag = 0; break; }
            if (continue_flag) { continue_flag = 0; continue; }
            if (throw_flag || return_flag) break;
        }
        break;

    case NK_BREAK:    break_flag = 1; break;
    case NK_CONTINUE: continue_flag = 1; break;

    case NK_PLOT: {
        double values[100];
        int count = 0;
        ASTNode *child = node->left;
        while (child != NULL && count < 100) {
            values[count++] = evaluate_number(child);
            child = child->next;
        }
        generate_plot(values, count);
        break;
    }

    default:
        /* unknown / unhandled node type: silent no-op */
        break;
    }
}


double evaluate_number(ASTNode *node) {
    if (node == NULL) return 0;
    if (strcmp(node->type, "NUMBER") == 0) return node->value;
    if (strcmp(node->type, "FLOAT") == 0) {
        return atof(node->str_value);
    }
    if (strcmp(node->type, "EXPRESSION") == 0) {
        return evaluate_number(node->left); // simplificado
    }
    fprintf(stderr, "Error: Tipo no soportado en evaluate_number: %s\n", node->type);
    return 0;
}

void interpret_if(ASTNode *node) {
    if (!node || !node->left) return;
    int result = evaluate_condition(node->left);
    if (result) {
        interpret_ast(node->right);
    } else if (node->next) {
        interpret_ast(node->next);
    }
}

void interpret_match(ASTNode *node) {
    /* interpret_match debug logs removed */
    if (!node || !node->left || !node->right) return;
    char* match_value = get_node_string(node->left);

    ASTNode* case_node = node->right;
    while (case_node) {
        if (strcmp(case_node->type, "CASE") == 0) {
            char* case_value = get_node_string(case_node->left);
            if (strcmp(match_value, case_value) == 0) {
                free(case_value);
                interpret_ast(case_node->right);
                break;
            }
            free(case_value);
        }
        case_node = case_node->next;
    }
    free(match_value);
}



int evaluate_condition(ASTNode* condition) {
    if (!condition) return 0;
    // Delegate to evaluate_expression which now supports string comparisons
    return (int)evaluate_expression(condition);
}

void generate_plot(double *values, int count) {
    FILE *fp = fopen("plot_data.txt", "w");
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %f\n", i, values[i]);
    }
    fclose(fp);
    FILE *gnuplot = popen("gnuplot -persistent", "w");
    fprintf(gnuplot, "set title 'Gráfico generado por TypeEasy'\n");
    fprintf(gnuplot, "plot 'plot_data.txt' with linespoints\n");
    pclose(gnuplot);
}


static void interpret_for(ASTNode *node) {
    add_or_update_variable(node->id, node->left);
    Variable *var = find_variable(node->id);
    if (!var || var->vtype != VAL_INT) {
        printf("Error: invalid control variable in FOR.\n");
        return;
    }

    /* Ola 1 (perf): try compiled-bytecode FOR. Fall back to AST walker if any
     * statement in the body is not bytecode-compilable. */
    {
        static int bc_for_init = 0;
        static int bc_for_enabled = 1;
        if (!bc_for_init) {
            const char *e = getenv("TYPEEASY_NO_BC");
            if (e && e[0] && e[0] != '0') bc_for_enabled = 0;
            bc_for_init = 1;
        }
        if (bc_for_enabled) {
            BCInfo *info = bc_get_or_compile_stmt(node);
            if (info) { bc_exec(info->code); return; }
        }
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
            if (throw_flag || return_flag || break_flag || continue_flag) break;
            stmt = stmt->right;
        }
        if (break_flag) { break_flag = 0; break; }
        if (continue_flag) { continue_flag = 0; }
        if (throw_flag || return_flag) break;
        var->value.int_value += incremento;
    }
}

static void interpret_model_object(ASTNode *node) {
    add_or_update_variable(node->id, node);
}

static void interpret_train_node(ASTNode *node) {
    int epochs = 1;
    ASTNode *opt = node->right->right;
    if (opt && strcmp(opt->id, "epochs") == 0) {
        epochs = opt->value;
    }   
    (void)epochs; // Suprime el warning de 'unused variable'
}

static void interpret_predict_node(ASTNode *node) {
    execute_predict(node->left, node->right);
}

/* ==========================================================================
 * Ola 13 — Stdlib aditiva: builtins free-standing.
 *   Devuelven valor v\u00eda __ret__ (igual que los m\u00e9todos existentes).
 *   Hooked al inicio de interpret_call_func.  No modifican Variable.value
 *   ni el AST de listas/maps existentes; son puramente aditivos.
 * ========================================================================*/
static int te_builtin_dispatch(ASTNode *node) {
    if (!node || !node->id) return 0;
    const char *fn = node->id;
    ASTNode *a0 = node->left;
    ASTNode *a1 = a0 ? a0->right : NULL;

    /* ---- len(x): string | list | map ---- */
    if (strcmp(fn, "len") == 0) {
        int n = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type, "STRING") == 0) {
                n = (int)strlen(a0->str_value ? a0->str_value : "");
            } else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *v = find_variable(a0->id);
                if (v) {
                    if (v->vtype == VAL_STRING) n = (int)strlen(v->value.string_value ? v->value.string_value : "");
                    else if (v->type && (strcmp(v->type,"LIST")==0 || strcmp(v->type,"MAP")==0)) {
                        ASTNode *root = (ASTNode*)(intptr_t)v->value.object_value;
                        if (root) {
                            ASTNode *cur = root->left;
                            if (strcmp(v->type,"LIST")==0) { while (cur) { n++; cur = cur->next; } }
                            else { while (cur) { n++; cur = cur->right; } }
                        }
                    } else if (v->vtype == VAL_INT) n = v->value.int_value;
                }
            } else {
                /* fallback: number length is just its int value semantics */
                n = (int)evaluate_expression(a0);
            }
        }
        ASTNode *r = create_ast_leaf_number("INT", n, NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ---- range(n) / range(start, end) / range(start, end, step) ---- */
    if (strcmp(fn, "range") == 0) {
        int start = 0, end = 0, step = 1;
        if (a0 && !a1) { end = (int)evaluate_expression(a0); }
        else if (a0 && a1) {
            start = (int)evaluate_expression(a0);
            end   = (int)evaluate_expression(a1);
            if (a1->right) step = (int)evaluate_expression(a1->right);
        }
        if (step == 0) step = 1;
        ASTNode *list = create_list_node(NULL);
        ASTNode *tail = NULL;
        for (int i = start; (step > 0 ? i < end : i > end); i += step) {
            ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
            item->type = strdup("NUMBER");
            item->value = i;
            item->next = NULL;
            if (!list->left) list->left = item;
            else tail->next = item;
            tail = item;
        }
        add_or_update_variable("__ret__", list);
        return 1;
    }

    /* ---- read_file(path) -> string  /  write_file(path, content) -> int ---- */
    if (strcmp(fn, "read_file") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        char *out = NULL;
        if (path) {
            FILE *fp = fopen(path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (sz < 0) sz = 0;
                out = (char*)malloc((size_t)sz + 1);
                if (out) {
                    size_t r = fread(out, 1, (size_t)sz, fp);
                    out[r] = 0;
                }
                fclose(fp);
            }
            free(path);
        }
        ASTNode *r = create_ast_leaf("STRING", 0, out ? out : "", NULL);
        if (out) free(out);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "write_file") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        char *content = a1 ? get_node_string(a1) : NULL;
        int ok = 0;
        if (path && content) {
            FILE *fp = fopen(path, "wb");
            if (fp) {
                size_t L = strlen(content);
                ok = (fwrite(content, 1, L, fp) == L) ? 1 : 0;
                fclose(fp);
            }
        }
        if (path) free(path);
        if (content) free(content);
        ASTNode *r = create_ast_leaf_number("INT", ok, NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "file_exists") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        int ok = 0;
        if (path) { FILE *fp = fopen(path, "rb"); if (fp) { ok = 1; fclose(fp); } free(path); }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
        return 1;
    }

    /* ---- type conversions: to_int, to_str, to_float ---- */
    if (strcmp(fn, "to_int") == 0) {
        int v = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type,"STRING")==0) v = atoi(a0->str_value ? a0->str_value : "0");
            else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *vv = find_variable(a0->id);
                if (vv) {
                    if (vv->vtype == VAL_STRING) v = atoi(vv->value.string_value ? vv->value.string_value : "0");
                    else if (vv->vtype == VAL_FLOAT) v = (int)vv->value.float_value;
                    else v = vv->value.int_value;
                }
            } else v = (int)evaluate_expression(a0);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", v, NULL, NULL));
        return 1;
    }
    if (strcmp(fn, "to_float") == 0) {
        double v = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type,"STRING")==0) v = atof(a0->str_value ? a0->str_value : "0");
            else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *vv = find_variable(a0->id);
                if (vv) {
                    if (vv->vtype == VAL_STRING) v = atof(vv->value.string_value ? vv->value.string_value : "0");
                    else if (vv->vtype == VAL_FLOAT) v = vv->value.float_value;
                    else v = (double)vv->value.int_value;
                }
            } else v = evaluate_expression(a0);
        }
        char buf[64]; snprintf(buf, 64, "%f", v);
        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        return 1;
    }
    if (strcmp(fn, "to_str") == 0) {
        char *s = a0 ? get_node_string(a0) : strdup("");
        ASTNode *r = create_ast_leaf("STRING", 0, s ? s : "", NULL);
        if (s) free(s);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ---- print_err(msg) -> writes to stderr (returns 0) ---- */
    if (strcmp(fn, "print_err") == 0) {
        char *s = a0 ? get_node_string(a0) : strdup("");
        fprintf(stderr, "%s\n", s ? s : "");
        if (s) free(s);
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", 0, NULL, NULL));
        return 1;
    }

    /* ---- abs(x), min(a,b), max(a,b): top-level convenience ---- */
    if (strcmp(fn, "abs") == 0) {
        double v = a0 ? evaluate_expression(a0) : 0;
        if (v == (int)v) add_or_update_variable("__ret__", create_ast_leaf_number("INT", abs((int)v), NULL, NULL));
        else { char buf[64]; snprintf(buf, 64, "%f", v < 0 ? -v : v); add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL)); }
        return 1;
    }
    if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
        double va = a0 ? evaluate_expression(a0) : 0;
        double vb = a1 ? evaluate_expression(a1) : va;
        double r = (strcmp(fn,"min")==0) ? (va < vb ? va : vb) : (va > vb ? va : vb);
        if (r == (int)r) add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)r, NULL, NULL));
        else { char buf[64]; snprintf(buf, 64, "%f", r); add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL)); }
        return 1;
    }

    return 0;
}

static void interpret_call_func(ASTNode *node) {
    if (te_builtin_dispatch(node)) return;

    if (strcmp(node->id, "json") == 0) {
        native_json(node->left);
        return;
    }

    // Try native functions (orm_query, mysql_*, etc.)
    if (call_native_function(node->id, node->left)) {
        return;
    }

    // If not native, check for global user functions
    MethodNode *m = global_methods;
    while (m) {
        if (strcmp(m->name, node->id) == 0) {
             // Setup arguments and execute body
             // Note: This is a simplified handling. Ideally we should share logic with interpret_call_method
             // For now, we assume global functions are mostly native or handled via METHOD_CALL_ALONE
             // But if we are here, it means it was parsed as CALL_FUNC (e.g. inside an expression)
             
             // Create a new scope/context if needed?
             // For embedded, we just execute the body.
             // But we need to map arguments.
             // Since we don't have argument mapping logic ready here, and orm_query is native,
             // we just print an error if it's not found.
             // If the user defines a global function and calls it inside an expression, it might fail here.
             // But our current goal is to fix orm_query.
             break;
        }
        m = m->next;
    }
    
    if (m) {
        // Found user function, but execution logic is missing here.
        // We rely on the fact that most user functions are called as statements (METHOD_CALL_ALONE).
        // If called as expression, we might need to implement this.
        // For now, let's assume it's not needed for orm_query.
         printf("Warning: Calling user function '%s' as expression is not fully supported yet.\n", node->id);
    } else {
        printf("Error: function '%s' not defined.\n", node->id);
    }
}

static void interpret_call_method(ASTNode *node) {
    ASTNode *objNode = node->left;
    Variable *v = (objNode && objNode->id) ? find_variable(objNode->id) : NULL;
    return_flag = 0;
    return_node = NULL;

    /* Fase 7: null-safe ?. — if obj is null, set __ret__ = null and return */
    if (node->value == 1) {
        if (!v || (v->type && strcmp(v->type, "NULL") == 0)) {
            ASTNode *r = create_ast_leaf("NULL", 0, NULL, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
    }

    /* Fase 8: Math.* — pseudo-static methods on identifier "Math" */
    if (objNode && objNode->id && strcmp(objNode->id, "Math") == 0) {
        ASTNode *arg = node->right;
        double a = arg ? evaluate_expression(arg) : 0;
        double b = (arg && arg->right) ? evaluate_expression(arg->right) : 0;
        double res = 0;
        const char *m = node->id;
        if      (strcmp(m, "sqrt") == 0)  res = sqrt(a);
        else if (strcmp(m, "abs") == 0)   res = fabs(a);
        else if (strcmp(m, "floor") == 0) res = floor(a);
        else if (strcmp(m, "ceil") == 0)  res = ceil(a);
        else if (strcmp(m, "round") == 0) res = floor(a + 0.5);
        else if (strcmp(m, "pow") == 0)   res = pow(a, b);
        else if (strcmp(m, "min") == 0)   res = (a < b) ? a : b;
        else if (strcmp(m, "max") == 0)   res = (a > b) ? a : b;
        else if (strcmp(m, "log") == 0)   res = log(a);
        else if (strcmp(m, "log2") == 0)  res = log2(a);
        else if (strcmp(m, "log10") == 0) res = log10(a);
        else if (strcmp(m, "exp") == 0)   res = exp(a);
        else if (strcmp(m, "sin") == 0)   res = sin(a);
        else if (strcmp(m, "cos") == 0)   res = cos(a);
        else if (strcmp(m, "tan") == 0)   res = tan(a);
        else if (strcmp(m, "asin") == 0)  res = asin(a);
        else if (strcmp(m, "acos") == 0)  res = acos(a);
        else if (strcmp(m, "atan") == 0)  res = atan(a);
        else if (strcmp(m, "atan2") == 0) res = atan2(a, b);
        else if (strcmp(m, "sinh") == 0)  res = sinh(a);
        else if (strcmp(m, "cosh") == 0)  res = cosh(a);
        else if (strcmp(m, "tanh") == 0)  res = tanh(a);
        else if (strcmp(m, "sign") == 0)  res = (a > 0) - (a < 0);
        else if (strcmp(m, "trunc") == 0) res = (double)(long long)a;
        else if (strcmp(m, "mod") == 0)   res = fmod(a, b);
        else if (strcmp(m, "PI") == 0)    res = 3.14159265358979323846;
        else if (strcmp(m, "E") == 0)     res = 2.71828182845904523536;
        else { printf("Error: Math.%s not supported.\n", m); return; }
        ASTNode *r;
        if (res == (int)res) r = create_ast_leaf_number("INT", (int)res, NULL, NULL);
        else { char buf[64]; snprintf(buf, 64, "%f", res); r = create_ast_leaf("FLOAT", 0, buf, NULL); }
        add_or_update_variable("__ret__", r);
        return;
    }

    /* Fase 8: string methods .upper/.lower/.trim/.contains/.split */
    const char *raw_str_lit = NULL;
    if (objNode && objNode->type && strcmp(objNode->type, "STRING") == 0) {
        raw_str_lit = objNode->str_value ? objNode->str_value : "";
    }
    if ((raw_str_lit || (v && v->vtype == VAL_STRING)) && node->id) {
        const char *m = node->id;
        const char *s = raw_str_lit ? raw_str_lit : (v->value.string_value ? v->value.string_value : "");
        if (strcmp(m, "upper") == 0) {
            char *out = strdup(s);
            for (char *p = out; *p; p++) *p = toupper((unsigned char)*p);
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "lower") == 0) {
            char *out = strdup(s);
            for (char *p = out; *p; p++) *p = tolower((unsigned char)*p);
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "trim") == 0) {
            const char *start = s;
            while (*start && isspace((unsigned char)*start)) start++;
            const char *end = s + strlen(s);
            while (end > start && isspace((unsigned char)*(end - 1))) end--;
            char *out = strndup(start, end - start);
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "contains") == 0) {
            const char *needle = NULL;
            ASTNode *arg = node->right;
            char *tmp = NULL;
            if (arg && arg->type && strcmp(arg->type, "STRING") == 0) needle = arg->str_value;
            else if (arg) { tmp = get_node_string(arg); needle = tmp; }
            int found = (needle && strstr(s, needle)) ? 1 : 0;
            if (tmp) free(tmp);
            ASTNode *r = create_ast_leaf_number("INT", found, NULL, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "split") == 0) {
            const char *sep = NULL;
            ASTNode *arg = node->right;
            char *tmp = NULL;
            if (arg && arg->type && strcmp(arg->type, "STRING") == 0) sep = arg->str_value;
            else if (arg) { tmp = get_node_string(arg); sep = tmp; }
            if (!sep || !*sep) sep = " ";
            ASTNode *list = create_list_node(NULL);
            const char *cur = s;
            size_t seplen = strlen(sep);
            while (1) {
                const char *next = strstr(cur, sep);
                size_t partlen = next ? (size_t)(next - cur) : strlen(cur);
                char *part = strndup(cur, partlen);
                ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
                memset(item, 0, sizeof(ASTNode));
                item->type = strdup("STRING");
                item->str_value = part;
                if (!list->left) list->left = item;
                else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
                if (!next) break;
                cur = next + seplen;
            }
            if (tmp) free(tmp);
            add_or_update_variable("__ret__", list);
            return;
        }
        if (strcmp(m, "length") == 0) {
            ASTNode *r = create_ast_leaf_number("INT", (int)strlen(s), NULL, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        /* ---- Ola 13: replace, substr, find, starts_with, ends_with, repeat, parse_int, parse_float ---- */
        if (strcmp(m, "replace") == 0) {
            ASTNode *arg = node->right;
            ASTNode *arg2 = arg ? arg->right : NULL;
            char *t1 = arg  ? get_node_string(arg)  : NULL;
            char *t2 = arg2 ? get_node_string(arg2) : NULL;
            const char *needle = t1 ? t1 : "";
            const char *repl   = t2 ? t2 : "";
            size_t nl = strlen(needle), rl = strlen(repl);
            if (nl == 0) {
                ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
                add_or_update_variable("__ret__", r);
            } else {
                /* count occurrences */
                int count = 0; const char *p = s;
                while ((p = strstr(p, needle)) != NULL) { count++; p += nl; }
                size_t out_len = strlen(s) + count * (rl > nl ? rl - nl : 0);
                char *out = (char*)malloc(out_len + 1);
                char *o = out; const char *cur = s;
                while (1) {
                    const char *next = strstr(cur, needle);
                    if (!next) { strcpy(o, cur); break; }
                    size_t pre = (size_t)(next - cur);
                    memcpy(o, cur, pre); o += pre;
                    memcpy(o, repl, rl); o += rl;
                    cur = next + nl;
                }
                ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
                free(out);
                add_or_update_variable("__ret__", r);
            }
            if (t1) free(t1);
            if (t2) free(t2);
            return;
        }
        if (strcmp(m, "substr") == 0) {
            ASTNode *arg = node->right;
            ASTNode *arg2 = arg ? arg->right : NULL;
            int start = arg  ? (int)evaluate_expression(arg)  : 0;
            int slen = (int)strlen(s);
            int len  = arg2 ? (int)evaluate_expression(arg2) : slen - start;
            if (start < 0) start = 0;
            if (start > slen) start = slen;
            if (len < 0) len = 0;
            if (start + len > slen) len = slen - start;
            char *out = strndup(s + start, len);
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            free(out);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "find") == 0) {
            ASTNode *arg = node->right;
            char *t = arg ? get_node_string(arg) : NULL;
            int idx = -1;
            if (t) {
                const char *p = strstr(s, t);
                if (p) idx = (int)(p - s);
                free(t);
            }
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", idx, NULL, NULL));
            return;
        }
        if (strcmp(m, "starts_with") == 0) {
            ASTNode *arg = node->right;
            char *t = arg ? get_node_string(arg) : NULL;
            int ok = 0;
            if (t) { ok = (strncmp(s, t, strlen(t)) == 0) ? 1 : 0; free(t); }
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
            return;
        }
        if (strcmp(m, "ends_with") == 0) {
            ASTNode *arg = node->right;
            char *t = arg ? get_node_string(arg) : NULL;
            int ok = 0;
            if (t) {
                size_t sl = strlen(s), tl = strlen(t);
                ok = (tl <= sl && memcmp(s + sl - tl, t, tl) == 0) ? 1 : 0;
                free(t);
            }
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
            return;
        }
        if (strcmp(m, "repeat") == 0) {
            ASTNode *arg = node->right;
            int n = arg ? (int)evaluate_expression(arg) : 0;
            if (n < 0) n = 0;
            size_t sl = strlen(s);
            char *out = (char*)malloc(sl * (size_t)n + 1);
            for (int i = 0; i < n; i++) memcpy(out + i * sl, s, sl);
            out[sl * n] = 0;
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            free(out);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "parse_int") == 0) {
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", atoi(s), NULL, NULL));
            return;
        }
        if (strcmp(m, "parse_float") == 0) {
            char buf[64]; snprintf(buf, 64, "%f", atof(s));
            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
            return;
        }
        if (strcmp(m, "char_at") == 0) {
            ASTNode *arg = node->right;
            int idx = arg ? (int)evaluate_expression(arg) : 0;
            int sl = (int)strlen(s);
            char buf[2] = {0, 0};
            if (idx >= 0 && idx < sl) buf[0] = s[idx];
            ASTNode *r = create_ast_leaf("STRING", 0, buf, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
        if (strcmp(m, "char_code") == 0) {
            ASTNode *arg = node->right;
            int idx = arg ? (int)evaluate_expression(arg) : 0;
            int sl = (int)strlen(s);
            int c = (idx >= 0 && idx < sl) ? (unsigned char)s[idx] : 0;
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", c, NULL, NULL));
            return;
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }

    /* Fase 1b: métodos built-in en LIST: push, pop */
    if (v && v->type && strcmp(v->type, "LIST") == 0) {
        ASTNode *list = (ASTNode*)(intptr_t)v->value.object_value;
        if (list && node->id && strcmp(node->id, "push") == 0) {
            ASTNode *arg = node->right;
            if (!arg) return;
            ASTNode *new_item = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(new_item, 0, sizeof(ASTNode));
            if (arg->type && strcmp(arg->type, "OBJECT") == 0) {
                /* Fix: empujar objetos creados con `new ClaseX(args)`.
                 * El parser eager-construye un OBJECT ASTNode con extra=ObjectNode*
                 * y left=args, pero no corre el constructor. Para que cada push
                 * en un loop produzca instancias independientes, clonamos el
                 * objeto y corremos el constructor con los args reevaluados. */
                ObjectNode *obj_orig = NULL;
                if (arg->extra) obj_orig = (ObjectNode*)arg->extra;
                else obj_orig = (ObjectNode*)(intptr_t)arg->value;
                if (obj_orig && obj_orig->class) {
                    ObjectNode *obj_clone = clone_object(obj_orig);
                    MethodNode *m = obj_clone->class->methods;
                    while (m && strcmp(m->name, "__constructor") != 0) m = m->next;
                    if (m) {
                        ParameterNode *p = m->params;
                        ASTNode *carg = arg->left;
                        while (p && carg) {
                            ASTNode *vn = NULL;
                            if (carg->type && strcmp(carg->type, "STRING") == 0) {
                                vn = create_ast_leaf("STRING", 0, carg->str_value, NULL);
                            } else if (carg->type && (strcmp(carg->type, "ID") == 0 || strcmp(carg->type, "IDENTIFIER") == 0)) {
                                Variable *vv = find_variable(carg->id);
                                if (vv && vv->vtype == VAL_STRING) {
                                    vn = create_ast_leaf("STRING", 0, strdup(vv->value.string_value), NULL);
                                } else if (vv) {
                                    vn = create_ast_leaf_number("INT", vv->value.int_value, NULL, NULL);
                                } else {
                                    vn = create_ast_leaf_number("INT", 0, NULL, NULL);
                                }
                            } else {
                                int val = (int)evaluate_expression(carg);
                                vn = create_ast_leaf_number("INT", val, NULL, NULL);
                            }
                            add_or_update_variable(p->name, vn);
                            p = p->next;
                            carg = carg->right;
                        }
                        call_method(obj_clone, "__constructor");
                        return_flag = 0;
                        return_node = NULL;
                    }
                    new_item->type = strdup("OBJECT");
                    new_item->extra = (struct ASTNode*)obj_clone;
                    new_item->value = (int)(intptr_t)obj_clone;
                } else {
                    /* Sin clase resoluble: degradar a NUMBER 0 para no segfaultear */
                    new_item->type = strdup("NUMBER");
                    new_item->value = 0;
                }
            } else if (arg->type && strcmp(arg->type, "STRING") == 0) {
                new_item->type = strdup("STRING");
                new_item->str_value = strdup(arg->str_value);
            } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
                Variable *av = find_variable(arg->id);
                if (av && av->vtype == VAL_OBJECT && av->value.object_value) {
                    /* Push de variable que contiene un objeto: compartir referencia
                     * (sin clonar — semántica de referencia, igual que asignación). */
                    new_item->type = strdup("OBJECT");
                    new_item->extra = (struct ASTNode*)av->value.object_value;
                    new_item->value = (int)(intptr_t)av->value.object_value;
                } else if (av && av->vtype == VAL_STRING) {
                    new_item->type = strdup("STRING");
                    new_item->str_value = strdup(av->value.string_value);
                } else if (av && av->vtype == VAL_FLOAT) {
                    new_item->type = strdup("FLOAT");
                    char buf[64]; snprintf(buf, 64, "%f", av->value.float_value);
                    new_item->str_value = strdup(buf);
                } else if (av) {
                    new_item->type = strdup("NUMBER");
                    new_item->value = av->value.int_value;
                }
            } else {
                double vv = evaluate_expression(arg);
                if (vv == (int)vv) { new_item->type = strdup("NUMBER"); new_item->value = (int)vv; }
                else { new_item->type = strdup("FLOAT"); char buf[64]; snprintf(buf, 64, "%f", vv); new_item->str_value = strdup(buf); }
            }
            new_item->next = NULL;
            te_list_append(list, new_item);   /* Ola 14b: O(1) amortizado */
            return;
        }
        if (list && node->id && strcmp(node->id, "pop") == 0) {
            ASTNode *cur = list->left;
            if (!cur) return;
            if (!cur->next) { list->left = NULL; te_invalidate_list_cache(list); return; }
            while (cur->next && cur->next->next) cur = cur->next;
            cur->next = NULL;
            te_invalidate_list_cache(list);  /* Ola 14 */
            return;
        }
        /* ---- Ola 13: list extras: size/length, contains, reverse, sort, get ---- */
        if (list && node->id && (strcmp(node->id, "size") == 0 || strcmp(node->id, "length") == 0)) {
            int n = list_length(list);  /* uses cache */
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", n, NULL, NULL));
            return;
        }
        if (list && node->id && strcmp(node->id, "contains") == 0) {
            ASTNode *arg = node->right;
            int found = 0;
            if (arg) {
                ASTNode *probe = build_item_from_value(arg);
                /* compare as string if string, else as number */
                ASTNode *cur = list->left;
                while (cur) {
                    if (cur->type && probe->type && strcmp(cur->type, probe->type) == 0) {
                        if (strcmp(cur->type, "STRING") == 0) {
                            if (cur->str_value && probe->str_value && strcmp(cur->str_value, probe->str_value) == 0) { found = 1; break; }
                        } else {
                            if (cur->value == probe->value) { found = 1; break; }
                        }
                    } else {
                        /* loose compare via numeric */
                        double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                        double pv = probe->str_value ? atof(probe->str_value) : (double)probe->value;
                        if (cv == pv) { found = 1; break; }
                    }
                    cur = cur->next;
                }
                if (probe->type) free(probe->type);
                if (probe->str_value) free(probe->str_value);
                free(probe);
            }
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", found, NULL, NULL));
            return;
        }
        if (list && node->id && strcmp(node->id, "reverse") == 0) {
            ASTNode *prev = NULL, *cur = list->left, *nxt = NULL;
            while (cur) { nxt = cur->next; cur->next = prev; prev = cur; cur = nxt; }
            list->left = prev;
            te_invalidate_list_cache(list);  /* Ola 14 */
            return;
        }
        if (list && node->id && strcmp(node->id, "sort") == 0) {
            /* Simple insertion sort on the linked list (numeric or string). OK for small lists.
             * For benchmark needs, replace with merge sort later. */
            int is_str = 0;
            if (list->left && list->left->type && strcmp(list->left->type, "STRING") == 0) is_str = 1;
            ASTNode *sorted = NULL;
            ASTNode *cur = list->left;
            while (cur) {
                ASTNode *nxt = cur->next;
                cur->next = NULL;
                if (!sorted) { sorted = cur; }
                else {
                    int cmp_into_head;
                    if (is_str) cmp_into_head = strcmp(cur->str_value ? cur->str_value : "", sorted->str_value ? sorted->str_value : "") < 0;
                    else {
                        double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                        double sv = sorted->str_value ? atof(sorted->str_value) : (double)sorted->value;
                        cmp_into_head = (cv < sv);
                    }
                    if (cmp_into_head) { cur->next = sorted; sorted = cur; }
                    else {
                        ASTNode *p = sorted;
                        while (p->next) {
                            int cmp_next;
                            if (is_str) cmp_next = strcmp(cur->str_value ? cur->str_value : "", p->next->str_value ? p->next->str_value : "") < 0;
                            else {
                                double cv = cur->str_value ? atof(cur->str_value) : (double)cur->value;
                                double sv = p->next->str_value ? atof(p->next->str_value) : (double)p->next->value;
                                cmp_next = (cv < sv);
                            }
                            if (cmp_next) break;
                            p = p->next;
                        }
                        cur->next = p->next;
                        p->next = cur;
                    }
                }
                cur = nxt;
            }
            list->left = sorted;
            te_invalidate_list_cache(list);  /* Ola 14 */
            return;
        }
        if (list && node->id && strcmp(node->id, "get") == 0) {
            ASTNode *arg = node->right;
            int idx = arg ? (int)evaluate_expression(arg) : 0;
            ASTNode *cur = list_get_item(list, idx);  /* Ola 14: O(1) */
            if (cur) { add_or_update_variable("__ret__", build_item_from_value(cur)); }
            else { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); }
            return;
        }
        if (list && node->id && strcmp(node->id, "join") == 0) {
            ASTNode *arg = node->right;
            char *sep = arg ? get_node_string(arg) : strdup("");
            size_t cap = 64; size_t len = 0;
            char *out = (char*)malloc(cap);
            out[0] = 0;
            ASTNode *cur = list->left;
            int first = 1;
            while (cur) {
                char *piece = get_node_string(cur);
                size_t pl = piece ? strlen(piece) : 0;
                size_t sl = sep ? strlen(sep) : 0;
                size_t need = len + pl + (first ? 0 : sl) + 1;
                if (need > cap) { while (cap < need) cap *= 2; out = (char*)realloc(out, cap); }
                if (!first && sep) { memcpy(out + len, sep, sl); len += sl; }
                if (piece) { memcpy(out + len, piece, pl); len += pl; free(piece); }
                out[len] = 0;
                first = 0;
                cur = cur->next;
            }
            ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
            free(out);
            if (sep) free(sep);
            add_or_update_variable("__ret__", r);
            return;
        }
    }

    /* Fase 1c: métodos built-in en MAP: keys, values, has, remove */
    if (v && v->type && strcmp(v->type, "MAP") == 0) {
        ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
        if (map && node->id && strcmp(node->id, "keys") == 0) {
            ASTNode *list = create_list_node(NULL);
            ASTNode *cur = map->left;
            while (cur) {
                ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
                memset(item, 0, sizeof(ASTNode));
                item->type = strdup("STRING");
                item->str_value = strdup(cur->id ? cur->id : "");
                if (!list->left) list->left = item;
                else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
                cur = cur->right;
            }
            add_or_update_variable("__ret__", list);
            return;
        }
        if (map && node->id && strcmp(node->id, "values") == 0) {
            ASTNode *list = create_list_node(NULL);
            ASTNode *cur = map->left;
            while (cur) {
                ASTNode *src = cur->left;
                ASTNode *item = build_item_from_value(src);
                if (!list->left) list->left = item;
                else { ASTNode *t = list->left; while (t->next) t = t->next; t->next = item; }
                cur = cur->right;
            }
            add_or_update_variable("__ret__", list);
            return;
        }
        if (map && node->id && strcmp(node->id, "has") == 0) {
            const char *key = NULL;
            ASTNode *arg = node->right;
            if (arg && arg->type && strcmp(arg->type, "STRING") == 0) key = arg->str_value;
            else if (arg && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
                Variable *kv = find_variable(arg->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            int found = (key && map_find_pair(map, key)) ? 1 : 0;
            ASTNode *r = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(r, 0, sizeof(ASTNode));
            r->type = strdup("NUMBER"); r->value = found;
            add_or_update_variable("__ret__", r);
            return;
        }
        if (map && node->id && strcmp(node->id, "remove") == 0) {
            const char *key = NULL;
            ASTNode *arg = node->right;
            if (arg && arg->type && strcmp(arg->type, "STRING") == 0) key = arg->str_value;
            else if (arg && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
                Variable *kv = find_variable(arg->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            if (!key) return;
            ASTNode *prev = NULL, *cur = map->left;
            while (cur) {
                if (cur->id && strcmp(cur->id, key) == 0) {
                    if (prev) prev->right = cur->right; else map->left = cur->right;
                    te_invalidate_map_cache(map);  /* Ola 14 */
                    return;
                }
                prev = cur; cur = cur->right;
            }
            return;
        }
        /* ---- Ola 13: map size/length, clear ---- */
        if (map && node->id && (strcmp(node->id, "size") == 0 || strcmp(node->id, "length") == 0)) {
            int n = map_length(map);  /* uses cache */
            add_or_update_variable("__ret__", create_ast_leaf_number("INT", n, NULL, NULL));
            return;
        }
        if (map && node->id && strcmp(node->id, "clear") == 0) {
            map->left = NULL;  /* leak: deliberate; lifetime tied to AST. */
            te_invalidate_map_cache(map);  /* Ola 14 */
            return;
        }
    }
    
    if (!v || v->vtype != VAL_OBJECT) {
        printf("Error: '%s' is not a valid object.\n", objNode->id);
        return;
    }
    // Try to get ObjectNode from value.object_value first, then from extra
    // This supports both storage patterns: direct storage and wrapper pattern (used by for-in)
    ObjectNode *obj = v->value.object_value;
    //printf("[DEBUG interpret_call_method] objNode->id='%s', v->type='%s', v->vtype=%d, obj=%p\n", 
      //     objNode->id, v->type, v->vtype, (void*)obj);
    if (!obj) {
        // Fallback: The ObjectNode might be stored in an ASTNode wrapper's extra field
        // This happens when objects are created by interpret_for_in()
        ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
       // printf("[DEBUG interpret_call_method] Trying fallback: wrapper=%p\n", (void*)wrapper);
        if (wrapper && wrapper->extra) {
            obj = (ObjectNode *)wrapper->extra;
            //printf("[DEBUG interpret_call_method] Got obj from wrapper->extra: obj=%p\n", (void*)obj);
        }
    }
    
    if (!obj) {
        printf("[DEBUG interpret_call_method] ERROR: obj is NULL after fallback!\n");
        return;
    }
    
   // printf("[DEBUG interpret_call_method] obj=%p, obj->class=%p\n", (void*)obj, (void*)obj->class);
//if (obj->class) {
    //    printf("[DEBUG interpret_call_method] obj->class->name='%s'\n", obj->class->name);
   // }

    // === FIX START: Handle Bridge method calls ===
    if (obj && strcmp(obj->class->name, "Bridge") == 0) {
        if (g_debug_mode) te_log_ast("Calling native bridge: %s.%s", v->id, node->id);
        // Aquí es donde la magia ocurre. Delegamos al manejador específico del bridge.
        if (strcmp(v->id, "Chat") == 0) {
            if (g_bridge_handlers.handle_chat_bridge) g_bridge_handlers.handle_chat_bridge(node->id, node->right);
        } else if (strcmp(v->id, "NLU") == 0) {
            if (g_debug_mode) te_log_ast("Calling NLU bridge");
            if (g_bridge_handlers.handle_nlu_bridge) {
                g_bridge_handlers.handle_nlu_bridge(node->id, node->right);
                // Diagnostic: did the bridge set __ret__? (only print when debug mode enabled)
                if (g_debug_mode) {
                    if (__ret_var_active) {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=1 type='%s'", __ret_var.type ? __ret_var.type : "(null)");
                    } else {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=0");
                    }
                }
            }
        } else if (strcmp(v->id, "API") == 0) {
            if (g_bridge_handlers.handle_api_bridge) g_bridge_handlers.handle_api_bridge(node->id, node->right);
        } else if (strcmp(v->id, "Gemini") == 0) {
            if (g_bridge_handlers.handle_gemini_bridge) g_bridge_handlers.handle_gemini_bridge(node->id, node->right);
        } else {
            printf("Advertencia: Bridge '%s' unknown o no implementado en este ejecutable.\n", v->id);
        }
        // Los bridges pueden o no devolver un valor. Si lo hacen, lo ponen en __ret_var.
        // Por ahora, no necesitamos un valor de retorno falso.
        // ASTNode* dummy_return = create_ast_leaf("STRING", 0, "dummy_return_from_bridge", NULL);
        // add_or_update_variable("__ret__", dummy_return);
        // free_ast(dummy_return);
        return; // Skip normal method dispatch
    }
    // === FIX END ===

    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) m = m->next;
    if (!m) {
        printf("Error: method '%s' not found in class '%s'.\n", node->id, obj->class->name);
        return;
    }

    // --- CACHE LOGIC ---
    if (m->cache_ttl > 0) {
        CachedResponse *cached = get_cached_response(m, node->right);
        if (cached && cached->result) {
            add_or_update_variable("__ret__", cached->result);
            return;
        }
    }

    /* ====================================================================
     * Ola 3 Fase D (perf): FAST `this` setup.
     * Cached: persistent Variable* "this" + single reusable wrapper.
     * Hot path patches both pointers (no calloc/strdup/add_or_update).
     * Switch: TYPEEASY_NO_FASTTHIS=1.
     * ==================================================================== */
    static int      ft_init        = 0;
    static int      ft_enabled     = 1;
    static Variable *g_this_var    = NULL;
    static ASTNode  *g_this_wrap   = NULL;
    if (!ft_init) {
        const char *e = getenv("TYPEEASY_NO_FASTTHIS");
        if (e && e[0] && e[0] != '0') ft_enabled = 0;
        ft_init = 1;
    }
    if (ft_enabled && g_this_var && g_this_wrap) {
        /* Hot path: just patch the cached objects. */
        g_this_var->vtype              = VAL_OBJECT;
        g_this_var->value.object_value = obj;
        g_this_wrap->extra             = (struct ASTNode*)obj;
    } else {
        /* Cold path: original setup, plus capture the cache. */
        ASTNode *thisNode = calloc(1, sizeof(ASTNode));
        thisNode->type  = strdup("OBJECT");
        thisNode->id    = strdup("this");
        thisNode->left  = thisNode->right = NULL;
        thisNode->extra = (struct ASTNode*)obj;
        thisNode->value = 0;
        add_or_update_variable("this", thisNode);
        if (ft_enabled) {
            g_this_var  = find_variable_for("this");
            g_this_wrap = thisNode;
        }
    }
    ParameterNode *p = m->params;
    ASTNode      *arg = node->right; // CORRECCIÓN
    /* ====================================================================
     * Ola 3 Fase B: FAST CALL PATH
     * --------------------------------------------------------------------
     * If every declared parameter is numeric (int/float) we bypass
     * create_ast_leaf_number + add_or_update_variable per arg and write
     * directly into the param's cached Variable*. Switch TYPEEASY_NO_FASTCALL=1
     * to disable.
     * ==================================================================== */
    {
        static int fc_init = 0;
        static int fc_enabled = 1;
        if (!fc_init) {
            const char *e = getenv("TYPEEASY_NO_FASTCALL");
            if (e && e[0] && e[0] != '0') fc_enabled = 0;
            fc_init = 1;
        }
        if (fc_enabled && p) {
            int all_numeric = 1;
            ParameterNode *pp = p;
            while (pp) {
                if (!pp->type
                    || (strcmp(pp->type, "int")    != 0
                     && strcmp(pp->type, "float")  != 0
                     && strcmp(pp->type, "INT")    != 0
                     && strcmp(pp->type, "FLOAT")  != 0)) {
                    all_numeric = 0; break;
                }
                pp = pp->next;
            }
            if (all_numeric) {
                ParameterNode *cur_p = p;
                ASTNode       *cur_a = arg;
                while (cur_p && cur_a) {
                    Variable *fv = (Variable*)cur_p->cached_var;
                    if (!fv) {
                        fv = find_variable_for(cur_p->name);
                        if (!fv) {
                            /* Create a fresh slot only once. */
                            if (var_count < MAX_VARS) {
                                vars[var_count].id       = strdup(cur_p->name);
                                vars[var_count].type     = strdup(cur_p->type);
                                vars[var_count].is_const = 0;
                                vars[var_count].vtype    = (strcmp(cur_p->type, "float")==0
                                                           || strcmp(cur_p->type, "FLOAT")==0)
                                                           ? VAL_FLOAT : VAL_INT;
                                if (vars[var_count].vtype == VAL_FLOAT)
                                    vars[var_count].value.float_value = 0.0;
                                else
                                    vars[var_count].value.int_value = 0;
                                fv = &vars[var_count];
                                var_count++;
                            }
                        }
                        cur_p->cached_var = fv;
                    }
                    if (fv) {
                        double d = evaluate_expression(cur_a);
                        if (fv->vtype == VAL_FLOAT) {
                            fv->value.float_value = d;
                        } else {
                            fv->vtype = VAL_INT;
                            fv->value.int_value  = (int)d;
                        }
                    }
                    cur_p = cur_p->next;
                    cur_a = cur_a->right;
                }
                /* args bound directly; skip slow path */
                goto fastcall_args_done;
            }
        }
    }
    while (p && arg) {
        ASTNode *vn = NULL;
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        } else if (arg->type && (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
            Variable *v_arg = find_variable(arg->id);
            if (!v_arg) {
                printf("Error: variable '%s' not found.\n", arg->id);
                return;
            }
            if (v_arg->vtype == VAL_STRING) {
                vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
            } else {
                vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
            }
        } else {
            int val = evaluate_expression(arg);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }
        add_or_update_variable(p->name, vn);
        p   = p->next;
        arg = arg->right;
    }
fastcall_args_done:

    /* ====================================================================
     * Ola 4 (perf): METHOD-BODY BYTECODE.
     * If the body is a single `return <numeric expr>;` and the declared
     * return type is int/float, run a precompiled bytecode program that
     * reads parameters and `this.attr` directly from cached pointers.
     * Skips both interpret_ast(m->body) and the FAST RETURN evaluator.
     * Switch: TYPEEASY_NO_BCMETHOD=1 disables.
     * ==================================================================== */
    {
        static int bcm_init = 0;
        static int bcm_enabled = 1;
        if (!bcm_init) {
            const char *e = getenv("TYPEEASY_NO_BCMETHOD");
            if (e && e[0] && e[0] != '0') bcm_enabled = 0;
            bcm_init = 1;
        }
        if (bcm_enabled
            && m->return_type
            && (strcmp(m->return_type, "int")   == 0
             || strcmp(m->return_type, "float") == 0)
            && obj && obj->class) {
            BCInfo *bi = bc_get_or_compile_method(m, obj->class);
            if (bi) {
                ObjectNode *saved_this = g_bc_this;
                g_bc_this = obj;
                double rv = bc_exec(bi->code);
                g_bc_this = saved_this;
                /* Write __ret_var directly (mirrors FAST RETURN path). */
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                        free(__ret_var.value.string_value);
                        __ret_var.value.string_value = NULL;
                    }
                    if (__ret_var.id)   { free(__ret_var.id);   __ret_var.id   = NULL; }
                    if (__ret_var.type) { free(__ret_var.type); __ret_var.type = NULL; }
                }
                __ret_var.id = strdup("__ret__");
                if (strcmp(m->return_type, "float") == 0) {
                    __ret_var.type  = strdup("FLOAT");
                    __ret_var.vtype = VAL_FLOAT;
                    __ret_var.value.float_value = rv;
                } else {
                    __ret_var.type  = strdup("INT");
                    __ret_var.vtype = VAL_INT;
                    __ret_var.value.int_value = (int)rv;
                }
                __ret_var_active = 1;
                return_flag = 0;
                return_node = NULL;
                return;
            }
        }
    }

    debugger_push_frame(m->name, node);
    interpret_ast(m->body);
    debugger_pop_frame();

    // --- TYPE CHECK: void method must NOT return a value ---
    if (m->return_type && strcmp(m->return_type, "void") == 0 && return_flag && return_node) {
        char buf[256];
        snprintf(buf, sizeof(buf), "TypeError: method '%s' is declared as 'void' and cannot return a value.", m->name);
        if (throw_message) free(throw_message);
        throw_message = strdup(buf);
        throw_flag = 1;
        return_flag = 0; return_node = NULL;
        return;
    }
    // --- TYPE CHECK: non-void method should produce a return value ---
    // Optional types (T?) and dynamic allow no return (treated as null).
    if (m->return_type
        && strcmp(m->return_type, "void") != 0
        && strcmp(m->return_type, "dynamic") != 0
        && m->return_type[strlen(m->return_type)-1] != '?'
        && (!return_flag || !return_node)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "TypeError: method '%s' is declared as '%s' but does not return a value.", m->name, m->return_type);
        if (throw_message) free(throw_message);
        throw_message = strdup(buf);
        throw_flag = 1;
        return;
    }

    // --- STORE RESULT IN CACHE IF NEEDED ---
    if (m->cache_ttl > 0 && __ret_var_active) {
        Variable *ret = find_variable("__ret__");
        if (ret && ret->type) {
            ASTNode *ret_node = create_ast_leaf(ret->type, ret->value.int_value, ret->value.string_value, ret->id);
            set_cached_response(m, node->right, ret_node);
        }
    }
   // printf("[DIAG] interpret_call_method: después de interpretar cuerpo de método, return_flag=%d\n", return_flag);
        if (return_flag && return_node) {
            /* ============================================================
             * Ola 3 Fase B: FAST RETURN path.
             * If the declared return type is numeric and the return
             * expression is NOT a special form (STRING/CALL/ACCESS_ATTR/
             * pure identifier), write the result straight into __ret_var
             * without allocating an intermediate ASTNode literal.
             * Switch: TYPEEASY_NO_FASTRET=1
             * ============================================================ */
            {
                static int fr_init = 0;
                static int fr_enabled = 1;
                if (!fr_init) {
                    const char *e = getenv("TYPEEASY_NO_FASTRET");
                    if (e && e[0] && e[0] != '0') fr_enabled = 0;
                    fr_init = 1;
                }
                if (fr_enabled
                    && m->return_type
                    && (strcmp(m->return_type, "int")   == 0
                     || strcmp(m->return_type, "float") == 0)
                    && return_node->type
                    && strcmp(return_node->type, "STRING")      != 0
                    && strcmp(return_node->type, "CALL_FUNC")   != 0
                    && strcmp(return_node->type, "CALL_METHOD") != 0
                    && strcmp(return_node->type, "RETURN_JSON") != 0
                    && strcmp(return_node->type, "RETURN_XML")  != 0) {
                    double rv = evaluate_expression(return_node);
                    /* Reset and write __ret_var directly. */
                    if (__ret_var_active) {
                        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                            free(__ret_var.value.string_value);
                            __ret_var.value.string_value = NULL;
                        }
                        if (__ret_var.id) { free(__ret_var.id); __ret_var.id = NULL; }
                        if (__ret_var.type) { free(__ret_var.type); __ret_var.type = NULL; }
                    }
                    __ret_var.id   = strdup("__ret__");
                    if (strcmp(m->return_type, "float") == 0) {
                        __ret_var.type  = strdup("FLOAT");
                        __ret_var.vtype = VAL_FLOAT;
                        __ret_var.value.float_value = rv;
                    } else {
                        __ret_var.type  = strdup("INT");
                        __ret_var.vtype = VAL_INT;
                        __ret_var.value.int_value = (int)rv;
                    }
                    __ret_var_active = 1;
                    return_flag = 0;
                    return_node = NULL;
                    return;
                }
            }
            // If the return is a call to json, print the JSON output
            if (return_node && return_node->type && strcmp(return_node->type, "CALL_FUNC") == 0 && return_node->id && strcmp(return_node->id, "json") == 0) {
            //    printf("[DIAG] Entrando a native_json desde interpret_call_method\n");
                native_json(return_node->left);
            } else {
                ASTNode *lit = NULL;
                if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
                    lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
                } else if (return_node->id) {
                    Variable *rv = find_variable(return_node->id);
                    if (rv) {
                        if (rv->vtype == VAL_STRING) {
                            lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                        } else if (rv->vtype == VAL_FLOAT) {
                            lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                        } else if (rv->vtype == VAL_INT) {
                            lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                        }
                    } else {
                        printf("Error: variable '%s' not found in return statement.\n", return_node->id);
                        return;
                    }
                } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
                    ASTNode *objN = return_node->left;
                    ASTNode *attrN = return_node->right;
                    Variable *ov = find_variable(objN->id);
                    if (ov && ov->vtype == VAL_OBJECT) {
                        ObjectNode *oobj = ov->value.object_value; int i = -1;
                        int idx = -1;
                        for (i=0; i<oobj->class->attr_count; i++) { // Inicializa 'i'
                            if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                        }
                        if (idx>=0) {
                            if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                                lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                            else if (strcmp(oobj->attributes[idx].type,"float")==0)
                                lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL); // Usar idx
                            else
                                lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                        }
                    }
                } else {
                    double rv_double = evaluate_expression(return_node);
                    if (rv_double == (int)rv_double) {
                        lit = create_ast_leaf_number("INT", (int)rv_double, NULL, NULL);
                    } else {
                        lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
                    }
                }
                if (lit) {
                    // --- TYPE CHECK: validate lit->type against declared return_type ---
                    if (m->return_type
                        && strcmp(m->return_type, "dynamic") != 0
                        && strcmp(m->return_type, "void") != 0
                        && lit->type) {
                        const char *expected = m->return_type;
                        const char *actual = lit->type;
                        /* T? : strip trailing '?' for the comparison; null is always OK for optional */
                        char base_expected[64];
                        size_t el = strlen(expected);
                        int optional = (el > 0 && expected[el-1] == '?');
                        if (optional) { strncpy(base_expected, expected, el-1); base_expected[el-1] = '\0'; }
                        else { strncpy(base_expected, expected, sizeof(base_expected)-1); base_expected[sizeof(base_expected)-1]='\0'; }
                        if (optional && strcmp(actual, "NULL") == 0) {
                            /* OK: optional type returning null */
                        } else {
                            const char *expected_cmp = base_expected;
                            int ok = 0;
                            if (strcmp(expected_cmp, "int") == 0    && strcmp(actual, "INT") == 0)    ok = 1;
                            if (strcmp(expected_cmp, "string") == 0 && strcmp(actual, "STRING") == 0) ok = 1;
                            if (strcmp(expected_cmp, "float") == 0  && (strcmp(actual, "FLOAT") == 0 || strcmp(actual, "INT") == 0)) ok = 1;
                            if (!ok) {
                                const char *actual_lower = "?";
                                if (strcmp(actual, "INT") == 0)    actual_lower = "int";
                                else if (strcmp(actual, "STRING") == 0) actual_lower = "string";
                                else if (strcmp(actual, "FLOAT") == 0)  actual_lower = "float";
                                else if (strcmp(actual, "NULL") == 0)   actual_lower = "null";
                                char buf[256];
                                snprintf(buf, sizeof(buf),
                                    "TypeError: method '%s' is declared as '%s' but returns a value of type '%s'.",
                                    m->name, expected, actual_lower);
                                if (throw_message) free(throw_message);
                                throw_message = strdup(buf);
                                throw_flag = 1;
                                return_flag = 0; return_node = NULL;
                                return;
                            }
                        }
                    }
                    add_or_update_variable("__ret__", lit);
                }
            }
            return_flag = 0;
            return_node = NULL;
        }
}

static void interpret_call_method_alone(ASTNode *node) {
    if (call_native_function(node->id, node->right)) {
        return_flag = 0;
        return_node = NULL;
        return;
    }
    // ...existing code...
    // Si node->left (objeto) existe, buscar método de clase
    if (node->left) {
        ObjectNode *obj = (ObjectNode*)node->left;
        MethodNode *m = obj->class->methods;
        while (m && strcmp(m->name, node->id) != 0) m = m->next;
        if (m) {
            // Ejecutar método de clase normalmente
            ParameterNode *p_class = m->params;
            ASTNode *arg_class = node->right;
            while (p_class && arg_class) {
                ASTNode *vn = NULL;
                if (arg_class->type && strcmp(arg_class->type, "STRING") == 0) {
                    vn = create_ast_leaf("STRING", 0, arg_class->str_value, NULL);
                } else if (arg_class->type && (strcmp(arg_class->type,"ID")==0 || strcmp(arg_class->type,"IDENTIFIER")==0)) {
                    Variable *v_arg = find_variable(arg_class->id);
                    if (!v_arg) {
                        printf("Error: variable '%s' not found.\n", arg_class->id);
                        return;
                    }
                    if (v_arg->vtype == VAL_STRING) {
                        vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
                    } else {
                        vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
                    }
                } else {
                    int val = evaluate_expression(arg_class);
                    vn = create_ast_leaf_number("INT", val, NULL, NULL);
                }
                add_or_update_variable(p_class->name, vn);
                p_class   = p_class->next;
                arg_class = arg_class->right;
            }
            debugger_push_frame(m->name, node);
            interpret_ast(m->body);
            debugger_pop_frame();
          //  printf("[DIAG] interpret_call_method_alone: después de interpretar cuerpo de método, return_flag=%d\n", return_flag);
            // ...manejo de return para método de clase si aplica...
            if (return_flag && return_node) {
                if (return_node && return_node->type && strcmp(return_node->type, "CALL_FUNC") == 0 && return_node->id && strcmp(return_node->id, "json") == 0) {
              //     printf("[DIAG] Entrando a native_json desde interpret_call_method_alone\n");
                    native_json(return_node->left);
                }
            }
            return;
        }
    }
    // Si no hay objeto, buscar método global
    MethodNode *gm = global_methods;
    while (gm && strcmp(gm->name, node->id) != 0) gm = gm->next;
    if (!gm) {
        printf("Error: method '%s' not found as a global method.\n", node->id);
        return;
    }
    if (g_debug_mode) te_log_ast("[LOG] Ejecutando método global: %s", node->id);
    ParameterNode *p_global = gm->params;
    ASTNode *arg_global = node->right;
    while (p_global && arg_global) {
        ASTNode *vn = NULL;
        if (arg_global->type && strcmp(arg_global->type, "STRING") == 0) {
            vn = create_ast_leaf("STRING", 0, arg_global->str_value, NULL);
        } else if (arg_global->type && (strcmp(arg_global->type,"ID")==0 || strcmp(arg_global->type,"IDENTIFIER")==0)) {
            Variable *v_arg = find_variable(arg_global->id);
            if (!v_arg) {
                printf("Error: variable '%s' not found.\n", arg_global->id);
                return;
            }
            if (v_arg->vtype == VAL_STRING) {
                vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
            } else {
                vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
            }
        } else {
            int val = evaluate_expression(arg_global);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }
        add_or_update_variable(p_global->name, vn);
        p_global   = p_global->next;
        arg_global = arg_global->right;
    }
    debugger_push_frame(gm->name, node);
    interpret_ast(gm->body);
    debugger_pop_frame();
   // printf("[DIAG] interpret_call_method_alone: after interpret_ast(gm->body), about to check return_flag and return_node\n");
   // printf("[DIAG] interpret_call_method: después de interpretar gm->body, return_flag=%d, return_node=%p\n", return_flag, (void*)return_node);
   // if (return_node) {
    //    printf("[DIAG] return_node: type=%s, id=%s, left=%p, right=%p, str_value=%s\n",
      //      return_node->type ? return_node->type : "NULL",
      //      return_node->id ? return_node->id : "NULL",
      //      (void*)return_node->left,
       //     (void*)return_node->right,
       //     return_node->str_value ? return_node->str_value : "NULL");
   // } else {
   //     printf("[DIAG] return_node is NULL after gm->body\n");
    //}
    if (return_flag && return_node) {
       // printf("[DIAG] interpret_call_method_alone: (global) about to check for CALL_FUNC/json, return_node type=%s id=%s left=%p\n",
        //    return_node->type ? return_node->type : "NULL",
        //    return_node->id ? return_node->id : "NULL",
        //    (void*)return_node->left);
        // Si el return es una llamada a función nativa 'json', ejecutarla directamente
        if (strcmp(return_node->type, "CALL_FUNC") == 0 && return_node->id && strcmp(return_node->id, "json") == 0) {
         //   printf("[DIAG] interpret_call_method: llamando native_json desde método global\n");
            native_json(return_node->left);
            return_flag = 0;
            return_node = NULL;
            return;
        }
        // ...existing code for other return types...
        ASTNode *lit = NULL;
        if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
            lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
        } else if (return_node->id) {
            Variable *rv = find_variable(return_node->id);
            if (rv) {
                if (rv->vtype == VAL_STRING) {
                    lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                } else if (rv->vtype == VAL_FLOAT) {
                    lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                } else if (rv->vtype == VAL_INT) {
                    lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                }
            } else {
                printf("Error: variable '%s' not found in return statement.\n", return_node->id);
                return;
            }
        } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
            ASTNode *objN = return_node->left;
            ASTNode *attrN = return_node->right;
            Variable *ov = find_variable(objN->id);
            if (ov && ov->vtype == VAL_OBJECT) {
                ObjectNode *oobj = ov->value.object_value; int i = -1;
                int idx = -1;
                for (i=0; i<oobj->class->attr_count; i++) {
                    if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                }
                if (idx>=0) {
                    if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                        lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                    else if (strcmp(oobj->attributes[idx].type,"float")==0)
                        lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL);
                    else
                        lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                }
            }
        } else {
            double rv_double = evaluate_expression(return_node);
            if (rv_double == (int)rv_double) {
                lit = create_ast_leaf_number("INT", (int)rv_double, NULL, NULL);
            } else {
                lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
            }
        }
        if (lit) {
            add_or_update_variable("__ret__", lit);
            if (g_debug_mode) te_log_ast("[LOG] Método global '%s' retornó valor en __ret__", node->id);
        }
        return_flag = 0;
        return_node = NULL;
    }
    return;
    printf("Warning: METHOD_CALL_ALONE is deprecated, use CALL_METHOD instead.\n"); 
    ASTNode *objNode = node->left;
    Variable *v = find_variable(objNode->id);
    return_flag = 0;
    return_node = NULL;

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
    
    if (!v || v->vtype != VAL_OBJECT) {
        printf("Error: '%s' is not a valid object.\n", objNode->id);
        return;
    }
    ObjectNode *obj = v->value.object_value;

    // === FIX START: Handle Bridge method calls ===
    if (strcmp(obj->class->name, "Bridge") == 0) {
        if (g_debug_mode) te_log_ast("Calling native bridge: %s.%s", v->id, node->id);
        if (strcmp(v->id, "Chat") == 0) {
            if (g_bridge_handlers.handle_chat_bridge) g_bridge_handlers.handle_chat_bridge(node->id, node->right);
        } else if (strcmp(v->id, "NLU") == 0) {
            if (g_debug_mode) te_log_ast("Calling NLU bridge");
            if (g_bridge_handlers.handle_nlu_bridge) {
                g_bridge_handlers.handle_nlu_bridge(node->id, node->right);
                if (g_debug_mode) {
                    if (__ret_var_active) {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=1 type='%s'", __ret_var.type ? __ret_var.type : "(null)");
                    } else {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=0");
                    }
                }
            }
        } else if (strcmp(v->id, "API") == 0) {
            if (g_bridge_handlers.handle_api_bridge) g_bridge_handlers.handle_api_bridge(node->id, node->right);
        } else if (strcmp(v->id, "Gemini") == 0) {
            if (g_bridge_handlers.handle_gemini_bridge) g_bridge_handlers.handle_gemini_bridge(node->id, node->right);
        } else {
            printf("Advertencia: Bridge '%s' unknown o no implementado en este ejecutable.\n", v->id);
        }
        return;
    }
    // === FIX END ===

    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) m = m->next;
    if (!m) {
        // Buscar método global (fuera de clase)
        MethodNode *gm = global_methods;
        while (gm && strcmp(gm->name, node->id) != 0) gm = gm->next;
        if (!gm) {
            printf("Error: method '%s' not found in class '%s' nor as a global method.\n", node->id, obj->class->name);
            return;
        }
        ParameterNode *p = gm->params;
        ASTNode *arg = node->right;
        while (p && arg) {
            ASTNode *vn = NULL;
            if (arg->type && strcmp(arg->type, "STRING") == 0) {
                vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
            } else if (arg->type && (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
                Variable *v_arg = find_variable(arg->id);
                if (!v_arg) {
                    printf("Error: variable '%s' not found.\n", arg->id);
                    return;
                }
                if (v_arg->vtype == VAL_STRING) {
                    vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
                } else {
                    vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
                }
            } else {
                int val = evaluate_expression(arg);
                vn = create_ast_leaf_number("INT", val, NULL, NULL);
            }
            add_or_update_variable(p->name, vn);
            p   = p->next;
            arg = arg->right;
        }
        debugger_push_frame(gm->name, node);
        interpret_ast(gm->body);
        debugger_pop_frame();
          //  printf("[DIAG] interpret_call_method_alone: antes de check, return_flag=%d, return_node=%p\n", return_flag, (void*)return_node);
            if (return_flag && return_node) {
           //     printf("[DIAG] interpret_call_method_alone: return_node type=%s id=%s\n", return_node->type ? return_node->type : "NULL", return_node->id ? return_node->id : "NULL");
            ASTNode *lit = NULL;
            if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
                lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
            } else if (return_node->id) {
                Variable *rv = find_variable(return_node->id);
                if (rv) {
                    if (rv->vtype == VAL_STRING) {
                        lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                    } else if (rv->vtype == VAL_FLOAT) {
                        lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                    } else if (rv->vtype == VAL_INT) {
                        lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                    }
                } else {
                    printf("Error: variable '%s' not found in return statement.\n", return_node->id);
                    return;
                }
            } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
                ASTNode *objN = return_node->left;
                ASTNode *attrN = return_node->right;
                Variable *ov = find_variable(objN->id);
                if (ov && ov->vtype == VAL_OBJECT) {
                    ObjectNode *oobj = ov->value.object_value; int i = -1;
                    int idx = -1;
                    for (i=0; i<oobj->class->attr_count; i++) {
                        if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                    }
                    if (idx>=0) {
                        if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                            lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                        else if (strcmp(oobj->attributes[idx].type,"float")==0)
                            lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL);
                        else
                            lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                    }
                }
            } else {
                double rv_double = evaluate_expression(return_node);
                if (rv_double == (int)rv_double) {
                    lit = create_ast_leaf_number("INT", (int)rv_double, NULL, NULL);
                } else {
                    lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
                }
            }
            if (lit) {
                add_or_update_variable("__ret__", lit);
            }
            return_flag = 0;
            return_node = NULL;
        }
        return;
    }
    ASTNode *thisNode = calloc(1, sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->extra = (struct ASTNode*)obj;
    thisNode->value = 0;
    add_or_update_variable("this", thisNode);
    ParameterNode *p = m->params;
    ASTNode *arg = node->right;
    while (p && arg) {
        ASTNode *vn = NULL;
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        } else if (arg->type && (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
            Variable *v_arg = find_variable(arg->id);
            if (!v_arg) {
                printf("Error: variable '%s' not found.\n", arg->id);
                return;
            }
            if (v_arg->vtype == VAL_STRING) {
                vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
            } else {
                vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
            }
        } else {
            int val = evaluate_expression(arg);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }
        add_or_update_variable(p->name, vn);
        p = p->next;
        arg = arg->right;
    }
    debugger_push_frame(m->name, NULL);
    interpret_ast(m->body);
    debugger_pop_frame();
    if (return_flag && return_node) {
        ASTNode *lit = NULL;
        if (return_node->type && strcmp(return_node->type, "STRING") == 0) {
            lit = create_ast_leaf("STRING", 0, return_node->str_value, NULL);
        } else if (return_node->id) {
            Variable *rv = find_variable(return_node->id);
            if (rv) {
                if (rv->vtype == VAL_STRING) {
                    lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                } else if (rv->vtype == VAL_FLOAT) {
                    lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                } else if (rv->vtype == VAL_INT) {
                    lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                }
            } else {
                printf("Error: variable '%s' not found in return statement.\n", return_node->id);
                return;
            }
        } else if (return_node->type && strcmp(return_node->type, "ACCESS_ATTR") == 0) {
            ASTNode *objN = return_node->left;
            ASTNode *attrN = return_node->right;
            Variable *ov = find_variable(objN->id);
            if (ov && ov->vtype == VAL_OBJECT) {
                ObjectNode *oobj = ov->value.object_value; int i = -1;
                int idx = -1;
                for (i=0; i<oobj->class->attr_count; i++) {
                    if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                }
                if (idx>=0) {
                    if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                        lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                    else if (strcmp(oobj->attributes[idx].type,"float")==0)
                        lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL);
                    else
                        lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                }
            }
        } else {
            double rv_double = evaluate_expression(return_node);
            if (rv_double == (int)rv_double) {
                lit = create_ast_leaf_number("INT", (int)rv_double, NULL, NULL);
            } else {
                lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
            }
        }
        if (lit) {
            add_or_update_variable("__ret__", lit);
        }
        return_flag = 0;
        return_node = NULL;
    }
}
ObjectNode* clone_object(ObjectNode *original) {
    if (!original || !original->class) {
        /* clone_object debug log removed */
        return NULL;
    }
    ObjectNode *clone = malloc(sizeof(ObjectNode));
    clone->class = original->class;
    clone->attributes = malloc(original->class->attr_count * sizeof(Variable));
    for (int i = 0; i < original->class->attr_count; i++) {
        clone->attributes[i].id = strdup(original->attributes[i].id);
        clone->attributes[i].type = strdup(original->attributes[i].type);
        clone->attributes[i].vtype = original->attributes[i].vtype;
        if (clone->attributes[i].vtype == VAL_STRING) {
            const char *src = original->attributes[i].value.string_value;
            clone->attributes[i].value.string_value = src ? strdup(src) : strdup("");
        } else if (clone->attributes[i].vtype == VAL_INT) {
            clone->attributes[i].value.int_value = original->attributes[i].value.int_value;
        } else {
            // (Manejar otros tipos como float si es necesario)
        }
    }
    return clone;
}

/* ---------- CSV reader (RFC 4180 subset) ----------
 * Soporta:
 *   - Campos entre comillas dobles, con "" como escape
 *   - Comas y saltos de línea dentro de comillas
 *   - Terminadores LF, CRLF y CR
 *   - BOM UTF-8 al inicio
 *   - Encabezado mapeado por nombre a los atributos de la clase
 *   - Coerción de tipos según `cls->attributes[i].type` ("int", "string",
 *     "int?", "string?"). Atributos `?` aceptan campos vacíos como `null`.
 * Errores: emiten a stderr y abortan (la llamada se resuelve en parse-time,
 * por lo que `try/catch` aún no está activo).
 *
 * Phase 2: bump-arena para ObjectNode/Variable[]/strings. Reduce ~1.4M
 * mallocs por carga de 200k filas a unas pocas docenas (chunks de 1MB).
 * El arena vive lo que dura el proceso (script mode); nunca se libera
 * individualmente. Esto es seguro porque ningún path en script mode
 * libera obj->attributes[i].* (verificado por inspección).
 */

typedef struct CSVChunk {
    char *base;
    size_t used, cap;
    struct CSVChunk *next;
} CSVChunk;

/* Per-thread head; main thread also uses this (its own __thread slot).
 * After parallel workers finish, their heads are linked into g_csv_arena_keepalive
 * so allocations live until process exit (script-mode, safe to leak). */
static __thread CSVChunk *t_csv_arena = NULL;
static CSVChunk *g_csv_arena_keepalive = NULL;
#if TE_HAS_PTHREAD
static pthread_mutex_t g_csv_keepalive_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static void csv_arena_keepalive_link(CSVChunk *head) {
    if (!head) return;
    /* Find tail of `head` chain. */
    CSVChunk *tail = head;
    while (tail->next) tail = tail->next;
#if TE_HAS_PTHREAD
    pthread_mutex_lock(&g_csv_keepalive_mu);
#endif
    tail->next = g_csv_arena_keepalive;
    g_csv_arena_keepalive = head;
#if TE_HAS_PTHREAD
    pthread_mutex_unlock(&g_csv_keepalive_mu);
#endif
}

static void *csv_arena_alloc(size_t n) {
    n = (n + 7u) & ~(size_t)7u; /* align 8 */
    if (!t_csv_arena || t_csv_arena->used + n > t_csv_arena->cap) {
        size_t cap = n > (8u << 20) ? n : (8u << 20); /* 8 MiB chunks (was 1MB): menos mallocs, mejor localidad por thread */
        CSVChunk *c = (CSVChunk*)malloc(sizeof(CSVChunk));
        c->base = (char*)malloc(cap);
        c->used = 0;
        c->cap = cap;
        c->next = t_csv_arena;
        t_csv_arena = c;
    }
    void *p = t_csv_arena->base + t_csv_arena->used;
    t_csv_arena->used += n;
    return p;
}

static char *csv_arena_dup(const char *s, size_t n) {
    char *p = (char*)csv_arena_alloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *csv_arena_strdup(const char *s) {
    return csv_arena_dup(s, strlen(s));
}

/* ---------- AST node pool (block allocator) ----------
 * Wrappers OBJECT ASTNode son ~80B y se crean uno por fila. malloc/calloc
 * por wrapper agrega ~5-8 ms en 200k filas. Pool en bloques de 4096 los
 * elimina. type/id se siguen strdup'ando individualmente porque algún
 * path del runtime los puede free()ar (verificado: meterlos a arena
 * crashea con munmap_chunk). El pool en sí mismo se "leakea" al exit.
 */
typedef struct ASTNodePool {
    ASTNode *block;
    int used, cap;
    struct ASTNodePool *next;
} ASTNodePool;

static __thread ASTNodePool *t_ast_pool = NULL;
static ASTNodePool *g_ast_pool_keepalive = NULL;
#if TE_HAS_PTHREAD
static pthread_mutex_t g_ast_pool_keepalive_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static void ast_pool_keepalive_link(ASTNodePool *head) {
    if (!head) return;
    ASTNodePool *tail = head;
    while (tail->next) tail = tail->next;
#if TE_HAS_PTHREAD
    pthread_mutex_lock(&g_ast_pool_keepalive_mu);
#endif
    tail->next = g_ast_pool_keepalive;
    g_ast_pool_keepalive = head;
#if TE_HAS_PTHREAD
    pthread_mutex_unlock(&g_ast_pool_keepalive_mu);
#endif
}

static ASTNode *ast_pool_alloc(void) {
    if (!t_ast_pool || t_ast_pool->used >= t_ast_pool->cap) {
        ASTNodePool *p = (ASTNodePool*)malloc(sizeof(ASTNodePool));
        p->cap = 4096;
        p->block = (ASTNode*)calloc((size_t)p->cap, sizeof(ASTNode));
        p->used = 0;
        p->next = t_ast_pool;
        t_ast_pool = p;
    }
    ASTNode *n = &t_ast_pool->block[t_ast_pool->used++];
    /* Block is calloc'd; slot is already zeroed. */
    return n;
}

/* ---------- pread-based CSV file loader ----------
 * Reemplaza mmap para evitar page-fault overhead en Docker overlay FS / WSL2-9P.
 *
 * PROBLEMA con mmap en Docker bind-mounts (WSL2 9P):
 *   - El archivo está en el host Windows, accedido via Plan-9 (9P protocol).
 *   - mmap paga page faults LAZILY: 3.5MB / 4KB = ~875 page faults, cada una
 *     sincrónica a través de kernel → VFS → overlay → 9P → Windows NTFS.
 *   - Con 12 workers paralelos, los faults se "solapan" pero siguen siendo
 *     costosos (~30-50ms total observado).
 *
 * SOLUCIÓN con pread():
 *   - El kernel recibe UNA petición grande (o pocas) y activa read-ahead
 *     agresivo (FADV_SEQUENTIAL/WILLNEED). El bloque de 3.5MB pasa de host
 *     a page-cache Linux en una sola ráfaga.
 *   - Con N threads haciendo pread() en paralelo sobre chunks, el SO puede
 *     pipelinear múltiples peticiones 9P.
 *   - El buffer resultante es malloc'd y WRITABLE (igual que mmap MAP_PRIVATE).
 *     Los parsers pueden escribir \0 in-place sin problema.
 *
 * NOTA: si el archivo ya está en page-cache (segunda ejecución), pread es
 *       casi gratis (~1-2ms). mmap también, pero sigue pagando TLB misses.
 */
#if TE_HAS_PTHREAD
typedef struct { int fd; char *buf; off_t offset; size_t size; } CSVPreadArgs;
static void *csv_pread_io_worker(void *p) {
    CSVPreadArgs *a = (CSVPreadArgs*)p;
    size_t done = 0;
    while (done < a->size) {
        ssize_t r = pread(a->fd, a->buf + (size_t)a->offset + done,
                          a->size - done, a->offset + (off_t)done);
        if (r <= 0) break;
        done += (size_t)r;
    }
    return NULL;
}
#endif

/* Forward declare: definida más abajo (mmap-based loader). */
static char *csv_mmap_file(const char *filename, size_t *out_len);

static char *csv_read_file(const char *filename, size_t *out_len, int *out_is_mmap) {
    if (out_is_mmap) *out_is_mmap = 0;
    /* Auto-detect: usar mmap en ext4/overlayfs (rápido, zero-copy),
     * pread paralelo en V9FS/FUSE (evita page-faults a través de 9P).
     * V9FS_MAGIC = 0x01021997 (WSL2 bind-mount desde Windows).      */
#if defined(__linux__)
    struct statfs sfs;
    int on_v9fs = 0;
    if (statfs(filename, &sfs) == 0 && (long)sfs.f_type == 0x01021997L)
        on_v9fs = 1;
    if (!on_v9fs) {
        /* ext4 / overlayfs: mmap MAP_PRIVATE (zero-copy, demand paging). */
        char *m = csv_mmap_file(filename, out_len);
        if (m && out_is_mmap) *out_is_mmap = 1;
        return m;
    }
#endif
    /* V9FS o fallback: pread paralelo (4 threads, evita page-faults VFS). */
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    if (st.st_size == 0) {
        close(fd); *out_len = 0;
        char *p = (char*)malloc(1); if (p) p[0] = '\0'; return p;
    }
    size_t size = (size_t)st.st_size;
    /* +1 para null-terminator de seguridad al final del buffer. */
    char *buf = (char*)malloc(size + 1);
    if (!buf) { close(fd); return NULL; }
    buf[size] = '\0';

    /* Hints al kernel: activa read-ahead agresivo antes de los pread(). */
#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, (off_t)size, POSIX_FADV_SEQUENTIAL);
#endif
#ifdef POSIX_FADV_WILLNEED
    posix_fadvise(fd, 0, (off_t)size, POSIX_FADV_WILLNEED);
#endif

#if TE_HAS_PTHREAD
    /* Múltiples pread paralelos saturan el queue 9P de WSL2 → mayor throughput.
     * 4 threads = sweet spot para bind-mount Windows→WSL2→Docker. */
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int nio = (int)ncpu;
    if (nio > 4) nio = 4;
    if (nio < 1) nio = 1;
    if (size >= 256u * 1024u && nio >= 2) {
        size_t chunk = size / (size_t)nio;
        CSVPreadArgs *args = (CSVPreadArgs*)malloc((size_t)nio * sizeof(CSVPreadArgs));
        pthread_t *tids   = (pthread_t*)malloc((size_t)nio * sizeof(pthread_t));
        for (int i = 0; i < nio; i++) {
            args[i].fd     = fd;
            args[i].buf    = buf;
            args[i].offset = (off_t)((size_t)i * chunk);
            args[i].size   = (i == nio - 1) ? (size - (size_t)i * chunk) : chunk;
        }
        for (int i = 0; i < nio; i++)
            pthread_create(&tids[i], NULL, csv_pread_io_worker, &args[i]);
        for (int i = 0; i < nio; i++)
            pthread_join(tids[i], NULL);
        free(tids); free(args);
    } else
#endif
    {
        /* Serial read (loop para manejar reads parciales). */
        size_t nread = 0;
        while (nread < size) {
            ssize_t r = read(fd, buf + nread, size - nread);
            if (r <= 0) break;
            nread += (size_t)r;
        }
    }
    close(fd);
    *out_len = size;
    return buf;
}

/* ---------- mmap-based CSV loader (mantenido como referencia) ----------
 * MAP_PRIVATE permite escribir (\0 in-place) sin tocar el archivo.
 * REEMPLAZADO por csv_read_file en from_csv_to_list — ver comentario arriba. */
static char *csv_mmap_file(const char *filename, size_t *out_len) {
#if TE_HAS_MMAP
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    if (st.st_size == 0) {
        close(fd);
        *out_len = 0;
        char *p = (char*)malloc(1);
        if (p) p[0] = '\0';
        return p;
    }
    void *m = mmap(NULL, (size_t)st.st_size,
                   PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd); /* fd ya no necesario tras mmap */
    if (m == MAP_FAILED) return NULL;
    /* Hint: lectura secuencial. Mejor prefetch del kernel. */
    madvise(m, (size_t)st.st_size, MADV_SEQUENTIAL);
    /* MADV_WILLNEED async-prefetcha; en Docker overlay FS suele dar speedup
     * marginal. MADV_POPULATE_READ (Linux 5.14+) bloquea sincr\u00f3nicamente y
     * tend\u00eda a EMPEORAR el total en estos benches \u2014 omitido. */
#ifdef MADV_WILLNEED
    madvise(m, (size_t)st.st_size, MADV_WILLNEED);
#endif
    *out_len = (size_t)st.st_size;
    return (char*)m;
#else
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    extern char *csv_read_all(FILE *fp, size_t *out_len);
    char *buf = csv_read_all(fp, out_len);
    fclose(fp);
    return buf;
#endif
}

/* ---------- SIMD scanner ----------
 * Encuentra el primer ',' '\n' '\r' a partir de `i`. Para AVX2 procesa
 * 32 bytes por iteración. Solo se usa en estado NO-quoted del scanner
 * (mayoría abrumadora de campos en CSVs reales). */
static inline size_t simd_find_csv_delim(const char *s, size_t i, size_t len) {
#if TE_HAS_AVX2
    const __m256i v_comma = _mm256_set1_epi8(',');
    const __m256i v_lf    = _mm256_set1_epi8('\n');
    const __m256i v_cr    = _mm256_set1_epi8('\r');
    while (i + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
        __m256i m = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(v, v_comma),
                            _mm256_cmpeq_epi8(v, v_lf)),
            _mm256_cmpeq_epi8(v, v_cr));
        unsigned mask = (unsigned)_mm256_movemask_epi8(m);
        if (mask) return i + (size_t)__builtin_ctz(mask);
        i += 32;
    }
#endif
    while (i < len && s[i] != ',' && s[i] != '\n' && s[i] != '\r') i++;
    return i;
}

static char *csv_read_all(FILE *fp, size_t *out_len) {
    size_t cap = 8192, len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            size_t ncap = cap * 2;
            char *nb = (char*)realloc(buf, ncap);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap = ncap;
        }
    }
    *out_len = len;
    return buf;
}

static char *csv_next_field(const char *src, size_t len, size_t *pos, int *end_of_record) {
    size_t i = *pos;
    size_t fcap = 64, flen = 0;
    char *f = (char*)malloc(fcap);
    int in_quotes = 0;
    *end_of_record = 0;

    if (i < len && src[i] == '"') { in_quotes = 1; i++; }

    while (i < len) {
        char c = src[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < len && src[i+1] == '"') {
                    if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
                    f[flen++] = '"';
                    i += 2;
                } else {
                    in_quotes = 0; i++;
                }
            } else {
                if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
                f[flen++] = c;
                i++;
            }
        } else {
            if (c == ',') { i++; break; }
            if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < len && src[i+1] == '\n') i++;
                i++;
                *end_of_record = 1;
                break;
            }
            if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
            f[flen++] = c;
            i++;
        }
    }
    if (i >= len) *end_of_record = 1;
    f[flen] = '\0';
    *pos = i;
    return f;
}

static char **csv_next_record(const char *src, size_t len, size_t *pos, int *out_count) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) { *out_count = 0; return NULL; }

    size_t cap = 8, n = 0;
    char **fields = (char**)malloc(cap * sizeof(char*));
    int eor = 0;
    while (!eor) {
        if (n == cap) { cap *= 2; fields = (char**)realloc(fields, cap * sizeof(char*)); }
        fields[n++] = csv_next_field(src, len, pos, &eor);
    }
    *out_count = (int)n;
    return fields;
}

static void csv_free_record(char **rec, int n) {
    for (int i = 0; i < n; i++) free(rec[i]);
    free(rec);
}

/* Versión que reusa un fields-array fijo (pre-dimensionado a max_cols).
 * Si una fila excede max_cols, descarta los campos extra. */
static int csv_next_record_into(const char *src, size_t len, size_t *pos,
                                 char **fields, int max_cols) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return 0;
    int n = 0, eor = 0;
    while (!eor) {
        if (n >= max_cols) {
            char *junk = csv_next_field(src, len, pos, &eor);
            free(junk);
            continue;
        }
        fields[n++] = csv_next_field(src, len, pos, &eor);
    }
    return n;
}

static void csv_free_fields_inplace(char **fields, int n) {
    for (int i = 0; i < n; i++) free(fields[i]);
}

/* ZERO-COPY scanner: muta `src` (escribe '\0' en separadores) y devuelve
 * punteros directos al buffer. Para campos no quoted (caso común), no
 * hay malloc/copy. Para campos quoted con `""` necesitamos in-place
 * collapse de las dobles-quotes (también sin alloc — escribimos en el
 * mismo span del field). El caller NO debe free() los punteros.
 *
 * Returns: número de campos llenados en `fields` (truncado a max_cols).
 *          -1 si no hay más datos. Mutua `*pos`. */
static int csv_next_record_zerocopy(char *src, size_t len, size_t *pos,
                                     char **fields, int max_cols) {
    /* Consume linebreaks separadores entre records. */
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return -1;

    int n = 0;
    while (1) {
        size_t i = *pos;
        char *field_start;
        int has_escape = 0;

        if (i < len && src[i] == '"') {
            /* Quoted field. */
            i++;
            field_start = src + i;
            char *write = src + i;
            while (i < len) {
                char c = src[i];
                if (c == '"') {
                    if (i + 1 < len && src[i+1] == '"') {
                        /* "" → " (collapse in place). */
                        *write++ = '"';
                        i += 2;
                        has_escape = 1;
                    } else {
                        i++;
                        break; /* end of quoted field */
                    }
                } else {
                    if (has_escape) *write++ = c;
                    i++;
                }
            }
            if (!has_escape) {
                /* No escape → terminar al cierre de la quote. */
                /* `i` apunta justo después del closing quote. Marcar fin. */
                src[i - 1] = '\0';
            } else {
                *write = '\0';
            }
            /* Saltar al siguiente delimitador. */
            i = simd_find_csv_delim(src, i, len);
        } else {
            field_start = src + i;
            i = simd_find_csv_delim(src, i, len);
            /* `i` apunta al delimitador. Lo NUL-terminamos para hacer el
             * span un C-string (mutación destructiva del source). */
            if (i < len) {
                /* Lo guardamos antes de pisar para detectar fin de record. */
                /* (lo manejamos abajo). */
            }
        }

        /* Determinar fin de record y avanzar pos. */
        int eor = 0;
        if (i >= len) {
            eor = 1;
            *pos = i;
        } else {
            char d = src[i];
            src[i] = '\0';      /* terminate field */
            if (d == ',') {
                *pos = i + 1;
            } else {
                /* '\n' o '\r' */
                size_t k = i + 1;
                if (d == '\r' && k < len && src[k] == '\n') k++;
                *pos = k;
                eor = 1;
            }
        }

        if (n < max_cols) {
            fields[n++] = field_start;
        }
        /* Si overflow (más campos que max_cols), descartamos sin guardar. */

        if (eor) break;
    }
    return n;
}

/* ------ Scanner readonly (no escribe \0): para buffers mmap MAP_PRIVATE. ------
 * Retorna pares (ptr, len) en lugar de punteros null-terminados.
 * Evita los COW page faults que disparan los writes en overlayfs (~100μs/page). */
static int csv_next_record_readonly(const char *src, size_t len, size_t *pos,
                                    const char **fields, int *field_lens, int max_cols) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return -1;

    int n = 0;
    while (1) {
        size_t i = *pos;
        const char *field_start;
        int field_len;

        if (i < len && src[i] == '"') {
            /* Quoted: field_start after opening quote, len until closing quote. */
            i++;
            field_start = src + i;
            while (i < len && !(src[i] == '"' && (i+1 >= len || src[i+1] != '"'))) {
                if (src[i] == '"' && i+1 < len && src[i+1] == '"') i += 2;
                else i++;
            }
            field_len = (int)(i - (size_t)(field_start - src));
            if (i < len) i++; /* skip closing quote */
            i = simd_find_csv_delim(src, i, len);
        } else {
            field_start = src + i;
            i = simd_find_csv_delim(src, i, len);
            field_len = (int)(i - (size_t)(field_start - src));
        }

        int eor = 0;
        if (i >= len) {
            eor = 1;
            *pos = i;
        } else {
            char d = src[i];
            if (d == ',') {
                *pos = i + 1;
            } else {
                size_t k = i + 1;
                if (d == '\r' && k < len && src[k] == '\n') k++;
                *pos = k;
                eor = 1;
            }
        }

        if (n < max_cols) {
            fields[n]     = field_start;
            field_lens[n] = field_len;
            n++;
        }
        if (eor) break;
    }
    return n;
}

static int csv_attr_is_int(const char *t) {
    return t && (!strcmp(t, "int") || !strcmp(t, "int?"));
}
static int csv_attr_is_string(const char *t) {
    return t && (!strcmp(t, "string") || !strcmp(t, "string?"));
}
static int csv_attr_is_nullable(const char *t) {
    if (!t) return 0;
    size_t L = strlen(t);
    return L > 0 && t[L-1] == '?';
}

/* Sentinel global: cuando un wrapper CSV ASTNode tiene `type` apuntando
 * a este string compartido, free_ast() lo deja vivo (evita strdup por fila). */
char *g_csv_wrapper_obj_type = NULL;

/* ---------- Parallel CSV parsing ----------
 * Cada worker procesa un rango [start, end) del buffer (mmap MAP_PRIVATE).
 * Limitación: asume que '\n' = fin de fila (no quoted-newlines). El
 * caller verifica esto antes de paralelizar.
 *
 * Boundary handling:
 *   - Worker 0 procesa desde data_start.
 *   - Worker i>0 avanza pos hasta el primer '\n'+1 dentro del chunk
 *     (descarta la fila parcial, que será procesada por worker i-1).
 *   - Worker procesa filas que COMIENZAN antes de end. La última puede
 *     cruzar end (lectura segura: nunca pasa de total_len).
 */

/* ============================================================
 * Ruta COLUMNAR (DataFrame-style): activada por env TE_CSV_DATAFRAME=1.
 * En lugar de crear un ObjectNode + Variable[] + ASTNode-wrapper por fila,
 * llena buffers de columnas contiguos (int*, char**) tipo Apache Arrow.
 *
 * Limitaciones (intencionalmente estrictas, fallback al path normal si no se
 * cumplen):
 *   - Solo atributos `int` y `string` (no `int?` / `string?` / otros).
 *   - Sin quoted fields ('"' en archivo → fallback).
 *
 * Resultado: wrapper ASTNode tipo "LIST" con left=NULL pero TEListIdx pre-set
 * con len=row_count. Por tanto `df.length` retorna O(1) sin objetos. Iteración
 * o indexación NO está soportada en este wrapper (TODO: materialización lazy).
 * Diseñado para análisis tipo "load + count/aggregate" que es el caso polars.
 * ============================================================*/
typedef struct DataFrame {
    int row_count;
    int col_count;       /* = nattr */
    int *col_kinds;      /* 0=int, 1=string */
    void **col_data;     /* col_data[a] points to int* or char** of length row_count */
    char **col_names;    /* aliases to interned/arena names */
    ClassNode *cls;
} DataFrame;

/* SIMD count of '\n' bytes in [s+lo, s+hi). */
static size_t csv_count_newlines(const char *s, size_t lo, size_t hi) {
    size_t i = lo, n = 0;
#if TE_HAS_AVX2
    __m256i nl = _mm256_set1_epi8('\n');
    while (i + 32 <= hi) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
        __m256i eq = _mm256_cmpeq_epi8(v, nl);
        unsigned m = (unsigned)_mm256_movemask_epi8(eq);
        n += (size_t)__builtin_popcount(m);
        i += 32;
    }
#endif
    while (i < hi) { if (s[i] == '\n') n++; i++; }
    return n;
}

/* ============================================================
 * CSVColWorkerArgs — Phase A + spinwait + Phase B (sin barriers/futex).
 *
 * Flujo prefix-sum con spinwait (elimina ~10ms de futex overhead):
 *   Phase A (paralelo): worker cuenta \n, pone phase_a_done=1 (release).
 *   Main spinea sobre phase_a_done[] (sin syscall) → prefix-sum → alloc.
 *   Main pone go_phase_b=1 (release) para cada worker.
 *   Phase B (paralelo): worker spinea go_phase_b, luego parsea directo al
 *                        global array. Sin locks, sin memcpy.
 * ============================================================ */
typedef struct CSVColWorkerArgs {
    char       *src;
    size_t      total_len;
    size_t      chunk_start;
    size_t      chunk_end;
    int         is_first;
    int         nattr;
    int         header_n;
    const int  *attr_kind;
    const int  *col_to_attr;
    size_t      actual_parse_start;
    int         row_count;
    int         row_offset;
    void      **global_col_data;
    /* Spinwait flags (cache-line aligned para evitar false sharing) */
    volatile int phase_a_done __attribute__((aligned(64)));
    volatile int go_phase_b   __attribute__((aligned(64)));
    /* Flag: si 1, el buffer src es mmap'd (read-only seguro) →
     * usar csv_next_record_readonly (sin writes, sin COW page faults). */
    int         readonly_src;
    /* Pool: fase actual (0=idle, 1=phase_a, 2=phase_b, 3=exit).
     * Cache-line aligned para evitar false sharing entre slots. */
    volatile int pool_phase __attribute__((aligned(64)));
} CSVColWorkerArgs;

#if TE_HAS_PTHREAD
static inline void cpu_relax(void) {
#if defined(__x86_64__)
    __builtin_ia32_pause();
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* === csv_do_phase_b ===
 * Parsea el chunk asignado y escribe directamente en los arrays globales del DataFrame.
 * Precondición: a->actual_parse_start, a->row_offset, a->global_col_data están listos. */
static void csv_do_phase_b(CSVColWorkerArgs *a) {
    char       *src       = a->src;
    size_t      total_len = a->total_len;
    size_t      pos       = a->actual_parse_start;
    size_t      end       = a->chunk_end;
    int         header_n  = a->header_n;
    const int  *attr_kind = a->attr_kind;
    const int  *col_to_attr = a->col_to_attr;
    void      **gcols     = a->global_col_data;
    int         row_off   = a->row_offset;

    char **rec_fields = (char**)malloc(header_n * sizeof(char*));
    int row_idx = 0;

    if (a->readonly_src) {
        /* === Readonly path: sin writes → sin COW en overlayfs/mmap. === */
        const char **ro_fields = (const char**)rec_fields; /* reuse buffer */
        int *field_lens = (int*)malloc(header_n * sizeof(int));
        while (pos < end) {
            size_t row_begin = pos;
            int rn = csv_next_record_readonly(src, total_len, &pos,
                                              ro_fields, field_lens, header_n);
            if (rn <= 0) break;
            if (row_begin >= end) break;
            if (rn == 1 && field_lens[0] == 0) continue;

            int global_row = row_off + row_idx;
            int upto = rn < header_n ? rn : header_n;
            for (int c = 0; c < upto; c++) {
                int aa = col_to_attr[c];
                if (aa < 0) continue;
                const char *raw = ro_fields[c];
                int rawlen     = field_lens[c];
                if (attr_kind[aa] == 0 /*INT*/) {
                    const char *q = raw;
                    const char *q_end = raw + rawlen;
                    int neg = 0;
                    if (q < q_end && *q == '-') { neg = 1; q++; }
                    else if (q < q_end && *q == '+') { q++; }
                    long v = 0;
                    while (q < q_end) {
                        unsigned d = (unsigned)(*q - '0');
                        if (d > 9u) { v = 0; break; }
                        v = v * 10 + (long)d; q++;
                    }
                    if (neg) v = -v;
                    ((int*)gcols[aa])[global_row] = (int)v;
                } else {
                    /* String: guardamos el puntero sin null-terminar.
                     * Válido en el buffer mmap'd (lifetime de proceso). */
                    ((const char**)gcols[aa])[global_row] = raw;
                }
            }
            row_idx++;
        }
        free(field_lens);
    } else {
        /* === Writable path: escribe \0 (buffer pread malloc'd). === */
        while (pos < end) {
            size_t row_begin = pos;
            int rn = csv_next_record_zerocopy(src, total_len, &pos, rec_fields, header_n);
            if (rn <= 0) break;
            if (row_begin >= end) break;
            if (rn == 1 && rec_fields[0][0] == '\0') continue;

            int global_row = row_off + row_idx;
            int upto = rn < header_n ? rn : header_n;
            for (int c = 0; c < upto; c++) {
                int aa = col_to_attr[c];
                if (aa < 0) continue;
                const char *raw = rec_fields[c];
                if (attr_kind[aa] == 0 /*INT*/) {
                    const char *q = raw;
                    int neg = 0;
                    if (*q == '-') { neg = 1; q++; }
                    else if (*q == '+') { q++; }
                    long v = 0;
                    int ok = (*q != '\0');
                    while (*q) {
                        unsigned d = (unsigned)(*q - '0');
                        if (d > 9u) { ok = 0; break; }
                        v = v * 10 + (long)d; q++;
                    }
                    if (!ok) {
                        char *endp = NULL;
                        v = strtol(raw, &endp, 10);
                    } else if (neg) v = -v;
                    ((int*)gcols[aa])[global_row] = (int)v;
                } else {
                    ((char**)gcols[aa])[global_row] = (char*)raw;
                }
            }
            row_idx++;
        }
    }
    free(rec_fields);
    a->row_count = row_idx;
}

static void *csv_combined_col_worker(void *p) {
    CSVColWorkerArgs *a = (CSVColWorkerArgs*)p;

    /* === Phase A: boundary adjustment + SIMD row count === */
    {
        char   *src       = a->src;
        size_t  total_len = a->total_len;
        size_t  pos       = a->chunk_start;
        if (!a->is_first && pos > 0 && src[pos - 1] != '\n') {
            while (pos < total_len && src[pos] != '\n') pos++;
            if (pos < total_len) pos++;
        }
        a->actual_parse_start = pos;
        a->row_count = (int)csv_count_newlines(src, pos, a->chunk_end);
    }

    /* Señalar Phase A completa (release → main la ve sin lag). */
    __atomic_store_n(&a->phase_a_done, 1, __ATOMIC_RELEASE);

    /* Spinear hasta que main señale Phase B (acquire → ve global_col_data). */
    while (!__atomic_load_n(&a->go_phase_b, __ATOMIC_ACQUIRE))
        cpu_relax();

    /* Phase B: skip si main no pudo alocar. */
    if (!a->global_col_data) return NULL;

    /* === Phase B: parsear y escribir directo al DataFrame global === */
    csv_do_phase_b(a);
    return NULL;
}

/* === Global CSV Worker Pool ===
 * Threads inicializados al startup del programa (antes del primer CSV load).
 * Esto elimina el overhead de pthread_create por cada carga de CSV en Docker
 * (~3ms/thread × 11 threads = ~33ms por carga → 0ms con pool pre-iniciado).
 * Workers esperan tareas con spinwait + sched_yield (idle ≈ 0% CPU). */
#define CSV_POOL_IDLE    0
#define CSV_POOL_PHASE_A 1
#define CSV_POOL_PHASE_B 2
#define CSV_POOL_EXIT    3
#define CSV_POOL_MAX     15  /* máximo de worker threads en pool (main es el +1) */

static CSVColWorkerArgs g_csv_slots[CSV_POOL_MAX];
static pthread_t        g_csv_pthreads[CSV_POOL_MAX];
static volatile int     g_csv_pool_n = 0;  /* threads vivos en pool */
static volatile int     g_csv_pool_ready_count = 0; /* threads que llegaron al idle loop */

static void *csv_pool_worker_fn(void *arg) {
    CSVColWorkerArgs *s = (CSVColWorkerArgs*)arg;
    /* Notificar al main que este thread llegó al idle spinwait loop. */
    __atomic_fetch_add(&g_csv_pool_ready_count, 1, __ATOMIC_RELEASE);
    int idle_spin = 0;
    while (1) {
        int p = __atomic_load_n(&s->pool_phase, __ATOMIC_ACQUIRE);
        if (p == CSV_POOL_IDLE) {
            cpu_relax();
            /* Evitar quemar CPU cuando no hay trabajo: yield periódicamente. */
            if (++idle_spin > 5000) { sched_yield(); idle_spin = 0; }
            continue;
        }
        idle_spin = 0;
        if (p == CSV_POOL_EXIT) return NULL;
        if (p == CSV_POOL_PHASE_A) {
            /* Phase A: boundary adjustment + SIMD count */
            size_t pos2 = s->chunk_start;
            if (!s->is_first && pos2 > 0 && s->src[pos2 - 1] != '\n') {
                while (pos2 < s->total_len && s->src[pos2] != '\n') pos2++;
                if (pos2 < s->total_len) pos2++;
            }
            s->actual_parse_start = pos2;
            s->row_count = (int)csv_count_newlines(s->src, pos2, s->chunk_end);
        } else if (p == CSV_POOL_PHASE_B) {
            /* Phase B: parsear y escribir en arrays globales */
            csv_do_phase_b(s);
        }
        /* Señalar idle (done) → main lo detecta sin spinwait externo */
        __atomic_store_n(&s->pool_phase, CSV_POOL_IDLE, __ATOMIC_RELEASE);
    }
}

/* Inicializar pool global. Llamar desde main() antes de ejecutar el script.
 * n = número total de workers (incluyendo main); crea n-1 pool threads.
 * BLOQUEA hasta que todos los threads estén listos en el idle spinwait loop.
 * Esto mueve el overhead de pthread_create (~3ms/thread en Docker) al startup,
 * ANTES del timer de CSV, para que los loads posteriores sean instantáneos. */
void te_csv_pool_init(int n) {
    /* DESHABILITADO: el pool path causaba starvation del main por workers
     * spinning en cores. Mantener el stub para compat con typeeasy_main.c. */
    (void)n;
}
#else /* !TE_HAS_PTHREAD */
void te_csv_pool_init(int n) { (void)n; }
#endif /* TE_HAS_PTHREAD */

/* Construye un DataFrame.
 *
 * Algoritmo: Phase A (spinwait, no futex) → prefix-sum → Phase B directo.
 * Elimina el doble pthread_barrier_wait (~10ms en Docker/WSL2) usando
 * atomic spinwait (<1μs latencia) para coordinar Phase A → Phase B.
 */
static DataFrame *csv_build_dataframe(char *src, size_t len, size_t pos,
                                       ClassNode *cls,
                                       int *attr_kind, int *attr_nullable,
                                       int *col_to_attr,
                                       char **header, int header_n,
                                       int n_workers,
                                       struct timespec *ts_after_count_out,
                                       int readonly_src) {
    int nattr = cls->attr_count;
    for (int a = 0; a < nattr; a++) {
        if (attr_kind[a] != 0 && attr_kind[a] != 1) return NULL;
        if (attr_nullable[a]) return NULL;
    }
    (void)header;
    if (n_workers < 1) n_workers = 1;

    CSVColWorkerArgs *args = (CSVColWorkerArgs*)calloc(n_workers, sizeof(CSVColWorkerArgs));
    size_t data_len = len - pos;
    for (int w = 0; w < n_workers; w++) {
        args[w].src         = src;
        args[w].total_len   = len;
        args[w].chunk_start = pos + (size_t)w * (data_len / n_workers);
        args[w].chunk_end   = (w == n_workers - 1) ? len
                              : pos + (size_t)(w + 1) * (data_len / n_workers);
        args[w].is_first    = (w == 0);
        args[w].nattr       = nattr;
        args[w].header_n    = header_n;
        args[w].attr_kind   = attr_kind;
        args[w].col_to_attr = col_to_attr;
        args[w].phase_a_done = 0;
        args[w].go_phase_b   = 0;
        args[w].readonly_src = readonly_src;
    }

#if TE_HAS_PTHREAD
    /* --- POOL PATH (DESHABILITADO): los threads spinning del pool consumen
     *     todos los cores y starvan al main durante el trabajo serial previo
     *     (has_quote scan, header parse). Net negativo. Mantenido el código
     *     para experimentación futura, pero el if siempre es false. --- */
    int pool_n = __atomic_load_n(&g_csv_pool_n, __ATOMIC_ACQUIRE);
    if (0 && pool_n > 0 && n_workers >= 2) {
        int nw = pool_n < n_workers - 1 ? pool_n : n_workers - 1;
        int n_total = nw + 1;  /* nw pool workers + 1 main */

        /* Rechazar chunks con data_len 0. */
        if (data_len == 0) { free(args); return NULL; }

        /* Llenar slots del pool para workers 1..nw (chunk 0 = main). */
        for (int w = 0; w < nw; w++) {
            size_t w_start = pos + (size_t)(w + 1) * (data_len / n_total);
            size_t w_end   = (w + 1 == nw) ? len
                             : pos + (size_t)(w + 2) * (data_len / n_total);
            g_csv_slots[w].src          = src;
            g_csv_slots[w].total_len    = len;
            g_csv_slots[w].chunk_start  = w_start;
            g_csv_slots[w].chunk_end    = w_end;
            g_csv_slots[w].is_first     = 0;
            g_csv_slots[w].nattr        = nattr;
            g_csv_slots[w].header_n     = header_n;
            g_csv_slots[w].attr_kind    = attr_kind;
            g_csv_slots[w].col_to_attr  = col_to_attr;
            g_csv_slots[w].readonly_src = readonly_src;
            g_csv_slots[w].global_col_data = NULL;
        }
        size_t main_chunk_end = pos + data_len / n_total;

        /* Dispatch Phase A a los pool workers (ya corriendo → latencia ~1μs). */
        for (int w = 0; w < nw; w++)
            __atomic_store_n(&g_csv_slots[w].pool_phase, CSV_POOL_PHASE_A, __ATOMIC_RELEASE);

        /* Main: Phase A para chunk 0 (inline, is_first → no boundary adjust). */
        int main_row_count = (int)csv_count_newlines(src, pos, main_chunk_end);

        /* Spinwait para pool workers. */
        for (int w = 0; w < nw; w++)
            while (__atomic_load_n(&g_csv_slots[w].pool_phase, __ATOMIC_ACQUIRE) != CSV_POOL_IDLE)
                cpu_relax();
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        /* Prefix-sum. */
        int total_rows = main_row_count;
        for (int w = 0; w < nw; w++) {
            g_csv_slots[w].row_offset = total_rows;
            total_rows += g_csv_slots[w].row_count;
        }

        if (total_rows <= 0) { free(args); return NULL; }

        /* Alocar DataFrame con tamaño exacto. */
        DataFrame *df = (DataFrame*)calloc(1, sizeof(DataFrame));
        df->col_count = nattr; df->cls = cls;
        df->col_kinds = (int*)malloc(nattr * sizeof(int));
        df->col_data  = (void**)calloc(nattr, sizeof(void*));
        df->col_names = (char**)malloc(nattr * sizeof(char*));
        for (int a = 0; a < nattr; a++) {
            df->col_kinds[a] = attr_kind[a];
            df->col_names[a] = cls->attributes[a].id;
            size_t slot = (attr_kind[a] == 0) ? sizeof(int) : sizeof(char*);
            df->col_data[a] = malloc((size_t)total_rows * slot);
        }

        /* Propagar global_col_data a pool workers. */
        for (int w = 0; w < nw; w++)
            g_csv_slots[w].global_col_data = df->col_data;

        /* Dispatch Phase B a los pool workers. */
        for (int w = 0; w < nw; w++)
            __atomic_store_n(&g_csv_slots[w].pool_phase, CSV_POOL_PHASE_B, __ATOMIC_RELEASE);

        /* Main: Phase B para chunk 0 (en paralelo con pool workers). */
        {
            CSVColWorkerArgs ma = {0};
            ma.src               = src;
            ma.total_len         = len;
            ma.actual_parse_start = pos;  /* is_first, no boundary */
            ma.chunk_end         = main_chunk_end;
            ma.header_n          = header_n;
            ma.attr_kind         = attr_kind;
            ma.col_to_attr       = col_to_attr;
            ma.global_col_data   = df->col_data;
            ma.row_offset        = 0;
            ma.readonly_src      = readonly_src;
            csv_do_phase_b(&ma);
            main_row_count = ma.row_count;
        }

        /* Spinwait hasta que todos los pool workers terminen Phase B. */
        for (int w = 0; w < nw; w++)
            while (__atomic_load_n(&g_csv_slots[w].pool_phase, __ATOMIC_ACQUIRE) != CSV_POOL_IDLE)
                cpu_relax();

        int total_actual = main_row_count;
        for (int w = 0; w < nw; w++) total_actual += g_csv_slots[w].row_count;
        df->row_count = total_actual;
        free(args);
        return df;
    }

    /* --- FALLBACK: pool no inicializado → crear threads (pthread_create). --- */
    if (n_workers >= 2) {
        /* Patrón "main-as-worker-0": solo crea (n_workers-1) threads. */
        int nw = n_workers - 1;
        pthread_t *tids = (pthread_t*)malloc(nw * sizeof(pthread_t));
        for (int w = 1; w <= nw; w++)
            pthread_create(&tids[w-1], NULL, csv_combined_col_worker, &args[w]);

        /* Main: Phase A para chunk 0. */
        {
            size_t p2 = args[0].chunk_start;
            args[0].actual_parse_start = p2;
            args[0].row_count = (int)csv_count_newlines(src, p2, args[0].chunk_end);
        }

        for (int w = 1; w < n_workers; w++)
            while (!__atomic_load_n(&args[w].phase_a_done, __ATOMIC_ACQUIRE))
                cpu_relax();
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        int total_rows = 0;
        for (int w = 0; w < n_workers; w++) {
            args[w].row_offset = total_rows;
            total_rows += args[w].row_count;
        }

        DataFrame *df = NULL;
        if (total_rows > 0) {
            df = (DataFrame*)calloc(1, sizeof(DataFrame));
            df->col_count = nattr; df->cls = cls;
            df->col_kinds = (int*)malloc(nattr * sizeof(int));
            df->col_data  = (void**)calloc(nattr, sizeof(void*));
            df->col_names = (char**)malloc(nattr * sizeof(char*));
            for (int a = 0; a < nattr; a++) {
                df->col_kinds[a] = attr_kind[a];
                df->col_names[a] = cls->attributes[a].id;
                size_t slot = (attr_kind[a] == 0) ? sizeof(int) : sizeof(char*);
                df->col_data[a] = malloc((size_t)total_rows * slot);
            }
            for (int w = 0; w < n_workers; w++)
                args[w].global_col_data = df->col_data;
        }

        for (int w = 1; w < n_workers; w++)
            __atomic_store_n(&args[w].go_phase_b, 1, __ATOMIC_RELEASE);

        args[0].go_phase_b = 1;
        if (df) csv_combined_col_worker(&args[0]);

        for (int w = 0; w < nw; w++) pthread_join(tids[w], NULL);
        free(tids);

        if (!df) { free(args); return NULL; }

        int total_actual = 0;
        for (int w = 0; w < n_workers; w++) total_actual += args[w].row_count;
        df->row_count = total_actual;
        free(args);
        return df;
    }
#endif /* TE_HAS_PTHREAD */

    /* Serial (n_workers==1 o sin pthread). */
    {
        size_t p2 = args[0].chunk_start;
        if (!args[0].is_first && p2 > 0 && src[p2-1] != '\n') {
            while (p2 < len && src[p2] != '\n') p2++;
            if (p2 < len) p2++;
        }
        args[0].actual_parse_start = p2;
        args[0].row_count = (int)csv_count_newlines(src, p2, args[0].chunk_end);
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        int total_rows = args[0].row_count;
        if (total_rows <= 0) { free(args); return NULL; }

        DataFrame *df = (DataFrame*)calloc(1, sizeof(DataFrame));
        df->col_count = nattr; df->cls = cls;
        df->col_kinds = (int*)malloc(nattr * sizeof(int));
        df->col_data  = (void**)calloc(nattr, sizeof(void*));
        df->col_names = (char**)malloc(nattr * sizeof(char*));
        for (int a = 0; a < nattr; a++) {
            df->col_kinds[a] = attr_kind[a];
            df->col_names[a] = cls->attributes[a].id;
            size_t slot = (attr_kind[a] == 0) ? sizeof(int) : sizeof(char*);
            df->col_data[a] = malloc((size_t)total_rows * slot);
        }
        args[0].row_offset = 0;
        args[0].global_col_data = df->col_data;
        args[0].go_phase_b = 1;   /* auto-señal para serial */
        csv_combined_col_worker(&args[0]); /* Phase B serial */
        df->row_count = args[0].row_count;
        free(args);
        return df;
    }
}

typedef struct CSVParseCfg {
    ClassNode *cls;
    int nattr;
    int header_n;
    int *attr_kind;
    int *attr_nullable;
    int *col_to_attr;
    char **shared_attr_id;
    char **shared_attr_type;
    char *null_type;
    char *shared_class_name;
    char *shared_obj_type;
    char **header_for_errors;
    const char *filename_for_errors;
    /* Pre-built Variable[nattr] template (id/type/vtype/is_const set, value zeroed).
     * Workers memcpy esto a obj->attributes en lugar del init-loop nattr*6 writes. */
    Variable *row_template;
    size_t row_template_bytes; /* nattr * sizeof(Variable) */
} CSVParseCfg;

typedef struct CSVWorkerArgs {
    const CSVParseCfg *cfg;
    char *src;
    size_t total_len;
    size_t chunk_start;
    size_t chunk_end;
    int is_first;
    /* outputs */
    ASTNode *first;
    ASTNode *last;
    CSVChunk *arena_head;
    ASTNodePool *pool_head;
} CSVWorkerArgs;

/* Parsea un rango. Usa thread-local arena/pool. Devuelve sub-lista. */
static void csv_parse_chunk(const CSVParseCfg *cfg, char *src, size_t total_len,
                             size_t start, size_t end, int is_first,
                             ASTNode **out_first, ASTNode **out_last) {
    int header_n = cfg->header_n;
    int nattr = cfg->nattr;
    const int *attr_kind = cfg->attr_kind;
    const int *attr_nullable = cfg->attr_nullable;
    const int *col_to_attr = cfg->col_to_attr;
    char **shared_attr_id = cfg->shared_attr_id;
    char **shared_attr_type = cfg->shared_attr_type;
    char *null_type = cfg->null_type;
    char *shared_class_name = cfg->shared_class_name;
    char **header = cfg->header_for_errors;

    size_t pos = start;
    if (!is_first) {
        /* Si chunk_start cae JUSTO al inicio de una fila (el byte anterior
         * es '\n'), entonces nuestra primera fila empieza EN start \u2014 no hay
         * fila parcial que saltar. Si saltáramos, perderíamos esa fila
         * (el worker previo paró en row_begin >= end == start, y no la
         * procesó). Sólo saltar si estamos a mitad de fila. */
        if (pos > 0 && src[pos - 1] != '\n') {
            /* Buscar SIEMPRE en [pos, total_len), no sólo en [pos, end),
             * porque el primer '\n' puede estar dentro del próximo chunk. */
            while (pos < total_len && src[pos] != '\n') pos++;
            if (pos < total_len) pos++;
        }
    }

    char **rec_fields = (char**)malloc(header_n * sizeof(char*));
    ASTNode *first = NULL, *last = NULL;
    int row_idx = 0;

    while (pos < end) {
        size_t row_begin = pos;
        int rn = csv_next_record_zerocopy(src, total_len, &pos, rec_fields, header_n);
        if (rn <= 0) break;
        /* Si la fila comenzó en/después de end, no es nuestra. */
        if (row_begin >= end) break;
        row_idx++;

        if (rn == 1 && rec_fields[0][0] == '\0') continue;

        char **rec = rec_fields;

        ObjectNode *obj = (ObjectNode*)csv_arena_alloc(sizeof(ObjectNode));
        obj->class = cfg->cls;
        obj->attributes = (Variable*)csv_arena_alloc(nattr * sizeof(Variable));
        /* Bulk-init via memcpy del template precomputado (id/type/vtype/is_const ya seteados).
         * Reemplaza nattr*6 writes individuales por una memcpy de ~48 bytes (cache hot). */
        memcpy(obj->attributes, cfg->row_template, cfg->row_template_bytes);

        int upto = rn < header_n ? rn : header_n;
        for (int c = 0; c < upto; c++) {
            int a = col_to_attr[c];
            if (a < 0) continue;
            const char *raw = rec[c];

            if (attr_kind[a] == 0 /*INT*/) {
                if (raw[0] == '\0') {
                    if (attr_nullable[a]) {
                        obj->attributes[a].type = null_type;
                        obj->attributes[a].value.int_value = 0;
                    } else {
                        fprintf(stderr,
                            "CSVError: fila %d, columna '%s' (int) está vacía.\n",
                            row_idx, header[c]);
                        exit(1);
                    }
                } else {
                    /* Fast int parser: hot path 100% dígitos ASCII (con signo opcional).
                     * strtol pesa por locale + setear errno + skip whitespace + base detect.
                     * Fallback a strtol si encontramos algo raro. */
                    const char *p = raw;
                    int neg = 0;
                    if (*p == '-') { neg = 1; p++; }
                    else if (*p == '+') { p++; }
                    long v = 0;
                    int ok = (*p != '\0');
                    while (*p) {
                        unsigned d = (unsigned)(*p - '0');
                        if (d > 9u) { ok = 0; break; }
                        v = v * 10 + (long)d;
                        p++;
                    }
                    if (!ok) {
                        char *endp = NULL;
                        v = strtol(raw, &endp, 10);
                        if (endp == raw || (endp && *endp != '\0')) {
                            fprintf(stderr,
                                "CSVError: fila %d, columna '%s': '%s' no es int.\n",
                                row_idx, header[c], raw);
                            exit(1);
                        }
                    } else if (neg) {
                        v = -v;
                    }
                    obj->attributes[a].value.int_value = (int)v;
                }
            } else if (attr_kind[a] == 1 /*STRING*/) {
                if (raw[0] == '\0' && attr_nullable[a]) {
                    obj->attributes[a].type = null_type;
                    obj->attributes[a].value.string_value = NULL;
                } else {
                    /* ZERO-COPY: raw apunta dentro de src (mmap MAP_PRIVATE)
                     * y ya está null-terminado por el scanner. src vive hasta
                     * exit (no se munmap). Saltamos memcpy de N filas × M cols.
                     * RIESGO: si el script reasigna obj.string_attr=X y ese
                     * código hace free(viejo), crash. Aceptable para CSV read-only. */
                    obj->attributes[a].value.string_value = (char*)raw;
                }
            } else {
                obj->attributes[a].vtype = VAL_STRING;
                obj->attributes[a].value.string_value = (char*)raw;
            }
        }

        for (int a = 0; a < nattr; a++) {
            if (attr_kind[a] == 1 /*STRING*/
                && obj->attributes[a].vtype == VAL_STRING
                && obj->attributes[a].value.string_value == NULL) {
                if (attr_nullable[a]) {
                    obj->attributes[a].type = null_type;
                } else {
                    fprintf(stderr,
                        "CSVError: fila %d: atributo '%s' (no nullable) sin valor.\n",
                        row_idx, cfg->cls->attributes[a].id);
                    exit(1);
                }
            }
        }

        /* Wrapper ASTNode desde POOL bump (4096 nodes/block, calloc'd).
         * free_ast respeta `from_pool` y no llama free(node).
         * type = sentinel global g_csv_wrapper_obj_type (free_ast lo skip).
         * id  = shared_class_name + id_interned=1 (free_ast lo skip).
         * Bloques del pool se linkean a g_ast_pool_keepalive y viven al exit. */
        ASTNode *item = ast_pool_alloc();
        item->type = cfg->shared_obj_type;
        item->id = shared_class_name;
        item->id_interned = 1;
        item->from_pool = 1;
        item->extra = (struct ASTNode*)obj;
        item->value = (int)(intptr_t)obj;

        if (!first) { first = last = item; }
        else        { last->next = item; last = item; }
    }

    free(rec_fields);
    *out_first = first;
    *out_last  = last;
}

#if TE_HAS_PTHREAD
static void *csv_parse_worker(void *p) {
    CSVWorkerArgs *a = (CSVWorkerArgs*)p;
    /* Reset thread-local arena/pool para esta corrida. */
    t_csv_arena = NULL;
    t_ast_pool = NULL;
    csv_parse_chunk(a->cfg, a->src, a->total_len,
                    a->chunk_start, a->chunk_end, a->is_first,
                    &a->first, &a->last);
    a->arena_head = t_csv_arena;
    a->pool_head  = t_ast_pool;
    return NULL;
}
#endif

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls) {
    /* Profiling opcional via env TE_CSV_TIMING=1 */
    const char *te_timing = getenv("TE_CSV_TIMING");
    /* Modo columnar opcional via env TE_CSV_DATAFRAME=1.
     * Skip ObjectNode/Variable/wrapper creation entirely: parsea directo a
     * column buffers tipo Arrow. df.length funciona; iter/index NO. */
    const char *te_df_env = getenv("TE_CSV_DATAFRAME");
    int want_columnar = te_df_env && atoi(te_df_env) > 0;
    struct timespec ts0, ts_after_mmap, ts_after_header, ts_before_parse, ts_after_parse, ts_end;
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts0);

    size_t len = 0;
    /* Usar pread en lugar de mmap para evitar page-fault overhead en Docker
     * overlay FS / WSL2 9P. Ver comentario en csv_read_file(). */
    int src_is_mmap = 0;
    char *src = csv_read_file(filename, &len, &src_is_mmap);
    if (!src) {
        fprintf(stderr, "IOError: no se pudo abrir/mapear el archivo CSV '%s'.\n", filename);
        exit(1);
    }
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_mmap);

    size_t pos = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF
                 && (unsigned char)src[1] == 0xBB
                 && (unsigned char)src[2] == 0xBF) {
        pos = 3;
    }

    int header_n = 0;
    char **header = csv_next_record(src, len, &pos, &header_n);
    if (!header) {
        fprintf(stderr, "CSVError: archivo '%s' está vacío.\n", filename);
        exit(1);
    }

    int nattr = cls->attr_count;

    /* Pre-classify each attribute slot. */
    enum { K_INT = 0, K_STRING = 1, K_OTHER = 2 };
    int *attr_kind = (int*)malloc(nattr * sizeof(int));
    int *attr_nullable = (int*)malloc(nattr * sizeof(int));
    for (int a = 0; a < nattr; a++) {
        const char *t = cls->attributes[a].type;
        attr_kind[a] = csv_attr_is_int(t) ? K_INT
                     : csv_attr_is_string(t) ? K_STRING
                     : K_OTHER;
        attr_nullable[a] = csv_attr_is_nullable(t);
    }

    /* Map columnas CSV -> índice de atributo. */
    int *col_to_attr = (int*)malloc(header_n * sizeof(int));
    for (int c = 0; c < header_n; c++) col_to_attr[c] = -1;
    for (int c = 0; c < header_n; c++) {
        for (int a = 0; a < nattr; a++) {
            if (cls->attributes[a].id && !strcmp(cls->attributes[a].id, header[c])) {
                col_to_attr[c] = a; break;
            }
        }
    }
    for (int a = 0; a < nattr; a++) {
        int found = 0;
        for (int c = 0; c < header_n; c++) if (col_to_attr[c] == a) { found = 1; break; }
        if (!found && !attr_nullable[a]) {
            fprintf(stderr,
                "CSVError: atributo '%s' (clase %s) no tiene columna en '%s'.\n",
                cls->attributes[a].id, cls->name, filename);
            csv_free_record(header, header_n);
            free(col_to_attr); free(attr_kind); free(attr_nullable);
            exit(1);
        }
    }

    /* Pre-cache attribute id/type strings UNA SOLA VEZ por carga, en arena
     * (main thread). Workers usan estos punteros directos (read-only). */
    char **shared_attr_id_arena   = (char**)malloc(nattr * sizeof(char*));
    char **shared_attr_type_arena = (char**)malloc(nattr * sizeof(char*));
    char *null_type_arena = csv_arena_strdup("NULL");
    char *shared_class_name_arena = csv_arena_strdup(cls->name);
    /* Inicializa una sola vez el literal global del type del wrapper. */
    if (!g_csv_wrapper_obj_type) {
        g_csv_wrapper_obj_type = csv_arena_strdup("OBJECT");
    }
    char *shared_obj_type = g_csv_wrapper_obj_type;
    for (int a = 0; a < nattr; a++) {
        shared_attr_id_arena[a]   = csv_arena_strdup(cls->attributes[a].id);
        shared_attr_type_arena[a] = csv_arena_strdup(cls->attributes[a].type);
    }

    /* Row template: prebuild Variable[nattr] que se memcpy-ea por fila. */
    Variable *row_template = (Variable*)csv_arena_alloc(nattr * sizeof(Variable));
    memset(row_template, 0, nattr * sizeof(Variable));
    for (int a = 0; a < nattr; a++) {
        row_template[a].id = shared_attr_id_arena[a];
        row_template[a].type = shared_attr_type_arena[a];
        row_template[a].is_const = 0;
        row_template[a].vtype = (attr_kind[a] == 0 /*INT*/) ? VAL_INT : VAL_STRING;
        /* value bytes ya en cero por memset; suficiente para int=0 y string_value=NULL. */
    }

    /* Config compartida read-only para workers. */
    CSVParseCfg cfg;
    cfg.cls = cls;
    cfg.nattr = nattr;
    cfg.header_n = header_n;
    cfg.attr_kind = attr_kind;
    cfg.attr_nullable = attr_nullable;
    cfg.col_to_attr = col_to_attr;
    cfg.shared_attr_id = shared_attr_id_arena;
    cfg.shared_attr_type = shared_attr_type_arena;
    cfg.null_type = null_type_arena;
    cfg.shared_class_name = shared_class_name_arena;
    cfg.shared_obj_type = shared_obj_type;
    cfg.header_for_errors = header;
    cfg.filename_for_errors = filename;
    cfg.row_template = row_template;
    cfg.row_template_bytes = (size_t)nattr * sizeof(Variable);

    ASTNode *first = NULL, *last = NULL;

    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_header);
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_before_parse);

    /* Decidir si paralelizar. Threshold: archivo > 256KB y >= 2 cores.
     * Limitación: el splitter por chunks asume que '\n' es siempre fin
     * de fila (no hay newlines dentro de campos quoted). Para CSVs con
     * embedded newlines, forzar serial. Heuristic simple: si hay '"' en
     * el archivo, NO paralelizar (conservador y rápido de testear).
     * (Para nuestro bench productos.csv: sin quotes → paraleliza.) */
    int can_parallel = 0;
#if TE_HAS_PTHREAD
    /* Override por env: TE_CSV_THREADS=N (0 fuerza serial). */
    const char *te_threads_env = getenv("TE_CSV_THREADS");
    int forced = te_threads_env ? atoi(te_threads_env) : -1;
    if (forced == 0) {
        can_parallel = 0;
    } else if (len - pos > (256u * 1024u)) {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpu < 1) ncpu = 1;
        /* Cap adaptativo según tamaño del archivo.
         * Empíricamente: ~2MB de CSV por thread es el sweet spot
         * (chunks más pequeños hacen que la coordinación domine sobre
         * el paralelismo real). Cap absoluto a 32 para evitar contention
         * extrema en cajas con muchos cores. */
        size_t bytes = len - pos;
        long ideal_by_size = (long)(bytes / (2u * 1024u * 1024u));
        if (ideal_by_size < 2) ideal_by_size = 2;
        if (ncpu > ideal_by_size) ncpu = ideal_by_size;
        if (ncpu > 32) ncpu = 32;
        if (forced > 0) ncpu = forced;
        if (ncpu >= 2) {
            /* Detección rápida de quotes (8 bytes a la vez). */
            int has_quote = 0;
            for (size_t i = pos; i < len; i++) {
                if (src[i] == '"') { has_quote = 1; break; }
            }
            if (!has_quote) can_parallel = (int)ncpu;
        }
    }
#endif

    /* ===== RUTA COLUMNAR (DataFrame). Solo si TE_CSV_DATAFRAME=1 y la
     * clase es elegible (int/string puros). Saltamos la fase row-objects.
     * Importante: usar la MISMA decisión de threads que el path normal. */
    if (want_columnar) {
        int n_workers = can_parallel >= 2 ? can_parallel : 1;
        struct timespec ts_after_count;
        DataFrame *df = csv_build_dataframe(src, len, pos, cls,
                                             attr_kind, attr_nullable, col_to_attr,
                                             header, header_n, n_workers,
                                             te_timing ? &ts_after_count : NULL,
                                             src_is_mmap);
        if (df) {
            if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_parse);
            csv_free_record(header, header_n);
            free(col_to_attr); free(attr_kind); free(attr_nullable);
            free(shared_attr_id_arena); free(shared_attr_type_arena);
            /* Wrapper LIST con TEListIdx pre-set para que .length sea O(1)
             * sin crear ASTNodes hijos. left=NULL → iter no soportada. */
            ASTNode *listNode = (ASTNode*)calloc(1, sizeof(ASTNode));
            listNode->type = strdup("LIST");
            listNode->left = NULL;
            listNode->value = 1; /* PREINIT FLAG */
            TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
            ix->len = df->row_count;
            ix->cap = 0;
            ix->items = NULL; /* sin items materializados */
            listNode->extra = (struct ASTNode*)ix;
            /* DataFrame se "leakea" (lifetime de proceso, modo script). */
            (void)df;
            if (te_timing) {
                clock_gettime(CLOCK_MONOTONIC, &ts_end);
                long us_mmap   = (ts_after_mmap.tv_sec   - ts0.tv_sec)*1000000L + (ts_after_mmap.tv_nsec   - ts0.tv_nsec)/1000L;
                long us_header = (ts_after_header.tv_sec - ts_after_mmap.tv_sec)*1000000L + (ts_after_header.tv_nsec - ts_after_mmap.tv_nsec)/1000L;
                long us_parse_par = (ts_after_count.tv_sec  - ts_before_parse.tv_sec)*1000000L + (ts_after_count.tv_nsec  - ts_before_parse.tv_nsec)/1000L;
                long us_memcpy    = (ts_after_parse.tv_sec  - ts_after_count.tv_sec)*1000000L + (ts_after_parse.tv_nsec  - ts_after_count.tv_nsec)/1000L;
                long us_total  = (ts_end.tv_sec - ts0.tv_sec)*1000000L + (ts_end.tv_nsec - ts0.tv_nsec)/1000L;
                /* parse = Phase A (count newlines paralelo, SIMD AVX2)
                 * phaseB = Phase B (parse campos + write directo al DataFrame, paralelo) */
                fprintf(stderr, "[CSV-COL] io=%ldus header=%ldus phaseA=%ldus phaseB=%ldus total=%ldus rows=%d\n",
                        us_mmap, us_header, us_parse_par, us_memcpy, us_total, df->row_count);
            }
            return listNode;
        }
        /* Fallback al path normal si la clase no era elegible. */
    }

#if TE_HAS_PTHREAD
    if (can_parallel >= 2) {
        int N = can_parallel;
        pthread_t *tids = (pthread_t*)malloc(N * sizeof(pthread_t));
        CSVWorkerArgs *args = (CSVWorkerArgs*)calloc(N, sizeof(CSVWorkerArgs));

        size_t data_start = pos;
        size_t data_len = len - data_start;
        size_t chunk = data_len / N;

        for (int i = 0; i < N; i++) {
            args[i].cfg = &cfg;
            args[i].src = src;
            args[i].total_len = len;
            args[i].chunk_start = data_start + (size_t)i * chunk;
            args[i].chunk_end   = (i == N - 1) ? len : (data_start + (size_t)(i + 1) * chunk);
            args[i].is_first    = (i == 0);
            pthread_create(&tids[i], NULL, csv_parse_worker, &args[i]);
        }
        for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);

        /* Linkar arenas y pools de cada worker para que sobrevivan al exit. */
        for (int i = 0; i < N; i++) {
            csv_arena_keepalive_link(args[i].arena_head);
            ast_pool_keepalive_link(args[i].pool_head);
            if (args[i].first) {
                if (!first) { first = args[i].first; last = args[i].last; }
                else        { last->next = args[i].first; last = args[i].last; }
            }
        }
        free(tids);
        free(args);
    } else
#endif
    {
        /* Serial fallback: 1 chunk = todo. */
        ASTNode *f = NULL, *l = NULL;
        csv_parse_chunk(&cfg, src, len, pos, len, /*is_first=*/1, &f, &l);
        first = f; last = l;
        /* Linkar el pool del thread principal a keepalive. Aunque vivirá hasta
         * exit de todos modos, mantener el invariante simplifica razonar. */
        ast_pool_keepalive_link(t_ast_pool);
        t_ast_pool = NULL;
    }

    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_parse);

    csv_free_record(header, header_n);
    free(col_to_attr);
    free(attr_kind);
    free(attr_nullable);
    free(shared_attr_id_arena);
    free(shared_attr_type_arena);
    /* `src` NO se libera: las strings de los objetos apuntan dentro (zero-copy
     * desde mmap con MAP_PRIVATE). Vive lo que dura el proceso. */

    ASTNode* listNode = calloc(1, sizeof(ASTNode));
    listNode->type = strdup("LIST");
    listNode->left = first;
    listNode->right = NULL;
    listNode->next = NULL;
    listNode->id = NULL;
    listNode->str_value = NULL;
    listNode->value = 1; /* PREINIT FLAG: skip clone+ctor en declare_variable */
    if (te_timing) {
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        long us_mmap   = (ts_after_mmap.tv_sec   - ts0.tv_sec)*1000000L + (ts_after_mmap.tv_nsec   - ts0.tv_nsec)/1000L;
        long us_header = (ts_after_header.tv_sec - ts_after_mmap.tv_sec)*1000000L + (ts_after_header.tv_nsec - ts_after_mmap.tv_nsec)/1000L;
        long us_parse  = (ts_after_parse.tv_sec  - ts_before_parse.tv_sec)*1000000L + (ts_after_parse.tv_nsec  - ts_before_parse.tv_nsec)/1000L;
        long us_end    = (ts_end.tv_sec - ts_after_parse.tv_sec)*1000000L + (ts_end.tv_nsec - ts_after_parse.tv_nsec)/1000L;
        long us_total  = (ts_end.tv_sec - ts0.tv_sec)*1000000L + (ts_end.tv_nsec - ts0.tv_nsec)/1000L;
        fprintf(stderr, "[CSV] mmap=%ldus header=%ldus parse=%ldus tail=%ldus total=%ldus\n",
                us_mmap, us_header, us_parse, us_end, us_total);
    }
    return listNode;
}



static void interpret_var_decl(ASTNode *node) {
    //printf("[DEBUG] interpret_var_decl: %s\n", node->id); fflush(stdout);
    int is_const_flag = node->value;
    const char* declared_type = node->str_value;
    ASTNode* value_node = node->left;
    Variable *evaluated_value_var = NULL;

    /* Fase 7: NULL_COALESCE — pick the right side at decl time */
    if (value_node && value_node->type && strcmp(value_node->type, "NULL_COALESCE") == 0) {
        ASTNode *l = value_node->left;
        int is_null = 0;
        if (l && l->type && strcmp(l->type, "NULL") == 0) is_null = 1;
        else if (l && l->type && strcmp(l->type, "IDENTIFIER") == 0) {
            Variable *vv = find_variable(l->id);
            if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) is_null = 1;
        }
        value_node = is_null ? value_node->right : l;
        node->left = value_node;
    }

    // 1. Si el valor es una llamada a función, ejecútala primero
if (value_node && (strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0 || strcmp(value_node->type, "FILTER_CALL") == 0 || strcmp(value_node->type, "CALL_FUNC") == 0)) {        
        //printf("[DEBUG] interpret_var_decl: executing function call\n"); fflush(stdout);
        interpret_ast(value_node);
        //printf("[DEBUG] interpret_var_decl: function call returned\n"); fflush(stdout);
        evaluated_value_var = find_variable("__ret__");
        //printf("[DEBUG] interpret_var_decl: find_variable returned %p\n", (void*)evaluated_value_var); fflush(stdout);
        if (!evaluated_value_var) {
            fprintf(stderr, "Error: no return value captured from expression '%s'. __ret_var_active=%d\n", value_node->type ? value_node->type : "unknown", __ret_var_active);
            exit(1);
        }
    }
    // ... (rest of function)
    // I will just replace the beginning to add debug prints.
    // But I need to include the rest of the function or use a smaller range.
    // I will use a smaller range.


    // 2. Comprobación de tipos (Tu lógica es correcta)
    //printf("[DEBUG] interpret_var_decl: type checking\n"); fflush(stdout);
    const char* effective_value_type_str = NULL;
    if (evaluated_value_var != NULL) {
        effective_value_type_str = evaluated_value_var->type;
        //printf("[DEBUG] interpret_var_decl: effective_value_type_str from var: %s\n", effective_value_type_str ? effective_value_type_str : "NULL"); fflush(stdout);
    } else if (value_node != NULL) {
        //printf("[DEBUG] interpret_var_decl: effective_value_type_str from node\n"); fflush(stdout);
        if (strcmp(value_node->type, "NUMBER") == 0) {
            effective_value_type_str = "INT";
        } else if (strcmp(value_node->type, "STRING_LITERAL") == 0 || strcmp(value_node->type, "STRING") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "STRING_INTERP") == 0) {
            effective_value_type_str = "STRING";
        } else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
            if (strcmp(value_node->type, "ADD") == 0 && is_string_type(value_node)) {
                effective_value_type_str = "STRING";
            } else {
                double result = evaluate_expression(value_node);
                if (result == (int)result) {
                    effective_value_type_str = "INT";
                } else {
                    effective_value_type_str = "FLOAT";
                }
            }
        } else {
            effective_value_type_str = value_node->type;
        }
    }

    if (declared_type != NULL && effective_value_type_str != NULL) {
        if (strcmp(declared_type, effective_value_type_str) != 0) {
            int allow_int_to_float = (strcmp(declared_type, "FLOAT") == 0 && strcmp(effective_value_type_str, "INT") == 0);
            if (!allow_int_to_float) {
                /* Fase 2: lanzar como excepción capturable en lugar de abortar */
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "TypeError: cannot assign a value of type '%s' to a variable of type '%s'.",
                    effective_value_type_str, declared_type);
                if (throw_message) free(throw_message);
                throw_message = strdup(buf);
                throw_flag = 1;
                return;
            }
        }
    }

    // 3. Asignación
    //printf("[DEBUG] interpret_var_decl: assignment\n"); fflush(stdout);
    ASTNode *value_to_assign_node = NULL;
    if (evaluated_value_var != NULL) {
        // ---- Si el valor vino de una función (como __ret_var) ----
        //printf("[DEBUG] interpret_var_decl: assigning from evaluated_value_var (vtype=%d)\n", evaluated_value_var->vtype); fflush(stdout);
        
        // Crea un nuevo nodo AST persistente para almacenar el valor
        if (evaluated_value_var->vtype == VAL_STRING) {
            value_to_assign_node = create_ast_leaf("STRING", 0, strdup(evaluated_value_var->value.string_value), NULL);
        } else if (evaluated_value_var->vtype == VAL_INT) {
            //printf("[DEBUG] interpret_var_decl: creating INT node\n"); fflush(stdout);
            value_to_assign_node = create_ast_leaf_number("INT", evaluated_value_var->value.int_value, NULL, NULL);
            //printf("[DEBUG] interpret_var_decl: created INT node\n"); fflush(stdout);
        } else if (evaluated_value_var->vtype == VAL_FLOAT) {
            value_to_assign_node = create_ast_leaf("FLOAT", 0, double_to_string(evaluated_value_var->value.float_value), NULL);
        } else if (evaluated_value_var->vtype == VAL_OBJECT) {
            // Si es LIST, asignar como VAL_OBJECT y type LIST, y value.object_value apunta al nodo LIST
            if (strcmp(evaluated_value_var->type, "LIST") == 0) {
                Variable *var = malloc(sizeof(Variable));
                var->id = strdup(node->id);
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("LIST");
                var->value.object_value = (ObjectNode *)evaluated_value_var->value.object_value; // Apunta al nodo LIST
                vars[var_count++] = *var;
                free(var);
               // printf("[DEBUG] interpret_var_decl: declared LIST variable\n"); fflush(stdout);
                // Limpia la variable de retorno
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
                    if (__ret_var.id) free(__ret_var.id);
                    if (__ret_var.type) free(__ret_var.type);
                    memset(&__ret_var, 0, sizeof(Variable));
                    // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
                }
                return_flag = 0;
                return_node = NULL;
                return;
            } else {
                value_to_assign_node = calloc(1, sizeof(ASTNode));
                value_to_assign_node->type = strdup(evaluated_value_var->type);
                value_to_assign_node->extra = (struct ASTNode*)evaluated_value_var->value.object_value;
                value_to_assign_node->id = evaluated_value_var->id ? strdup(evaluated_value_var->id) : NULL;
                value_to_assign_node->str_value = NULL;
                value_to_assign_node->left = NULL;
                value_to_assign_node->right = NULL;
                value_to_assign_node->next = NULL;
            }
        } else {
            fprintf(stderr, "Internal error: unknown return type for variable assignment '%s'.\n", node->id);
            exit(1);
        }
        
        //printf("[DEBUG] interpret_var_decl: declaring variable %s\n", node->id); fflush(stdout);
        declare_variable(node->id, value_to_assign_node, is_const_flag);
       // printf("[DEBUG] interpret_var_decl: declared variable\n"); fflush(stdout);
        
    // NOTE: Do NOT free 'value_to_assign_node' here. The declared variable
    // stores pointers into the node (or copies them) and freeing it here
    // caused a use-after-free and intermittent segfaults (exit code 139).
    // free_ast(value_to_assign_node); // removed intentionally
        
        // Limpia la variable de retorno
       // printf("[DEBUG] interpret_var_decl: cleaning __ret_var\n"); fflush(stdout);
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
            // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
        }
        return_flag = 0;
        return_node = NULL;
        // ¡OJO! No pongas un 'return' aquí, el código del constructor debe ejecutarse
    
    } else {
        // ---- Si el valor es un literal (ej. let x = 10) ----
        declare_variable(node->id, value_node, is_const_flag);
    }

    // 4. Ejecutar el constructor (Tu lógica es correcta)
    // (Este código se ejecuta para *ambos* casos, lo cual es correcto)
   // printf("[DEBUG] interpret_var_decl: checking constructor\n"); fflush(stdout);
    if (node->left && strcmp(node->left->type, "OBJECT")==0) {
        Variable *var = find_variable(node->id);
        if (!var || var->vtype!=VAL_OBJECT) return;
        
        // (Corrección sutil: `value_node` puede no ser el correcto si vino de __ret_var)
        // Obtener el ASTNode que *realmente* se usó para la declaración
        ASTNode* object_node_for_constructor = (evaluated_value_var != NULL) ? value_to_assign_node : value_node;
        
        if (!object_node_for_constructor || !object_node_for_constructor->left) {
             // Si no hay argumentos (ej. NLU.parse), no hay nada que hacer.
             return;
        }

        MethodNode *m = var->value.object_value->class->methods;
        while (m && strcmp(m->name,"__constructor")!=0) m=m->next;
        if (m) {
            ParameterNode *p = m->params;
            ASTNode      *arg = object_node_for_constructor->left; // Usar el nodo correcto
            while (p && arg) {
                ASTNode *vn = NULL;
               // fprintf(stderr, "[DEBUG] Constructor arg: param=%s, arg->type=%s\n", p->name, arg->type ? arg->type : "NULL");
                if (arg->type && (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)) {
                    vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                    //fprintf(stderr, "[DEBUG] Constructor arg string val: %s\n", arg->str_value);
                } else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                    Variable *v = find_variable(arg->id);
                    if (!v) {
                        fprintf(stderr, "Error: variable '%s' not found.\n", arg->id);
                        return;
                    }
                    if (v->vtype == VAL_STRING) {
                        vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                      //  fprintf(stderr, "[DEBUG] Constructor arg var string val: %s\n", v->value.string_value);
                    } else {
                        vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                      //  fprintf(stderr, "[DEBUG] Constructor arg var int val: %d\n", v->value.int_value);
                    }
                } else {
                    int val = evaluate_expression(arg);
                    vn = create_ast_leaf_number("INT", val, NULL, NULL);
                  //  fprintf(stderr, "[DEBUG] Constructor arg expr int val: %d\n", val);
                }
                add_or_update_variable(p->name, vn);
                if (g_debug_mode) fprintf(stderr, "[DEBUG] Constructor param '%s' set\n", p->name);
                p   = p->next;
                arg = arg->right;
            }
            if (g_debug_mode) fprintf(stderr, "[DEBUG] Calling __constructor for class '%s'\n", var->value.object_value->class->name);
            call_method(var->value.object_value, "__constructor");
            if (g_debug_mode) fprintf(stderr, "[DEBUG] __constructor completed\n");
        }
    }
   // printf("[DEBUG] interpret_var_decl: done\n"); fflush(stdout);
}

ASTNode *create_list_node(ASTNode *items) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("LIST");
    node->left = items; // 'left' apunta al primer item
    node->right = NULL;
    node->next = NULL;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    
    // Convertir la lista enlazada de 'right' a 'next'
    if(items) {
        ASTNode *cur = items;
        while(cur) {
            ASTNode* temp = cur->right; // Guardamos el siguiente item (de la regla expr_list)
            cur->right = NULL;         // Limpiamos 'right'
            cur->next = temp;          // Lo movemos a 'next'
            cur = temp;
        }
    }

    /* Ola 14c: pre-build TEListIdx así el primer push es O(1) puro
     * sin pasar por la rama lazy-build de te_list_append. */
    {
        TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
        if (ix) {
            ix->cap = 8;
            ix->len = 0;
            ix->items = (ASTNode**)calloc((size_t)ix->cap, sizeof(ASTNode*));
            if (!ix->items) { free(ix); ix = NULL; }
            if (ix && items) {
                /* poblar con items existentes */
                ASTNode *cur = items;
                while (cur) {
                    if (ix->len >= ix->cap) {
                        int nc = ix->cap * 2;
                        ASTNode **nb = (ASTNode**)realloc(ix->items, (size_t)nc * sizeof(ASTNode*));
                        if (!nb) { free(ix->items); free(ix); ix = NULL; break; }
                        ix->items = nb; ix->cap = nc;
                    }
                    ix->items[ix->len++] = cur;
                    cur = cur->next;
                }
            }
        }
        node->extra = ix;
    }
    
    return node;
}

ASTNode *append_to_list_parser(ASTNode *list, ASTNode *item) {
    if (!list) return item;
    ASTNode *cur = list;
    while (cur->right) {
        cur = cur->right;
    }
    cur->right = item; // Usamos 'right' temporalmente
    item->right = NULL;
    return list;
}

ASTNode *append_argument_raw(ASTNode *list, ASTNode *arg) {
    if (!list) return arg;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = arg;
    return list;
}


ASTNode* append_to_list(ASTNode* list, ASTNode* item) {
    if (!item) return list;
    if (strcmp(item->type, "LIST") == 0) {
        printf("Error: No se puede insertar un nodo LIST dentro de una lista.\n");
        return list;
    }
    if (list == item) {
        printf("Error: attempt to insert the same list into itself.\n");
        return list;
    }
    if (!list) {
        if (strcmp(item->type, "LIST") == 0) {
            printf("Error: No se puede crear lista con nodo LIST como item.\n");
            return NULL;
        }
        item->next = NULL;
        ASTNode *listNode = calloc(1, sizeof(ASTNode));
        listNode->type = strdup("LIST");
        listNode->left = item;
        listNode->right = NULL;
        listNode->next = NULL;
        listNode->id = NULL;
        listNode->str_value = NULL;
        listNode->value = 0;
        return listNode;
    }
    if (strcmp(list->type, "LIST") != 0) {
        return NULL;
    }
    ASTNode* current = list->left;
    if (!current) {
        list->left = item;
    } else {
        int safety = 0;
        while (current->next) {
            if (++safety > 10000000) {
                printf("Error: Bucle infinito detectado en append_to_list()\n");
                break;
            }
            current = current->next;
        }     
        current->next = item;
    }
    item->next = NULL;
    return list;
}


ASTNode* create_list_function_call_node(ASTNode* list, const char* funcName, ASTNode* lambda) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("FILTER_CALL");
    node->id = strdup(funcName);
    node->left = list;
    node->right = lambda;
    node->next = NULL;
    return node;
}


ASTNode* create_lambda_node(const char* argName, ASTNode* body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(argName);
    node->left = body;
    node->right = NULL;
    node->next = NULL;
    return node;
}


static void interpret_assign_attr(ASTNode *node) {
    /* trace removed */
    ASTNode *access = node->left;
    /* access internals trace removed */
    ASTNode *value_node = node->right;
    Variable *var = find_variable(access->left->id);
    /* find_variable trace removed */
    if (!var || var->vtype!=VAL_OBJECT) {
        printf("Error: Objeto '%s' no definido o no es un objeto.\n", access->left->id); return;
    }
    ObjectNode *obj = var->value.object_value;
    const char *attr_name = access->right->id;
    int idx=-1;
    for(int i=0;i<obj->class->attr_count;i++){
        if(strcmp(obj->class->attributes[i].id,attr_name)==0){idx=i;break;}
    }
    if(idx<0){ printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name); return; }
    const char *declared = obj->attributes[idx].type; 

    /* detailed attribute assignment trace removed */

    if (declared == NULL) {
        printf("Error: El atributo '%s' no tiene tipo definido.\n", attr_name);
        return;
    }    

    if (strcmp(declared, "string") == 0) {
        if (value_node->type && strcmp(value_node->type, "STRING") == 0) {
          obj->attributes[idx].value.string_value = strdup(value_node->str_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (STRING)\n", attr_name, value_node->str_value);
        }
        else if (value_node->type && (strcmp(value_node->type, "IDENTIFIER") == 0 || strcmp(value_node->type, "ID") == 0)) {
          Variable *v2 = find_variable(value_node->id ? value_node->id : value_node->str_value);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string or variable not found.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR)\n", attr_name, v2->value.string_value);
        }
        else if (value_node->id) {
          Variable *v2 = find_variable(value_node->id);
          if (!v2 || v2->vtype != VAL_STRING) {
            fprintf(stderr, "Error: expression is not a valid string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(v2->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (VAR ID)\n", attr_name, v2->value.string_value);
        }
        else if (strcmp(value_node->type, "CALL_FUNC") == 0) {
          interpret_ast(value_node);
          Variable *r = find_variable("__ret__");
          if (!r || r->vtype != VAL_STRING) {
            fprintf(stderr, "Error: function result is not a string.\n");
            return;
          }
          obj->attributes[idx].value.string_value = strdup(r->value.string_value);
          if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %s (CALL)\n", attr_name, r->value.string_value);
        }
        obj->attributes[idx].vtype = VAL_STRING;
      } else {
        int val = evaluate_expression(value_node);
        obj->attributes[idx].value.int_value = val;
        obj->attributes[idx].vtype = VAL_INT;
        if (g_debug_mode) fprintf(stderr, "[DEBUG] Assign attr %s = %d (INT)\n", attr_name, val);
    }
}

static void interpret_assign(ASTNode *node) {
    /* trace removed */
    ASTNode *var_node = node->left;
    ASTNode *value_node = node->right;
    if (!var_node || !var_node->id || !value_node) {
        printf("Error: invalid assignment.\n"); 
        return; 
    }

    /* Fase 2 (perf): fast-path for "x = numeric_expr" with cached Variable*.
     * Hot in for/while loops. Skips strdup/find_variable_for/temp_node alloc. */
    {
        Variable *fv = (Variable *)var_node->cached_var;
        if (!fv) {
            fv = find_variable_for(var_node->id);
            if (fv) var_node->cached_var = fv;
        }
        if (fv && !fv->is_const && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)) {
            NodeKind vk = nk_of(value_node);
            if (vk == NK_ADD || vk == NK_SUB || vk == NK_MUL || vk == NK_DIV
                || vk == NK_NUMBER || vk == NK_INT || vk == NK_FLOAT
                || vk == NK_IDENTIFIER) {
                /* Avoid string-typed ADD (concat) which needs the slow path. */
                if (!(vk == NK_ADD && is_string_type(value_node))) {
                    double r = evaluate_expression(value_node);
                    if (r == (double)(int)r) {
                        fv->vtype = VAL_INT;
                        fv->value.int_value = (int)r;
                    } else {
                        fv->vtype = VAL_FLOAT;
                        fv->value.float_value = r;
                    }
                    return;
                }
            }
        }
    }

    // --- INICIO DE LA CORRECCIÓN ---

    // ¿Es una llamada a función (como concat)?
    if (strcmp(value_node->type, "CALL_FUNC") == 0 || strcmp(value_node->type, "CALL_METHOD") == 0 || strcmp(value_node->type, "PREDICT") == 0) {
        
        // 1. Ejecutar la función (ej. concat)
        interpret_ast(value_node);

        /* ============================================================
         * Ola 3 Fase B: FAST READ of __ret_var when target var is
         * already int/float. Skip create_ast_leaf*+add_or_update+free.
         * Switch: TYPEEASY_NO_FASTRET=1 (shared with FASTRET path)
         * ============================================================ */
        {
            static int fr_init = 0;
            static int fr_enabled = 1;
            if (!fr_init) {
                const char *e = getenv("TYPEEASY_NO_FASTRET");
                if (e && e[0] && e[0] != '0') fr_enabled = 0;
                fr_init = 1;
            }
            if (fr_enabled && __ret_var_active
                && (__ret_var.vtype == VAL_INT || __ret_var.vtype == VAL_FLOAT)) {
                Variable *fv = (Variable *)var_node->cached_var;
                if (!fv) {
                    fv = find_variable_for(var_node->id);
                    if (fv) var_node->cached_var = fv;
                }
                if (fv && !fv->is_const
                    && (fv->vtype == VAL_INT || fv->vtype == VAL_FLOAT)) {
                    if (__ret_var.vtype == VAL_FLOAT) {
                        fv->vtype = VAL_FLOAT;
                        fv->value.float_value = __ret_var.value.float_value;
                    } else {
                        fv->vtype = VAL_INT;
                        fv->value.int_value = __ret_var.value.int_value;
                    }
                    /* Reset return state but DO NOT free __ret_var fields:
                     * leaving them avoids strdup/free churn. */
                    return_flag = 0;
                    return_node = NULL;
                    return;
                }
            }
        }

        // 2. Obtener el resultado de __ret_var
        Variable *ret_val = find_variable("__ret__");
        if (!ret_val) {
            printf("Error: function in assignment returned nothing.\n");
            return;
        }

        // 3. Crear un nodo temporal para el valor
        ASTNode *temp_node = NULL;
        if (ret_val->vtype == VAL_STRING) {
            temp_node = create_ast_leaf("STRING", 0, strdup(ret_val->value.string_value), NULL);
        } else if (ret_val->vtype == VAL_INT) {
            temp_node = create_ast_leaf_number("INT", ret_val->value.int_value, NULL, NULL);
        } else if (ret_val->vtype == VAL_FLOAT) {
            temp_node = create_ast_leaf("FLOAT", 0, double_to_string(ret_val->value.float_value), NULL);
        } else if (ret_val->vtype == VAL_OBJECT) {
            temp_node = calloc(1, sizeof(ASTNode));
            memset(temp_node, 0, sizeof(ASTNode));
            temp_node->type = strdup(ret_val->type);
            temp_node->extra = (struct ASTNode*)ret_val->value.object_value;
        }
        
        if (temp_node) {
            // 4. Asignar el valor
            add_or_update_variable(var_node->id, temp_node);
            // 5. Limpiar el nodo temporal
            free_ast(temp_node);
        }

        // 6. Limpiar __ret_var
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
            // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
        }
        return_flag = 0;
        return_node = NULL;
    }
    // ¿Es un acceso a atributo (como intencion.item)?
    else if (strcmp(value_node->type, "ACCESS_ATTR") == 0) {
        // Esta lógica ya la escribimos para declare_variable, la usamos aquí
        ASTNode *o = value_node->left, *a = value_node->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
             printf("Error: object '%s' not found for assignment.\n", o->id);
             return;
        }
        
        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
                // Encontramos el atributo. Creamos un nodo temporal y lo asignamos.
                ASTNode* temp_node = NULL;
                if (obj->attributes[i].vtype == VAL_STRING) {
                    temp_node = create_ast_leaf("STRING", 0, strdup(obj->attributes[i].value.string_value), NULL);
                } else if (obj->attributes[i].vtype == VAL_INT) {
                    temp_node = create_ast_leaf_number("INT", obj->attributes[i].value.int_value, NULL, NULL);
                } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                    temp_node = create_ast_leaf("FLOAT", 0, double_to_string(obj->attributes[i].value.float_value), NULL);
                }
                
                if(temp_node) {
                    add_or_update_variable(var_node->id, temp_node);
                    free_ast(temp_node);
                }
                return; // ¡Asignación completada!
            }
        }
        printf("Error: Atributo '%s' no encontrado en '%s'.\n", a->id, o->id);
        return;
    }
    // ¿Es una expresión matemática?
    else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0) {
        if (strcmp(value_node->type, "ADD") == 0 && is_string_type(value_node)) {
            char *s = get_node_string(value_node);
            ASTNode* temp_node = create_ast_leaf("STRING", 0, s, NULL);
            add_or_update_variable(var_node->id, temp_node);
            free_ast(temp_node);
            return;
        }
        double result = evaluate_expression(value_node);
        ASTNode* temp_node = NULL;
        if (result == (int)result) {
            temp_node = create_ast_leaf_number("INT", (int)result, NULL, NULL);
        } else {
            char* str_res = double_to_string(result);
            temp_node = create_ast_leaf("FLOAT", 0, str_res, NULL);
            free(str_res);
        }
        add_or_update_variable(var_node->id, temp_node);
        free_ast(temp_node);
    } 
    // Es un valor simple (literal, variable)
    else {
        add_or_update_variable(var_node->id, value_node);
    }
    // --- FIN DE LA CORRECCIÓN ---
}

static void interpret_print(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf("Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "NULL") == 0) {
        dbg_printf("null");
        append_to_stdout("null");
        return;
    }
    if (arg->type && strcmp(arg->type, "IDENTIFIER") == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, "NULL") == 0) { dbg_printf("null"); append_to_stdout("null"); return; }
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_printf("%s", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(arg->str_value);
        dbg_printf("%s", s);
        append_to_stdout(s);
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "ADD") == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        dbg_printf("%s", s);
        append_to_stdout(s);
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_EXPR") == 0) {
        ASTNode *map = resolve_to_map(arg->left);
        if (map) {
            const char *key = NULL;
            if (arg->right && arg->right->type && strcmp(arg->right->type, "STRING") == 0) key = arg->right->str_value;
            else if (arg->right && (strcmp(arg->right->type, "IDENTIFIER") == 0 || strcmp(arg->right->type, "ID") == 0)) {
                Variable *kv = find_variable(arg->right->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            if (!key) { dbg_printf("Error: clave Map debe ser string.\n"); return; }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) { dbg_printf("Error: clave '%s' no encontrada.\n", key); return; }
            ASTNode *val = pair->left;
            if (val && val->type && strcmp(val->type, "STRING") == 0) dbg_printf("%s", val->str_value);
            else if (val && val->type && strcmp(val->type, "FLOAT") == 0) dbg_printf("%f", atof(val->str_value));
            else { double v_ = evaluate_expression(val); if (v_ == (int)v_) dbg_printf("%d", (int)v_); else dbg_printf("%f", v_); }
            return;
        }
        ASTNode *list = resolve_to_list(arg->left);
        if (!list) { dbg_printf("Error: no es lista ni Map.\n"); return; }
        int idx = (int)evaluate_expression(arg->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) { dbg_printf("Error: index %d out of range.\n", idx); return; }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return;
        if (item->type && strcmp(item->type, "STRING") == 0) dbg_printf("%s", item->str_value);
        else if (item->type && strcmp(item->type, "FLOAT") == 0) dbg_printf("%f", atof(item->str_value));
        else { double v_ = evaluate_expression(item); if (v_ == (int)v_) dbg_printf("%d", (int)v_); else dbg_printf("%f", v_); }
        return;
    }
    /* Fase 1a: print(arr[i]) — soporta strings y números */
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        /* Fase 1a: arr.length */
        if (a && a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) { dbg_printf("%d", list_length(list)); return; }
            ASTNode *map = resolve_to_map(o);
            if (map) { dbg_printf("%d", map_length(map)); return; }
        }
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_printf("Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_printf("Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            dbg_printf("%s", attr->value.string_value);
        else
            dbg_printf("%d", attr->value.int_value);
        return;
    }

    if (arg->id) { 
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_printf("Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_printf("[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_printf("]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_printf("%s", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_printf("%d", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            dbg_printf("%f", v->value.float_value);
        else
            dbg_printf("Objeto de clase: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_printf("%d", (int)val);
        } else {
            dbg_printf("%f", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}

static void interpret_fprint(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf( "Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_eprintf( "%s", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_eprintf( "Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_eprintf( "Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            dbg_eprintf( "%s", attr->value.string_value);
        else
            dbg_eprintf( "%d", attr->value.int_value);
        return;
    }

    if (arg->id) { 
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_eprintf( "Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_eprintf( "[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_eprintf( "]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_eprintf( "%s", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_eprintf( "%d", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            dbg_eprintf( "%f", v->value.float_value);
        else
            dbg_eprintf( "Objeto de clase: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_eprintf( "%d", (int)val);
        } else {
            dbg_eprintf( "%f", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}

static void interpret_fprintln(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_eprintf( "Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_eprintf( "%s\n", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_eprintf( "Error: Objeto '%s' no definido o no es un objeto.\n", o->id);
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_eprintf( "Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            dbg_eprintf( "%s\n", attr->value.string_value);
        else
            dbg_eprintf( "%d\n", attr->value.int_value);
        return;
    }

    if (arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_eprintf( "Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_eprintf( "[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_eprintf( "]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_eprintf( "%s\n", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_eprintf( "%d\n", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            dbg_eprintf( "%f\n", v->value.float_value);
        else
            dbg_eprintf( "Objeto de clase: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_eprintf( "%d\n", (int)val);
        } else {
            dbg_eprintf( "%f\n", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}

static void interpret_println(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf("Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "NULL") == 0) {
        dbg_printf("null\n");
        append_to_stdout("null\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "IDENTIFIER") == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, "NULL") == 0) { dbg_printf("null\n"); append_to_stdout("null\n"); return; }
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_printf("%s\n", arg->str_value);
        append_to_stdout(arg->str_value);
        append_to_stdout("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(arg->str_value);
        dbg_printf("%s\n", s);
        append_to_stdout(s);
        append_to_stdout("\n");
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "ADD") == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        dbg_printf("%s\n", s);
        append_to_stdout(s);
        append_to_stdout("\n");
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "CALL_METHOD") == 0) {
        interpret_call_method(arg);
        Variable *r = find_variable("__ret__");
        if (!r) { dbg_printf("\n"); return; }
        if (r->vtype == VAL_STRING) { dbg_printf("%s\n", r->value.string_value ? r->value.string_value : ""); return; }
        if (r->vtype == VAL_INT) { dbg_printf("%d\n", r->value.int_value); return; }
        if (r->vtype == VAL_FLOAT) { dbg_printf("%f\n", r->value.float_value); return; }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "NULL") == 0) { dbg_printf("null\n"); return; }
        dbg_printf("\n");
        return;
    }
    /* Ola 13: println(builtin(...)) — dispatch via __ret__ */
    if (arg->type && strcmp(arg->type, "CALL_FUNC") == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable("__ret__");
        if (!r) { dbg_printf("\n"); return; }
        if (r->vtype == VAL_STRING) { dbg_printf("%s\n", r->value.string_value ? r->value.string_value : ""); append_to_stdout(r->value.string_value ? r->value.string_value : ""); append_to_stdout("\n"); return; }
        if (r->vtype == VAL_INT)    { dbg_printf("%d\n", r->value.int_value); char tmp[32]; snprintf(tmp,32,"%d\n",r->value.int_value); append_to_stdout(tmp); return; }
        if (r->vtype == VAL_FLOAT)  { dbg_printf("%f\n", r->value.float_value); return; }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode*)(intptr_t)r->value.object_value;
            if (listNode) { ASTNode *cur = listNode->left; dbg_printf("["); int first = 1;
                while (cur) { if (!first) dbg_printf(","); first = 0;
                    if (cur->type && strcmp(cur->type,"STRING")==0) dbg_printf("%s", cur->str_value ? cur->str_value : "");
                    else if (cur->type && strcmp(cur->type,"FLOAT")==0) dbg_printf("%f", cur->str_value ? atof(cur->str_value) : 0.0);
                    else dbg_printf("%d", cur->value); cur = cur->next; } dbg_printf("]\n"); return; }
        }
        dbg_printf("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "NULL_COALESCE") == 0) {
        ASTNode *l = arg->left;
        int is_null = 0;
        if (l && l->type && strcmp(l->type, "NULL") == 0) is_null = 1;
        else if (l && l->type && strcmp(l->type, "IDENTIFIER") == 0) {
            Variable *vv = find_variable(l->id);
            if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) is_null = 1;
        }
        ASTNode *chosen = is_null ? arg->right : l;
        ASTNode wrapper; memset(&wrapper, 0, sizeof(ASTNode));
        wrapper.type = strdup("PRINTLN"); wrapper.left = chosen;
        interpret_println(&wrapper);
        free(wrapper.type);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_EXPR") == 0) {
        /* Fase 1c: println(m["k"]) */
        ASTNode *map = resolve_to_map(arg->left);
        if (map) {
            const char *key = NULL;
            if (arg->right && arg->right->type && strcmp(arg->right->type, "STRING") == 0) key = arg->right->str_value;
            else if (arg->right && (strcmp(arg->right->type, "IDENTIFIER") == 0 || strcmp(arg->right->type, "ID") == 0)) {
                Variable *kv = find_variable(arg->right->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
            if (!key) { dbg_printf("Error: clave Map debe ser string.\n"); return; }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) { dbg_printf("Error: clave '%s' no encontrada.\n", key); return; }
            ASTNode *val = pair->left;
            if (val && val->type && strcmp(val->type, "STRING") == 0) dbg_printf("%s\n", val->str_value);
            else if (val && val->type && strcmp(val->type, "FLOAT") == 0) dbg_printf("%f\n", atof(val->str_value));
            else { double v_ = evaluate_expression(val); if (v_ == (int)v_) dbg_printf("%d\n", (int)v_); else dbg_printf("%f\n", v_); }
            return;
        }
        ASTNode *list = resolve_to_list(arg->left);
        if (!list) { dbg_printf("Error: no es lista.\n"); return; }
        int idx = (int)evaluate_expression(arg->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            dbg_printf("Error: index %d out of range (length=%d).\n", idx, len);
            return;
        }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return;
        if (item->type && strcmp(item->type, "STRING") == 0) {
            dbg_printf("%s\n", item->str_value);
        } else if (item->type && strcmp(item->type, "FLOAT") == 0) {
            dbg_printf("%f\n", atof(item->str_value));
        } else {
            double v = evaluate_expression(item);
            if (v == (int)v) dbg_printf("%d\n", (int)v); else dbg_printf("%f\n", v);
        }
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        /* Fase 1a: arr.length */
        if (a && a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) {
                int n = list_length(list);
                dbg_printf("%d\n", n);
                char tmp[32]; snprintf(tmp, 32, "%d\n", n);
                append_to_stdout(tmp);
                return;
            }
            ASTNode *map = resolve_to_map(o);
            if (map) {
                int n = map_length(map);
                dbg_printf("%d\n", n);
                char tmp[32]; snprintf(tmp, 32, "%d\n", n);
                append_to_stdout(tmp);
                return;
            }
        }
        /* Bug fix: println(arr[i].attr) — o es ACCESS_EXPR.
         * Va antes de find_variable porque o->id es NULL aquí. */
        if (o && o->type && strcmp(o->type, "ACCESS_EXPR") == 0) {
            ASTNode *list2 = resolve_to_list(o->left);
            if (list2 && o->right) {
                int idx2 = (int)evaluate_expression(o->right);
                int len2 = list_length(list2);
                if (idx2 < 0 || idx2 >= len2) {
                    dbg_printf("Error: index %d out of range (length=%d).\n", idx2, len2);
                    return;
                }
                ASTNode *item = list_get_item(list2, idx2);
                ObjectNode *iobj = NULL;
                if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                    if (item->extra) iobj = (ObjectNode*)item->extra;
                    else iobj = (ObjectNode*)(intptr_t)item->value;
                }
                if (iobj && iobj->class) {
                    for (int i = 0; i < iobj->class->attr_count; i++) {
                        if (strcmp(iobj->class->attributes[i].id, a->id) == 0) {
                            Variable *attr2 = &iobj->attributes[i];
                            if (attr2->vtype == VAL_STRING) {
                                dbg_printf("%s\n", attr2->value.string_value);
                                append_to_stdout(attr2->value.string_value);
                                append_to_stdout("\n");
                            } else if (attr2->vtype == VAL_FLOAT) {
                                dbg_printf("%f\n", attr2->value.float_value);
                            } else {
                                dbg_printf("%d\n", attr2->value.int_value);
                                char tmpx[32]; snprintf(tmpx, 32, "%d\n", attr2->value.int_value);
                                append_to_stdout(tmpx);
                            }
                            return;
                        }
                    }
                    dbg_printf("Error: attribute '%s' not found in indexed object.\n", a->id);
                    return;
                }
            }
            dbg_printf("Error: cannot resolve indexed expression for attribute access.\n");
            return;
        }
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_printf("Error: Objeto '%s' no definido o no es un objeto.\n",
                       (o && o->id) ? o->id : "<expr>");
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_printf("Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING) {
            dbg_printf("%s\n", attr->value.string_value);
            append_to_stdout(attr->value.string_value);
            append_to_stdout("\n");
        }
        else {
            dbg_printf("%d\n", attr->value.int_value);
            char temp[32]; snprintf(temp, 32, "%d\n", attr->value.int_value);
            append_to_stdout(temp);
        }
        return;
    }

    if (arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_printf("Error: Variable '%s' no definida.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_printf("[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_printf("]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING) {
            dbg_printf("%s\n", v->value.string_value);
            append_to_stdout(v->value.string_value);
            append_to_stdout("\n");
        }
        else if (v->vtype == VAL_INT) {
            dbg_printf("%d\n", v->value.int_value);
            char temp[32]; snprintf(temp, 32, "%d\n", v->value.int_value);
            append_to_stdout(temp);
        }
        else if (v->vtype == VAL_FLOAT) {
            dbg_printf("%f\n", v->value.float_value);
            char temp[64]; snprintf(temp, 64, "%f\n", v->value.float_value);
            append_to_stdout(temp);
        }
        else {
            dbg_printf("Objeto de clase: %s\n", v->value.object_value->class->name);
            // Don't append object description to stdout for API response usually
        }
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_printf("%d\n", (int)val);
        } else {
            dbg_printf("%f\n", val);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}


static void interpret_statement_list(ASTNode *node) {
    interpret_ast(node->left);
    if (throw_flag || return_flag || break_flag || continue_flag) return;
    interpret_ast(node->right);
}

// ====================== MANEJO DE ATRIBUTOS ======================

int get_attribute_value(ObjectNode *obj, const char *attr_name) {
    if (!obj) return 0;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (strcmp(obj->attributes[i].id, attr_name) == 0) {
            return obj->attributes[i].value.int_value;
        }
    }
    printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name);
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
    printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name);
    }
// ====================== LIBERACIÓN DE MEMORIA ======================

void free_ast(ASTNode *node) {
    /* Ola 14c: iterar la cadena `->next` en bucle (no recursión) para
     * evitar stack overflow en listas grandes (50k+ items). `left`/`right`
     * siguen siendo recursivos porque su profundidad es típicamente baja. */
    while (node) {
        ASTNode *next = node->next;
        /* Pool nodes (CSV wrappers): no liberar nada del nodo — type es sentinel,
         * id está interned, str_value es NULL, left/right son NULL. El bloque
         * del pool vive en g_ast_pool_keepalive hasta exit. */
        if (node->from_pool) { node = next; continue; }
        /* CSV wrapper sin pool: type apunta al literal global compartido — no liberar. */
        if (node->type && node->type != g_csv_wrapper_obj_type) free(node->type);
        /* Ola 3 Fase A: never free interned strings (immortal in global table). */
        if (node->str_value && !node->str_interned) free(node->str_value);
        /* Ola 15: same for id slot. */
        if (node->id && !node->id_interned) free(node->id);
        free_ast(node->left);
        free_ast(node->right);
        free(node);
        node = next;
    }
}

// Serializa un objeto a XML string dado su id y lo guarda en __ret__
void print_object_as_xml_by_id(const char* id) {
    Variable *var = find_variable((char*)id);
    if (!var) {
        printf("Error: No se encontró variable con id '%s' para serializar a XML.\n", id);
        return;
    }
    
    if (var->vtype == VAL_STRING) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, var->value.string_value, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle LISTs
    if (var->type && strcmp(var->type, "LIST") == 0) {
        ASTNode *listNode = (ASTNode *)(intptr_t)var->value.object_value;
        if (!listNode) {
             ASTNode *empty = create_ast_leaf("STRING", 0, "<Items></Items>", NULL);
             add_or_update_variable("__ret__", empty);
             free_ast(empty);
             return;
        }
        
        int estimated_size = 4096; 
        char *xml_buffer = malloc(estimated_size);
        if(!xml_buffer) return;
        strcpy(xml_buffer, "<Items>\n");
        
        ASTNode *cur = listNode->left;
        
        while (cur) {
            if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                ObjectNode *obj = NULL;
                if (cur->extra) obj = (ObjectNode *)cur->extra;
                else obj = (ObjectNode *)(intptr_t)cur->value;
                
                if (obj && obj->class) {
                    if (strlen(xml_buffer) + 1024 > estimated_size) {
                        estimated_size *= 2;
                        xml_buffer = realloc(xml_buffer, estimated_size);
                    }
                    
                    strcat(xml_buffer, "  <Item>\n");
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "    <%s>", obj->class->attributes[i].id);
                        strcat(xml_buffer, temp);
                        
                        Variable *attr = &obj->attributes[i];
                        if (attr->vtype == VAL_STRING) {
                            strcat(xml_buffer, attr->value.string_value ? attr->value.string_value : "");
                        } else if (attr->vtype == VAL_FLOAT) {
                            snprintf(temp, sizeof(temp), "%f", attr->value.float_value);
                            strcat(xml_buffer, temp);
                        } else {
                            snprintf(temp, sizeof(temp), "%d", attr->value.int_value);
                            strcat(xml_buffer, temp);
                        }
                        snprintf(temp, sizeof(temp), "</%s>\n", obj->class->attributes[i].id);
                        strcat(xml_buffer, temp);
                    }
                    strcat(xml_buffer, "  </Item>\n");
                }
            }
            cur = cur->next;
        }
        strcat(xml_buffer, "</Items>");
        
        ASTNode *result_node = create_ast_leaf("STRING", 0, xml_buffer, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(xml_buffer);
        return;
    }

    if (var->vtype != VAL_OBJECT || !var->value.object_value) {
        printf("Error: Variable '%s' no es un objeto válido para serializar a XML.\n", id);
        return;
    }

    ObjectNode *obj = var->value.object_value;
    char *xml_buffer = malloc(4096);
    snprintf(xml_buffer, 4096, "<%s>\n", obj->class->name);
    for (int i = 0; i < obj->class->attr_count; i++) {
        char temp[512];
        snprintf(temp, sizeof(temp), "  <%s>", obj->attributes[i].id);
        strcat(xml_buffer, temp);
        if (obj->attributes[i].vtype == VAL_STRING) {
            strcat(xml_buffer, obj->attributes[i].value.string_value ? obj->attributes[i].value.string_value : "");
        } else if (obj->attributes[i].vtype == VAL_INT) {
            snprintf(temp, sizeof(temp), "%d", obj->attributes[i].value.int_value);
            strcat(xml_buffer, temp);
        } else if (obj->attributes[i].vtype == VAL_FLOAT) {
            snprintf(temp, sizeof(temp), "%f", obj->attributes[i].value.float_value);
            strcat(xml_buffer, temp);
        } else {
            strcat(xml_buffer, "null");
        }
        snprintf(temp, sizeof(temp), "</%s>\n", obj->attributes[i].id);
        strcat(xml_buffer, temp);
    }
    char end_tag[256];
    snprintf(end_tag, sizeof(end_tag), "</%s>", obj->class->name);
    strcat(xml_buffer, end_tag);
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, xml_buffer, NULL);
    add_or_update_variable("__ret__", result_node);
    free_ast(result_node);
    free(xml_buffer);
}

void print_object_as_json_by_id(const char* id) {
    ASTNode *arg = (ASTNode*)calloc(1, sizeof(ASTNode));
    if(!arg) return;
    arg->type = strdup("IDENTIFIER");
    arg->id = strdup(id);
    arg->left = NULL;
    arg->right = NULL;
    arg->str_value = NULL;
    arg->value = 0;
    arg->next = NULL;
    arg->extra = NULL;

    native_json(arg);
    
    free(arg->id);
    free(arg->type);
    free(arg);
}

static void interpret_return_node(ASTNode *node) {
   // fprintf(stderr, "[DEBUG] interpret_return_node called\n"); fflush(stderr);
    return_node = node->left;
    
    if (return_node) {
       // fprintf(stderr, "[DEBUG] interpret_return_node: executing return expression type=%s\n", return_node->type); fflush(stderr);
        interpret_ast(return_node);
        //fprintf(stderr, "[DEBUG] interpret_return_node: expression executed\n"); fflush(stderr);
    } 
    /*else {
        if (g_debug_mode) { fprintf(stderr, "[DEBUG] interpret_return_node: no return expression\n"); fflush(stderr); }
    }*/
    return_flag = 1;
}
