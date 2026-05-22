
#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include "bytecode.h"
#include "mysql_bridge.h"
#include "postgres_bridge.h"
#include "sqlserver_bridge.h"
#include "debugger.h"
#include "te_builtins.h"
#include "te_buf.h"
#include "te_http.h"
#include "te_json.h"
#include "te_bytecode.h"
#include "te_csv.h"
#include "te_colcache.h"
#include "te_stdlib.h"
#include "te_linq.h"

#ifdef TE_HAVE_OPENMP
#  include <omp.h>
#endif

/* Crypto / encoding stdlib (linked via -lssl -lcrypto en el Makefile). */
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <time.h>

/* Headers POSIX/sockets disponibles en todas las plataformas (MSYS2 / Linux / macOS).
 * En Windows usamos winsock; en POSIX, sockets BSD. */
#ifdef _WIN32
  #include <io.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifndef STDOUT_FILENO
    #define STDOUT_FILENO _fileno(stdout)
  #endif
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

/* libcurl optional: enabled with -DTE_HAVE_LIBCURL at compile time.
 * Provides HTTPS + custom headers + arbitrary methods for http_*. */
#ifdef TE_HAVE_LIBCURL
  #include <curl/curl.h>
#endif

/* Ola 10: JIT availability — must be defined BEFORE any use further down. */
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <sys/vfs.h>
#include <stddef.h>
#include <pthread.h>

/* Phase F: globals shared with test runner (typeeasy_main.c).
 * Defined here as weak so binaries that don't link the test runner
 * still satisfy references from te_builtin_dispatch. */
int g_test_failed __attribute__((weak)) = 0;
int g_test_assertions __attribute__((weak)) = 0;

/* Phase F.3: error capture for --syntax-check. typeeasy_main.c provides
 * strong defs; weak here so typeeasy_agent (which doesn't link main) builds. */
int g_capture_errors __attribute__((weak)) = 0;
void te_capture_error(int line, const char *msg, const char *near) __attribute__((weak));
void te_capture_error(int line, const char *msg, const char *near) {
    (void)line; (void)msg; (void)near;
}
#define TE_JIT_AVAILABLE 1
#define TE_HAS_MMAP 1
#define TE_HAS_PTHREAD 1
#elif defined(_WIN32)
/* MSYS2/mingw-w64 trae winpthreads y soporta clock_gettime; no hay JIT ni mmap
 * pero si pthread, asi que CSV multi-thread funciona en Windows tambien. */
#include <pthread.h>
#include <time.h>
#define TE_JIT_AVAILABLE 0
#define TE_HAS_MMAP 0
#define TE_HAS_PTHREAD 1
#else
#define TE_JIT_AVAILABLE 0
#define TE_HAS_MMAP 0
#define TE_HAS_PTHREAD 0
#endif

/* Detección portable de número de CPUs (sysconf no existe en MSYS2/mingw). */
#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
static inline long te_nprocs_online(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
/* mingw no trae pread(); emulamos con ReadFile + OVERLAPPED (thread-safe,
 * no toca el file pointer global). fd es un descriptor de CRT, lo
 * convertimos a HANDLE Win32. */
#  include <sys/types.h>
static inline ssize_t te_pread(int fd, void *buf, size_t count, off_t offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
    ov.Offset     = (DWORD)((unsigned long long)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(((unsigned long long)offset >> 32) & 0xFFFFFFFFu);
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)count, &got, &ov)) {
        DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return 0;
        return -1;
    }
    return (ssize_t)got;
}
#  define pread(fd, buf, count, off) te_pread((fd), (buf), (count), (off))
#else
#  include <unistd.h>
static inline long te_nprocs_online(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? n : 1;
}
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

/* Phase H: forward declaration so get_node_string / native_json can recursively
 * evaluate nested CALL_FUNC arguments (e.g. json(concat(...)), concat(a, request_param("id"), b)). */
static void interpret_call_func(ASTNode *node);

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

/* When non-zero, dbg_printf does NOT write to real stdout — only to the
 * capture buffer (g_stdout_buffer) and the debugger sink. Set during
 * --invoke so the body output isn't duplicated alongside __ret__. */
int g_suppress_stdout = 0;

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

    /* Phase H: json(call(...)) — invoke nested function, leave __ret__ as the
     * resulting string. interpret_call_func already sets __ret__ via the
     * native dispatcher (concat, request_param, etc.). */
    if (arg && arg->type && strcmp(arg->type, "CALL_FUNC") == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable("__ret__");
        if (r && r->vtype == VAL_STRING) {
            /* already a string — nothing else to do */
            return;
        }
        if (r && r->vtype == VAL_INT) {
            char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", r->value.int_value);
            ASTNode *result_node = create_ast_leaf("STRING", 0, tmp, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            return;
        }
        ASTNode *result_node = create_ast_leaf("STRING", 0, "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

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

    /* Phase H: accept a STRING / STRING_INTERP / ADD literal directly so that
     * `return json("{\"x\":1}")` or `return json(concat(...))` works without
     * the println+buffer dance. */
    if (arg->type && (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, arg->str_value ? arg->str_value : "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(arg->str_value ? arg->str_value : "");
        ASTNode *result_node = create_ast_leaf("STRING", 0, s, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "ADD") == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        ASTNode *result_node = create_ast_leaf("STRING", 0, s ? s : "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        if (s) free(s);
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


/* Forward declarations: usados antes de su definición para soportar
 * resolución de arr[i].attr en get_node_string / is_string_type. */
ASTNode* list_get_item(ASTNode *list, int idx);
int list_length(ASTNode *list);
ASTNode* resolve_to_list(ASTNode *node);
ASTNode* resolve_to_map(ASTNode *node);
static int map_length(ASTNode *map);
ASTNode* map_find_pair(ASTNode *map, const char *key);
static void interpret_call_method(ASTNode *node);

// Helper function to get string representation of any node
char* get_node_string(ASTNode* node) {
    if (!node) return strdup("");
    
    char temp[64];

    /* Phase H: nested function call — invoke it and read __ret__. Lets
     * concat(a, request_param("id"), b) work, plus any future nesting. */
    if (node->type && strcmp(node->type, "CALL_FUNC") == 0) {
        interpret_call_func(node);
        Variable *r = find_variable("__ret__");
        if (r) {
            if (r->vtype == VAL_STRING) return strdup(r->value.string_value ? r->value.string_value : "");
            if (r->vtype == VAL_INT)    { snprintf(temp, sizeof(temp), "%d", r->value.int_value); return strdup(temp); }
            if (r->vtype == VAL_FLOAT)  { snprintf(temp, sizeof(temp), "%f", r->value.float_value); return strdup(temp); }
        }
        return strdup("");
    }

    /* v0.0.11: nested method call — invoke and read __ret__.
     * Enables `"sum=" + nums.sum()` and similar LINQ chains in string contexts. */
    if (node->type && strcmp(node->type, "CALL_METHOD") == 0) {
        interpret_call_method(node);
        Variable *r = find_variable("__ret__");
        if (r) {
            if (r->vtype == VAL_STRING) return strdup(r->value.string_value ? r->value.string_value : "");
            if (r->vtype == VAL_INT)    { snprintf(temp, sizeof(temp), "%d", r->value.int_value); return strdup(temp); }
            if (r->vtype == VAL_FLOAT)  { snprintf(temp, sizeof(temp), "%g", r->value.float_value); return strdup(temp); }
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "NULL") == 0) return strdup("null");
        }
        return strdup("");
    }

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
        if (!o || !a) return strdup("");
        /* str.length / list.length / map.length — mayo 2026 */
        if (a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) { snprintf(temp, sizeof(temp), "%d", list_length(list)); return strdup(temp); }
            ASTNode *map = resolve_to_map(o);
            if (map)  { snprintf(temp, sizeof(temp), "%d", map_length(map));  return strdup(temp); }
            if (o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    snprintf(temp, sizeof(temp), "%zu", sv->value.string_value ? strlen(sv->value.string_value) : 0);
                    return strdup(temp);
                }
            }
        }
        ObjectNode *obj = NULL;
        /* Caso 1: o es IDENTIFIER → variable OBJECT. */
        if (o->id) {
            Variable *v = find_variable(o->id);
            if (v && v->vtype == VAL_OBJECT) obj = v->value.object_value;
        }
        /* Caso 2: arr[i].attr — o es ACCESS_EXPR sobre LIST. */
        if (!obj && o->type && strcmp(o->type, "ACCESS_EXPR") == 0) {
            ASTNode *list = resolve_to_list(o->left);
            if (list && o->right) {
                int idx = (int)evaluate_expression(o->right);
                if (idx >= 0 && idx < list_length(list)) {
                    ASTNode *item = list_get_item(list, idx);
                    if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                        obj = item->extra ? (ObjectNode*)item->extra
                                          : (ObjectNode*)(intptr_t)item->value;
                    }
                }
            }
            /* Caso 3: m["k"].attr — o es ACCESS_EXPR sobre MAP (toMap result). */
            if (!obj) {
                ASTNode *map = resolve_to_map(o->left);
                if (map && o->right && o->right->type) {
                    const char *key = NULL;
                    if (strcmp(o->right->type, "STRING") == 0) key = o->right->str_value;
                    else if (strcmp(o->right->type, "IDENTIFIER") == 0 || strcmp(o->right->type, "ID") == 0) {
                        Variable *kv = find_variable(o->right->id);
                        if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                    }
                    if (key) {
                        ASTNode *pair = map_find_pair(map, key);
                        ASTNode *val  = pair ? pair->left : NULL;
                        if (val && val->type && strcmp(val->type, "OBJECT") == 0) {
                            obj = val->extra ? (ObjectNode*)val->extra
                                             : (ObjectNode*)(intptr_t)val->value;
                        }
                    }
                }
            }
        }
        if (obj && obj->class) {
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

    /* Fallback: arithmetic / bitwise sub-expressions (MUL, SUB, DIV, MOD,
     * NEG, BIT_*, SHL, SHR, paren-wrapped, etc.). Let evaluate_expression
     * compute the numeric value and format it. Keeps `"x" + (s*2)` working. */
    if (node->type && (
        strcmp(node->type, "MUL") == 0 ||
        strcmp(node->type, "SUB") == 0 ||
        strcmp(node->type, "DIV") == 0 ||
        strcmp(node->type, "MOD") == 0 ||
        strcmp(node->type, "NEG") == 0 ||
        strcmp(node->type, "BIT_AND") == 0 ||
        strcmp(node->type, "BIT_OR") == 0 ||
        strcmp(node->type, "BIT_XOR") == 0 ||
        strcmp(node->type, "BIT_NOT") == 0 ||
        strcmp(node->type, "SHL") == 0 ||
        strcmp(node->type, "SHR") == 0)) {
        double d = evaluate_expression(node);
        if (d == (double)(long long)d) {
            snprintf(temp, sizeof(temp), "%lld", (long long)d);
        } else {
            snprintf(temp, sizeof(temp), "%g", d);
        }
        return strdup(temp);
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

/* ============================================================================
 * Phase H: HTTP request/response state (used by API server to pass request data
 * into the interpreted endpoint body, and read back response status/headers).
 *
 * The server populates these globals BEFORE calling typeeasy_embedded_invoke()
 * via the typeeasy_http_* setters declared below; resets them between requests
 * via typeeasy_http_reset(). Inside the interpreted body, builtins like
 * request_query("q") / response_status(404) read/write these.
 * ============================================================================ */
typedef struct TeKV { char *k; char *v; struct TeKV *next; } TeKV;

static char *g_req_method = NULL;
static char *g_req_path   = NULL;
static char *g_req_body   = NULL;
static TeKV *g_req_query   = NULL;
static TeKV *g_req_headers = NULL;
static TeKV *g_req_params  = NULL;
static int   g_resp_status = 200;
static TeKV *g_resp_headers = NULL;

static void te_kv_free_list(TeKV **head) {
    TeKV *c = *head; while (c) { TeKV *n = c->next; free(c->k); free(c->v); free(c); c = n; }
    *head = NULL;
}
static void te_kv_add(TeKV **head, const char *k, const char *v) {
    if (!k) return;
    TeKV *e = (TeKV*)malloc(sizeof(TeKV));
    e->k = strdup(k); e->v = strdup(v ? v : ""); e->next = *head; *head = e;
}
static const char *te_kv_find(TeKV *head, const char *k) {
    if (!k) return NULL;
    while (head) { if (head->k && strcmp(head->k, k) == 0) return head->v; head = head->next; }
    return NULL;
}

void typeeasy_http_reset(void) {
    free(g_req_method); g_req_method = NULL;
    free(g_req_path);   g_req_path   = NULL;
    free(g_req_body);   g_req_body   = NULL;
    te_kv_free_list(&g_req_query);
    te_kv_free_list(&g_req_headers);
    te_kv_free_list(&g_req_params);
    te_kv_free_list(&g_resp_headers);
    g_resp_status = 200;
}
void typeeasy_http_set_method(const char *m) { free(g_req_method); g_req_method = m ? strdup(m) : NULL; }
void typeeasy_http_set_path  (const char *p) { free(g_req_path);   g_req_path   = p ? strdup(p) : NULL; }
void typeeasy_http_set_body  (const char *b) { free(g_req_body);   g_req_body   = b ? strdup(b) : NULL; }
void typeeasy_http_add_query (const char *k, const char *v) { te_kv_add(&g_req_query,  k, v); }
void typeeasy_http_add_header(const char *k, const char *v) { te_kv_add(&g_req_headers, k, v); }
void typeeasy_http_add_param (const char *k, const char *v) { te_kv_add(&g_req_params,  k, v); }
int  typeeasy_http_get_status(void) { return g_resp_status; }
int  typeeasy_http_iter_response_header(int idx, const char **k, const char **v) {
    TeKV *c = g_resp_headers; int i = 0;
    while (c) { if (i == idx) { if (k) *k = c->k; if (v) *v = c->v; return 1; } c = c->next; i++; }
    return 0;
}

/* Debugger introspection */
const char *typeeasy_http_get_method(void) { return g_req_method; }
const char *typeeasy_http_get_path  (void) { return g_req_path;   }
const char *typeeasy_http_get_body  (void) { return g_req_body;   }
static int te_kv_iter(TeKV *head, int idx, const char **k, const char **v) {
    int i = 0; for (TeKV *c = head; c; c = c->next, ++i) {
        if (i == idx) { if (k) *k = c->k; if (v) *v = c->v; return 1; }
    }
    return 0;
}
int typeeasy_http_iter_param (int idx, const char **k, const char **v) { return te_kv_iter(g_req_params,  idx, k, v); }
int typeeasy_http_iter_query (int idx, const char **k, const char **v) { return te_kv_iter(g_req_query,   idx, k, v); }
int typeeasy_http_iter_header(int idx, const char **k, const char **v) { return te_kv_iter(g_req_headers, idx, k, v); }

/* Helpers: extract the first STRING argument from a function-call arg AST.
 * The argument may be a single STRING node, or a chain via ->right, or wrapped
 * in evaluate_native_args (which already resolves identifiers to STRING). */
const char *te_arg_string(ASTNode *arg) {
    if (!arg) return NULL;
    if (arg->type) {
        if (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)
            return arg->str_value;
        if (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0) {
            Variable *v = find_variable(arg->id);
            if (v && v->vtype == VAL_STRING) return v->value.string_value;
        }
    }
    return NULL;
}
int te_arg_int(ASTNode *arg, int dflt) {
    if (!arg) return dflt;
    if (arg->type) {
        if (strcmp(arg->type, "NUMBER") == 0 || strcmp(arg->type, "INT") == 0) return arg->value;
        if (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0) {
            Variable *v = find_variable(arg->id);
            if (v && v->vtype == VAL_INT) return v->value.int_value;
        }
    }
    return dflt;
}

void te_set_ret_string(const char *s) {
    ASTNode *r = create_ast_leaf("STRING", 0, s ? s : "", NULL);
    add_or_update_variable("__ret__", r);
    free_ast(r);
}
void te_set_ret_int(int n) {
    ASTNode *r = create_ast_leaf("NUMBER", n, NULL, NULL);
    add_or_update_variable("__ret__", r);
    free_ast(r);
}

static void native_request_method(ASTNode *arg) { (void)arg; te_set_ret_string(g_req_method ? g_req_method : ""); }
static void native_request_path  (ASTNode *arg) { (void)arg; te_set_ret_string(g_req_path   ? g_req_path   : ""); }
static void native_request_body  (ASTNode *arg) { (void)arg; te_set_ret_string(g_req_body   ? g_req_body   : ""); }
static void native_request_query (ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find(g_req_query,   k); te_set_ret_string(v ? v : ""); }
static void native_request_header(ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find(g_req_headers, k); te_set_ret_string(v ? v : ""); }
static void native_request_param (ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find(g_req_params,  k); te_set_ret_string(v ? v : ""); }

/* v0.0.13: Debug-print que va a stderr (NO entra al pipeline de captura
 * de stdout que arma el cuerpo HTTP). Pensado para inspeccionar handlers
 * POST/GET en `docker compose logs typeeasy` sin contaminar la respuesta.
 *
 * Usa get_node_string() (no te_arg_string()) para que arguments como
 *   debug_log(concat(...))      -> evalua la CALL_FUNC anidada
 *   debug_log(u.name)           -> resuelve ACCESS_ATTR sobre OBJECT
 *   debug_log("a" + 1)          -> resuelve ADD con coercion a string
 * funcionen en vez de imprimir vacio. */
void native_debug_log(ASTNode *arg) {
    char *s = get_node_string(arg);
    fprintf(stderr, "[debug] %s\n", s ? s : "");
    fflush(stderr);
    if (s) free(s);
    te_set_ret_int(0);
}

/* JSON-encode a TeKV list as { "k": "v", ... } into out (truncates safely). */
static void te_kv_to_json(TeKV *head, char *out, size_t cap) {
    size_t o = 0;
    if (cap < 3) { if (cap) out[0] = 0; return; }
    out[o++] = '{';
    int first = 1;
    for (TeKV *c = head; c && o + 8 < cap; c = c->next) {
        const char *k = c->k ? c->k : "";
        const char *v = c->v ? c->v : "";
        if (!first) { if (o + 1 < cap) out[o++] = ','; }
        first = 0;
        if (o + 1 < cap) out[o++] = '"';
        for (const char *p = k; *p && o + 2 < cap; ++p) {
            if (*p == '"' || *p == '\\') { if (o + 2 < cap) out[o++] = '\\'; }
            out[o++] = *p;
        }
        if (o + 3 < cap) { out[o++] = '"'; out[o++] = ':'; out[o++] = '"'; }
        for (const char *p = v; *p && o + 2 < cap; ++p) {
            if (*p == '"' || *p == '\\') { if (o + 2 < cap) out[o++] = '\\'; }
            else if (*p == '\n') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 'n'; } continue; }
            else if (*p == '\r') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 'r'; } continue; }
            else if (*p == '\t') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 't'; } continue; }
            out[o++] = *p;
        }
        if (o + 1 < cap) out[o++] = '"';
    }
    if (o + 1 < cap) out[o++] = '}';
    out[o < cap ? o : cap - 1] = 0;
}

