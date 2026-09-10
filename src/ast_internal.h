/* ast_internal.h — API INTERNA entre los módulos del intérprete (ast.c, te_print.c,
 * te_interp_flow.c, ...). NO es API pública: no incluir desde fuera de src/.
 * Fase 2 del plan de deuda: lo que era `static` en ast.c y ahora comparten varios
 * archivos vive aquí. Al partir más módulos, agregar sus prototipos en su sección. */
#ifndef TE_AST_INTERNAL_H
#define TE_AST_INTERNAL_H

#include "ast.h"
#include "te_bytecode.h"
#include "debugger.h"
#include "te_vm.h"
#include "te_value.h"

/* Estado global del intérprete definido en ast.c (la Fase 3 lo agrupa en TeVM). */

/* Resolver perezoso de NodeKind (hot path de dispatch). */
static inline NodeKind nk_of(ASTNode *n) {
    if (!n) return NK_UNKNOWN;
    if (n->kind != NK_UNKNOWN) return n->kind;
    if (n->type) n->kind = nk_from_str(n->type);
    return n->kind;
}

/* Estado de control de flujo (break/continue) compartido por el intérprete. */

/* Funciones compartidas (antes static en ast.c). */
/* Helpers no-static de ast.c que no estaban en ast.h (implicit-declaration = puntero
 * truncado en LLP64; por eso van declarados aquí). */
char* expand_interp_string(const char *raw);
ASTNode* resolve_to_map(ASTNode *node);
ASTNode* resolve_to_list(ASTNode *node);
int list_length(ASTNode *list);
ASTNode* list_get_item(ASTNode *list, int idx);
int map_length(ASTNode *map);
ASTNode* map_find_pair(ASTNode *map, const char *key);
void append_to_stdout(const char *str);
void interpret_println(ASTNode *node);
void interpret_print(ASTNode *node);
char *te_map_field_display(Variable *v, const char *key);
void te_print_list_node(ASTNode *listNode, int nl);
void interpret_for_c(TeVM *vm, ASTNode *node);
void interpret_for(TeVM *vm, ASTNode *node);
void interpret_if(TeVM *vm, ASTNode *node);
void interpret_for_in(TeVM *vm, ASTNode *node);
int dbg_printf(const char *fmt, ...);
void interpret_call_func(ASTNode *node);
void interpret_call_method(ASTNode *node);
const char* te_map_key_coerce(ASTNode *keyNode, char *buf, size_t cap);
char* te_map_node_to_string(ASTNode *mapNode);
char* te_list_node_to_string(ASTNode *listNode);
int te_expr_is_null(ASTNode *l);
void te_scope_unwind_to(int target);
void interpret_statement_list(ASTNode *node);

/* te_interp_decl.c (Fase 2 cut 3). */
extern char *throw_message;          /* mensaje del throw en vuelo (ast.c) */
int  te_stderr_is_tty(void);
#define TE_ERR_RED   (te_stderr_is_tty() ? "\033[31m" : "")
#define TE_ERR_RESET (te_stderr_is_tty() ? "\033[0m"  : "")
ASTNode *te_csv_runtime_load(ASTNode *placeholder);
void declare_variable(char *id, ASTNode *value, int is_const);
void te_colcache_invalidate(ASTNode *list_head);
void te_runtime_fatalf(const char *fmt, ...);
void interpret_assign(TeVM *vm, ASTNode *node);
void interpret_assign_attr(TeVM *vm, ASTNode *node);
void interpret_var_decl(TeVM *vm, ASTNode *node);
char* double_to_string(double x);
ASTNode *te_capture_lambda(ASTNode *lam);
Variable *te_decl_slot(const char *id);
ASTNode* resolve_access_item(ASTNode *node);
void te_sym_insert(const char *id, int idx);
void te_value_to_variable(Variable *dst, ASTNode *value);
void te_list_literal_construct_objects(ASTNode *value);   /* construye los `new X()` de un literal LIST (ast.c) */
ObjectNode* clone_object(ObjectNode *original);
Variable *find_variable_for(char *id);
void te_set_ret_string(const char *s);   /* __ret__ = STRING (ast.c) */
void te_set_ret_int(int n);

/* Fase F: frames de llamada (lambdas, métodos, constructores). El frame es DUEÑO de los slots
 * creados durante la llamada (se liberan al pop). Un nombre que ya existía fuera NUNCA se pisa:
 * la llamada appendea un slot nuevo que lo sombrea y anota (nombre, idx previo) para restaurar
 * la entrada de la symtab al pop. También salva/restaura el registro `this`. */
typedef struct { const char *name; int prev_idx; } TeSymRestore;
typedef struct TeClosureRef { ASTNode *cl; struct TeClosureRef *next; } TeClosureRef;
typedef struct TeFrame {
    TeSymRestore *sh;
    int n, cap;
    int base;                 /* g_vm.var_count al entrar */
    ObjectNode *saved_this; int saved_this_active;
    TeClosureRef *closures;   /* closures que capturaron slots de ESTE frame (se cierran al pop) */
    struct TeFrame *prev;
} TeFrame;
void te_frame_push(TeFrame *f);
void te_frame_pop(TeFrame *f);
void te_set_this(ObjectNode *obj);
void te_call_ctor(ObjectNode *obj, ASTNode *args);   /* frame + bind args + __constructor + limpia return */
int  te_sym_lookup(const char *id);                   /* idx en vars[] o -1 (hash O(1)) */
/* Resuelve el slot de un IDENTIFIER validando node->cached_var contra la symtab (identidad real:
 * el slot VISIBLE con ese nombre, no cualquier slot que se llame igual). */
Variable *te_resolve_cached(ASTNode *n);

/* Fase F: closures por REFERENCIA (upvalues). te_closure_make: instancia de un lambda que
 * captura las variables libres que viven en frames activos (abiertas mientras el frame vive,
 * cerradas —copiadas al env— en te_frame_pop). Devuelve el template si no hay nada que capturar. */
typedef struct TeClosureEnv {
    int n;
    char **names;
    Variable **open;          /* slot vivo en vars[] o NULL si ya se cerró */
    Variable *cells;          /* valor cerrado (owned) */
    ObjectNode *this_obj; int has_this;
} TeClosureEnv;
ASTNode *te_closure_make(ASTNode *lam);
void te_closure_bind_in(ASTNode *cl);                 /* tras te_frame_push + params: liga upvalues + this */
void te_closure_snapshot(ASTNode *cl, Variable *tmp); /* antes del pop: lee los upvalues ligados */
void te_closure_write_back(ASTNode *cl, Variable *tmp);/* tras el pop: escribe en slot abierto / cell */
void te_frame_close_upvalues(TeFrame *f);            /* llamado por te_frame_pop */
void te_closures_free_request(void);                   /* libera las closures del request (typeeasy_api.c) */

#endif /* TE_AST_INTERNAL_H */
