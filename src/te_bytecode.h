/* te_bytecode.h — Acelerador de bytecode del intérprete TypeEasy.
 *
 * NO es un segundo motor. El walker (ast.c) es la única definición de la
 * semántica del lenguaje; el bytecode solo elimina el despacho del árbol para
 * el subconjunto numérico (literales INT/FLOAT, variables numéricas, aritmética,
 * comparaciones, this.attr / lista[i].attr numéricos, asignaciones y bucles
 * while/if/for cuyo cuerpo es puramente numérico). Toda operación con
 * semántica propia (INT vs FLOAT al almacenar, división/módulo por cero,
 * lectura de variables, aritmética exacta int64) se delega a las primitivas
 * compartidas del walker (te_num_binop/te_num_unop/te_num_store,
 * te_var_as_double, te_walk_expression) y cualquier nodo que no pueda
 * delegarse NO se compila (vuelve al walker).
 *
 * Contrato verificado por tests/regress/run_bc_diff.py (stdout byte-idéntico
 * con y sin TYPEEASY_NO_BC=1 sobre toda la suite de lenguaje).
 */
#ifndef TE_BYTECODE_H
#define TE_BYTECODE_H

#include <stdint.h>

#include "ast.h"   /* ASTNode, MethodNode, ClassNode, ObjectNode, Variable, NodeKind */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BC_HALT = 0,
    BC_LOAD_CONST,
    BC_LOAD_VAR,        /* numeric var (INT/FLOAT) — pointer cached at compile time */
    BC_ADD, BC_SUB, BC_MUL, BC_DIV,
    BC_LT, BC_GT, BC_LE, BC_GE, BC_EQ, BC_NEQ,
    BC_AND, BC_OR, BC_NOT,
    BC_NEG,
    BC_MOD,
    BC_BAND, BC_BOR, BC_BXOR, BC_BNOT,
    BC_SHL, BC_SHR,
    BC_STORE_VAR,       /* pop top, te_num_store() into Variable* */
    BC_JUMP,            /* ip += offset (relative, signed) */
    BC_JUMP_IF_FALSE,   /* pop; if 0, ip += offset */
    BC_POP,
    BC_LOAD_THIS_ATTR,  /* operand=slot in g_bc_this->attributes */
    BC_SET_THIS,        /* operand=Variable* holding the object; pushes saved this */
    BC_RESTORE_THIS,
    BC_LIST_ITEM_ATTR,  /* arr[idx].attr fastpath. operand=ListItemAttrSite* */
    /* Espejo de te_eval_i64 (walker): sección entera exacta de una asignación.
     * Si algo no es entero (var FLOAT, división inexacta, módulo por cero) se
     * salta a la sección double (offset relativo en BC_I64_BEGIN), igual que el
     * walker cae de te_eval_i64 a evaluate_expression. */
    BC_I64_BEGIN,       /* u.offset = salto a la sección double si falla */
    BC_I64_CONST,       /* u.ival */
    BC_I64_VAR,         /* u.var: falla si no es VAL_INT (o tipo NULL) */
    BC_I64_ADD, BC_I64_SUB, BC_I64_MUL, BC_I64_MOD, BC_I64_DIV, BC_I64_NEG,
    BC_I64_LT, BC_I64_GT, BC_I64_LE, BC_I64_GE, BC_I64_EQ, BC_I64_NEQ,
    BC_I64_STORE        /* u.var: guarda INT exacto; el instr siguiente salta la sección double */
} BCOp;

/* Precomputed call site for `list_var[idx].attr` (numeric attr). On a class
 * mismatch at runtime the value is computed by the walker for `node`. */
typedef struct ListItemAttrSite {
    struct Variable  *list_var;
    struct ClassNode *expected_class;
    int               attr_slot;
    struct ASTNode   *node;           /* the ACCESS_ATTR node (walker fallback) */
} ListItemAttrSite;

typedef struct {
    uint8_t op;
    union {
        Variable *var;
        double    constant;
        long long ival;
        int32_t   offset;
        int32_t   slot;
        ListItemAttrSite *lia_site;
    } u;
} Instr;

/* Guard de entrada: las Variable* cacheadas en las Instrs deben seguir siendo
 * del tipo que tenían al compilar; si no, el programa NO se ejecuta y el
 * walker toma el nodo (deopt). kind 0 = numérica, 1 = objeto de clase `cls`,
 * 2 = LIST. */
typedef struct BCGuard {
    Variable *var;
    int kind;
    struct ClassNode *cls;
    const char *id;          /* Fase F: identidad del slot (id del nodo AST); un slot reciclado con otro nombre no pasa */
} BCGuard;

typedef struct BCInfo {
    Instr   *code;
    int      len;
    BCGuard *guards;
    int      n_guards;
    int      this_slots[8];   /* slots de this.attr leídos por un cuerpo de método */
    int      n_this_slots;
} BCInfo;

#define BC_NOT_COMPILABLE ((void*)0x1)

/* ---- Public API (walker -> bytecode) ---------------------------------- */

/* Drop all cached bytecode whose Instrs hold stale Variable* pointers.
 * Called by runtime_reset_vars_to_initial_state between API requests. */
void bc_invalidate_all(void);

/* Lazily compile `node` (numeric expression) → BCInfo*, or NULL. */
BCInfo *bc_get_or_compile(ASTNode *node);

/* Same, compiling `node` as a full statement (while / if / for). */
BCInfo *bc_get_or_compile_stmt(ASTNode *node);

/* Compile `m`'s body (`{ return <numeric expr>; }`) bound to class `cls`. */
/* in_method_frame=1: llamado desde el cuerpo del método (params ya ligados en este frame);
 * 0: inline desde el llamador — un slot existente con el nombre de un param es AJENO (no compila). */
BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls, int in_method_frame);

/* Run a compiled program. Returns 1 and sets *result (top of stack, 0.0 for
 * statements) — or 0 if the entry guard failed (deopt: caller must run the
 * walker on the same node; no side effect happened). */
int bc_run(BCInfo *info, double *result);

#ifdef __cplusplus
}
#endif

#endif /* TE_BYTECODE_H */
