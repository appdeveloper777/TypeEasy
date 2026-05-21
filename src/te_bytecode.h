/* te_bytecode.h — TypeEasy bytecode mini-VM, profile/trace and JIT engine.
 *
 * Extracted from ast.c (Fase 2 modularization). All BC instructions, the
 * call-site structures used by inlined method calls, and the public API
 * consumed by the AST walker live here. Internal helpers (compile, exec,
 * tracing, JIT emitters) stay file-static in te_bytecode.c.
 *
 * The walker uses only:
 *   bc_invalidate_all, bc_get_or_compile, bc_get_or_compile_stmt,
 *   bc_get_or_compile_method, bc_exec, plus the BCInfo type and the
 *   BC_NOT_COMPILABLE sentinel stored in ASTNode.bc / MethodNode.bc_body.
 */
#ifndef TE_BYTECODE_H
#define TE_BYTECODE_H

#include <stdint.h>

#include "ast.h"   /* ASTNode, MethodNode, ClassNode, ObjectNode, Variable, NodeKind */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opcodes ---------------------------------------------------------- */
typedef enum {
    BC_HALT = 0,
    BC_LOAD_CONST,
    BC_LOAD_VAR,        /* numeric var (INT/FLOAT) — pointer cached at compile time */
    BC_ADD, BC_SUB, BC_MUL, BC_DIV,
    BC_LT, BC_GT, BC_LE, BC_GE, BC_EQ, BC_NEQ,
    BC_AND, BC_OR, BC_NOT,
    BC_NEG,             /* unary minus (compiled from SUB(0, x)) — currently unused */
    /* Phase G: integer-only ops. Operate on long long via cast. */
    BC_MOD,
    BC_BAND, BC_BOR, BC_BXOR, BC_BNOT,
    BC_SHL, BC_SHR,
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
    /* Ola 14d: arr[idx].attr fastpath. operand=ListItemAttrSite*. */
    BC_LIST_ITEM_ATTR
} BCOp;

/* Ola 5: precomputed call site for `obj.method(args)`. */
typedef struct MethodCallSite {
    struct Variable   *obj_var;       /* var holding ObjectNode* */
    struct MethodNode *method;        /* target method (informational) */
    void              *body_bc;       /* BCInfo* (compiled return expression) */
    int                n_params;      /* number of params */
    struct Variable   *param_vars[8]; /* cached param Variable*s */
} MethodCallSite;

/* Ola 14d: precomputed call site for `list_var[idx].attr` (numeric attr). */
typedef struct ListItemAttrSite {
    struct Variable  *list_var;
    struct ClassNode *expected_class;
    int               attr_slot;
} ListItemAttrSite;

typedef struct {
    uint8_t op;
    union {
        Variable *var;
        double    constant;
        int32_t   offset;
        int32_t   slot;
        MethodCallSite *site;
        ListItemAttrSite *lia_site;
    } u;
} Instr;

typedef struct BCInfo {
    Instr *code;
    int    len;
} BCInfo;

#define BC_NOT_COMPILABLE ((void*)0x1)

/* ---- Public API (walker -> bytecode) ---------------------------------- */

/* Drop all cached bytecode whose Instrs hold stale Variable* pointers.
 * Called by runtime_reset_vars_to_initial_state between API requests. */
void bc_invalidate_all(void);

/* Lazily compile `node` (a numeric expression) and return its BCInfo,
 * or BC_NOT_COMPILABLE if the node can't be compiled. Caller should
 * check the returned pointer against BC_NOT_COMPILABLE before deref. */
BCInfo *bc_get_or_compile(ASTNode *node);

/* Same as bc_get_or_compile but compiles `node` as a full statement
 * (assign / while / if / block) rather than as an expression. */
BCInfo *bc_get_or_compile_stmt(ASTNode *node);

/* Compile `m`'s body as bytecode bound to class `cls`. Returns
 * BC_NOT_COMPILABLE if incompatible. */
BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls);

/* Execute compiled bytecode and return the numeric result on top of
 * the stack (0.0 for statement programs that end on HALT). */
double bc_exec(Instr *code);

/* Walker bridge: current `this` while executing a method body under
 * bc_exec(). The AST walker must save/swap/restore around bc_exec
 * calls that enter a method (mirrors BC_SET_THIS/BC_RESTORE_THIS). */
extern ObjectNode *g_bc_this;

#ifdef __cplusplus
}
#endif

#endif /* TE_BYTECODE_H */
