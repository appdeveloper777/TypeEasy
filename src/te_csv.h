/* te_csv.h — TypeEasy CSV reader, ORM arena and DataFrame analytics.
 *
 * Extracted from ast.c (Fase 2 paso 2). Contains:
 *   - RFC 4180 CSV reader (with parallel parsing path)
 *   - Bump arena for CSV rows (process-lifetime, script-mode safe)
 *   - AST node block-pool (per-thread, lives until exit)
 *   - DataFrame columnar typedef + analytics (count/sum/min/max/group_sum)
 *   - Top-level loaders consumed by the parser: from_csv_to_list /
 *     from_csv_to_dataframe (already declared in ast.h)
 *
 * The walker uses only the symbols declared here. All workers, mmap
 * helpers, IO threads and SIMD fast paths stay file-static in te_csv.c.
 */
#ifndef TE_CSV_H
#define TE_CSV_H

#include <stddef.h>
#include <stdint.h>

#include "ast.h"   /* ASTNode, ClassNode, ObjectNode, Variable */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque DataFrame: walker only stores pointers + dispatches methods. */
typedef struct DataFrame DataFrame;

/* Shared interned "OBJECT" sentinel. free_ast skips free() for any node
 * whose `type` field equals this pointer. Lazily initialised on first
 * wrapper allocation. */
extern char *g_csv_wrapper_obj_type;

/* Bump arena allocators (per-thread; chained to a process-lifetime keepalive
 * list when worker threads finish). */
void *csv_arena_alloc(size_t n);
char *csv_arena_dup(const char *s, size_t n);
char *csv_arena_strdup(const char *s);

/* CSV attribute type helpers (used by ORM bridge and the loader). */
int csv_attr_is_int(const char *t);
int csv_attr_is_string(const char *t);
int csv_attr_is_nullable(const char *t);

/* AST node block pool (zero-initialised slots, never freed individually). */
ASTNode *ast_pool_alloc(void);

/* Pool-allocated wrapper for an OBJECT value. Used by LINQ fast-paths
 * (orderBy/groupBy/toMap) that materialize wrappers per call. Returns NULL
 * when the value isn't a plain OBJECT (caller falls back to build_item_from_value). */
ASTNode *build_object_wrapper_pooled(ASTNode *value);

/* DataFrame analytics fast-path consumed by interpret_call_method. */
DataFrame *te_list_df(ASTNode *list);
int te_df_dispatch_method(DataFrame *df, ASTNode *node);

/* v0.0.14: Per-call override for columnar mode in from_csv_to_list.
 *   -1 = unset (fall through to env TE_CSV_COLUMNAR)
 *    0 = force legacy (allocate wrappers)
 *    1 = force pure-columnar (skip wrappers)
 * Consumed (and reset to -1) by the next from_csv_to_list invocation. */
extern int g_te_csv_columnar_next;

/* v0.0.14: Lazy CSV-load registry.
 * Used by the parser to defer eager loads until the full AST is built, then
 * decide per-variable whether COLUMNAR is safe (all uses are LINQ aggregates
 * with no element access / iteration / mutation).
 *
 * Flow:
 *   1) Parser action creates a placeholder LIST node, stores it as
 *      var_decl->left, then calls te_csv_lazy_register(var_decl, file, cls).
 *   2) parse_file() calls te_csv_lazy_resolve_all(root) after yyparse().
 *   3) Resolver scans root for usages of var_decl->id, classifies them,
 *      sets g_te_csv_columnar_next accordingly, invokes from_csv_to_list
 *      and replaces var_decl->left with the real loaded list. */
void te_csv_lazy_register(ASTNode *var_decl, const char *filename, const char *class_name);
void te_csv_lazy_resolve_all(ASTNode *root);

#ifdef __cplusplus
}
#endif

#endif /* TE_CSV_H */