static void native_request_headers(ASTNode *arg) {
    (void)arg;
    char buf[8192]; te_kv_to_json(g_req_headers, buf, sizeof(buf));
    te_set_ret_string(buf);
}
static void native_request_queries(ASTNode *arg) {
    (void)arg;
    char buf[4096]; te_kv_to_json(g_req_query, buf, sizeof(buf));
    te_set_ret_string(buf);
}
static void native_request_params_all(ASTNode *arg) {
    (void)arg;
    char buf[2048]; te_kv_to_json(g_req_params, buf, sizeof(buf));
    te_set_ret_string(buf);
}

static void native_response_status(ASTNode *arg) { g_resp_status = te_arg_int(arg, 200); te_set_ret_int(g_resp_status); }
static void native_response_header(ASTNode *arg) {
    const char *k = te_arg_string(arg);
    const char *v = arg && arg->right ? te_arg_string(arg->right) : NULL;
    if (k) te_kv_add(&g_resp_headers, k, v ? v : "");
    te_set_ret_int(0);
}

int call_native_function(const char *name, ASTNode *arg) {
    /* Fase 1: registry first. New builtins live in the hash table only;
     * the legacy if-chain below remains as fallback for transparency. */
    {
        ASTNode tmp = (ASTNode){0};
        tmp.id = (char*)name;
        TEBuiltinFn fn = te_builtin_lookup(name);
        if (fn) return fn(&tmp, arg);
    }
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
    /* Phase H: HTTP request/response builtins */
    if (strcmp(name, "request_method") == 0) { native_request_method(arg); return 1; }
    if (strcmp(name, "request_path")   == 0) { native_request_path(arg);   return 1; }
    if (strcmp(name, "request_body")   == 0) { native_request_body(arg);   return 1; }
    if (strcmp(name, "request_query")  == 0) { native_request_query(arg);  return 1; }
    if (strcmp(name, "request_header") == 0) { native_request_header(arg); return 1; }
    if (strcmp(name, "request_param")  == 0) { native_request_param(arg);  return 1; }
    if (strcmp(name, "request_headers")== 0) { native_request_headers(arg);return 1; }
    if (strcmp(name, "request_queries")== 0) { native_request_queries(arg);return 1; }
    if (strcmp(name, "request_params") == 0) { native_request_params_all(arg); return 1; }
    if (strcmp(name, "response_status")== 0) { native_response_status(arg);return 1; }
    if (strcmp(name, "response_header")== 0) { native_response_header(arg);return 1; }
    if (strcmp(name, "debug_log")      == 0) { native_debug_log(arg);      return 1; }
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
    if (strcmp(name, "postgres_connect") == 0) { native_postgres_connect(arg); return 1; }
    if (strcmp(name, "postgres_query") == 0)   { native_postgres_query(arg);   return 1; }
    if (strcmp(name, "postgres_close") == 0)   { native_postgres_close(arg);   return 1; }
    if (strcmp(name, "sqlserver_connect") == 0) { native_sqlserver_connect(arg); return 1; }
    if (strcmp(name, "sqlserver_query") == 0)   { native_sqlserver_query(arg);   return 1; }
    if (strcmp(name, "sqlserver_close") == 0)   { native_sqlserver_close(arg);   return 1; }
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
        if (!g_suppress_stdout) fputs(stackbuf, stdout);
        /* NOTE: capture-buffer (g_stdout_buffer) is filled by callers via
         * explicit append_to_stdout(...) calls in interpret_print/println.
         * dbg_printf must NOT append here or every line is duplicated in
         * __ret__ when json() reads the buffer. */
        if (g_debug_enabled) debugger_emit_output("stdout", stackbuf);
        va_end(ap2);
        return n;
    }
    /* Output too big for stack buffer: allocate. */
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { va_end(ap2); return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    if (!g_suppress_stdout) fputs(buf, stdout);
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
    if (node) node->line = yylineno;
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
char *throw_message = NULL;

const char *get_throw_message(void) { return throw_message; }

/* Fase 4: break/continue */
static int break_flag = 0;
static int continue_flag = 0;

/* === Bloque D: helper to reset all interpreter control-flow flags ===
 * Called by typeeasy_embedded_load_script() between successive global-scope
 * loads. Without this, an aborted script (uncaught throw, stray return at
 * top level, break/continue leaking) leaves a flag set; the next file's
 * interpret_ast() short-circuits immediately and silently drops its body.
 *
 * Exposed via prototype in ast.h. break_flag/continue_flag are file-static
 * here, so an external "extern int break_flag;" reset would not link — that
 * is why the reset must live in this translation unit. */
void te_runtime_reset_flags(void) {
    return_flag = 0;
    return_node = NULL;
    throw_flag = 0;
    if (throw_message) {
        free(throw_message);
        throw_message = NULL;
    }
    break_flag = 0;
    continue_flag = 0;
}

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
    /* Invalidate all cached bytecode whose Instrs hold raw Variable*
     * pointers into vars[]. After this reset, slots are recycled and
     * those cached pointers become stale. Force recompilation on next
     * access by clearing node->bc / m->bc_body. (Defined later in file
     * after BCInfo is declared.) */
    extern void bc_invalidate_all(void);
    bc_invalidate_all();

    // Libera la memoria de todas las variables CREADAS DURANTE LA ÚLTIMA EJECUCIÓN
    // (es decir, todas las variables DESPUÉS de los bridges)
    for (int i = g_initial_var_count; i < var_count; i++) {
        if (vars[i].id) free(vars[i].id);
        if (vars[i].type) free(vars[i].type);
        if (vars[i].vtype == VAL_STRING && vars[i].value.string_value) {
            free(vars[i].value.string_value);
        }
        /* Wipe the slot so any AST node that cached `&vars[i]` from a
         * previous request will read garbage-free zeros, and code paths
         * that validate against `id != NULL` can detect staleness. */
        memset(&vars[i], 0, sizeof(Variable));

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
    class_node->parent = NULL;
    class_node->next = NULL;
    return class_node;
}

/* Phase E: copy parent attributes + methods into child class.
 * Called from parser action BEFORE class_body, so child's own members
 * (added later via add_method_to_class which prepends) override parent's. */
void inherit_from(ClassNode *child, char *parent_name) {
    if (!child || !parent_name) return;
    ClassNode *parent = find_class(parent_name);
    if (!parent) {
        fprintf(stderr, "Error: cannot extend unknown class '%s'.\n", parent_name);
        return;
    }
    child->parent = parent;
    /* Copy attributes (child gets a deep-copied array). */
    for (int i = 0; i < parent->attr_count; i++) {
        child->attributes = realloc(child->attributes, (child->attr_count + 1) * sizeof(Variable));
        child->attributes[child->attr_count].id = strdup(parent->attributes[i].id ? parent->attributes[i].id : "");
        child->attributes[child->attr_count].type = strdup(parent->attributes[i].type ? parent->attributes[i].type : "");
        child->attributes[child->attr_count].is_const = parent->attributes[i].is_const;
        child->attributes[child->attr_count].vtype = parent->attributes[i].vtype;
        memset(&child->attributes[child->attr_count].value, 0, sizeof(child->attributes[child->attr_count].value));
        child->attr_count++;
    }
    /* Copy methods. Parent's method list is already in some order; we
     * append at the END of the (currently empty) child list, so that
     * subsequent add_method_to_class (which prepends) puts child's own
     * methods FIRST → child overrides win in lookup. */
    MethodNode **tail = &child->methods;
    for (MethodNode *pm = parent->methods; pm; pm = pm->next) {
        MethodNode *cm = (MethodNode*)malloc(sizeof(MethodNode));
        cm->name = strdup(pm->name ? pm->name : "");
        cm->params = pm->params;       /* share params list (read-only) */
        cm->body = pm->body;           /* share body AST (read-only at runtime) */
        cm->route_path = pm->route_path ? strdup(pm->route_path) : NULL;
        cm->http_method = pm->http_method ? strdup(pm->http_method) : NULL;
        cm->cache_ttl = pm->cache_ttl;
        cm->return_type = pm->return_type ? strdup(pm->return_type) : NULL;
        cm->bc_body = NULL;            /* don't share bytecode cache */
        cm->next = NULL;
        *tail = cm;
        tail = &cm->next;
    }
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
    /* Fase 2: silent — `new IDENTIFIER(...)` may be a builtin call, not a
     * class instantiation. The grammar action falls back to create_call_node
     * which surfaces "function not defined" downstream if neither matches. */
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
    obj->owning_list = NULL;  /* v0.0.13 (perf) */
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
    else if (strcmp(value->type, "LAMBDA") == 0) {
        /* Fase B: lambda como first-class value. Guardamos el ASTNode tal cual. */
        free(vars[my_index].type);
        vars[my_index].type = strdup("LAMBDA");
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
             strcmp(value->type, "MUL") == 0 || strcmp(value->type, "DIV") == 0 ||
             strcmp(value->type, "MOD") == 0 || strcmp(value->type, "NEG") == 0 ||
             strcmp(value->type, "BIT_AND") == 0 || strcmp(value->type, "BIT_OR") == 0 ||
             strcmp(value->type, "BIT_XOR") == 0 || strcmp(value->type, "BIT_NOT") == 0 ||
             strcmp(value->type, "SHL") == 0 || strcmp(value->type, "SHR") == 0 ||
             strcmp(value->type, "IN") == 0) {
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
    else if (strcmp(value->type, "LAZY_ITER") == 0) {
        /* v0.0.12 #8: lazy iterator. The actual LAZY_ITER ASTNode pointer is
         * either in value->extra (when called from interpret_var_decl with a
         * wrapper) or value itself (when called directly). */
        vars[my_index].vtype = VAL_OBJECT;
        vars[my_index].value.object_value = value->extra
            ? (ObjectNode *)value->extra
            : (ObjectNode *)(intptr_t)value;
    }
    else if (strcmp(value->type, "ACCESS_ATTR") == 0) {
        // Esto maneja: let item = intencion.item;
        ASTNode *o = value->left, *a = value->right;

        /* Caso especial: .length / .size sobre LIST o MAP (atributo virtual,
         * no presente en obj->attributes). Delegamos a evaluate_expression
         * que ya implementa toda la lógica (incluyendo TEListIdx). */
        if (a && a->id && (strcmp(a->id, "length") == 0 || strcmp(a->id, "size") == 0)) {
            double r = evaluate_expression(value);
            vars[my_index].vtype = VAL_INT;
            vars[my_index].value.int_value = (int)r;
            return;
        }

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
        } else if (strcmp(value->type, "LAZY_ITER") == 0) {
            /* v0.0.12 #8: lazy iterator carries pointer to LAZY_ITER ASTNode */
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("LAZY_ITER");
            __ret_var.value.object_value = (ObjectNode *)value;
        } else if (strcmp(value->type, "MAP") == 0 || strcmp(value->type, "OBJECT_LITERAL") == 0) {
            __ret_var.vtype = VAL_OBJECT;
            __ret_var.type = strdup("MAP");
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
        } else if (strcmp(value->type, "LAMBDA") == 0) {
            var->vtype = VAL_OBJECT;
            var->value.object_value = (ObjectNode *)(intptr_t)value;
        } else if (strcmp(value->type, "LIST") == 0 || strcmp(value->type, "OBJECT_LITERAL") == 0 || strcmp(value->type, "LAZY_ITER") == 0) {
            var->vtype = VAL_OBJECT;
            var->value.object_value = (ObjectNode *)(intptr_t)value;
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
        } else if (strcmp(value->type, "LAMBDA") == 0) {
            vars[var_count].vtype = VAL_OBJECT;
            vars[var_count].value.object_value = (ObjectNode *)(intptr_t)value;
        } else if (strcmp(value->type, "LIST") == 0 || strcmp(value->type, "OBJECT_LITERAL") == 0 || strcmp(value->type, "LAZY_ITER") == 0) {
            vars[var_count].vtype = VAL_OBJECT;
            vars[var_count].value.object_value = (ObjectNode *)(intptr_t)value;
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
            if (!strcmp(t, "BIT_AND")) return NK_BIT_AND;
            if (!strcmp(t, "BIT_OR"))  return NK_BIT_OR;
            if (!strcmp(t, "BIT_XOR")) return NK_BIT_XOR;
            if (!strcmp(t, "BIT_NOT")) return NK_BIT_NOT;
            /* BOOL literal: store as numeric (value=0|1). Special-cased in
             * print/JSON to emit "true"/"false" instead of "0"/"1". */
            if (!strcmp(t, "BOOL")) return NK_NUMBER;
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
            if (!strcmp(t, "IN")) return NK_IN;
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
            if (!strcmp(t, "MOD")) return NK_MOD;
            break;
        case 'N':
            if (!strcmp(t, "NUMBER")) return NK_NUMBER;
            if (!strcmp(t, "NULL")) return NK_NULL;
            if (!strcmp(t, "NULL_COALESCE")) return NK_NULL_COALESCE;
            if (!strcmp(t, "NOT")) return NK_NOT;
            if (!strcmp(t, "NEG")) return NK_NEG;
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
            if (!strcmp(t, "SHL")) return NK_SHL;
            if (!strcmp(t, "SHR")) return NK_SHR;
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
        if (!o || !a) return 0;
        ObjectNode *obj = NULL;
        /* Caso 1: o es IDENTIFIER (var de tipo OBJECT). */
        if (o->id) {
            Variable *v = find_variable(o->id);
            /* v1.0.0: skip the ObjectNode cast if var is MAP/OBJECT_LITERAL —
             * value.object_value is actually an ASTNode*, and reading
             * obj->class on it crashes. For maps, check KV value type. */
            if (v && v->type && (strcmp(v->type, "MAP") == 0 ||
                                 strcmp(v->type, "OBJECT_LITERAL") == 0)) {
                ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
                if (map && a->id) {
                    ASTNode *pair = map_find_pair(map, a->id);
                    if (pair && pair->left && pair->left->type) {
                        const char *t = pair->left->type;
                        if (strcmp(t, "STRING") == 0 ||
                            strcmp(t, "DATETIME") == 0 ||
                            strcmp(t, "UUID") == 0) return 1;
                    }
                }
                return 0;
            }
            if (v && v->vtype == VAL_OBJECT) {
                obj = v->value.object_value;
                if (!obj) {
                    ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
                    if (wrapper && wrapper->extra) obj = (ObjectNode *)wrapper->extra;
                }
            }
        }
        /* Caso 2: arr[i].attr — o es ACCESS_EXPR. Resolvemos el item OBJECT. */
        if (!obj && o->type && strcmp(o->type, "ACCESS_EXPR") == 0) {
            ASTNode *list = resolve_to_list(o->left);
            if (list && o->right) {
                int idx = (int)evaluate_expression(o->right);
                if (idx >= 0 && idx < list_length(list)) {
                    ASTNode *item = list_get_item(list, idx);
                    if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                        obj = item->extra ? (ObjectNode*)item->extra
                                          : (ObjectNode*)(intptr_t)item->value;
                    }
                }
            }
        }
        if (obj && obj->class) {
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    if (obj->attributes[i].vtype == VAL_STRING) return 1;
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
void te_list_append(ASTNode *list, ASTNode *item) {
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

ASTNode* list_get_item(ASTNode *list, int idx) {
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

int list_length(ASTNode *list) {
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
ASTNode* resolve_to_list(ASTNode *node) {
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
ASTNode* resolve_to_map(ASTNode *node) {
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
ASTNode* map_find_pair(ASTNode *map, const char *key) {
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

/* g_csv_wrapper_obj_type and ast_pool_alloc now declared in te_csv.h. */

/* Pool-allocated wrapper for an OBJECT value. Used by LINQ fast-paths
 * (orderBy/groupBy) that materialize millions of wrappers per call: avoids the
 * per-item calloc + strdup("OBJECT") + strdup(class_name). Returns NULL when
 * the value isn't a plain OBJECT (caller must fall back to build_item_from_value). */
static inline ASTNode* build_object_wrapper_pooled(ASTNode *value) {
    if (!value || !value->type || strcmp(value->type, "OBJECT") != 0) return NULL;
    if (!g_csv_wrapper_obj_type) g_csv_wrapper_obj_type = strdup("OBJECT");
    ASTNode *w = ast_pool_alloc();  /* slot is calloc'd → zero-initialised */
    w->type = g_csv_wrapper_obj_type;
    if (value->id_interned) {
        w->id = value->id;
        w->id_interned = 1;
    } else if (value->id) {
        /* Fallback: still strdup (rare path; CSV objects are always interned). */
        w->id = strdup(value->id);
        w->id_interned = 0;
    }
    w->extra = value->extra;   /* ObjectNode* */
    w->value = value->value;
    w->from_pool = 1;          /* free_ast: skip free(node) and all field frees */
    return w;
}

/* Build a fresh ASTNode item from a runtime value (used for index assign / push). */
ASTNode* build_item_from_value(ASTNode *value) {
    ASTNode *new_item = (ASTNode*)calloc(1, sizeof(ASTNode));
    memset(new_item, 0, sizeof(ASTNode));
    if (!value) { new_item->type = strdup("NUMBER"); return new_item; }
    /* v0.0.11: preserve OBJECT items (e.g., user-class instances inside lists)
     * so callers of minBy/maxBy/first/last/firstWhere etc. can access attrs.
     * Fast-path (post-Phase 2): when called millions of times by LINQ
     * materializers (orderBy/groupBy/where/...), strdup("OBJECT") and
     * strdup(value->id) dominate. Share the sentinel "OBJECT" string (free_ast
     * already skips g_csv_wrapper_obj_type) and reuse interned class-name id. */
    if (value->type && strcmp(value->type, "OBJECT") == 0) {
        if (!g_csv_wrapper_obj_type) g_csv_wrapper_obj_type = strdup("OBJECT");
        new_item->type = g_csv_wrapper_obj_type;
        if (value->id_interned) {
            new_item->id = value->id;
            new_item->id_interned = 1;
        } else {
            new_item->id = value->id ? strdup(value->id) : NULL;
        }
        new_item->extra = value->extra;  /* ObjectNode* */
        return new_item;
    }
    if (value->type && (strcmp(value->type, "LIST") == 0 ||
                        strcmp(value->type, "MAP") == 0 ||
                        strcmp(value->type, "OBJECT_LITERAL") == 0)) {
        /* Return the original list/map directly — they're shared by design. */
        free(new_item);
        return value;
    }
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

/* Bytecode VM (compile+exec), profiler, tracer and x86_64 JIT now live in
 * te_bytecode.c (Fase 2 modularization). Public API in te_bytecode.h. */
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
        if (bc_enabled && !g_debug_enabled) {
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
         * vars[] is append-only with stable pointers, so this is safe.
         * EXCEPT: runtime_reset_vars_to_initial_state() between API
         * requests truncates var_count and wipes slots, so a cached
         * pointer from a previous request may now point to a recycled
         * (or zeroed) slot. Re-validate by comparing the slot's id. */
        Variable *var = (Variable *)node->cached_var;
        if (var && node->id) {
            if (!var->id || strcmp(var->id, node->id) != 0) {
                var = NULL;
                node->cached_var = NULL;
            }
        }
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
    case NK_MOD: {
        long long lv = (long long)evaluate_expression(node->left);
        long long rv = (long long)evaluate_expression(node->right);
        if (rv == 0) { printf("Error: modulo by zero.\n"); return 0; }
        return (double)(lv % rv);
    }
    case NK_NEG:
        return -evaluate_expression(node->left);
    case NK_BIT_AND: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a & b);
    }
    case NK_BIT_OR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a | b);
    }
    case NK_BIT_XOR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a ^ b);
    }
    case NK_BIT_NOT: {
        long long a = (long long)evaluate_expression(node->left);
        return (double)(~a);
    }
    case NK_SHL: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a << b);
    }
    case NK_SHR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a >> b);
    }
    case NK_IN: {
        /* `key in container` — container puede ser map (busca clave) o list (busca elemento) */
        ASTNode *container = node->right;
        ASTNode *map = resolve_to_map(container);
        if (map) {
            char *key = get_node_string(node->left);
            int found = (key && map_find_pair(map, key)) ? 1 : 0;
            free(key);
            return (double)found;
        }
        ASTNode *list = resolve_to_list(container);
        if (list) {
            double needle = evaluate_expression(node->left);
            char *needle_s = is_string_type(node->left) ? get_node_string(node->left) : NULL;
            ASTNode *cur = list->left;
            int found = 0;
            while (cur) {
                if (needle_s) {
                    char *cs = get_node_string(cur);
                    if (cs && strcmp(cs, needle_s) == 0) { free(cs); found = 1; break; }
                    if (cs) free(cs);
                } else {
                    if (evaluate_expression(cur) == needle) { found = 1; break; }
                }
                cur = cur->next;
            }
            if (needle_s) free(needle_s);
            return (double)found;
        }
        return 0;
    }

    case NK_ACCESS_ATTR: {
        ASTNode *objRef = node->left;
        ASTNode *attr   = node->right;

        /* Fase 7: null-safe ?. — return 0 (null-as-number) if obj is null */
        if (node->value == 1 && objRef && nk_of(objRef) == NK_IDENTIFIER) {
            Variable *vv = find_variable(objRef->id);
            if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) return 0;
        }

        /* Fase 1a: arr.length sobre listas / maps / strings */
        if (attr && attr->id && strcmp(attr->id, "length") == 0) {
            ASTNode *list = resolve_to_list(objRef);
            if (list) return (double)list_length(list);
            ASTNode *map = resolve_to_map(objRef);
            if (map) return (double)map_length(map);
            /* str.length — mayo 2026 */
            if (objRef && objRef->id) {
                Variable *sv = find_variable(objRef->id);
                if (sv && sv->vtype == VAL_STRING)
                    return (double)(sv->value.string_value ? strlen(sv->value.string_value) : 0);
            }
            if (objRef && objRef->type &&
                (strcmp(objRef->type, "STRING") == 0 || strcmp(objRef->type, "STRING_LITERAL") == 0)) {
                return (double)(objRef->str_value ? strlen(objRef->str_value) : 0);
            }
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
            /* m["k"].attr — extend to support MAP-indexed objects (toMap result). */
            ASTNode *map = resolve_to_map(objRef->left);
            if (map && objRef->right) {
                const char *key = NULL;
                if (nk_of(objRef->right) == NK_STRING) key = objRef->right->str_value;
                else if (nk_of(objRef->right) == NK_IDENTIFIER || nk_of(objRef->right) == NK_ID) {
                    Variable *kv = find_variable(objRef->right->id);
                    if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                }
                if (key) {
                    ASTNode *pair = map_find_pair(map, key);
                    ASTNode *val  = pair ? pair->left : NULL;
                    ObjectNode *iobj = NULL;
                    if (val && val->type && strcmp(val->type, "OBJECT") == 0) {
                        if (val->extra) iobj = (ObjectNode*)val->extra;
                        else iobj = (ObjectNode*)(intptr_t)val->value;
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
                        printf("Error: attribute '%s' not found in mapped object.\n", attr->id);
                        return 0;
                    }
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

        /* v1.0.0: variable is a MAP (e.g. lambda param bound to an
         * OBJECT_LITERAL list item). Treat `r.activo` as `r["activo"]`.
         * Without this branch the code casts value.object_value (ASTNode*)
         * to ObjectNode* and segfaults. */
        if (v->type && (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0)) {
            ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
            if (map && attr && attr->id) {
                ASTNode *pair = map_find_pair(map, attr->id);
                if (!pair) return 0;
                ASTNode *val = pair->left;
                if (!val || !val->type) return 0;
                if (strcmp(val->type, "BOOL") == 0) return (double)val->value;
                if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) return (double)val->value;
                if (strcmp(val->type, "FLOAT") == 0) return val->str_value ? atof(val->str_value) : 0;
                if (strcmp(val->type, "NULL") == 0) return 0;
                if (strcmp(val->type, "STRING") == 0 || strcmp(val->type, "DATETIME") == 0 ||
                    strcmp(val->type, "UUID") == 0) {
                    const char *s = val->str_value;
                    if (!s || !*s) return 0;
                    char *endp = NULL; double d = strtod(s, &endp);
                    if (endp && endp != s && *endp == '\0') return d;
                    return 1.0;
                }
                /* CALL_FUNC/CALL_METHOD or expression — evaluate */
                return evaluate_expression(val);
            }
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

    /* v1.0.0: CALL_FUNC / CALL_METHOD inside an expression context.
     * Previously these fell through to `default` returning 0, which broke
     * `builtin(x) OP y` inside LINQ lambdas and `{"k": builtin()}` in map
     * literals. interpret_call_* sets __ret__; we read it back here. */
    case NK_CALL_FUNC:
    case NK_CALL_METHOD: {
        if (k == NK_CALL_FUNC) interpret_call_func(node);
        else                   interpret_call_method(node);
        Variable *r = find_variable("__ret__");
        if (!r) return 0;
        if (r->vtype == VAL_INT)    return (double)r->value.int_value;
        if (r->vtype == VAL_FLOAT)  return r->value.float_value;
        if (r->vtype == VAL_STRING) {
            const char *s = r->value.string_value;
            if (!s || !*s) return 0;
            /* numeric-looking string -> parse; otherwise non-zero presence */
            char *endp = NULL;
            double d = strtod(s, &endp);
            if (endp && endp != s && *endp == '\0') return d;
            return 1.0; /* truthy non-numeric string */
        }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "NULL") == 0) return 0;
        return 0;
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
/* te_colcache_invalidate declared in te_colcache.h. */
static void interpret_assign(ASTNode *node);
static void interpret_print(ASTNode *node);
static void interpret_println(ASTNode *node);
static void interpret_fprint(ASTNode *node);
static void interpret_fprintln(ASTNode *node);
static void interpret_statement_list(ASTNode *node);
/* te_builtin_dispatch now declared in te_stdlib.h. */
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
    node->line      = yylineno;
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

    /* One-time registration of JSON eval hooks (Fase 1: te_json modularizado). */
    static int s_te_json_hooks_set = 0;
    if (!s_te_json_hooks_set) {
        te_json_set_eval_hooks(interpret_call_func, interpret_call_method);
        s_te_json_hooks_set = 1;
    }

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
        /* Phase F: top-level bare calls like `assert(0);` are parsed as
         * METHOD_CALL_ALONE; try built-ins before user-defined methods. */
        if (te_builtin_dispatch(node)) break;
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
        te_colcache_invalidate(list);    /* v0.0.13 (perf) */
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
         * for any non-trivial body.
         * Debugger: skip bytecode entirely when attached, otherwise the
         * loop runs in one shot and breakpoints / step inside the body
         * never fire. */
        {
            static int bc4_init = 0;
            static int bc4_enabled = 1;
            if (!bc4_init) {
                const char *e = getenv("TYPEEASY_NO_BC");
                if (e && e[0] && e[0] != '0') bc4_enabled = 0;
                bc4_init = 1;
            }
            if (bc4_enabled && !g_debug_enabled) {
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
        if (bc_for_enabled && !g_debug_enabled) {
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
/* =================== Phase D: stdlib helpers =================== */
/* te_resolve_arg + te_builtin_dispatch + adapt_* + te_register_ast_builtins + te_fill_host_api
 * now live in te_stdlib.{c,h} (Fase 2 paso 4). */

/* TeBuf + tebuf_* now live in te_buf.h (Fase 1 modularization). */

/* JSON parser+emitter now live in te_json.c (Fase 1 modularization). */
/* ---------------------------------------------------------------
 * v0.0.13: POST/PUT body -> typed-class model binding helper.
 * Parses `json` as an object and copies matching attribute values
 * into a freshly-created ObjectNode of `cls`. Missing keys keep
 * the class default; extra keys are ignored. Returns NULL on bad
 * class; on bad JSON returns an object with defaults.
 * --------------------------------------------------------------- */
ObjectNode *te_object_from_json(ClassNode *cls, const char *json) {
    if (!cls) return NULL;
    ObjectNode *obj = create_object(cls);
    if (!json || !*json) return obj;
    const char *p = json;
    /* Skip leading whitespace (te_json_parse_value also does this). */
    ASTNode *root = te_json_parse_value(&p);
    if (!root || !root->type || strcmp(root->type, "OBJECT_LITERAL") != 0) {
        return obj; /* Not a JSON object; keep defaults. */
    }
    for (ASTNode *pair = root->left; pair; pair = pair->right) {
        const char *key = pair->id;
        ASTNode *val = pair->left;
        if (!key || !val || !val->type) continue;
        for (int a = 0; a < cls->attr_count; a++) {
            if (!cls->attributes[a].id || strcmp(cls->attributes[a].id, key) != 0) continue;
            Variable *dst = &obj->attributes[a];
            const char *atype = cls->attributes[a].type ? cls->attributes[a].type : "int";
            if (strcmp(atype, "string") == 0) {
                if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
                if (strcmp(val->type, "STRING") == 0) {
                    dst->value.string_value = strdup(val->str_value ? val->str_value : "");
                } else if (strcmp(val->type, "INT") == 0) {
                    char buf[32]; snprintf(buf, sizeof buf, "%d", val->value);
                    dst->value.string_value = strdup(buf);
                } else if (strcmp(val->type, "FLOAT") == 0) {
                    dst->value.string_value = strdup(val->str_value ? val->str_value : "0");
                } else {
                    dst->value.string_value = strdup("");
                }
                dst->vtype = VAL_STRING;
            } else if (strcmp(atype, "float") == 0) {
                double d = 0;
                if (strcmp(val->type, "FLOAT") == 0)      d = atof(val->str_value ? val->str_value : "0");
                else if (strcmp(val->type, "INT") == 0)   d = (double)val->value;
                else if (strcmp(val->type, "STRING") == 0) d = atof(val->str_value ? val->str_value : "0");
                dst->value.float_value = d;
                dst->vtype = VAL_FLOAT;
            } else {
                /* int / default */
                int iv = 0;
                if (strcmp(val->type, "INT") == 0)         iv = val->value;
                else if (strcmp(val->type, "FLOAT") == 0)  iv = (int)atof(val->str_value ? val->str_value : "0");
                else if (strcmp(val->type, "STRING") == 0) iv = atoi(val->str_value ? val->str_value : "0");
                dst->value.int_value = iv;
                dst->vtype = VAL_INT;
            }
            break;
        }
    }
    /* Note: leaking `root` AST here for simplicity (per-request lifetime). */
    return obj;
}

/* Lookup a path-param value by name (returns NULL if not bound). */
const char *typeeasy_http_find_param(const char *k) {
    return te_kv_find(g_req_params, k);
}
const char *typeeasy_http_find_query(const char *k) {
    return te_kv_find(g_req_query, k);
}

/* ─── HTTP client moved to te_http.c (Fase 1 modularization).
 *     Exposed via te_http.h → te_http_do(method, url, body, headers_str).
 *     See docs/REFACTOR_AST_C.md for the full plan.
 */

/* stdlib dispatcher + plugin host API moved to te_stdlib.c (Fase 2 paso 4). */

static void interpret_call_func(ASTNode *node) {
    if (te_builtin_dispatch(node)) return;

    /* Fase B: si node->id es una variable de tipo LAMBDA, invocar el lambda. */
    if (node->id) {
        Variable *fv = find_variable(node->id);
        if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0) {
            ASTNode *lambda = (ASTNode*)(intptr_t)fv->value.object_value;
            ASTNode *r = call_lambda(lambda, node->left);
            if (r) add_or_update_variable("__ret__", r);
            return;
        }
    }

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
         fprintf(stderr, "Warning: Calling user function '%s' as expression is not fully supported yet.\n", node->id);
    } else {
        fprintf(stderr, "Error: function '%s' not defined.\n", node->id);
        exit(1);
    }
}

/* CSV/LINQ columnar cache + lambda specializer now live in te_colcache.{c,h}
 * (Fase 2 paso 3). Public surface declared in te_colcache.h. */


/* ---- orderBy / groupBy support ---- */
/* qsort comparator: sort indices by precomputed key. globals are safe because
 * TypeEasy LINQ runs on the main interpreter thread (the parallelism is in
 * CSV loading only). */
static double *g_sort_keys_d = NULL;
static char  **g_sort_keys_s = NULL;
static int     g_sort_is_str = 0;
static int     g_sort_desc   = 0;
static int te_sort_cmp_idx(const void *pa, const void *pb) {
    int a = *(const int*)pa, b = *(const int*)pb;
    if (g_sort_is_str) {
        const char *sa = g_sort_keys_s[a] ? g_sort_keys_s[a] : "";
        const char *sb = g_sort_keys_s[b] ? g_sort_keys_s[b] : "";
        int c = strcmp(sa, sb);
        return g_sort_desc ? -c : c;
    }
    double da = g_sort_keys_d[a], db = g_sort_keys_d[b];
    if (da == db) return 0;
    int c = (da < db) ? -1 : 1;
    return g_sort_desc ? -c : c;
}

/* lazy_* LINQ helpers moved to te_linq.{c,h} (Nivel B paso 1). */
/* DataFrame, te_list_df, te_df_dispatch_method now declared in te_csv.h. */

static void interpret_call_method(ASTNode *node) {
    ASTNode *objNode = node->left;

    /* ===== v0.0.11-pre: DataFrame analytics fast-path =====
     * Si el receptor es un LIST con DataFrame columnar adjunto, intentamos
     * despachar a sum/min/max/count/group_sum/print directamente sobre las
     * columnas (SIMD AVX2 + pthread parallel). Si el método no aplica,
     * caemos al dispatch estándar. */
    {
        ASTNode *recv_list = NULL;
        if (objNode && objNode->type) {
            if (strcmp(objNode->type, "LIST") == 0) {
                recv_list = objNode;
            } else if (strcmp(objNode->type, "ID") == 0 || strcmp(objNode->type, "IDENTIFIER") == 0) {
                Variable *lv = find_variable(objNode->id);
                if (lv && lv->type && strcmp(lv->type, "LIST") == 0)
                    recv_list = (ASTNode*)(intptr_t)lv->value.object_value;
            }
        }
        DataFrame *df_recv = te_list_df(recv_list);
        if (df_recv) {
            if (te_df_dispatch_method(df_recv, node)) return;
            /* fallthrough si el método no es analítico */
        }
    }

    /* ===== v0.0.12 #5 Fusion peephole: where(p).{select|map|first|find|firstWhere|sum} =====
     * Detect `xs.where(p).OUTER(...)` where xs is an ID resolving to a LIST.
     * Avoids materializing the intermediate filtered list. Falls through to
     * standard chained-call handling if pattern doesn't match. */
    if (objNode && objNode->type && strcmp(objNode->type, "CALL_METHOD") == 0 &&
        objNode->id && node->id) {
        const char *inner_m = objNode->id;
        const char *outer_m = node->id;
        int inner_is_where = (strcmp(inner_m, "where") == 0 || strcmp(inner_m, "filter") == 0);
        int outer_kind = 0; /* 1=map,2=first,3=sum */
        if      (strcmp(outer_m, "select") == 0 || strcmp(outer_m, "map") == 0) outer_kind = 1;
        else if (strcmp(outer_m, "first")  == 0 || strcmp(outer_m, "find") == 0 ||
                 strcmp(outer_m, "firstWhere") == 0) outer_kind = 2;
        else if (strcmp(outer_m, "sum")    == 0) outer_kind = 3;
        ASTNode *inner_lhs = objNode->left;
        if (inner_is_where && outer_kind &&
            inner_lhs && inner_lhs->type && inner_lhs->id &&
            (strcmp(inner_lhs->type, "ID") == 0 || strcmp(inner_lhs->type, "IDENTIFIER") == 0)) {
            Variable *lv = find_variable(inner_lhs->id);
            if (lv && lv->type && strcmp(lv->type, "LIST") == 0) {
                ASTNode *list = (ASTNode*)(intptr_t)lv->value.object_value;
                /* Resolve predicate (inner arg) */
                ASTNode *pred_arg = objNode->right;
                ASTNode *pred = NULL;
                if (pred_arg && pred_arg->type && strcmp(pred_arg->type, "LAMBDA") == 0) pred = pred_arg;
                else if (pred_arg && pred_arg->type &&
                         (strcmp(pred_arg->type, "ID") == 0 || strcmp(pred_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(pred_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        pred = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                /* Resolve outer action lambda (if any: select/firstWhere/find take fn; first/sum/firstWhere optional) */
                ASTNode *act_arg = node->right;
                ASTNode *act = NULL;
                int outer_needs_lambda = (outer_kind == 1 ||
                                          strcmp(outer_m, "find") == 0 ||
                                          strcmp(outer_m, "firstWhere") == 0);
                if (act_arg && act_arg->type && strcmp(act_arg->type, "LAMBDA") == 0) act = act_arg;
                else if (act_arg && act_arg->type &&
                         (strcmp(act_arg->type, "ID") == 0 || strcmp(act_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(act_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        act = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                /* For find/firstWhere, treat outer fn as ADDITIONAL predicate (and-combined).
                 * For plain first(), no act needed. For sum, no act needed. */
                if (pred && list && (!outer_needs_lambda || act)) {
                    /* For first(): outer_kind=2, act may be NULL (plain .first()) or a predicate (find/firstWhere). */
                    int is_find_variant = (outer_kind == 2 && act != NULL);
                    ASTNode *result = NULL;
                    double sum_acc = 0.0; int sum_is_int = 1;
                    if (outer_kind == 1) result = create_list_node(NULL);
                    ASTNode *item = list->left;
                    while (item) {
                        ASTNode *pr = call_lambda(pred, item);
                        int truthy = 0;
                        if (pr) {
                            if (pr->type && strcmp(pr->type, "STRING") == 0) {
                                truthy = (pr->str_value && pr->str_value[0]) ? 1 : 0;
                            } else if (pr->type && strcmp(pr->type, "NULL") == 0) {
                                truthy = 0;
                            } else {
                                truthy = (evaluate_expression(pr) != 0) ? 1 : 0;
                            }
                        }
                        if (truthy) {
                            if (outer_kind == 1) {
                                ASTNode *mapped = call_lambda(act, item);
                                if (mapped) te_list_append(result, mapped);
                            } else if (outer_kind == 2) {
                                if (is_find_variant) {
                                    ASTNode *ar = call_lambda(act, item);
                                    int at = 0;
                                    if (ar) {
                                        if (ar->type && strcmp(ar->type, "STRING") == 0)
                                            at = (ar->str_value && ar->str_value[0]) ? 1 : 0;
                                        else if (ar->type && strcmp(ar->type, "NULL") == 0)
                                            at = 0;
                                        else
                                            at = (evaluate_expression(ar) != 0) ? 1 : 0;
                                    }
                                    if (at) {
                                        add_or_update_variable("__ret__", build_item_from_value(item));
                                        return;
                                    }
                                } else {
                                    add_or_update_variable("__ret__", build_item_from_value(item));
                                    return;
                                }
                            } else if (outer_kind == 3) {
                                double v = item->str_value ? atof(item->str_value) : (double)item->value;
                                if (v != (double)(long long)v) sum_is_int = 0;
                                sum_acc += v;
                            }
                        }
                        item = item->next;
                    }
                    if (outer_kind == 1) {
                        add_or_update_variable("__ret__", result);
                        return;
                    }
                    if (outer_kind == 2) {
                        add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return;
                    }
                    if (outer_kind == 3) {
                        if (sum_is_int && sum_acc == (double)(long long)sum_acc) {
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)sum_acc, NULL, NULL));
                        } else {
                            char buf[64]; snprintf(buf, sizeof(buf), "%g", sum_acc);
                            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                        }
                        return;
                    }
                }
            }
        }
    }

    /* ===== v0.0.12 #N Fusion-parallel: where(pred).sumBy(proj) / .countWhere(p2) =====
     * Detect `xs.where(pred).sumBy(proj)` and `xs.where(pred).countWhere(p2)`
     * where BOTH lambdas are fast-pathable (SPEC_*). Runs as a single parallel
     * pass with OpenMP, never materializing the intermediate filtered list.
     * Falls through if any lambda is not fast-pathable. */
    if (objNode && objNode->type && strcmp(objNode->type, "CALL_METHOD") == 0 &&
        objNode->id && node->id) {
        const char *inner_m = objNode->id;
        const char *outer_m = node->id;
        int inner_is_where = (strcmp(inner_m, "where") == 0 || strcmp(inner_m, "filter") == 0);
        int outer_is_sumBy = (strcmp(outer_m, "sumBy") == 0);
        int outer_is_countWhere = (strcmp(outer_m, "countWhere") == 0);
        ASTNode *inner_lhs = objNode->left;
        if (inner_is_where && (outer_is_sumBy || outer_is_countWhere) &&
            inner_lhs && inner_lhs->type && inner_lhs->id &&
            (strcmp(inner_lhs->type, "ID") == 0 || strcmp(inner_lhs->type, "IDENTIFIER") == 0)) {
            Variable *lv = find_variable(inner_lhs->id);
            if (lv && lv->type && strcmp(lv->type, "LIST") == 0) {
                ASTNode *list = (ASTNode*)(intptr_t)lv->value.object_value;
                ASTNode *pred_arg = objNode->right;
                ASTNode *pred = NULL;
                if (pred_arg && pred_arg->type && strcmp(pred_arg->type, "LAMBDA") == 0) pred = pred_arg;
                else if (pred_arg && pred_arg->type &&
                         (strcmp(pred_arg->type, "ID") == 0 || strcmp(pred_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(pred_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        pred = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                ASTNode *proj_arg = node->right;
                ASTNode *proj = NULL;
                if (proj_arg && proj_arg->type && strcmp(proj_arg->type, "LAMBDA") == 0) proj = proj_arg;
                else if (proj_arg && proj_arg->type &&
                         (strcmp(proj_arg->type, "ID") == 0 || strcmp(proj_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(proj_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        proj = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                if (pred && proj && list) {
                    FastLambda flp; fast_lambda_analyze(pred, &flp);
                    FastLambda flj; fast_lambda_analyze(proj, &flj);
                    /* v0.0.13 (perf) COLUMNAR FUSION: where(pred).sumBy(proj) or
                     * .countWhere(p2) where pred maps to typed column AND proj
                     * (if sumBy) is SPEC_ATTR over a numeric column. */
                    if (flp.spec != SPEC_NONE && flp.attr_name && list->col_cache) {
                        TeColCache *cc = (TeColCache*)list->col_cache;
                        int pidx = te_class_attr_idx(cc->cls, flp.attr_name);
                        int jidx = (outer_is_countWhere || flj.spec != SPEC_ATTR || !flj.attr_name)
                                    ? -1 : te_class_attr_idx(cc->cls, flj.attr_name);
                        if (pidx >= 0 && (outer_is_countWhere || jidx >= 0)) {
                            int nrow = cc->n_rows;
                            char *mask = (char*)malloc((size_t)nrow);
                            if (te_colcache_eval_pred(cc, pidx, &flp, mask)) {
                                if (outer_is_countWhere) {
                                    long long total = te_colcache_count(cc, mask);
                                    free(mask);
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)total, NULL, NULL));
                                    return;
                                }
                                long long itotal = 0; double dtotal = 0.0; int is_int = 1;
                                if (te_colcache_sum(cc, jidx, mask, &itotal, &dtotal, &is_int)) {
                                    free(mask);
                                    if (is_int) {
                                        if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                        } else {
                                            char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                        }
                                    } else {
                                        char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                    return;
                                }
                            }
                            free(mask);
                        }
                    }
                    if (flp.spec != SPEC_NONE && flj.spec != SPEC_NONE) {
                        int n = 0;
                        for (ASTNode *it = list->left; it; it = it->next) n++;
#ifdef TE_HAVE_OPENMP
                        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = list->left; it; it = it->next) arr[i++] = it;
                            long long itotal = 0; double dtotal = 0.0; long long cnt = 0;
                            int fail = 0, any_float = 0;
                            #pragma omp parallel reduction(+:itotal,dtotal,cnt)
                            {
                                FastLambda flp_local = flp; flp_local.cached_class = NULL; flp_local.cached_idx = -1;
                                FastLambda flj_local = flj; flj_local.cached_class = NULL; flj_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double vp; int vpi;
                                    if (!fast_eval(&flp_local, arr[k], &vp, &vpi)) { fail = 1; continue; }
                                    if (vp == 0.0) continue;
                                    if (outer_is_countWhere) { cnt++; continue; }
                                    double vj; int vji;
                                    if (!fast_eval(&flj_local, arr[k], &vj, &vji)) { fail = 1; continue; }
                                    if (vji) itotal += (long long)vj;
                                    else { dtotal += vj; any_float = 1; }
                                }
                            }
                            free(arr);
                            if (!fail) {
                                if (outer_is_countWhere) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)cnt, NULL, NULL));
                                } else if (!any_float) {
                                    if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                    } else {
                                        char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                } else {
                                    char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal + (double)itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                                return;
                            }
                        }
#endif
                        /* Sequential fused fallback. */
                        long long itotal = 0; double dtotal = 0.0; long long cnt = 0;
                        int fail = 0, any_float = 0;
                        for (ASTNode *it = list->left; it; it = it->next) {
                            double vp; int vpi;
                            if (!fast_eval(&flp, it, &vp, &vpi)) { fail = 1; break; }
                            if (vp == 0.0) continue;
                            if (outer_is_countWhere) { cnt++; continue; }
                            double vj; int vji;
                            if (!fast_eval(&flj, it, &vj, &vji)) { fail = 1; break; }
                            if (vji) itotal += (long long)vj;
                            else { dtotal += vj; any_float = 1; }
                        }
                        if (!fail) {
                            if (outer_is_countWhere) {
                                add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)cnt, NULL, NULL));
                            } else if (!any_float) {
                                if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal + (double)itotal);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return;
                        }
                        /* fall through to standard chained handling */
                    }
                }
            }
        }
    }

    /* v0.0.11: chained method/function call — `a.foo().bar()` or `foo().bar()`.
     * Evaluate the inner call first, bind result to a unique temp variable,
     * and rewrite node->left to an ID node so the rest of this function
     * (which assumes objNode is an identifier) works unchanged. */
    if (objNode && objNode->type &&
        (strcmp(objNode->type, "CALL_METHOD") == 0 || strcmp(objNode->type, "CALL_FUNC") == 0)) {
        if (strcmp(objNode->type, "CALL_METHOD") == 0) interpret_call_method(objNode);
        else interpret_call_func(objNode);
        Variable *rv = find_variable("__ret__");
        if (rv && rv->vtype == VAL_OBJECT && rv->value.object_value) {
            static int _chain_seq = 0;
            char tmp[40];
            snprintf(tmp, sizeof(tmp), "__chain_%d__", _chain_seq++);
            ASTNode *holder = (ASTNode*)calloc(1, sizeof(ASTNode));
            holder->type = strdup(rv->type ? rv->type : "LIST");
            /* For LIST/MAP, add_or_update_variable stores value as ASTNode*
             * via object_value, so synthesize a wrapper that points to it. */
            ASTNode *inner = (ASTNode*)(intptr_t)rv->value.object_value;
            holder->left = inner ? inner->left : NULL;
            holder->right = inner ? inner->right : NULL;  /* v0.0.12 #8: preserve LAZY_ITER op chain */
            holder->str_value = inner ? inner->str_value : NULL;
            holder->value = inner ? inner->value : 0;
            add_or_update_variable(tmp, holder);
            ASTNode *id = (ASTNode*)calloc(1, sizeof(ASTNode));
            id->type = strdup("ID");
            id->id = strdup(tmp);
            node->left = id;
            objNode = id;
        }
    }

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

    /* ===== v0.0.12 #8 Lazy iterators dispatch =====
     * If LHS resolves to a LAZY_ITER, dispatch intermediate/terminal methods
     * here BEFORE the LIST handlers. */
    if (v && v->type && strcmp(v->type, "LAZY_ITER") == 0 && node->id) {
        ASTNode *lz = (ASTNode*)(intptr_t)v->value.object_value;
        const char *m = node->id;
        /* Intermediate: where/filter/select/map (lambda) */
        if (strcmp(m, "where") == 0 || strcmp(m, "filter") == 0 ||
            strcmp(m, "select") == 0 || strcmp(m, "map") == 0) {
            ASTNode *lam = lazy_resolve_lambda_arg(node->right);
            if (lam) {
                add_or_update_variable("__ret__", lazy_extend(lz, NULL, m, lam, 0));
                return;
            }
        }
        /* Intermediate: take/skip (int arg) */
        if (strcmp(m, "take") == 0 || strcmp(m, "skip") == 0) {
            int n = node->right ? (int)evaluate_expression(node->right) : 0;
            add_or_update_variable("__ret__", lazy_extend(lz, NULL, m, NULL, n));
            return;
        }
        /* Terminal: toList/toArray/count/sum/first/forEach/reduce/any/every */
        if (lazy_terminal(lz, node)) return;
        /* Unknown method on LAZY_ITER — fall through (will error or be a no-op). */
    }

    /* Fase 1b: métodos built-in en LIST: push, pop */
    if (v && v->type && strcmp(v->type, "LIST") == 0) {
        ASTNode *list = (ASTNode*)(intptr_t)v->value.object_value;

        /* ---- Fase B: higher-order methods sobre LIST ----
         * .map(fn), .filter(fn), .reduce(fn, init), .forEach(fn),
         * .find(fn), .any(fn)/.every(fn). El argumento puede ser un lambda
         * inline (NK_LAMBDA / type=="LAMBDA") o una variable de tipo LAMBDA. */
        if (list && node->id && (
                strcmp(node->id, "map") == 0 ||
                strcmp(node->id, "filter") == 0 ||
                strcmp(node->id, "reduce") == 0 ||
                strcmp(node->id, "forEach") == 0 ||
                strcmp(node->id, "find") == 0 ||
                strcmp(node->id, "any") == 0 ||
                strcmp(node->id, "every") == 0 ||
                /* v0.0.11 LINQ-style operators (lambda required) */
                strcmp(node->id, "where") == 0 ||
                strcmp(node->id, "select") == 0 ||
                strcmp(node->id, "all") == 0 ||
                strcmp(node->id, "none") == 0 ||
                strcmp(node->id, "firstWhere") == 0 ||
                strcmp(node->id, "lastWhere") == 0 ||
                strcmp(node->id, "countWhere") == 0 ||
                strcmp(node->id, "sumBy") == 0 ||
                strcmp(node->id, "avgBy") == 0 ||
                strcmp(node->id, "minBy") == 0 ||
                strcmp(node->id, "maxBy") == 0 ||
                strcmp(node->id, "takeWhile") == 0 ||
                strcmp(node->id, "skipWhile") == 0 ||
                strcmp(node->id, "flatMap") == 0 ||
                strcmp(node->id, "selectMany") == 0 ||
                strcmp(node->id, "groupBy") == 0 ||
                strcmp(node->id, "orderBy") == 0 ||
                strcmp(node->id, "orderByDescending") == 0 ||
                strcmp(node->id, "distinctBy") == 0 ||
                strcmp(node->id, "aggregate") == 0 ||
                strcmp(node->id, "fold") == 0 ||
                strcmp(node->id, "toMap") == 0 ||
                strcmp(node->id, "toDictionary") == 0)) {
            ASTNode *arg = node->right;
            ASTNode *fn = NULL;
            if (arg && arg->type && strcmp(arg->type, "LAMBDA") == 0) {
                fn = arg;
            } else if (arg && arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                Variable *fv = find_variable(arg->id);
                if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0) {
                    fn = (ASTNode*)(intptr_t)fv->value.object_value;
                }
            }
            if (!fn) {
                printf("Error: %s() requires a lambda or function value.\n", node->id);
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            /* v0.0.11: normalize aliases so we can reuse existing branches */
            const char *fname = node->id;
            if (strcmp(fname, "where") == 0) fname = "filter";
            else if (strcmp(fname, "select") == 0) fname = "map";
            else if (strcmp(fname, "all") == 0) fname = "every";
            else if (strcmp(fname, "firstWhere") == 0) fname = "find";
            else if (strcmp(fname, "aggregate") == 0) fname = "reduce";
            else if (strcmp(fname, "fold") == 0) fname = "reduce";
            else if (strcmp(fname, "toDictionary") == 0) fname = "toMap";
            /* Phase 2 fast-path: pre-analyse the lambda body once. */
            FastLambda fl; fast_lambda_analyze(fn, &fl);
            if (strcmp(fname, "map") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) te_list_append(result, r);
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "filter") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                /* v0.0.13 (perf) COLUMNAR FAST PATH: list has col_cache and
                 * predicate matches a typed column. Skips per-item pointer
                 * chase entirely. Uses AVX2 + OpenMP inside helper. */
                if (fl.spec != SPEC_NONE && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        int n = cc->n_rows;
                        char *mask = (char*)malloc((size_t)n);
                        if (te_colcache_eval_pred(cc, aidx, &fl, mask)) {
                            /* Build result list AND collect pointers to the new
                             * items so we can attach a derived columnar cache. */
                            int new_n = 0;
                            ASTNode **sel = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            for (int k = 0; k < n; k++) {
                                if (mask[k]) {
                                    ASTNode *ni = build_item_from_value(cc->items[k]);
                                    te_list_append(result, ni);
                                    sel[new_n++] = ni;
                                }
                            }
                            if (new_n > 0) {
                                te_colcache_build_from_mask(cc, mask, result, sel, new_n);
                            }
                            free(sel);
                            free(mask);
                            add_or_update_variable("__ret__", result);
                            return;
                        }
                        free(mask);
                    }
                }
                if (fl.spec != SPEC_NONE) {
#ifdef TE_HAVE_OPENMP
                    /* v0.0.12 #N: parallel filter on fast-path. Pattern:
                     *   1) materialize item pointers into arr[n] (sequential, cheap)
                     *   2) parallel evaluate predicate into mask[n] of chars
                     *   3) sequential build of result list from mask
                     * The build is sequential because te_list_append walks the
                     * list tail and updates ->next chains; doing it in the main
                     * thread avoids needing a thread-safe append. The cost is
                     * O(matches), which is much smaller than O(n) predicate eval. */
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            char *mask = (char*)malloc((size_t)n);
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            int fail = 0;
                            #pragma omp parallel
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) {
                                        mask[k] = 0; fail = 1;
                                    } else {
                                        mask[k] = (v != 0.0) ? 1 : 0;
                                    }
                                }
                            }
                            if (!fail) {
                                for (int k = 0; k < n; k++) {
                                    if (mask[k]) te_list_append(result, build_item_from_value(arr[k]));
                                }
                                free(arr); free(mask);
                                add_or_update_variable("__ret__", result);
                                return;
                            }
                            free(arr); free(mask);
                            /* fall through to sequential fallback */
                            result = create_list_node(NULL);
                            item = list->left;
                        }
                    }
#endif
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return;
                    }
                    /* fallback: rebuild a fresh list (cannot reuse partial). */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                while (item) {
                    ASTNode *next_item = item->next; /* v1.0.0: save before append; build_item_from_value may share OBJECT_LITERAL/MAP/LIST and te_list_append clobbers ->next */
                    ASTNode *r = call_lambda(fn, item);
                    int truthy = 0;
                    if (r) {
                        if (r->type && strcmp(r->type, "STRING") == 0) {
                            truthy = (r->str_value && r->str_value[0]) ? 1 : 0;
                        } else if (r->type && strcmp(r->type, "NULL") == 0) {
                            truthy = 0;
                        } else {
                            truthy = (evaluate_expression(r) != 0) ? 1 : 0;
                        }
                    }
                    if (truthy) {
                        ASTNode *copy = build_item_from_value(item);
                        te_list_append(result, copy);
                    }
                    item = next_item;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "reduce") == 0) {
                /* arg list: (fn, init). arg->right es init. */
                ASTNode *initArg = arg->right;
                ASTNode *acc = NULL;
                if (initArg) {
                    if (is_string_type(initArg)) {
                        char *s = get_node_string(initArg);
                        acc = create_ast_leaf("STRING", 0, s, NULL); free(s);
                    } else {
                        double r = evaluate_expression(initArg);
                        if (r == (double)(long long)r) acc = create_ast_leaf_number("NUMBER", (int)r, NULL, NULL);
                        else { char b[64]; snprintf(b,sizeof(b),"%g",r); acc = create_ast_leaf("FLOAT", 0, b, NULL); }
                    }
                } else {
                    acc = create_ast_leaf_number("NUMBER", 0, NULL, NULL);
                }
                ASTNode *item = list->left;
                while (item) {
                    /* Construir args: acc, item — los enlazamos por ->right. */
                    ASTNode *acc_copy = acc;
                    acc_copy->right = item;
                    ASTNode saved_item_right;
                    saved_item_right = *item; /* unused but keeps pattern */
                    (void)saved_item_right;
                    ASTNode *prev_item_right = item->right;
                    /* call_lambda lee acc->right como segundo arg */
                    ASTNode *r = call_lambda(fn, acc);
                    /* restaurar */
                    item->right = prev_item_right;
                    acc->right = NULL;
                    if (r) acc = r;
                    item = item->next;
                }
                add_or_update_variable("__ret__", acc);
                return;
            }
            if (strcmp(fname, "forEach") == 0) {
                ASTNode *item = list->left;
                while (item) {
                    call_lambda(fn, item);
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            if (strcmp(fname, "find") == 0) {
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) {
                            add_or_update_variable("__ret__", build_item_from_value(item));
                            return;
                        }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return;
                    }
                    item = list->left;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) {
                        add_or_update_variable("__ret__", build_item_from_value(item));
                        return;
                    }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            if (strcmp(fname, "any") == 0) {
                ASTNode *item = list->left;
                int found = 0;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) { found = 1; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", found, NULL, NULL));
                return;
            }
            if (strcmp(fname, "every") == 0) {
                ASTNode *item = list->left;
                int all = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v == 0.0) { all = 0; break; }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", all, NULL, NULL));
                        return;
                    }
                    item = list->left; all = 1;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (!r || evaluate_expression(r) == 0) { all = 0; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", all, NULL, NULL));
                return;
            }
            /* ===== v0.0.11 LINQ-style higher-order operators ===== */
            if (strcmp(fname, "none") == 0) {
                ASTNode *item = list->left;
                int none_match = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) { none_match = 0; break; }
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", none_match, NULL, NULL));
                        return;
                    }
                    item = list->left; none_match = 1;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) { none_match = 0; break; }
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", none_match, NULL, NULL));
                return;
            }
            if (strcmp(fname, "lastWhere") == 0) {
                ASTNode *item = list->left;
                ASTNode *last_match = NULL;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v != 0.0) last_match = item;
                        item = item->next;
                    }
                    if (ok) {
                        if (last_match) add_or_update_variable("__ret__", build_item_from_value(last_match));
                        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return;
                    }
                    item = list->left; last_match = NULL;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) last_match = item;
                    item = item->next;
                }
                if (last_match) add_or_update_variable("__ret__", build_item_from_value(last_match));
                else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            if (strcmp(fname, "countWhere") == 0) {
                ASTNode *item = list->left;
                int cnt = 0;
                /* v0.0.13 (perf) COLUMNAR FAST PATH for countWhere. */
                if (fl.spec != SPEC_NONE && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        int n = cc->n_rows;
                        char *mask = (char*)malloc((size_t)n);
                        if (te_colcache_eval_pred(cc, aidx, &fl, mask)) {
                            long long total = te_colcache_count(cc, mask);
                            free(mask);
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)total, NULL, NULL));
                            return;
                        }
                        free(mask);
                    }
                }
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
#ifdef TE_HAVE_OPENMP
                    /* Parallel path: count list, materialize pointers, parallel reduce. */
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            long long total = 0; int fail = 0;
                            #pragma omp parallel reduction(+:total)
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) { fail = 1; }
                                    else if (v != 0.0) total++;
                                }
                            }
                            free(arr);
                            if (!fail) {
                                add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)total, NULL, NULL));
                                return;
                            }
                            /* fall through to sequential fallback */
                            ok = 0;
                        }
                    }
                    if (ok)
