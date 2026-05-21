/* te_json.h — JSON serializer + parser extracted from ast.c (Fase 1).
 *
 * Public API exposes 2 functions:
 *   - te_json_emit_node:  serialize an AST node into a TeBuf
 *   - te_json_parse_value: parse a JSON value into an AST tree
 *
 * Embedded CALL_FUNC / CALL_METHOD evaluation during serialization is
 * delegated back to ast.c via runtime hooks (see te_json_set_eval_hooks).
 * If hooks are NULL the emitter falls back to "null" for those nodes.
 */

#ifndef TE_JSON_H
#define TE_JSON_H

#include "ast.h"
#include "te_buf.h"

/* Hook type used by te_json to evaluate embedded CALL_FUNC / CALL_METHOD
 * nodes during emit. ast.c registers its `interpret_call_func` /
 * `interpret_call_method` once at interpreter startup. */
typedef void (*te_json_eval_fn)(ASTNode *node);

/* Register the eval hooks. Safe to call multiple times (idempotent).
 * Pass NULL to clear. */
void te_json_set_eval_hooks(te_json_eval_fn call_func,
                            te_json_eval_fn call_method);

/* Serialize `n` as JSON into `b`. Handles STRING/INT/NUMBER/FLOAT/BOOL/
 * LIST/MAP/OBJECT_LITERAL/IDENTIFIER and (if hooks set) CALL_FUNC/CALL_METHOD. */
void te_json_emit_node(TeBuf *b, ASTNode *n);

/* Parse a JSON value from *p (advances *p past consumed input).
 * Returns a freshly-allocated AST tree, or NULL on hard error. */
ASTNode *te_json_parse_value(const char **p);

#endif /* TE_JSON_H */
