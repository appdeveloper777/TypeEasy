/* te_vm.h — Estado central del intérprete agrupado en UNA struct (Fase 3 del plan de deuda).
 * Paso A: TODO el estado por-VM (vars[], flags de control, callstack, recovery de fatales,
 *         captura de stdout, índice de símbolos, frames) vive en TeVM; ningún global suelto.
 * Paso B: `g_vm` es una MACRO sobre la VM actual (`*te_vm_cur`). Se pueden crear varias VMs
 *         (te_vm_create) y conmutarlas (te_vm_set_current); el código existente que escribe
 *         `g_vm.x` sigue compilando sin cambios. `--selftest-vm` verifica el aislamiento.
 * Paso C (pendiente, no bloqueante): hilos con una VM por hilo (te_vm_cur thread-local) y
 *         parser/lexer reentrantes; hoy la concurrencia sigue siendo por prefork (--workers).
 * Regla: NO agregar globales nuevos al intérprete; el estado nuevo va aquí. */
#ifndef TE_VM_H
#define TE_VM_H

#include <setjmp.h>
#include <stdint.h>
#include "ast.h"

#define TE_CALLSTACK_MAX 128
#define TE_SYM_CAP 16384  /* MAX_VARS=4096 -> cap 16384 mantiene load < 0.25 */
/* Slot del índice nombre->slot de vars[] (FNV-1a). key es alias del id interned. */
typedef struct TESymSlot { uint64_t hash; const char *key; int idx; } TESymSlot;
/* Error capturado por --syntax-check (yyerror / pases semánticos). */
typedef struct TeErr { int line; int file_id; char msg[256]; char near_tok[128]; } TeErr;
/* Par clave/valor de la request HTTP (query/headers/params/resp headers). */
typedef struct TeKV { char *k; char *v; struct TeKV *next; } TeKV;
/* Entrada del profiler por función (--profile / TYPEEASY_PROF_FN). */
typedef struct TeProfEntry { const char *name; long long calls, incl_ns, self_ns; } TeProfEntry;
/* Entrada de la tabla de strings interned. */
typedef struct InternEntry { char *str; size_t len; uint32_t hash; struct InternEntry *next; } InternEntry;
#define TE_SRC_FILES_MAX 512
#define TE_PROF_MAX 1024
#define INTERN_BUCKETS 1024
struct TeFrame;