#endif
                    {
                        while (item) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (v != 0.0) cnt++;
                            item = item->next;
                        }
                        if (ok) {
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", cnt, NULL, NULL));
                            return;
                        }
                    }
                    item = list->left; cnt = 0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && evaluate_expression(r) != 0) cnt++;
                    item = item->next;
                }
                add_or_update_variable("__ret__", create_ast_leaf_number("INT", cnt, NULL, NULL));
                return;
            }
            if (strcmp(fname, "sumBy") == 0) {
                ASTNode *item = list->left;
                /* v0.0.13 (perf) COLUMNAR FAST PATH: sumBy(fn(p)=>p.attr) over
                 * an int/float column. Uses parallel reduction inside helper. */
                if (fl.spec == SPEC_ATTR && fl.attr_name && list->col_cache) {
                    TeColCache *cc = (TeColCache*)list->col_cache;
                    int aidx = te_class_attr_idx(cc->cls, fl.attr_name);
                    if (aidx >= 0) {
                        long long itotal = 0; double dtotal = 0.0; int is_int = 1;
                        if (te_colcache_sum(cc, aidx, NULL, &itotal, &dtotal, &is_int)) {
                            if (is_int) {
                                if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return;
                        }
                    }
                }
                /* Fast-path: numeric pattern over OBJECT list — use int64 acc to
                 * avoid the 32-bit overflow that hits at ~10M-row benches. */
                if (fl.spec != SPEC_NONE) {
                    long long iacc = 0; double dacc = 0.0;
                    int is_int = 1, ok = 1;
#ifdef TE_HAVE_OPENMP
                    if (te_openmp_enabled()) {
                        int n = 0;
                        for (ASTNode *it = item; it; it = it->next) n++;
                        if (n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = item; it; it = it->next) arr[i++] = it;
                            long long itotal = 0; double dtotal = 0.0;
                            int fail = 0, any_float = 0;
                            #pragma omp parallel reduction(+:itotal,dtotal)
                            {
                                FastLambda fl_local = fl;
                                fl_local.cached_class = NULL; fl_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double v; int vint;
                                    if (!fast_eval(&fl_local, arr[k], &v, &vint)) { fail = 1; continue; }
                                    if (vint) itotal += (long long)v;
                                    else { dtotal += v; any_float = 1; }
                                }
                            }
                            free(arr);
                            if (!fail) {
                                if (!any_float) {
                                    if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)itotal, NULL, NULL));
                                    } else {
                                        char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                } else {
                                    char buf[64]; snprintf(buf, sizeof(buf), "%g", dtotal + (double)itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                                return;
                            }
                            ok = 0;
                        }
                    }
                    if (ok)
