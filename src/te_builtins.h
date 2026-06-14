/* te_builtins.h — Runtime registry for TypeEasy built-in functions.
 *
 * Replaces the historical if/strcmp chains in `call_native_function` and
 * `te_builtin_dispatch` with an O(1) hash lookup so that adding a new
 * builtin is a one-liner instead of a parser change + dispatch chain edit.
 *
 * Fase 3 (plugins) uses the same registry: a `.so` loaded via
 * `load_native("name")` receives a `TEHostAPI*` and registers its functions
 * by calling `host->register_builtin(...)`.
 */
#ifndef TE_BUILTINS_H
#define TE_BUILTINS_H

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A builtin receives the full call node (for line numbers, debug info, etc.)
 * and a pre-extracted args head (`node->left` for CALL_FUNC,
 * `node->right` for METHOD_CALL_ALONE — the dispatcher picks the right one).
 * Return 1 when the call was handled, 0 to fall through. */
typedef int (*TEBuiltinFn)(ASTNode *node, ASTNode *args);

/* Register / lookup. Names are interned via strdup on first registration.
 * Re-registering the same name overwrites (useful for plugin reloads). */
void          te_builtin_register(const char *name, TEBuiltinFn fn);
TEBuiltinFn   te_builtin_lookup(const char *name);
int           te_builtin_dispatch_registry(ASTNode *node, ASTNode *args);

/* Lazy bootstrap: registers all core builtins (mysql_*, postgres_*,
 * sqlserver_*, json/xml, request/response, len/range/read_file/...).
 * Idempotent. Called automatically on first lookup. */
void          te_builtins_ensure_loaded(void);

/* Fase 3 — plugins.
 *
 * A `.so` plugin must export:
 *     void te_module_register(const TEHostAPI *host);
 *
 * The plugin then calls host->register_builtin("mybuiltin", &my_fn) for
 * each function it wishes to expose. Helpers below let the plugin do
 * everything that core builtins do without linking against the host
 * binary directly (clean ABI). */
typedef struct TEHostAPI {
    int  abi_version;  /* TE_HOST_API_VERSION at compile time */
    void (*register_builtin)(const char *name, TEBuiltinFn fn);

    /* Common helpers a plugin will need. Mirror existing internals. */
    void  (*set_ret_int)(int v);
    void  (*set_ret_str)(const char *s);
    void  (*set_ret_float)(double v);
    char *(*arg_string)(ASTNode *arg);   /* malloc'd, caller frees */
    int   (*arg_int)(ASTNode *arg, int defv);
    double(*arg_float)(ASTNode *arg, double defv);

    /* === ABI v2 — Dapper-style parametros para plugins DB ============
     * `arg_map_head(arg, &owned)` devuelve la cabeza de una lista de
     * KV_PAIR si `arg` es:
     *   - un OBJECT_LITERAL inline `{ "k": v, ... }`         (owned=0)
     *   - una variable de tipo MAP                            (owned=0)
     *   - una instancia de clase (sus atributos como params)  (owned=1)
     * Si owned=1, el plugin DEBE llamar `free_node(head)` cuando termine.
     * Devuelve NULL si `arg` no aplica. `out_owned` puede ser NULL.
     *
     * Iteracion (en el plugin):
     *   for (ASTNode *p = head; p; p = p->right) {
     *       const char *key = p->id;       // "@id", "name", etc.
     *       ASTNode    *val = p->left;     // pasalo a host->arg_int/string/float
     *   }
     *
     * Disponible solo si abi_version >= 2. */
    ASTNode* (*arg_map_head)(ASTNode *arg, int *out_owned);
    void     (*free_node)(ASTNode *node);

    /* === Guard append-only (Nivel 2.1) =================================
     * sizeof(TEHostAPI) que el HOST compiló. Permite detectar drift de
     * layout aunque alguien olvide subir TE_HOST_API_VERSION: si un plugin
     * obsoleto (compilado contra otra disposición de campos) se carga, su
     * sizeof local NO coincidirá y debe rechazarse RUIDOSAMENTE en vez de
     * registrar punteros desalineados que devuelven [] en silencio.
     * Plugins viejos que no leen este campo simplemente lo ignoran (0). */
    int      struct_size;

    /* === ABI v3 — auto-cleanup de conexiones por request =============
     * register_request_cleanup(fn): el plugin DB registra un callback que el
     * host invoca al final de CADA request HTTP, para que cierre las
     * conexiones abiertas durante el request que el script olvidó cerrar
     * (return temprano, throw, error). Evita fugas que agotan el pool.
     *
     * db_request_phase(): devuelve 1 una vez que el servidor despacha
     * requests, 0 durante el load global del script. Permite al plugin marcar
     * qué conexiones son request-scoped (auto-cerrables) vs. globales
     * (persistentes). Disponible solo si abi_version >= 3. */
    void (*register_request_cleanup)(void (*fn)(void));
    int  (*db_request_phase)(void);
} TEHostAPI;

#define TE_HOST_API_VERSION 3

/* Returns 0 on success, non-zero on error (file not found, missing
 * `te_module_register`, ABI mismatch). On error, sets __ret__ to 0;
 * on success, sets __ret__ to 1. */
int  te_load_native_module(const char *name_or_path);

#ifdef __cplusplus
}
#endif

#endif /* TE_BUILTINS_H */
