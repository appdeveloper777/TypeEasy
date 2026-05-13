/* te_builtins.c — Hash-table registry for TypeEasy built-in functions.
 *
 * Replaces the historical if/strcmp dispatch chains in ast.c so that
 * adding a builtin is a single te_builtin_register() call instead of a
 * parser+dispatch edit. Also serves as the entry point for native
 * plugins loaded via load_native("name") (Fase 3).
 */
#define _GNU_SOURCE
#include "te_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

/* ─── Hash table ──────────────────────────────────────────────────────── */

#define TE_BUILTIN_BUCKETS 256

typedef struct TEBuiltinEntry {
    char *name;
    TEBuiltinFn fn;
    struct TEBuiltinEntry *next;
} TEBuiltinEntry;

static TEBuiltinEntry *g_buckets[TE_BUILTIN_BUCKETS];
static int g_loaded = 0;

/* FNV-1a 32-bit. */
static unsigned int te_hash(const char *s) {
    unsigned int h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

void te_builtin_register(const char *name, TEBuiltinFn fn) {
    if (!name || !fn) return;
    unsigned int b = te_hash(name) % TE_BUILTIN_BUCKETS;
    /* Overwrite if already present (allows plugin reload / monkey-patch). */
    for (TEBuiltinEntry *e = g_buckets[b]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            e->fn = fn;
            return;
        }
    }
    TEBuiltinEntry *e = (TEBuiltinEntry *)calloc(1, sizeof(TEBuiltinEntry));
    if (!e) return;
    e->name = strdup(name);
    e->fn = fn;
    e->next = g_buckets[b];
    g_buckets[b] = e;
}

TEBuiltinFn te_builtin_lookup(const char *name) {
    if (!name) return NULL;
    if (!g_loaded) te_builtins_ensure_loaded();
    unsigned int b = te_hash(name) % TE_BUILTIN_BUCKETS;
    for (TEBuiltinEntry *e = g_buckets[b]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->fn;
    }
    return NULL;
}

int te_builtin_dispatch_registry(ASTNode *node, ASTNode *args) {
    if (!node || !node->id) return 0;
    TEBuiltinFn fn = te_builtin_lookup(node->id);
    if (!fn) return 0;
    return fn(node, args);
}

/* ─── Bootstrap ───────────────────────────────────────────────────────── */

/* Implemented in ast.c (where all native and stdlib helpers live as static).
 * Strong reference — link will fail if ast.o is not in the build. */
extern void te_register_ast_builtins(void);
extern void te_fill_host_api(TEHostAPI *out);

void te_builtins_ensure_loaded(void) {
    if (g_loaded) return;
    g_loaded = 1;  /* set first to break recursion if a registrant calls lookup */
    te_register_ast_builtins();
}

/* ─── Fase 3: dynamic plugin loading ──────────────────────────────────── */

#ifndef _WIN32

int te_load_native_module(const char *name_or_path) {
    if (!name_or_path || !*name_or_path) return -1;

    /* Resolve: absolute/relative path → use as-is. Bare name → try
     *   ./libte_<name>.so, /usr/local/lib/libte_<name>.so, ~/.te/packages/<name>/libte_<name>.so
     */
    const char *raw = name_or_path;
    char buf[1024];
    void *h = NULL;
    if (strchr(raw, '/') || strstr(raw, ".so")) {
        h = dlopen(raw, RTLD_NOW | RTLD_GLOBAL);
    } else {
        const char *candidates[] = {
            "./libte_%s.so",
            "/usr/local/lib/libte_%s.so",
            "/typeeasy/libte_%s.so",
            NULL
        };
        for (int i = 0; candidates[i] && !h; i++) {
            snprintf(buf, sizeof(buf), candidates[i], raw);
            h = dlopen(buf, RTLD_NOW | RTLD_GLOBAL);
        }
        if (!h) {
            const char *home = getenv("HOME");
            if (home) {
                snprintf(buf, sizeof(buf), "%s/.te/packages/%s/libte_%s.so", home, raw, raw);
                h = dlopen(buf, RTLD_NOW | RTLD_GLOBAL);
            }
        }
    }
    if (!h) {
        fprintf(stderr, "[load_native] dlopen failed: %s\n", dlerror());
        return -2;
    }
    typedef void (*RegisterFn)(const TEHostAPI *);
    RegisterFn reg = (RegisterFn)dlsym(h, "te_module_register");
    if (!reg) {
        fprintf(stderr, "[load_native] missing symbol te_module_register in '%s'\n", name_or_path);
        dlclose(h);
        return -3;
    }
    TEHostAPI host;
    te_fill_host_api(&host);
    reg(&host);
    return 0;
}

#else /* _WIN32 */

int te_load_native_module(const char *name_or_path) {
    (void)name_or_path;
    fprintf(stderr, "[load_native] not supported on Windows host build\n");
    return -1;
}

#endif