#endif
                    {
                        while (item) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (!vint) is_int = 0;
                            if (is_int) iacc += (long long)v; else dacc += v;
                            item = item->next;
                        }
                        if (ok) {
                            if (is_int) {
                                /* Build a NUMBER node carrying the int64 via str_value
                                 * when it overflows int32, otherwise the usual int slot. */
                                if (iacc >= INT_MIN && iacc <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)iacc, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", iacc);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; snprintf(buf, sizeof(buf), "%g", dacc + (double)iacc);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return;
                        }
                    }
                    item = list->left; /* reset for fallback */
                }
                double acc = 0.0;
                int is_int = 1;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) {
                        double v = evaluate_expression(r);
                        if (v != (double)(long long)v) is_int = 0;
                        acc += v;
                    }
                    item = item->next;
                }
                if (is_int && acc == (double)(long long)acc) {
                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)acc, NULL, NULL));
                } else {
                    char buf[64]; snprintf(buf, sizeof(buf), "%g", acc);
                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                }
                return;
            }
            if (strcmp(fname, "avgBy") == 0) {
                ASTNode *item = list->left;
                double acc = 0.0; int cnt = 0;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        acc += v; cnt++;
                        item = item->next;
                    }
                    if (ok) {
                        double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
                        char buf[64]; snprintf(buf, sizeof(buf), "%g", res);
                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                        return;
                    }
                    item = list->left; acc = 0.0; cnt = 0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r) { acc += evaluate_expression(r); cnt++; }
                    item = item->next;
                }
                double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
                char buf[64]; snprintf(buf, sizeof(buf), "%g", res);
                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                return;
            }
            if (strcmp(fname, "minBy") == 0 || strcmp(fname, "maxBy") == 0) {
                int want_max = (strcmp(fname, "maxBy") == 0);
                ASTNode *item = list->left;
                ASTNode *best_item = NULL;
                double best_key = 0.0;
                int first = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (first) { best_key = v; best_item = item; first = 0; }
                        else if (want_max ? (v > best_key) : (v < best_key)) { best_key = v; best_item = item; }
                        item = item->next;
                    }
                    if (ok) {
                        if (best_item) add_or_update_variable("__ret__", build_item_from_value(best_item));
                        else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return;
                    }
                    item = list->left; best_item = NULL; first = 1; best_key = 0.0;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    double k = r ? evaluate_expression(r) : 0.0;
                    if (first) { best_key = k; best_item = item; first = 0; }
                    else if (want_max ? (k > best_key) : (k < best_key)) { best_key = k; best_item = item; }
                    item = item->next;
                }
                if (best_item) add_or_update_variable("__ret__", build_item_from_value(best_item));
                else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            if (strcmp(fname, "takeWhile") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        if (v == 0.0) break;
                        te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return;
                    }
                    result = create_list_node(NULL);
                    item = list->left;
                }
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (!r || evaluate_expression(r) == 0) break;
                    te_list_append(result, build_item_from_value(item));
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "skipWhile") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                int skipping = 1;
                if (fl.spec != SPEC_NONE) {
                    int ok = 1;
                    while (item) {
                        if (skipping) {
                            double v; int vint;
                            if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                            if (v == 0.0) skipping = 0;
                        }
                        if (!skipping) te_list_append(result, build_item_from_value(item));
                        item = item->next;
                    }
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return;
                    }
                    result = create_list_node(NULL);
                    item = list->left;
                    skipping = 1;
                }
                while (item) {
                    if (skipping) {
                        ASTNode *r = call_lambda(fn, item);
                        if (!r || evaluate_expression(r) == 0) skipping = 0;
                    }
                    if (!skipping) te_list_append(result, build_item_from_value(item));
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "flatMap") == 0 || strcmp(fname, "selectMany") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *r = call_lambda(fn, item);
                    if (r && r->type && strcmp(r->type, "LIST") == 0) {
                        ASTNode *inner = r->left;
                        while (inner) {
                            te_list_append(result, build_item_from_value(inner));
                            inner = inner->next;
                        }
                    } else if (r) {
                        te_list_append(result, r);
                    }
                    item = item->next;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "groupBy") == 0) {
                /* Returns LIST of OBJECT_LITERAL {key, items: LIST}.
                 * Fast-path (numeric int key): linear probe over int64 keys;
                 * for low-cardinality keys (typical: groupBy day, status, bucket)
                 * this is ~50× faster than per-item call_lambda + strcmp.
                 * Slow path is the original string-keyed implementation. */
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE && (fl.spec == SPEC_ATTR || fl.spec == SPEC_MOD_K || fl.spec == SPEC_IDENT)) {
                    int cap = 16, nkeys = 0;
                    long long *bk = (long long*)malloc(cap * sizeof(long long));
                    ASTNode **bg = (ASTNode**)malloc(cap * sizeof(ASTNode*));
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        long long key = (long long)v;
                        int idx_g = -1;
                        for (int g = 0; g < nkeys; g++) if (bk[g] == key) { idx_g = g; break; }
                        ASTNode *group_items;
                        if (idx_g < 0) {
                            if (nkeys == cap) {
                                cap *= 2;
                                bk = (long long*)realloc(bk, cap * sizeof(long long));
                                bg = (ASTNode**)realloc(bg, cap * sizeof(ASTNode*));
                            }
                            bk[nkeys] = key;
                            ASTNode *grp = (ASTNode*)calloc(1, sizeof(ASTNode));
                            grp->type = strdup("OBJECT_LITERAL");
                            char kbuf[32]; snprintf(kbuf, sizeof(kbuf), "%lld", key);
                            ASTNode *kv_key = create_kv_pair_node("key", create_ast_leaf("STRING", 0, kbuf, NULL));
                            group_items = create_list_node(NULL);
                            ASTNode *kv_items = create_kv_pair_node("items", group_items);
                            grp->left = kv_key;
                            kv_key->next = kv_items;
                            te_list_append(result, grp);
                            bg[nkeys] = group_items;
                            nkeys++;
                        } else {
                            group_items = bg[idx_g];
                        }
                        {
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(group_items, w ? w : build_item_from_value(item));
                        }
                        item = item->next;
                    }
                    free(bk); free(bg);
                    if (ok) {
                        add_or_update_variable("__ret__", result);
                        return;
                    }
                    /* fallback: rebuild from scratch */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                /* Slow path (string keys via lambda eval): replace O(n^2) linear
                 * scan over groups with open-addressing hash table. Critical when
                 * grouping 100K+ rows by a string attribute. */
                {
                    size_t hcap = 64;
                    size_t hcount = 0;
                    char    **hk = (char**)calloc(hcap, sizeof(char*));
                    ASTNode **hv = (ASTNode**)calloc(hcap, sizeof(ASTNode*));
                    while (item) {
                        ASTNode *kr = call_lambda(fn, item);
                        char *key_str = kr ? get_node_string(kr) : strdup("");
                        /* FNV-1a 32-bit hash */
                        uint32_t h = 2166136261u;
                        for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                            h ^= *p; h *= 16777619u;
                        }
                        size_t mask = hcap - 1;
                        size_t i_h = (size_t)h & mask;
                        while (hk[i_h] && strcmp(hk[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                        ASTNode *group_items;
                        if (!hk[i_h]) {
                            hk[i_h] = key_str;   /* hash table owns this strdup */
                            ASTNode *grp = (ASTNode*)calloc(1, sizeof(ASTNode));
                            grp->type = strdup("OBJECT_LITERAL");
                            ASTNode *kv_key = create_kv_pair_node("key",
                                create_ast_leaf("STRING", 0, key_str, NULL));
                            group_items = create_list_node(NULL);
                            ASTNode *kv_items = create_kv_pair_node("items", group_items);
                            grp->left = kv_key;
                            kv_key->next = kv_items;
                            te_list_append(result, grp);
                            hv[i_h] = group_items;
                            hcount++;
                            /* Grow at 50% load */
                            if (hcount * 2 >= hcap) {
                                size_t new_cap = hcap * 2;
                                char    **nhk = (char**)calloc(new_cap, sizeof(char*));
                                ASTNode **nhv = (ASTNode**)calloc(new_cap, sizeof(ASTNode*));
                                size_t nmask = new_cap - 1;
                                for (size_t k = 0; k < hcap; k++) {
                                    if (!hk[k]) continue;
                                    uint32_t h2 = 2166136261u;
                                    for (const unsigned char *p = (const unsigned char*)hk[k]; *p; p++) {
                                        h2 ^= *p; h2 *= 16777619u;
                                    }
                                    size_t j = (size_t)h2 & nmask;
                                    while (nhk[j]) j = (j + 1) & nmask;
                                    nhk[j] = hk[k]; nhv[j] = hv[k];
                                }
                                free(hk); free(hv);
                                hk = nhk; hv = nhv; hcap = new_cap;
                            }
                        } else {
                            group_items = hv[i_h];
                            free(key_str);  /* duplicate of existing key — drop */
                        }
                        {
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(group_items, w ? w : build_item_from_value(item));
                        }
                        item = item->next;
                    }
                    /* Free hash table: hk[] strings are owned by us (not aliased
                     * into result tree — STRING leaves were created via intern). */
                    for (size_t k = 0; k < hcap; k++) if (hk[k]) free(hk[k]);
                    free(hk); free(hv);
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "orderBy") == 0 || strcmp(fname, "orderByDescending") == 0) {
                int descending = (strcmp(fname, "orderByDescending") == 0);
                /* Materialize items + their keys into parallel arrays, sort, rebuild list.
                 * Phase 2: replaced O(n^2) insertion sort with O(n log n) qsort and
                 * added fast-path key extraction (no call_lambda when lambda is a
                 * trivial accessor / arithmetic). Sort indices, not the heavy
                 * ASTNode* themselves, to keep swaps cheap. */
                int n = list_length(list);
                if (n <= 0) { add_or_update_variable("__ret__", create_list_node(NULL)); return; }
                ASTNode **items_arr = (ASTNode**)calloc(n, sizeof(ASTNode*));
                double *keys = (double*)calloc(n, sizeof(double));
                char **skeys = (char**)calloc(n, sizeof(char*));
                int is_str_key = 0;
                int i = 0;
                ASTNode *it = list->left;
                int fast_ok = 0;
                if (fl.spec != SPEC_NONE) {
                    fast_ok = 1;
                    while (it && i < n) {
                        double v; int vint;
                        if (!fast_eval(&fl, it, &v, &vint)) { fast_ok = 0; break; }
                        keys[i] = v;
                        items_arr[i] = it;
                        i++; it = it->next;
                    }
                    if (!fast_ok) {
                        /* Reset and continue via slow path below. */
                        i = 0; it = list->left;
                        memset(keys, 0, n * sizeof(double));
                        memset(items_arr, 0, n * sizeof(ASTNode*));
                    }
                }
                if (!fast_ok) {
                    while (it && i < n) {
                        ASTNode *k = call_lambda(fn, it);
                        if (k && k->type && strcmp(k->type, "STRING") == 0) {
                            is_str_key = 1;
                            skeys[i] = strdup(k->str_value ? k->str_value : "");
                        } else if (k) {
                            keys[i] = evaluate_expression(k);
                        }
                        items_arr[i] = it;
                        i++; it = it->next;
                    }
                }
                int *idx = (int*)malloc(n * sizeof(int));
                for (int a = 0; a < n; a++) idx[a] = a;
                g_sort_keys_d = keys;
                g_sort_keys_s = skeys;
                g_sort_is_str = is_str_key;
                g_sort_desc   = descending;
                qsort(idx, (size_t)n, sizeof(int), te_sort_cmp_idx);
                ASTNode *result = create_list_node(NULL);
                /* Pool fast-path: if all items are plain OBJECTs (typical for LINQ
                 * over CSV), allocate wrappers from the bump pool — avoids n×calloc
                 * and n×strdup. Otherwise fall back to build_item_from_value. */
                for (int a = 0; a < n; a++) {
                    ASTNode *src = items_arr[idx[a]];
                    ASTNode *w = build_object_wrapper_pooled(src);
                    te_list_append(result, w ? w : build_item_from_value(src));
                }
                for (int a = 0; a < n; a++) if (skeys[a]) free(skeys[a]);
                free(items_arr); free(keys); free(skeys); free(idx);
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "distinctBy") == 0) {
                /* Like distinct but keys come from a lambda. int64 fast-path via
                 * fast_eval (open-addressing hash); else string-keyed hash. */
                ASTNode *result = create_list_node(NULL);
                ASTNode *item = list->left;
                if (fl.spec != SPEC_NONE && (fl.spec == SPEC_ATTR || fl.spec == SPEC_MOD_K || fl.spec == SPEC_IDENT)) {
                    size_t icap = 64, icount = 0;
                    long long *ikeys = (long long*)calloc(icap, sizeof(long long));
                    unsigned char *iused = (unsigned char*)calloc(icap, 1);
                    int ok = 1;
                    while (item) {
                        double v; int vint;
                        if (!fast_eval(&fl, item, &v, &vint)) { ok = 0; break; }
                        long long key = (long long)v;
                        uint64_t x = (uint64_t)key;
                        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
                        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
                        size_t mask = icap - 1;
                        size_t i_h = (size_t)x & mask;
                        while (iused[i_h] && ikeys[i_h] != key) i_h = (i_h + 1) & mask;
                        if (!iused[i_h]) {
                            iused[i_h] = 1; ikeys[i_h] = key; icount++;
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(result, w ? w : build_item_from_value(item));
                            if (icount * 2 >= icap) {
                                size_t nc = icap * 2;
                                long long *nk = (long long*)calloc(nc, sizeof(long long));
                                unsigned char *nu = (unsigned char*)calloc(nc, 1);
                                size_t nm = nc - 1;
                                for (size_t k = 0; k < icap; k++) if (iused[k]) {
                                    uint64_t x2 = (uint64_t)ikeys[k];
                                    x2 ^= x2 >> 33; x2 *= 0xff51afd7ed558ccdULL;
                                    x2 ^= x2 >> 33; x2 *= 0xc4ceb9fe1a85ec53ULL; x2 ^= x2 >> 33;
                                    size_t j = (size_t)x2 & nm;
                                    while (nu[j]) j = (j + 1) & nm;
                                    nu[j] = 1; nk[j] = ikeys[k];
                                }
                                free(ikeys); free(iused); ikeys = nk; iused = nu; icap = nc;
                            }
                        }
                        item = item->next;
                    }
                    free(ikeys); free(iused);
                    if (ok) { add_or_update_variable("__ret__", result); return; }
                    /* fallback */
                    result = create_list_node(NULL);
                    item = list->left;
                }
                {
                    size_t scap = 64, scount = 0;
                    char **skeys = (char**)calloc(scap, sizeof(char*));
                    while (item) {
                        ASTNode *kr = call_lambda(fn, item);
                        char *key_str = kr ? get_node_string(kr) : strdup("");
                        uint32_t h = 2166136261u;
                        for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                            h ^= *p; h *= 16777619u;
                        }
                        size_t mask = scap - 1;
                        size_t i_h = (size_t)h & mask;
                        while (skeys[i_h] && strcmp(skeys[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                        if (skeys[i_h]) {
                            free(key_str);
                        } else {
                            skeys[i_h] = key_str;
                            scount++;
                            ASTNode *w = build_object_wrapper_pooled(item);
                            te_list_append(result, w ? w : build_item_from_value(item));
                            if (scount * 2 >= scap) {
                                size_t nc = scap * 2;
                                char **ns = (char**)calloc(nc, sizeof(char*));
                                size_t nm = nc - 1;
                                for (size_t k = 0; k < scap; k++) if (skeys[k]) {
                                    uint32_t h2 = 2166136261u;
                                    for (const unsigned char *p = (const unsigned char*)skeys[k]; *p; p++) {
                                        h2 ^= *p; h2 *= 16777619u;
                                    }
                                    size_t j = (size_t)h2 & nm;
                                    while (ns[j]) j = (j + 1) & nm;
                                    ns[j] = skeys[k];
                                }
                                free(skeys); skeys = ns; scap = nc;
                            }
                        }
                        item = item->next;
                    }
                    for (size_t k = 0; k < scap; k++) if (skeys[k]) free(skeys[k]);
                    free(skeys);
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "toMap") == 0) {
                /* toMap(keyFn): builds OBJECT_LITERAL {key1: item1, key2: item2, ...}.
                 * Last write wins on duplicate keys. Hash table dedupes keys in O(1). */
                ASTNode *result = (ASTNode*)calloc(1, sizeof(ASTNode));
                result->type = strdup("OBJECT_LITERAL");
                ASTNode *tail = NULL;
                size_t hcap = 64, hcount = 0;
                char    **hk = (char**)calloc(hcap, sizeof(char*));
                ASTNode **hv = (ASTNode**)calloc(hcap, sizeof(ASTNode*));
                ASTNode *item = list->left;
                while (item) {
                    ASTNode *kr = call_lambda(fn, item);
                    char *key_str = kr ? get_node_string(kr) : strdup("");
                    uint32_t h = 2166136261u;
                    for (const unsigned char *p = (const unsigned char*)key_str; *p; p++) {
                        h ^= *p; h *= 16777619u;
                    }
                    size_t mask = hcap - 1;
                    size_t i_h = (size_t)h & mask;
                    while (hk[i_h] && strcmp(hk[i_h], key_str) != 0) i_h = (i_h + 1) & mask;
                    ASTNode *val = build_object_wrapper_pooled(item);
                    if (!val) val = build_item_from_value(item);
                    if (hk[i_h]) {
                        ASTNode *pair = hv[i_h];
                        if (pair->left) free_ast(pair->left);
                        pair->left = val;
                        free(key_str);
                    } else {
                        hk[i_h] = key_str;
                        ASTNode *pair = create_kv_pair_node(key_str, val);
                        /* MAP pairs are linked via ->right (see resolve_to_map /
                         * te_map_get_hash which iterate cur = cur->right). */
                        if (!result->left) result->left = pair;
                        else tail->right = pair;
                        tail = pair;
                        hv[i_h] = pair;
                        hcount++;
                        if (hcount * 2 >= hcap) {
                            size_t nc = hcap * 2;
                            char    **nhk = (char**)calloc(nc, sizeof(char*));
                            ASTNode **nhv = (ASTNode**)calloc(nc, sizeof(ASTNode*));
                            size_t nm = nc - 1;
                            for (size_t k = 0; k < hcap; k++) if (hk[k]) {
                                uint32_t h2 = 2166136261u;
                                for (const unsigned char *p = (const unsigned char*)hk[k]; *p; p++) {
                                    h2 ^= *p; h2 *= 16777619u;
                                }
                                size_t j = (size_t)h2 & nm;
                                while (nhk[j]) j = (j + 1) & nm;
                                nhk[j] = hk[k]; nhv[j] = hv[k];
                            }
                            free(hk); free(hv); hk = nhk; hv = nhv; hcap = nc;
                        }
                    }
                    item = item->next;
                }
                for (size_t k = 0; k < hcap; k++) if (hk[k]) free(hk[k]);
                free(hk); free(hv);
                add_or_update_variable("__ret__", result);
                return;
            }
        }

        /* ===== v0.0.12 #8 Lazy iterator promotion: xs.lazy() → LAZY_ITER ===== */
        if (list && node->id && strcmp(node->id, "lazy") == 0) {
            ASTNode *lz = (ASTNode*)calloc(1, sizeof(ASTNode));
            lz->type = strdup("LAZY_ITER");
            lz->left = list;   /* source LIST (shared ref) */
            lz->right = NULL;  /* empty op chain */
            add_or_update_variable("__ret__", lz);
            return;
        }

        /* ===== v0.0.11 LINQ: numeric / no-arg methods on LIST ===== */
        if (list && node->id && (
                strcmp(node->id, "sum") == 0 ||
                strcmp(node->id, "avg") == 0 ||
                strcmp(node->id, "average") == 0 ||
                strcmp(node->id, "minVal") == 0 ||
                strcmp(node->id, "maxVal") == 0 ||
                strcmp(node->id, "first") == 0 ||
                strcmp(node->id, "last") == 0 ||
                strcmp(node->id, "take") == 0 ||
                strcmp(node->id, "skip") == 0 ||
                strcmp(node->id, "distinct") == 0 ||
                strcmp(node->id, "toList") == 0 ||
                strcmp(node->id, "concat") == 0 ||
                strcmp(node->id, "zip") == 0)) {
            const char *fname = node->id;
            if (strcmp(fname, "sum") == 0) {
                double acc = 0.0; int is_int = 1;
                ASTNode *it = list->left;
                while (it) {
                    double v = it->str_value ? atof(it->str_value) : (double)it->value;
                    if (v != (double)(long long)v) is_int = 0;
                    acc += v;
                    it = it->next;
                }
                if (is_int && acc == (double)(long long)acc) {
                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)acc, NULL, NULL));
                } else {
                    char buf[64]; snprintf(buf, sizeof(buf), "%g", acc);
                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                }
                return;
            }
            if (strcmp(fname, "avg") == 0 || strcmp(fname, "average") == 0) {
                double acc = 0.0; int cnt = 0;
                ASTNode *it = list->left;
                while (it) {
                    acc += it->str_value ? atof(it->str_value) : (double)it->value;
                    cnt++; it = it->next;
                }
                double res = cnt > 0 ? (acc / (double)cnt) : 0.0;
                char buf[64]; snprintf(buf, sizeof(buf), "%g", res);
                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                return;
            }
            if (strcmp(fname, "minVal") == 0 || strcmp(fname, "maxVal") == 0) {
                int want_max = (strcmp(fname, "maxVal") == 0);
                ASTNode *it = list->left;
                if (!it) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return; }
                int is_str = (it->type && strcmp(it->type, "STRING") == 0);
                double best_n = 0.0; char *best_s = NULL;
                if (is_str) best_s = it->str_value ? it->str_value : "";
                else best_n = it->str_value ? atof(it->str_value) : (double)it->value;
                ASTNode *best_node = it;
                it = it->next;
                while (it) {
                    if (is_str) {
                        const char *cs = it->str_value ? it->str_value : "";
                        int c = strcmp(cs, best_s);
                        if (want_max ? (c > 0) : (c < 0)) { best_s = (char*)cs; best_node = it; }
                    } else {
                        double v = it->str_value ? atof(it->str_value) : (double)it->value;
                        if (want_max ? (v > best_n) : (v < best_n)) { best_n = v; best_node = it; }
                    }
                    it = it->next;
                }
                add_or_update_variable("__ret__", build_item_from_value(best_node));
                return;
            }
            if (strcmp(fname, "first") == 0) {
                if (list->left) add_or_update_variable("__ret__", build_item_from_value(list->left));
                else add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                return;
            }
            if (strcmp(fname, "last") == 0) {
                ASTNode *cur = list->left;
                if (!cur) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return; }
                while (cur->next) cur = cur->next;
                add_or_update_variable("__ret__", build_item_from_value(cur));
                return;
            }
            if (strcmp(fname, "take") == 0) {
                int n = node->right ? (int)evaluate_expression(node->right) : 0;
                ASTNode *result = create_list_node(NULL);
                ASTNode *it = list->left;
                int i = 0;
                while (it && i < n) {
                    te_list_append(result, build_item_from_value(it));
                    it = it->next; i++;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "skip") == 0) {
                int n = node->right ? (int)evaluate_expression(node->right) : 0;
                ASTNode *result = create_list_node(NULL);
                ASTNode *it = list->left;
                int i = 0;
                while (it) {
                    if (i >= n) te_list_append(result, build_item_from_value(it));
                    it = it->next; i++;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "distinct") == 0) {
                /* Hash-based dedupe. Two parallel tables (int and string) so
                 * mixed lists are handled correctly (numeric == numeric only,
                 * string == string only — matches the previous O(n^2) impl). */
                ASTNode *result = create_list_node(NULL);
                size_t icap = 64, scap = 64;
                size_t icount = 0, scount = 0;
                long long *ikeys = (long long*)calloc(icap, sizeof(long long));
                unsigned char *iused = (unsigned char*)calloc(icap, 1);
                char **skeys = (char**)calloc(scap, sizeof(char*));
                ASTNode *it = list->left;
                while (it) {
                    int is_str = (it->type && strcmp(it->type, "STRING") == 0);
                    int dup = 0;
                    if (is_str) {
                        const char *s = it->str_value ? it->str_value : "";
                        uint32_t h = 2166136261u;
                        for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
                            h ^= *p; h *= 16777619u;
                        }
                        size_t mask = scap - 1;
                        size_t i_h = (size_t)h & mask;
                        while (skeys[i_h] && strcmp(skeys[i_h], s) != 0) i_h = (i_h + 1) & mask;
                        if (skeys[i_h]) dup = 1;
                        else {
                            skeys[i_h] = strdup(s);
                            scount++;
                            if (scount * 2 >= scap) {
                                size_t nc = scap * 2;
                                char **ns = (char**)calloc(nc, sizeof(char*));
                                size_t nm = nc - 1;
                                for (size_t k = 0; k < scap; k++) if (skeys[k]) {
                                    uint32_t h2 = 2166136261u;
                                    for (const unsigned char *p = (const unsigned char*)skeys[k]; *p; p++) {
                                        h2 ^= *p; h2 *= 16777619u;
                                    }
                                    size_t j = (size_t)h2 & nm;
                                    while (ns[j]) j = (j + 1) & nm;
                                    ns[j] = skeys[k];
                                }
                                free(skeys); skeys = ns; scap = nc;
                            }
                        }
                    } else {
                        long long key = it->str_value ? (long long)atof(it->str_value) : (long long)it->value;
                        /* hash for int64 */
                        uint64_t x = (uint64_t)key;
                        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
                        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
                        size_t mask = icap - 1;
                        size_t i_h = (size_t)x & mask;
                        while (iused[i_h] && ikeys[i_h] != key) i_h = (i_h + 1) & mask;
                        if (iused[i_h]) dup = 1;
                        else {
                            iused[i_h] = 1; ikeys[i_h] = key; icount++;
                            if (icount * 2 >= icap) {
                                size_t nc = icap * 2;
                                long long *nk = (long long*)calloc(nc, sizeof(long long));
                                unsigned char *nu = (unsigned char*)calloc(nc, 1);
                                size_t nm = nc - 1;
                                for (size_t k = 0; k < icap; k++) if (iused[k]) {
                                    uint64_t x2 = (uint64_t)ikeys[k];
                                    x2 ^= x2 >> 33; x2 *= 0xff51afd7ed558ccdULL;
                                    x2 ^= x2 >> 33; x2 *= 0xc4ceb9fe1a85ec53ULL; x2 ^= x2 >> 33;
                                    size_t j = (size_t)x2 & nm;
                                    while (nu[j]) j = (j + 1) & nm;
                                    nu[j] = 1; nk[j] = ikeys[k];
                                }
                                free(ikeys); free(iused); ikeys = nk; iused = nu; icap = nc;
                            }
                        }
                    }
                    if (!dup) te_list_append(result, build_item_from_value(it));
                    it = it->next;
                }
                for (size_t k = 0; k < scap; k++) if (skeys[k]) free(skeys[k]);
                free(skeys); free(ikeys); free(iused);
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "toList") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *it = list->left;
                while (it) { te_list_append(result, build_item_from_value(it)); it = it->next; }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "concat") == 0) {
                ASTNode *result = create_list_node(NULL);
                ASTNode *it = list->left;
                while (it) { te_list_append(result, build_item_from_value(it)); it = it->next; }
                /* Resolve other arg — must be LIST */
                ASTNode *arg = node->right;
                ASTNode *other_list = NULL;
                if (arg) {
                    if (arg->type && strcmp(arg->type, "LIST") == 0) other_list = arg;
                    else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                        Variable *ov = find_variable(arg->id);
                        if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "LIST") == 0) {
                            other_list = (ASTNode*)(intptr_t)ov->value.object_value;
                        }
                    }
                }
                if (other_list) {
                    ASTNode *oi = other_list->left;
                    while (oi) { te_list_append(result, build_item_from_value(oi)); oi = oi->next; }
                }
                add_or_update_variable("__ret__", result);
                return;
            }
            if (strcmp(fname, "zip") == 0) {
                /* zip(other): pair items by index. Returns LIST of OBJECT_LITERAL {left, right}.
                 * Length = min(len(this), len(other)). KV_PAIR: id=key, left=value,
                 * right=next-pair-in-map chain (per map convention). */
                ASTNode *result = create_list_node(NULL);
                ASTNode *arg = node->right;
                ASTNode *other_list = NULL;
                if (arg) {
                    if (arg->type && strcmp(arg->type, "LIST") == 0) other_list = arg;
                    else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                        Variable *ov = find_variable(arg->id);
                        if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "LIST") == 0) {
                            other_list = (ASTNode*)(intptr_t)ov->value.object_value;
                        }
                    }
                }
                if (!other_list) {
                    add_or_update_variable("__ret__", result);
                    return;
                }
                ASTNode *a = list->left;
                ASTNode *b = other_list->left;
                while (a && b) {
                    ASTNode *lit = (ASTNode*)calloc(1, sizeof(ASTNode));
                    lit->type = strdup("OBJECT_LITERAL");
                    ASTNode *p1 = create_kv_pair_node("left",  build_item_from_value(a));
                    ASTNode *p2 = create_kv_pair_node("right", build_item_from_value(b));
                    lit->left = p1;
                    p1->right = p2;
                    te_list_append(result, lit);
                    a = a->next;
                    b = b->next;
                }
                add_or_update_variable("__ret__", result);
                return;
            }
        }

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
            te_colcache_invalidate(list);     /* v0.0.13 (perf) */
            return;
        }
        if (list && node->id && strcmp(node->id, "pop") == 0) {
            ASTNode *cur = list->left;
            if (!cur) return;
            te_colcache_invalidate(list);     /* v0.0.13 (perf) */
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
            te_colcache_invalidate(list);    /* v0.0.13 (perf) */
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
            te_colcache_invalidate(list);    /* v0.0.13 (perf) */
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
    clone->owning_list = NULL;  /* v0.0.13 (perf): clone is not in any list yet */
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

/* CSV reader, ORM arena, AST node pool and DataFrame analytics now live
 * in te_csv.c (Fase 2 paso 2). Public surface declared in te_csv.h. */




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
        } else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0 || strcmp(value_node->type, "MOD") == 0 || strcmp(value_node->type, "NEG") == 0 || strcmp(value_node->type, "BIT_AND") == 0 || strcmp(value_node->type, "BIT_OR") == 0 || strcmp(value_node->type, "BIT_XOR") == 0 || strcmp(value_node->type, "BIT_NOT") == 0 || strcmp(value_node->type, "SHL") == 0 || strcmp(value_node->type, "SHR") == 0 || strcmp(value_node->type, "IN") == 0) {
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

    /* Si el tipo "efectivo" es en realidad un AST node type que requiere
     * evaluación (ej. ACCESS_ATTR, ACCESS_INDEX, NEG, MOD, IDENTIFIER),
     * deferimos la verificación a runtime. La fase posterior ya valida
     * el tipo concreto cuando se asigna efectivamente. */
    if (declared_type != NULL && effective_value_type_str != NULL) {
        int is_runtime_only =
            strcmp(effective_value_type_str, "ACCESS_ATTR") == 0 ||
            strcmp(effective_value_type_str, "ACCESS_INDEX") == 0 ||
            strcmp(effective_value_type_str, "NEG") == 0 ||
            strcmp(effective_value_type_str, "MOD") == 0 ||
            strcmp(effective_value_type_str, "IDENTIFIER") == 0;
        if (is_runtime_only) {
            effective_value_type_str = NULL; /* skip static check */
        }
    }

    if (declared_type != NULL && effective_value_type_str != NULL) {
        if (strcmp(declared_type, effective_value_type_str) != 0) {
            int allow_int_to_float = (strcmp(declared_type, "FLOAT") == 0 && strcmp(effective_value_type_str, "INT") == 0);
            /* v1.0.0: DATETIME and UUID are storage-aliased to STRING. */
            int allow_string_alias = (strcmp(effective_value_type_str, "STRING") == 0 &&
                                       (strcmp(declared_type, "DATETIME") == 0 ||
                                        strcmp(declared_type, "UUID") == 0));
            /* BOOL accepts BOOL literals, INT 0/1, and other BOOL exprs. */
            int allow_bool_alias = (strcmp(declared_type, "BOOL") == 0 &&
                                     (strcmp(effective_value_type_str, "INT") == 0 ||
                                      strcmp(effective_value_type_str, "BOOL") == 0 ||
                                      strcmp(effective_value_type_str, "NUMBER") == 0));
            if (!allow_int_to_float && !allow_string_alias && !allow_bool_alias) {
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

    /* Bug fix mayo 2026: `let p = arr[i]` cuando arr es LIST de OBJECT/STRING/INT.
     * Sin esto, declare_variable cae al final-else y trata p como INT 0, perdiendo
     * la referencia al ObjectNode. Aplicable a CSV-loaded lists y a literales.
     * Si el ítem no se puede resolver, cae al flujo legacy. */
    if (evaluated_value_var == NULL && value_node && value_node->type &&
        strcmp(value_node->type, "ACCESS_EXPR") == 0) {
        ASTNode *list = resolve_to_list(value_node->left);
        if (list) {
            int idx = (int)evaluate_expression(value_node->right);
            int len = list_length(list);
            if (idx >= 0 && idx < len) {
                ASTNode *item = list_get_item(list, idx);
                if (item && item->type) {
                    if (strcmp(item->type, "OBJECT") == 0) {
                        ObjectNode *obj = item->extra
                            ? (ObjectNode*)item->extra
                            : (ObjectNode*)(intptr_t)item->value;
                        if (obj) {
                            Variable *var = &vars[var_count++];
                            var->id = strdup(node->id);
                            var->is_const = is_const_flag;
                            var->vtype = VAL_OBJECT;
                            var->type = strdup("OBJECT");
                            var->value.object_value = obj;
                            te_sym_insert(var->id, var_count - 1);
                            return;
                        }
                    } else if (strcmp(item->type, "STRING") == 0) {
                        Variable *var = &vars[var_count++];
                        var->id = strdup(node->id);
                        var->is_const = is_const_flag;
                        var->vtype = VAL_STRING;
                        var->type = strdup("STRING");
                        var->value.string_value = strdup(item->str_value ? item->str_value : "");
                        te_sym_insert(var->id, var_count - 1);
                        return;
                    } else if (strcmp(item->type, "NUMBER") == 0 || strcmp(item->type, "INT") == 0) {
                        Variable *var = &vars[var_count++];
                        var->id = strdup(node->id);
                        var->is_const = is_const_flag;
                        var->vtype = VAL_INT;
                        var->type = strdup("INT");
                        var->value.int_value = item->value;
                        te_sym_insert(var->id, var_count - 1);
                        return;
                    } else if (strcmp(item->type, "FLOAT") == 0) {
                        Variable *var = &vars[var_count++];
                        var->id = strdup(node->id);
                        var->is_const = is_const_flag;
                        var->vtype = VAL_FLOAT;
                        var->type = strdup("FLOAT");
                        var->value.float_value = item->str_value ? atof(item->str_value) : 0.0;
                        te_sym_insert(var->id, var_count - 1);
                        return;
                    }
                }
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
            } else if (strcmp(evaluated_value_var->type, "MAP") == 0) {
                /* Phase D: fast-path for MAP returned from a builtin (e.g. json_parse). */
                Variable *var = malloc(sizeof(Variable));
                var->id = strdup(node->id);
                var->is_const = is_const_flag;
                var->vtype = VAL_OBJECT;
                var->type = strdup("MAP");
                var->value.object_value = (ObjectNode *)evaluated_value_var->value.object_value;
                vars[var_count++] = *var;
                free(var);
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
                    if (__ret_var.id) free(__ret_var.id);
                    if (__ret_var.type) free(__ret_var.type);
                    memset(&__ret_var, 0, sizeof(Variable));
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
    
    // v0.0.11: items now come pre-chained via ->next from append_to_list_parser.
    // No right→next conversion needed (and doing it would clobber BINOP right operands).

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
    if (!list) { if (item) item->next = NULL; return item; }
    /* v0.0.11 fix: chain via ->next ONLY so BINOP/method-call items (which use
     * ->right for their own right operand) are not clobbered. Constructor arg
     * walkers that previously walked ->right have been migrated to use
     * expression_list (still ->right) for NEW; this list-literal builder is
     * for `[...]` only. */
    ASTNode *cur = list;
    while (cur->next) cur = cur->next;
    cur->next = item;
    if (item) item->next = NULL;
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

/* Fase B: lambda multi-param. paramsCsv = nombres separados por '\1'.
 * Usamos el mismo NodeKind LAMBDA. body puede ser expr o STATEMENT_LIST. */
ASTNode* create_lambda_multi_node(const char *paramsCsv, ASTNode *body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(paramsCsv ? paramsCsv : "");
    node->left = body;
    node->right = NULL;
    node->next = NULL;
    node->line = yylineno;
    return node;
}

/* Fase B: invoca un lambda con argsList (lista enlazada por ->right o ->next).
 * Setea cada parámetro en el scope global, evalúa el body y devuelve el ASTNode*
 * con el resultado. Si body es expresión, retorna su valor evaluado.
 * Si body es STATEMENT_LIST, ejecuta y lee return_node / __ret__. */
ASTNode* call_lambda(ASTNode *lambda, ASTNode *argsList) {
    if (!lambda || !lambda->id) {
        return create_ast_leaf("NULL", 0, NULL, NULL);
    }
    /* Parsear paramsCsv ('\1'-separated). Casos: "" (0 params), "x", "x\1y", etc. */
    const char *params = lambda->id;
    /* Bind args a params, en orden. argsList puede ser NULL o lista por ->right. */
    ASTNode *cur_arg = argsList;
    if (params[0]) {
        const char *p = params;
        while (*p) {
            const char *e = p;
            while (*e && *e != '\1') e++;
            size_t n = (size_t)(e - p);
            char name[128];
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, p, n); name[n] = '\0';
            /* Construir un ASTNode "valor concreto" para este param */
            ASTNode *valNode = NULL;
            if (cur_arg) {
                /* v1.0.0: list items can be CALL_FUNC/CALL_METHOD (e.g.
                 * `[uuid_v4(), uuid_v4(), ...]` was materialized without
                 * eager evaluation). Dispatch the call and bind from
                 * __ret__ so lambdas see the actual produced value. */
                if (cur_arg->type && (strcmp(cur_arg->type, "CALL_FUNC") == 0 ||
                                      strcmp(cur_arg->type, "CALL_METHOD") == 0)) {
                    if (strcmp(cur_arg->type, "CALL_FUNC") == 0) interpret_call_func(cur_arg);
                    else                                          interpret_call_method(cur_arg);
                    Variable *rr = find_variable("__ret__");
                    if (rr && rr->vtype == VAL_STRING) {
                        const char *tt = (rr->type && (strcmp(rr->type, "DATETIME") == 0 ||
                                                       strcmp(rr->type, "UUID") == 0))
                                         ? rr->type : "STRING";
                        valNode = create_ast_leaf((char*)tt, 0,
                            rr->value.string_value ? rr->value.string_value : "", NULL);
                    } else if (rr && rr->vtype == VAL_INT) {
                        const char *tt = (rr->type && strcmp(rr->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
                        valNode = create_ast_leaf_number((char*)tt, rr->value.int_value, NULL, NULL);
                    } else if (rr && rr->vtype == VAL_FLOAT) {
                        char buf[64]; snprintf(buf, sizeof(buf), "%g", rr->value.float_value);
                        valNode = create_ast_leaf("FLOAT", 0, buf, NULL);
                    } else {
                        valNode = create_ast_leaf("NULL", 0, NULL, NULL);
                    }
                } else if (cur_arg->type && (strcmp(cur_arg->type, "OBJECT") == 0 ||
                                      strcmp(cur_arg->type, "STRING") == 0 ||
                                      strcmp(cur_arg->type, "NUMBER") == 0 ||
                                      strcmp(cur_arg->type, "INT") == 0 ||
                                      strcmp(cur_arg->type, "FLOAT") == 0 ||
                                      strcmp(cur_arg->type, "NULL") == 0 ||
                                      strcmp(cur_arg->type, "LIST") == 0 ||
                                      strcmp(cur_arg->type, "OBJECT_LITERAL") == 0)) {
                    valNode = cur_arg;
                } else if (is_string_type(cur_arg)) {
                    char *s = get_node_string(cur_arg);
                    valNode = create_ast_leaf("STRING", 0, s, NULL);
                    free(s);
                } else {
                    double r = evaluate_expression(cur_arg);
                    if (r == (double)(long long)r) {
                        valNode = create_ast_leaf_number("NUMBER", (int)r, NULL, NULL);
                    } else {
                        char buf[64]; snprintf(buf, sizeof(buf), "%g", r);
                        valNode = create_ast_leaf("FLOAT", 0, buf, NULL);
                    }
                }
                cur_arg = cur_arg->right ? cur_arg->right : cur_arg->next;
            } else {
                valNode = create_ast_leaf("NULL", 0, NULL, NULL);
            }
            add_or_update_variable(name, valNode);
            if (*e == '\1') p = e + 1; else p = e;
        }
    }
    /* Ejecutar el body */
    ASTNode *body = lambda->left;
    if (!body) return create_ast_leaf("NULL", 0, NULL, NULL);
    if (body->type && strcmp(body->type, "STATEMENT_LIST") == 0) {
        int saved_return_flag = return_flag;
        ASTNode *saved_return_node = return_node;
        return_flag = 0;
        return_node = NULL;
        interpret_ast(body);
        ASTNode *ret = return_node;
        return_flag = saved_return_flag;
        return_node = saved_return_node;
        if (!ret) return create_ast_leaf("NULL", 0, NULL, NULL);
        if (ret->type && strcmp(ret->type, "RETURN") == 0 && ret->left) ret = ret->left;
        /* Normalizar a valor concreto (evaluar la expresión) */
        if (ret->type && (
                strcmp(ret->type, "STRING") == 0 ||
                strcmp(ret->type, "NUMBER") == 0 ||
                strcmp(ret->type, "INT") == 0 ||
                strcmp(ret->type, "FLOAT") == 0 ||
                strcmp(ret->type, "NULL") == 0 ||
                strcmp(ret->type, "LIST") == 0 ||
                strcmp(ret->type, "OBJECT") == 0 ||
                strcmp(ret->type, "OBJECT_LITERAL") == 0 ||
                strcmp(ret->type, "MAP") == 0)) {
            return ret;
        }
        if (is_string_type(ret)) {
            char *s = get_node_string(ret);
            ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
            free(s);
            return r;
        }
        if (ret->type && (strcmp(ret->type, "ID") == 0 || strcmp(ret->type, "IDENTIFIER") == 0)) {
            Variable *v = find_variable(ret->id);
            if (v && v->vtype == VAL_OBJECT && v->type) {
                if (strcmp(v->type, "LIST") == 0 || strcmp(v->type, "MAP") == 0) {
                    return (ASTNode*)(intptr_t)v->value.object_value;
                }
            }
        }
        double dr = evaluate_expression(ret);
        if (dr == (double)(long long)dr) return create_ast_leaf_number("NUMBER", (int)dr, NULL, NULL);
        char buf[64]; snprintf(buf, sizeof(buf), "%g", dr);
        return create_ast_leaf("FLOAT", 0, buf, NULL);
    }
    /* body es expresión. Si produce string, devolver STRING; si numérica, NUMBER/FLOAT. */
    if (is_string_type(body)) {
        char *s = get_node_string(body);
        ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
        free(s);
        return r;
    }
    /* v0.0.11: lambda expression-body returns a LIST/MAP literal directly.
     * For LIST, build a fresh list with each item evaluated (items may be
     * expressions like `n * 10` that reference bound params). */
    if (body->type && strcmp(body->type, "LIST") == 0) {
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = body->left;
        while (it) {
            ASTNode *valNode = NULL;
            if (it->type && (strcmp(it->type, "STRING") == 0 ||
                             strcmp(it->type, "LIST") == 0 ||
                             strcmp(it->type, "MAP") == 0 ||
                             strcmp(it->type, "OBJECT_LITERAL") == 0 ||
                             strcmp(it->type, "NULL") == 0)) {
                valNode = it;
            } else if (is_string_type(it)) {
                char *s = get_node_string(it);
                valNode = create_ast_leaf("STRING", 0, s, NULL);
                free(s);
            } else {
                double dv = evaluate_expression(it);
                if (dv == (double)(long long)dv) valNode = create_ast_leaf_number("NUMBER", (int)dv, NULL, NULL);
                else { char buf[64]; snprintf(buf, sizeof(buf), "%g", dv); valNode = create_ast_leaf("FLOAT", 0, buf, NULL); }
            }
            te_list_append(result, valNode);
            it = it->next;
        }
        return result;
    }
    if (body->type && (strcmp(body->type, "MAP") == 0 ||
                       strcmp(body->type, "OBJECT_LITERAL") == 0)) {
        return body;
    }
    /* v1.0.0: lambda body is a CALL_FUNC/CALL_METHOD. Dispatch the call,
     * read __ret__, and return a typed leaf (preserves STRING vs NUMBER —
     * needed for builtins like uuid_v4()/now() (STRING) and date_parse()
     * (INT) used inside LINQ where/select). Without this, generic
     * evaluate_expression collapses everything to a double and string
     * results come back as empty. */
    if (body->type && (strcmp(body->type, "CALL_FUNC") == 0 ||
                       strcmp(body->type, "CALL_METHOD") == 0)) {
        if (strcmp(body->type, "CALL_FUNC") == 0) interpret_call_func(body);
        else                                       interpret_call_method(body);
        Variable *r = find_variable("__ret__");
        if (!r) return create_ast_leaf("NULL", 0, NULL, NULL);
        if (r->vtype == VAL_STRING) {
            const char *s = r->value.string_value ? r->value.string_value : "";
            const char *t = (r->type && (strcmp(r->type, "DATETIME") == 0 ||
                                         strcmp(r->type, "UUID") == 0))
                            ? r->type : "STRING";
            return create_ast_leaf((char*)t, 0, (char*)s, NULL);
        }
        if (r->vtype == VAL_INT) {
            const char *t = (r->type && strcmp(r->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
            return create_ast_leaf_number((char*)t, r->value.int_value, NULL, NULL);
        }
        if (r->vtype == VAL_FLOAT) {
            char buf[64]; snprintf(buf, sizeof(buf), "%g", r->value.float_value);
            return create_ast_leaf("FLOAT", 0, buf, NULL);
        }
        if (r->vtype == VAL_OBJECT && r->type) {
            if (strcmp(r->type, "LIST") == 0 || strcmp(r->type, "MAP") == 0)
                return (ASTNode*)(intptr_t)r->value.object_value;
            if (strcmp(r->type, "NULL") == 0)
                return create_ast_leaf("NULL", 0, NULL, NULL);
        }
        return create_ast_leaf("NULL", 0, NULL, NULL);
    }
    /* Si la expresión produce un OBJECT/LIST/MAP, evaluate_expression no lo refleja.
     * Detectar referencias a IDENTIFIER que apunten a OBJECT/LIST/MAP. */
    if (body->type && (strcmp(body->type, "ID") == 0 || strcmp(body->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(body->id);
        if (v && v->vtype == VAL_OBJECT && v->type) {
            if (strcmp(v->type, "LIST") == 0 || strcmp(v->type, "MAP") == 0) {
                return (ASTNode*)(intptr_t)v->value.object_value;
            }
            if (strcmp(v->type, "OBJECT") == 0) {
                ASTNode *r = (ASTNode*)calloc(1, sizeof(ASTNode));
                r->type = strdup("OBJECT");
                r->extra = (struct ASTNode*)v->value.object_value;
                return r;
            }
            if (strcmp(v->type, "NULL") == 0) {
                return create_ast_leaf("NULL", 0, NULL, NULL);
            }
        }
    }
    double r = evaluate_expression(body);
    if (r == (double)(long long)r) {
        return create_ast_leaf_number("NUMBER", (int)r, NULL, NULL);
    }
    char buf[64]; snprintf(buf, sizeof(buf), "%g", r);
    return create_ast_leaf("FLOAT", 0, buf, NULL);
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
    /* v0.0.13 (perf) Invalidate columnar mirror cache if this object is in a
     * cached list. */
    if (obj && obj->owning_list) te_colcache_invalidate((ASTNode*)obj->owning_list);
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
    else if (strcmp(value_node->type, "ADD") == 0 || strcmp(value_node->type, "SUB") == 0 || strcmp(value_node->type, "MUL") == 0 || strcmp(value_node->type, "DIV") == 0 || strcmp(value_node->type, "MOD") == 0 || strcmp(value_node->type, "NEG") == 0 || strcmp(value_node->type, "BIT_AND") == 0 || strcmp(value_node->type, "BIT_OR") == 0 || strcmp(value_node->type, "BIT_XOR") == 0 || strcmp(value_node->type, "BIT_NOT") == 0 || strcmp(value_node->type, "SHL") == 0 || strcmp(value_node->type, "SHR") == 0 || strcmp(value_node->type, "IN") == 0) {
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
    if (arg->type && strcmp(arg->type, "BOOL") == 0) {
        const char *s = arg->value ? "true" : "false";
        dbg_printf("%s", s);
        append_to_stdout(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "IDENTIFIER") == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, "NULL") == 0) { dbg_printf("null"); append_to_stdout("null"); return; }
        if (_v && _v->type && strcmp(_v->type, "BOOL") == 0) {
            const char *s = _v->value.int_value ? "true" : "false";
            dbg_printf("%s", s); append_to_stdout(s); return;
        }
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
        /* Fase 1a: arr.length / map.length / str.length */
        if (a && a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) { dbg_printf("%d", list_length(list)); return; }
            ASTNode *map = resolve_to_map(o);
            if (map) { dbg_printf("%d", map_length(map)); return; }
            /* str.length — mayo 2026 */
            if (o && o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    dbg_printf("%zu", sv->value.string_value ? strlen(sv->value.string_value) : 0);
                    return;
                }
            }
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
    if (arg->type && strcmp(arg->type, "BOOL") == 0) {
        const char *s = arg->value ? "true" : "false";
        dbg_printf("%s\n", s);
        append_to_stdout(s); append_to_stdout("\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "IDENTIFIER") == 0) {
        Variable *_v = find_variable(arg->id);
        if (_v && _v->type && strcmp(_v->type, "NULL") == 0) { dbg_printf("null\n"); append_to_stdout("null\n"); return; }
        if (_v && _v->type && strcmp(_v->type, "BOOL") == 0) {
            const char *s = _v->value.int_value ? "true" : "false";
            dbg_printf("%s\n", s); append_to_stdout(s); append_to_stdout("\n"); return;
        }
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
        /* Fase 1a: arr.length / map.length / str.length */
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
            /* str.length — mayo 2026 */
            if (o && o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    size_t n = sv->value.string_value ? strlen(sv->value.string_value) : 0;
                    dbg_printf("%zu\n", n);
                    char tmp[32]; snprintf(tmp, 32, "%zu\n", n);
                    append_to_stdout(tmp);
                    return;
                }
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
            /* m["k"].attr — MAP-indexed object attr access (toMap result). */
            ASTNode *map2 = resolve_to_map(o->left);
            if (map2 && o->right) {
                const char *key2 = NULL;
                if (nk_of(o->right) == NK_STRING) key2 = o->right->str_value;
                else if (nk_of(o->right) == NK_IDENTIFIER || nk_of(o->right) == NK_ID) {
                    Variable *kv = find_variable(o->right->id);
                    if (kv && kv->vtype == VAL_STRING) key2 = kv->value.string_value;
                }
                if (key2) {
                    ASTNode *pair = map_find_pair(map2, key2);
                    ASTNode *val  = pair ? pair->left : NULL;
                    ObjectNode *iobj = NULL;
                    if (val && val->type && strcmp(val->type, "OBJECT") == 0) {
                        if (val->extra) iobj = (ObjectNode*)val->extra;
                        else iobj = (ObjectNode*)(intptr_t)val->value;
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
                        dbg_printf("Error: attribute '%s' not found in mapped object.\n", a->id);
                        return;
                    }
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
