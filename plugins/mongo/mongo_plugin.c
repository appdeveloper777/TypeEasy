/*
 * libte_mongo — Plugin nativo de MongoDB para TypeEasy.
 *
 * Builtins expuestos:
 *   mongo_connect("mongodb://host:port/db")            -> int slot
 *   mongo_query(slot, "collection", { filter }, "json"|"xml")  -> string en __ret__
 *   mongo_close(slot)                                  -> int 0
 *
 * mongo_query()'s `filter` es whitelist, no blacklist: solo acepta pares
 * campo=escalar planos (string/int/float/bool/null/fecha/ObjectId). Una
 * clave `$`-prefijada o con '.', o un valor documento/array/regex/code
 * (incluido el Extended JSON legacy {"$regex":...}) hace fallar la query
 * ENTERA (devuelve "[]") en vez de ejecutarse con un filtro parcial. Esto
 * es deliberado: si `filter` viene de datos de la petición del cliente
 * (p.ej. `mongo_query(conn, "usuarios", json_parse(request_body()), "json")`),
 * un blacklist tendría que enumerar cada forma en que BSON puede codificar
 * un operador — ejercicio que ya falló tres veces ($ne, $regex/$options,
 * un valor-tipo-peligroso borrando un campo entero). Si un script legítimo
 * necesita operadores ($gt, $in, ...), debe construir/ejecutar ese filtro
 * por una vía separada que elija explícitamente, nunca aceptarlos gratis
 * desde JSON controlado por el cliente.
 *
 * Carga desde un script .te:
 *   load_native("mongo");
 *   let conn = mongo_connect("mongodb://localhost:27017/meri");
 *   let r    = mongo_query(conn, "usuarios", { "activo": 1 }, "json");
 *   println(r);
 *   mongo_close(conn);
 *
 * Build:
 *   gcc -shared -fPIC -I<typeeasy>/src -I<typeeasy>/api_server \
 *       mongo_plugin.c $(pkg-config --cflags --libs libmongoc-1.0) \
 *       -o libte_mongo.so
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mongoc/mongoc.h>

#include "te_builtins.h"

#define POOL_SIZE 10

static mongoc_client_t *g_pool[POOL_SIZE];
static char            *g_pool_db[POOL_SIZE];   /* default DB extraída de la URI */
static int              g_pool_used[POOL_SIZE];
static int              g_inited = 0;

/* IMPORTANTE: copiamos la struct, no el puntero — el host la pasa en su stack. */
static TEHostAPI        g_host;
#define H (&g_host)

/* ─── helpers ─────────────────────────────────────────────────────── */

static int pool_alloc(void) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (!g_pool_used[i]) { g_pool_used[i] = 1; return i; }
    }
    return -1;
}

static void pool_free(int slot) {
    if (slot < 0 || slot >= POOL_SIZE) return;
    if (g_pool[slot]) { mongoc_client_destroy(g_pool[slot]); g_pool[slot] = NULL; }
    if (g_pool_db[slot]) { free(g_pool_db[slot]); g_pool_db[slot] = NULL; }
    g_pool_used[slot] = 0;
}

/* Walks ASTNode args list (linked via ->next) and returns nth (0-based). */
static ASTNode *arg_at(ASTNode *args, int n) {
    ASTNode *p = args;
    for (int i = 0; p && i < n; i++) p = p->next; /* gotcha #1: step args via ->next */
    return p;
}

/* Best-effort: extract default DB from a mongodb URI like "mongodb://h:p/dbname?..." */
static char *extract_default_db(const char *uri) {
    if (!uri) return NULL;
    const char *s = strstr(uri, "://");
    if (!s) return NULL;
    s += 3;
    const char *slash = strchr(s, '/');
    if (!slash) return NULL;
    slash++;
    const char *end = slash;
    while (*end && *end != '?' && *end != '#') end++;
    if (end == slash) return NULL;
    size_t n = (size_t)(end - slash);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, slash, n);
    out[n] = '\0';
    return out;
}

/* ─── builtins ────────────────────────────────────────────────────── */

static int te_mongo_connect(ASTNode *node, ASTNode *args) {
    (void)node;
    char *uri = H->arg_string(arg_at(args, 0));
    if (!uri || !*uri) {
        fprintf(stderr, "[mongo_connect] missing URI argument\n");
        if (uri) free(uri);
        H->set_ret_int(-1);
        return 1;
    }
    int slot = pool_alloc();
    if (slot < 0) {
        fprintf(stderr, "[mongo_connect] pool exhausted (max %d)\n", POOL_SIZE);
        free(uri);
        H->set_ret_int(-1);
        return 1;
    }
    bson_error_t err;
    mongoc_uri_t *muri = mongoc_uri_new_with_error(uri, &err);
    if (!muri) {
        fprintf(stderr, "[mongo_connect] bad URI: %s\n", err.message);
        pool_free(slot);
        free(uri);
        H->set_ret_int(-1);
        return 1;
    }
    g_pool[slot] = mongoc_client_new_from_uri(muri);
    g_pool_db[slot] = extract_default_db(uri);
    mongoc_uri_destroy(muri);
    free(uri);
    if (!g_pool[slot]) {
        pool_free(slot);
        H->set_ret_int(-1);
        return 1;
    }
    /* App name for diagnostics on the server side. */
    mongoc_client_set_appname(g_pool[slot], "typeeasy-mongo-plugin");
    H->set_ret_int(slot);
    return 1;
}

