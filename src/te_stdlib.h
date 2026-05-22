/* te_stdlib.h — TypeEasy stdlib dispatcher + builtin registrations + plugin host API.
 *
 * Extracted from ast.c (Fase 2 paso 4, "Nivel A" refactor). Contains:
 *  - te_builtin_dispatch():    if-chain fallback for builtins not yet in the
 *                              registry (len, range, to_int, http_get, sha256,
 *                              base64_*, json_stringify/parse, etc.).
 *  - te_register_ast_builtins(): one-time registration of adapt_* wrappers
 *                              for migrated builtins (mysql/postgres/sqlserver,
 *                              env, env_required, now, date_*, uuid_*, etc.).
 *  - te_fill_host_api():       fills the TEHostAPI struct passed to dynamically
 *                              loaded `.so` plugins via load_native().
 *  - te_resolve_arg():         shared arg resolver used by te_builtin_dispatch
 *                              and other stdlib paths.
 *
 * The AST walker calls te_builtin_dispatch() at the top of interpret_call_func.
 */

#ifndef TE_STDLIB_H
#define TE_STDLIB_H

#include "ast.h"
#include "te_builtins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve an arg AST node to a root container node.
 * Writes resolved type (e.g. "LIST","MAP","STRING") to *out_type, or NULL. */
ASTNode *te_resolve_arg(ASTNode *arg, const char **out_type);

/* Returns 1 if the call was dispatched (builtin handled), 0 otherwise.
 * On hit, the result is stored in __ret__ via add_or_update_variable. */
int te_builtin_dispatch(ASTNode *node);

/* Idempotent — register all adapt_* wrappers for core builtins. Called
 * once by te_builtins_ensure_loaded() (in te_builtins.c). */
void te_register_ast_builtins(void);

/* Populate the host API struct used by dynamically loaded plugins. */
void te_fill_host_api(TEHostAPI *out);

#ifdef __cplusplus
}
#endif

#endif /* TE_STDLIB_H */
