/* te_bytecode.c — TypeEasy bytecode mini-VM, profiler, tracer, and JIT.
 *
 * Extracted from ast.c (Fase 2 modularization). Self-contained block:
 * the only walker symbols it touches are find_variable / find_variable_for
 * (public in ast.h) and nk_from_str (also public). All bytecode globals,
 * trace tables, JIT slab, and emitters are file-static here.
 */

#include "te_bytecode.h"
#include "ast.h"
#include "strvars.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bridge to walker (ast.c) symbols not exported via ast.h. The struct
 * layout MUST stay in sync with the canonical typedef in ast.c. */
typedef struct TEListIdx {
    int len;
    int cap;
    ASTNode **items;
} TEListIdx;

/* MAX_VARS comes from ast.h — keep in sync with ast.c's vars[] size. */
extern Variable vars[MAX_VARS];
extern int var_count;
extern int is_string_type(ASTNode *node);

#ifdef __GNUC__
#  define BC_UNUSED __attribute__((unused))
#else
#  define BC_UNUSED
#endif

/* JIT availability: x86_64 on Linux/macOS (mmap PROT_EXEC) and Windows
 * (VirtualAlloc PAGE_EXECUTE_READWRITE). The trace JIT emits a self-contained
 * leaf function that only touches rax/rdx/xmm0 (volatile in both SysV and
 * Win64 ABIs) and rbx/r12-r15 (callee-saved in both, push/pop in pro/epilogue)
 * and never calls back into C, so the same machine code is ABI-correct on
 * Windows without any register renaming.
 *
 * GATE (TE_ENABLE_JIT): the experimental trace JIT is COMPILED OUT by default.
 * It must be explicitly enabled at build time with -DTE_ENABLE_JIT=1. Reason:
 * the JIT path emits W+X executable memory (VirtualAlloc PAGE_EXECUTE_READWRITE
 * / mmap PROT_EXEC), a byte pattern that Windows Defender / SmartScreen flag as
 * malware heuristics in release binaries. Since the JIT is off at runtime
 * unless TYPEEASY_JIT=1, excluding it from release builds changes NOTHING for
 * normal users (same interpreter, same performance) while removing the
 * antivirus trigger from the shipped .exe. Dev builds can still opt in via
 * -DTE_ENABLE_JIT=1. */
#if !defined(TE_ENABLE_JIT)
/* JIT disabled (default): no executable-memory allocation in the binary. */
#  define TE_JIT_AVAILABLE 0
#elif defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__))
#  define TE_JIT_AVAILABLE 1
#  include <sys/mman.h>
#  include <unistd.h>
#elif (defined(__x86_64__) || defined(_M_X64)) && defined(_WIN32)
#  define TE_JIT_AVAILABLE 1
#  define TE_JIT_WIN32 1
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  define TE_JIT_AVAILABLE 0
#endif

/* Local copy of the ast.c static-inline nk_of helper — pure, no globals. */
static inline NodeKind nk_of(ASTNode *n) {
    if (!n) return NK_UNKNOWN;
    if (n->kind != NK_UNKNOWN) return n->kind;
    if (n->type) n->kind = nk_from_str(n->type);
    return n->kind;
}

/* Track all AST nodes / methods that have a cached BCInfo so that
 * runtime_reset_vars_to_initial_state() can invalidate them. The cached
 * Instrs hold raw `Variable*` pointers into vars[], which become stale
 * when slots are recycled across API requests. */
#define BC_REG_MAX_NODES 65536
#define BC_REG_MAX_METHODS 4096
static struct ASTNode *g_bc_reg_nodes[BC_REG_MAX_NODES];
static int g_bc_reg_node_count = 0;
static struct MethodNode *g_bc_reg_methods[BC_REG_MAX_METHODS];
static int g_bc_reg_method_count = 0;

static inline void bc_register_node(struct ASTNode *n) {
    if (g_bc_reg_node_count < BC_REG_MAX_NODES) g_bc_reg_nodes[g_bc_reg_node_count++] = n;
}
static inline void bc_register_method(struct MethodNode *m) {
    if (g_bc_reg_method_count < BC_REG_MAX_METHODS) g_bc_reg_methods[g_bc_reg_method_count++] = m;
}

/* Called from runtime_reset_vars_to_initial_state (forward-declared
 * extern) to drop all cached bytecode whose Instrs hold stale Variable*
 * pointers into recycled slots.
 *
 * NOTE: we do NOT free the BCInfo / Instrs blocks. Some code paths
 * (method bodies, inherited methods sharing AST, recursive compile)
 * may have multiple registrations of the same pointer; double-free
 * causes hang/crash. The leak is bounded by the program's compiled
 * code size and only happens once per script reload boundary, not
 * per request — totally acceptable. */