static int te_mongo_close(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = H->arg_int(arg_at(args, 0), -1);
    pool_free(slot);
    H->set_ret_int(0);
    return 1;
}

/* mongo_query()'s default filter is built by WHITELIST, not by blacklist.
 *
 * Three rounds of trying to enumerate every *dangerous* shape a client-
 * controlled JSON document can take once parsed into BSON ($ne as a key,
 * $regex/$options as Extended JSON decoding straight into a native
 * BSON_TYPE_REGEX value, a type-dangerous value silently deleting an
 * unrelated field's constraint...) kept finding a new bypass, because BSON
 * has many ways to encode "this is actually an operator, not data" and a
 * blacklist has to know all of them. A whitelist only has to know what a
 * SAFE filter looks like: a flat map of plain field names to scalar values,
 * exactly what the plugin's own documented example uses
 * (`mongo_query(conn, "usuarios", { "activo": 1 }, "json")`). Anything that
 * doesn't fit that shape — a nested document/array, a regex, code, an
 * operator-like `$`-prefixed or dotted key — is refused outright rather than
 * partially accepted, and the WHOLE query is rejected (returns "[]") rather
 * than run with a partial filter: a script that genuinely needs range
 * queries, $in, or other operators should build/execute those with a
 * dedicated raw-filter path it explicitly opts into, not get them for free
 * from client-controlled JSON. */

/* Los únicos tipos BSON que una comparación de igualdad simple puede tomar
 * sin acarrear semántica de operador o ejecución de código. */
static int te_mongo_bson_type_is_safe_scalar(bson_type_t t) {
    switch (t) {
        case BSON_TYPE_UTF8:
        case BSON_TYPE_INT32:
        case BSON_TYPE_INT64:
        case BSON_TYPE_DOUBLE:
        case BSON_TYPE_BOOL:
        case BSON_TYPE_NULL:
        case BSON_TYPE_DATE_TIME:
        case BSON_TYPE_OID:
            return 1;
        default:
            return 0;
    }
}

/* Construye el filtro final tomando SOLO los pares de `filter_in` cuya
 * clave es un nombre de campo simple (no `$`-prefijada, sin '.') y cuyo
 * valor es uno de los escalares seguros de arriba. Cualquier otro par deja
 * `*rejected` en 1: el llamador debe entonces descartar la query completa
 * en vez de ejecutarla con un filtro parcial. Un filtro de entrada vacío
 * (`{}`, "traer todo" intencional) sigue produciendo `{}` sin rechazo. */
static bson_t *te_mongo_build_whitelisted_filter(const bson_t *filter_in, int *rejected) {
    *rejected = 0;
    bson_t *out = bson_new();
    if (!filter_in) return out;
    bson_iter_t iter;
    if (!bson_iter_init(&iter, filter_in)) return out;
    while (bson_iter_next(&iter)) {
        const char *key = bson_iter_key(&iter);
        int key_ok = key && *key && key[0] != '$' && !strchr(key, '.');
        bson_type_t t = bson_iter_type(&iter);
        if (!key_ok || !te_mongo_bson_type_is_safe_scalar(t)) {
            *rejected = 1;
            continue;
        }
        const bson_value_t *v = bson_iter_value(&iter);
        bson_append_value(out, key, -1, v);
    }
    return out;
}

