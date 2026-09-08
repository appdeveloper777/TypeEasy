/* ast_internal.h — API INTERNA entre los módulos del intérprete (ast.c, te_print.c,
 * te_interp_flow.c, ...). NO es API pública: no incluir desde fuera de src/.
 * Fase 2 del plan de deuda: lo que era `static` en ast.c y ahora comparten varios
 * archivos vive aquí. Al partir más módulos, agregar sus prototipos en su sección. */
#ifndef TE_AST_INTERNAL_H
#define TE_AST_INTERNAL_H

#include "ast.h"
#include "te_bytecode.h"
#include "debugger.h"

/* Estado global del intérprete definido en ast.c (la Fase 3 lo agrupa en TeVM). */
extern int var_count;
extern int return_flag;
extern int throw_flag;

/* Resolver perezoso de NodeKind (hot path de dispatch). */
static inline NodeKind nk_of(ASTNode *n) {
    if (!n) return NK_UNKNOWN;
    if (n->kind != NK_UNKNOWN) return n->kind;
    if (n->type) n->kind = nk_from_str(n->type);
    return n->kind;
}

/* Estado de control de flujo (break/continue) compartido por el intérprete. */
extern int break_flag;
extern int continue_flag;

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
void interpret_for_c(ASTNode *node);
void interpret_for(ASTNode *node);
void interpret_if(ASTNode *node);
void interpret_for_in(ASTNode *node);
int dbg_printf(const char *fmt, ...);
void interpret_call_func(ASTNode *node);
void interpret_call_method(ASTNode *node);
const char* te_map_key_coerce(ASTNode *keyNode, char *buf, size_t cap);
char* te_map_node_to_string(ASTNode *mapNode);
int te_expr_is_null(ASTNode *l);
void te_scope_unwind_to(int target);
void interpret_statement_list(ASTNode *node);

#endif /* TE_AST_INTERNAL_H */