void bc_invalidate_all(void) {
    for (int bi = 0; bi < g_bc_reg_node_count; bi++) {
        ASTNode *n = g_bc_reg_nodes[bi];
        if (n && n->bc != BC_NOT_COMPILABLE) {
            n->bc = NULL;
        }
    }
    g_bc_reg_node_count = 0;
    for (int bi = 0; bi < g_bc_reg_method_count; bi++) {
        MethodNode *mm = g_bc_reg_methods[bi];
        if (mm && mm->bc_body != BC_NOT_COMPILABLE) {
            mm->bc_body = NULL;
        }
    }
    g_bc_reg_method_count = 0;
}

/* Ola 4: per-call current `this` for BC_LOAD_THIS_ATTR. Set by
 * interpret_call_method before invoking bc_exec on a compiled method body. */
ObjectNode *g_bc_this = NULL;
/* Ola 5b: small save/restore stack for inline-expanded method calls. */
static ObjectNode *g_bc_this_stack[16];
static int         g_bc_this_sp = 0;
/* Ola 4: class context for compiling method bodies — used to resolve
 * `this.attr` to a fixed slot index at compile time. */
static ClassNode *g_bc_compile_class = NULL;

/* Forward decl */
static int bc_compile(ASTNode *node, Instr *out, int *pos, int max);

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
        if (v && node->id && (!v->id || strcmp(v->id, node->id) != 0)) {
            v = NULL;
            node->cached_var = NULL;
        }
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
    /* Phase G: integer-only ops. Operands compiled via bc_compile (must be
     * numeric); folded if both are LOAD_CONST. Runtime cast a/b to long long. */
    case NK_MOD: case NK_BIT_AND: case NK_BIT_OR: case NK_BIT_XOR:
    case NK_SHL: case NK_SHR: {
        int saved = *pos;
        if (!bc_compile(node->left,  out, pos, max)) return 0;
        if (!bc_compile(node->right, out, pos, max)) return 0;
        if (*pos >= saved + 2 &&
            out[*pos - 2].op == BC_LOAD_CONST &&
            out[*pos - 1].op == BC_LOAD_CONST) {
            long long a = (long long)out[*pos - 2].u.constant;
            long long b = (long long)out[*pos - 1].u.constant;
            long long r = 0;
            int folded = 1;
            switch (k) {
                case NK_MOD:     if (b == 0) folded = 0; else r = a % b; break;
                case NK_BIT_AND: r = a & b; break;
                case NK_BIT_OR:  r = a | b; break;
                case NK_BIT_XOR: r = a ^ b; break;
                case NK_SHL:     r = a << b; break;
                case NK_SHR:     r = a >> b; break;
                default:         folded = 0; break;
            }
            if (folded) {
                *pos -= 2;
                out[*pos].op = BC_LOAD_CONST;
                out[*pos].u.constant = (double)r;
                (*pos)++;
                return 1;
            }
        }
        BCOp op;
        switch (k) {
            case NK_MOD:     op = BC_MOD;  break;
            case NK_BIT_AND: op = BC_BAND; break;
            case NK_BIT_OR:  op = BC_BOR;  break;
            case NK_BIT_XOR: op = BC_BXOR; break;
            case NK_SHL:     op = BC_SHL;  break;
            case NK_SHR:     op = BC_SHR;  break;
            default:         return 0;
        }
        if (*pos >= max) return 0;
        out[*pos].op = op;
        (*pos)++;
        return 1;
    }
    case NK_BIT_NOT: {
        if (!node->left) return 0;
        int saved = *pos;
        if (!bc_compile(node->left, out, pos, max)) return 0;
        if (*pos == saved + 1 && out[saved].op == BC_LOAD_CONST) {
            out[saved].u.constant = (double)~(long long)out[saved].u.constant;
            return 1;
        }
        if (*pos >= max) return 0;
        out[*pos].op = BC_BNOT;
        (*pos)++;
        return 1;
    }
    case NK_NEG: {
        if (!node->left) return 0;
        int saved = *pos;
        if (!bc_compile(node->left, out, pos, max)) return 0;
        if (*pos == saved + 1 && out[saved].op == BC_LOAD_CONST) {
            out[saved].u.constant = -out[saved].u.constant;
            return 1;
        }
        if (*pos >= max) return 0;
        out[*pos].op = BC_NEG;
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
        while (a) { n_args++; a = a->next; } /* gotcha #1: step args via ->next */
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
            a = a->next; /* gotcha #1: step args via ->next */
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
#if defined(TE_JIT_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        size_t ps = si.dwPageSize ? (size_t)si.dwPageSize : 4096;
        g_jit_slab_sz = ps * 16;  /* 64 KiB */
        g_jit_slab = VirtualAlloc(NULL, g_jit_slab_sz,
                                  MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
        if (g_jit_slab == NULL) {
            fprintf(stderr, "[OLA10] VirtualAlloc PAGE_EXECUTE_READWRITE failed; JIT disabled\n");
            g_jit_on = 0;
        } else {
            fprintf(stderr, "[OLA10] JIT slab=%p size=%zu (PAGE_EXECUTE_READWRITE ok)\n",
                    g_jit_slab, g_jit_slab_sz);
        }
#else
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
#endif
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
double bc_exec(Instr *code) {
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
        /* Phase G */
        [BC_MOD]            = &&do_mod,
        [BC_BAND]           = &&do_band,
        [BC_BOR]            = &&do_bor,
        [BC_BXOR]           = &&do_bxor,
        [BC_BNOT]           = &&do_bnot,
        [BC_SHL]            = &&do_shl,
        [BC_SHR]            = &&do_shr,
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
    #define TY_OF(v) (((v) == (double)(long long)(v)) ? T_INT : T_FLOAT)
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
/* Phase G: integer ops. Trace recording is aborted; these ops aren't in IR. */
do_mod: {
    long long b = (long long)stack[sp-1];
    if (b == 0) { printf("Error: modulo by zero.\n"); stack[sp-2] = 0; }
    else        { stack[sp-2] = (double)((long long)stack[sp-2] % b); }
    sp--; TRACE_ABORT(); ip++; DISPATCH();
}
do_band:
    stack[sp-2] = (double)((long long)stack[sp-2] & (long long)stack[sp-1]);
    sp--; TRACE_ABORT(); ip++; DISPATCH();
do_bor:
    stack[sp-2] = (double)((long long)stack[sp-2] | (long long)stack[sp-1]);
    sp--; TRACE_ABORT(); ip++; DISPATCH();
do_bxor:
    stack[sp-2] = (double)((long long)stack[sp-2] ^ (long long)stack[sp-1]);
    sp--; TRACE_ABORT(); ip++; DISPATCH();
do_bnot:
    stack[sp-1] = (double)(~(long long)stack[sp-1]);
    TRACE_ABORT(); ip++; DISPATCH();
do_shl:
    stack[sp-2] = (double)((long long)stack[sp-2] << (long long)stack[sp-1]);
    sp--; TRACE_ABORT(); ip++; DISPATCH();
do_shr:
    stack[sp-2] = (double)((long long)stack[sp-2] >> (long long)stack[sp-1]);
    sp--; TRACE_ABORT(); ip++; DISPATCH();
do_store: {
    /* Pop top, write to var. Auto-detect INT vs FLOAT to keep parity with
     * the Fase 2 fast-path: integral doubles stay as VAL_INT. */
    Variable *v = ip->u.var;
    double r = stack[--sp];
    if (r == (double)(long long)r) {
        v->vtype = VAL_INT;
        v->value.int_value = (long long)r;
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
            pv->value.int_value = (long long)v;
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
BCInfo *bc_get_or_compile(ASTNode *node) {
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
    bc_register_node(node);
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

BCInfo *bc_get_or_compile_method(MethodNode *m, ClassNode *cls) {
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
    bc_register_method(m);
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

    /* Only compile to bytecode when init/limit/step are NUMBER literals. If the
     * init, limit or step is a variable/expression, bail out so the AST walker
     * (which evaluates them fresh each run) handles it — baking a stale constant
     * here would be wrong when the same loop node re-runs with a different value.
     * The literal-free `for(START; STOP; STEP)` forms can carry expression INITs,
     * so we must guard node->left too (not only limit/step). */
    {
        NodeKind ik = nk_of(node->left);             /* init  */
        NodeKind lk = nk_of(node->right);            /* limit */
        NodeKind sk = nk_of(update_body->left);      /* step  */
        if ((ik != NK_NUMBER && ik != NK_INT) ||
            (lk != NK_NUMBER && lk != NK_INT) ||
            (sk != NK_NUMBER && sk != NK_INT)) {
            if (getenv("TYPEEASY_BCDEBUG")) fprintf(stderr,"[BCFOR] fail: non-literal init/limit/step (ik=%d lk=%d sk=%d)\n", ik, lk, sk);
            return 0;
        }
    }

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
BCInfo *bc_get_or_compile_stmt(ASTNode *node) {
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
    bc_register_node(node);
    return info;
}