static int te_mongo_query(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = H->arg_int(arg_at(args, 0), -1);
    char *coll_name = H->arg_string(arg_at(args, 1));
    char *filter_json = H->arg_string(arg_at(args, 2));   /* puede venir como JSON string si el host lo serializa */
    char *fmt = H->arg_string(arg_at(args, 3));

    if (slot < 0 || slot >= POOL_SIZE || !g_pool[slot]) {
        fprintf(stderr, "[mongo_query] invalid connection slot\n");
        if (coll_name) free(coll_name);
        if (filter_json) free(filter_json);
        if (fmt) free(fmt);
        H->set_ret_str("[]");
        return 1;
    }
    if (!coll_name || !*coll_name) {
        fprintf(stderr, "[mongo_query] missing collection name\n");
        if (coll_name) free(coll_name);
        if (filter_json) free(filter_json);
        if (fmt) free(fmt);
        H->set_ret_str("[]");
        return 1;
    }

    bson_t *raw_filter = NULL;
    bson_error_t berr;
    if (filter_json && *filter_json && strcmp(filter_json, "null") != 0) {
        raw_filter = bson_new_from_json((const uint8_t *)filter_json, -1, &berr);
        if (!raw_filter) {
            fprintf(stderr, "[mongo_query] bad filter JSON: %s\n", berr.message);
            raw_filter = bson_new();
        }
    } else {
        raw_filter = bson_new();
    }
    int filter_rejected = 0;
    bson_t *filter;
    /* Escape hatch para scripts que construyen sus propios operadores ($gt, $in, ...)
     * a partir de datos que NO vienen del cliente: TE_MONGO_ALLOW_OPERATORS=1. */
    const char *allow_ops = getenv("TE_MONGO_ALLOW_OPERATORS");
    if (allow_ops && allow_ops[0] && allow_ops[0] != '0') {
        filter = raw_filter;
    } else {
        filter = te_mongo_build_whitelisted_filter(raw_filter, &filter_rejected);
        bson_destroy(raw_filter);
    }
    if (filter_rejected) {
        /* El filtro traia una clave operador ($.../con '.') o un valor no
         * escalar (documento/array/regex/code/...). En vez de intentar
         * adivinar que tan peligroso es y sanear parcialmente, rechazamos
         * la query completa: mas seguro que arriesgar un filtro debilitado
         * o un dump completo de la coleccion. */
        fprintf(stderr, "[mongo_query] filter rejected: contains a non-scalar value or operator-like key (mongo_query only accepts flat field=scalar filters)\n");
        bson_destroy(filter);
        free(coll_name); if (filter_json) free(filter_json); if (fmt) free(fmt);
        H->set_ret_str("[]");
        return 1;
    }

    const char *db_name = g_pool_db[slot] ? g_pool_db[slot] : "test";
    mongoc_collection_t *coll = mongoc_client_get_collection(g_pool[slot], db_name, coll_name);
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(coll, filter, NULL, NULL);

    /* Construye un JSON array con los documentos. */
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        bson_destroy(filter);
        mongoc_cursor_destroy(cursor);
        mongoc_collection_destroy(coll);
        free(coll_name); if (filter_json) free(filter_json); if (fmt) free(fmt);
        H->set_ret_str("[]");
        return 1;
    }
    buf[0] = '['; len = 1;

    int first = 1;
    const bson_t *doc;
    while (mongoc_cursor_next(cursor, &doc)) {
        size_t jlen = 0;
        char *json = bson_as_relaxed_extended_json(doc, &jlen);
        if (!json) continue;
        size_t need = len + jlen + 4;
        if (need > cap) {
            while (cap < need) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { bson_free(json); break; }
            buf = nb;
        }
        if (!first) { buf[len++] = ','; }
        memcpy(buf + len, json, jlen); len += jlen;
        first = 0;
        bson_free(json);
    }
    if (mongoc_cursor_error(cursor, &berr)) {
        fprintf(stderr, "[mongo_query] cursor error: %s\n", berr.message);
    }

    if (len + 2 >= cap) {
        cap = len + 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
            free(buf);
            bson_destroy(filter);
            mongoc_cursor_destroy(cursor);
            mongoc_collection_destroy(coll);
            free(coll_name); if (filter_json) free(filter_json); if (fmt) free(fmt);
            H->set_ret_str("[]");
            return 1;
        }
        buf = nb;
    }
    buf[len++] = ']';
    buf[len]   = '\0';

    /* XML naive: envuelve cada doc */
    if (fmt && strcmp(fmt, "xml") == 0) {
        /* Por simplicidad devolvemos el JSON dentro de <result> */
        size_t xn = len + 64;
        char *x = (char *)malloc(xn);
        if (!x) {
            H->set_ret_str(buf);
        } else {
            snprintf(x, xn, "<result>%s</result>", buf);
            H->set_ret_str(x);
            free(x);
        }
    } else {
        H->set_ret_str(buf);
    }

    free(buf);
    bson_destroy(filter);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(coll);
    free(coll_name);
    if (filter_json) free(filter_json);
    if (fmt) free(fmt);
    return 1;
}

/* ─── entry point ─────────────────────────────────────────────────── */

TE_PLUGIN_EXPORT_ABI()

void te_module_register(const TEHostAPI *host) {
    if (!host) return;
    if (host->abi_version < TE_HOST_API_VERSION) {
        fprintf(stderr, "[libte_mongo] ABI mismatch: host=%d plugin=%d\n",
                host->abi_version, TE_HOST_API_VERSION);
        return;
    }
    g_host = *host;   /* copia la API por valor (el original vive en el stack del host) */
    if (!g_inited) { mongoc_init(); g_inited = 1; }
    host->register_builtin("mongo_connect", te_mongo_connect);
    host->register_builtin("mongo_query",   te_mongo_query);
    host->register_builtin("mongo_close",   te_mongo_close);
    fprintf(stderr, "[libte_mongo] registered: mongo_connect, mongo_query, mongo_close\n");
}