typedef struct TeVM {
    Variable vars[MAX_VARS];   /* slots de variables (globales + locales por request) */
    int var_count;
    int return_flag;           /* un return está en vuelo */
    int throw_flag;            /* un throw está en vuelo */
    int break_flag;
    int continue_flag;
    ASTNode *return_node;      /* valor del return en vuelo */
    /* --- Fase 3 (completar A): resto del estado por VM --- */
    jmp_buf *runtime_recovery;             /* punto de recuperación de fatales (--api) */
    int call_depth, max_call_depth;
    int current_exec_line, current_exec_file;
    int runtime_error_line, runtime_error_file;
    char runtime_error_msg[256];
    const char *callstack[TE_CALLSTACK_MAX];
    int callstack_n;
    char *stdout_buffer; size_t stdout_size; int suppress_stdout;   /* captura de stdout (API) */
    int initial_var_count;                 /* globales del script; lo que sigue es por request */
    TESymSlot sym_slots[TE_SYM_CAP]; int sym_init;
    struct TeFrame *frame_top;             /* frames de fn activos */
    /* --- Fase 3C: registros del programa que antes eran globales de proceso --- */
    ClassNode **classes; int classes_cap; int class_count;   /* clases declaradas (parser + runtime) */
    MethodNode *global_methods;                              /* endpoints/métodos globales (lista enlazada) */
    Variable ret_var; int ret_var_active;                    /* valor de retorno (__ret_var) */
    /* --- acelerador bytecode (te_bytecode.c): sin globales propios --- */
    ASTNode **bc_nodes; int bc_nodes_n, bc_nodes_cap;        /* nodos con BCInfo cacheado (invalidar entre requests) */
    MethodNode **bc_methods; int bc_methods_n, bc_methods_cap;
    ObjectNode *bc_this;                                     /* `this` del cuerpo de método en ejecución */
    ObjectNode *bc_this_stack[16]; int bc_this_sp;           /* llamadas inline anidadas */
    /* --- posición del lexer (antes yylineno/g_vm.lex_file_id/g_decl_stmt_line globales) --- */
    int lex_line;              /* línea del último token (la actualiza el wrapper yylex) */
    int lex_file_id;           /* archivo que se está lexeando (0 = principal) */
    int decl_stmt_line;        /* línea del keyword que abre una declaración (let/var/...) */
    void *parse_scanner;       /* scanner activo (para liberar si un fatal abortó el parse) */
    int debug_mode;            /* --debug (antes g_debug_mode en parser.y) */
    int quiet_parse_errors;    /* silenciar yyerror (fuzzer) */
    /* --- debugger (debugger.c): flag caliente + sesión con alloc perezoso --- */
    int debug_enabled;                 /* 1 = hooks del debugger activos */
    const char *debug_source_file;     /* ruta visible al usuario (argv[1]) */
    struct TeDebugger *dbg;            /* estado de la sesión (NULL hasta el primer uso) */
    struct TeServer *srv;              /* servidor --api embebido (typeeasy_api_server.c), alloc perezoso */
    struct TeEvLoop *ev;               /* fibers + pool de IO async (te_evloop.c), alloc perezoso */
    /* --- request en curso (typeeasy_api.c): objetos/ASTs que se liberan al terminar --- */
    ObjectNode **req_owned_objects; int req_owned_count, req_owned_cap;
    ASTNode **req_owned_ast; int req_owned_ast_count, req_owned_ast_cap;
    char *param_validation_error;
    /* --- --test / --syntax-check (typeeasy_main.c) --- */
    int test_failed, test_assertions;
    int capture_errors;                 /* yyerror acumula en syntax_errors[] en vez de imprimir */
    TeErr syntax_errors[64]; int syntax_error_count;
    /* --- flags de la capa DB (db_params.c), configurables por env --- */
    int db_empty_as_null, db_strict_errors, db_envelope;
    /* --- hooks de evaluación para te_json.c (los registra interpret_ast) --- */
    void (*json_eval_call_func)(ASTNode *);
    void (*json_eval_call_method)(ASTNode *);
    /* --- estado menor de módulos --- */
    int db_cleanup_count;              /* te_stdlib.c */
    int http_last_status;              /* te_http.c */
    char mssql_last_err[1024], mssql_last_msg[1024];   /* sqlserver_bridge.c */
    struct TeAsyncState *async;        /* te_async.c: pool de tasks (alloc perezoso) */
    struct TeBuiltins *builtins;       /* te_builtins.c: registro de nativas (alloc perezoso) */
    struct TeBridgeState *bridge;      /* te_bridge.c: procesos hijos (alloc perezoso) */
    struct RuntimeHost *agent;         /* servidor_agent.c */
    struct mg_connection *agent_conn;
    struct TeLinqThen *linq_then;      /* te_linq_ops.c: contexto orderBy->thenBy (alloc perezoso) */
    struct TeCsv *csv;                 /* te_csv.c: arenas keepalive, script_dir, lazy loads (alloc perezoso) */
    /* --- programa (ast.c): tabla de archivos fuente, profiler, intern, bridges --- */
    char *src_files[TE_SRC_FILES_MAX]; int src_file_count;
    const char *script_path;           /* ruta del script en ejecución (mensajes de error) */
    int api_mode;                      /* 1 = corriendo bajo --api */
    BridgeHandlers bridge_handlers;
    int db_request_phase;
    int profile_enabled;               /* -1 = aún no leído de env */
    TeProfEntry prof[TE_PROF_MAX]; int prof_n;
    long long prof_child_ns[TE_CALLSTACK_MAX], prof_start_ns[TE_CALLSTACK_MAX];
    InternEntry *intern_table[INTERN_BUCKETS]; int intern_init, intern_enabled;
    int te_request_active;             /* >0 dentro de un request del servidor */
    /* --- request/response HTTP en curso (typeeasy_http_* setters) --- */
    char *req_method, *req_path, *req_body; size_t req_body_len;
    TeKV *req_query, *req_headers, *req_params;
    int resp_status; TeKV *resp_headers;
    char *resp_body; size_t resp_body_len; char *resp_content_type;   /* canal binario (xlsx/pdf) */
    int response_is_raw_text;
    char *current_claims;              /* JWT claims del request */
} TeVM;

/* Valores por defecto NO-cero de una VM nueva (VM principal estática y te_vm_create). */
#define TE_VM_DEFAULTS .profile_enabled = -1, .intern_enabled = 1, .resp_status = 200

/* VM actual DEL HILO. Todo el intérprete accede al estado vía `g_vm.campo`, que expande
 * a la VM apuntada por te_vm_cur (thread-local; nunca NULL: cada hilo arranca apuntando
 * a la VM principal estática). */
extern __thread TeVM *te_vm_cur;
#define g_vm (*te_vm_cur)

TeVM *te_vm_create(void);                 /* VM nueva, vacía (calloc). NULL si no hay memoria */
void  te_vm_destroy(TeVM *vm);            /* libera la VM (no debe ser la actual) */
TeVM *te_vm_set_current(TeVM *vm);        /* conmuta la VM actual; devuelve la anterior */
TeVM *te_vm_main(void);                   /* la VM principal (estática) */
int   te_vm_selftest(void);               /* 0 = ok; imprime JSON con el resultado */

#endif /* TE_VM_H */
