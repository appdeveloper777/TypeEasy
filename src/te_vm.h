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
} TeVM;

/* VM actual. Todo el intérprete accede al estado vía `g_vm.campo`, que expande a la VM
 * apuntada por te_vm_cur (nunca NULL: arranca apuntando a la VM principal estática). */
extern TeVM *te_vm_cur;
#define g_vm (*te_vm_cur)

TeVM *te_vm_create(void);                 /* VM nueva, vacía (calloc). NULL si no hay memoria */
void  te_vm_destroy(TeVM *vm);            /* libera la VM (no debe ser la actual) */
TeVM *te_vm_set_current(TeVM *vm);        /* conmuta la VM actual; devuelve la anterior */
TeVM *te_vm_main(void);                   /* la VM principal (estática) */
int   te_vm_selftest(void);               /* 0 = ok; imprime JSON con el resultado */

#endif /* TE_VM_H */
