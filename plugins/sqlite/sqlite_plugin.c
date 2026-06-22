/*
 * libte_sqlite — Plugin nativo de SQLite para TypeEasy.
 *
 * Builtins expuestos:
 *   sqlite_connect("path/to.db" | ":memory:")           -> int slot   (-1 en error)
 *   sqlite_exec(slot, sql [, {params}])                 -> int rows_affected (-1 en err)
 *   sqlite_query(slot, sql [, {params}] [, "json"|"xml"]) -> string en __ret__
 *   sqlite_last_id(slot)                                -> int last_insert_rowid
 *   sqlite_close(slot)                                  -> int 0
 *
 * Estilo Dapper (mismo que mysql_query / postgres_query):
 *   let r = sqlite_query(db,
 *       "SELECT * FROM users WHERE id=@id AND name=@name",
 *       { "@id": 5, "@name": "foo" }, "json");
 *   sqlite_exec(db,
 *       "INSERT INTO users(name,email) VALUES(@n,@e)",
 *       { "@n": "O'Reilly", "@e": "a@b" });
 *
 * Los placeholders usan la sintaxis nativa de sqlite3 (@name, :name, $name).
 * Las claves del map pueden ir con o sin '@'. Internamente usamos
 * sqlite3_bind_* (no interpolación textual) → inmune a inyección SQL.
 *
 * Requiere host con ABI >= 2 para usar params; el resto funciona en cualquier ABI.
 *
 * Uso desde un script .te:
 *   load_native("sqlite");
 *   let db = sqlite_connect("game.db");
 *   sqlite_exec(db, "CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, name TEXT)");
 *   sqlite_exec(db, "INSERT INTO t(name) VALUES(@n)", { "@n": "alice" });
 *   let rows = sqlite_query(db, "SELECT * FROM t WHERE id>@min", { "@min": 0 }, "json");
 *   println(rows);
 *   sqlite_close(db);
 *
 * Build (Linux/Docker):
 *   gcc -shared -fPIC -O2 -I<typeeasy>/src \
 *       sqlite_plugin.c -lsqlite3 -o libte_sqlite.so
 *
 * Build (Windows / MSYS2 MINGW64):
 *   gcc -shared -O2 -I../../src \
 *       sqlite_plugin.c -lsqlite3 -o libte_sqlite.dll
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sqlite3.h>

#include "te_builtins.h"

#define POOL_SIZE 10

static sqlite3 *g_pool[POOL_SIZE];
static int      g_pool_used[POOL_SIZE];
/* Auto-cleanup por request: marca qué slots se abrieron durante un request
 * (host->db_request_phase()==1) para cerrarlos al final si el script no llamó
 * sqlite_close(). Los abiertos en el load global persisten. */
static int      g_req_scoped[POOL_SIZE];

/* IMPORTANTE: copiamos la struct, no el puntero — el host la pasa en su stack. */
static TEHostAPI g_host;
#define H (&g_host)

/* ─── pool helpers ─────────────────────────────────────────────────── */

static int pool_alloc(void) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (!g_pool_used[i]) { g_pool_used[i] = 1; return i; }
    }
    return -1;
}

static void pool_free(int slot) {
    if (slot < 0 || slot >= POOL_SIZE) return;
    if (g_pool[slot]) { sqlite3_close(g_pool[slot]); g_pool[slot] = NULL; }
    g_pool_used[slot] = 0;
    g_req_scoped[slot] = 0;
}

/* Cierre automático al final de cada request (registrado vía host API v3).
 * Cierra solo las conexiones abiertas durante este request que el script no
 * cerró; las conexiones globales (abiertas en el load) se preservan. */
static void sqlite_close_request_conns(void) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (g_pool[i] && g_req_scoped[i]) {
            sqlite3_close(g_pool[i]);
            g_pool[i] = NULL;
            g_pool_used[i] = 0;
            g_req_scoped[i] = 0;
        }
    }
}

static ASTNode *arg_at(ASTNode *args, int n) {
    ASTNode *p = args;
    for (int i = 0; p && i < n; i++) p = p->next; /* gotcha #1: step args via ->next */
    return p;
}

/* ─── string buffer ────────────────────────────────────────────────── */

typedef struct { char *p; size_t len, cap; int oom; } SB;

static int sb_reserve(SB *b, size_t add) {
    if (b->oom) return -1;
    if (b->len + add + 1 <= b->cap) return 0;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < b->len + add + 1) nc *= 2;
    char *np = (char *)realloc(b->p, nc);
    if (!np) { b->oom = 1; return -1; }
    b->p = np; b->cap = nc;
    return 0;
}

static void sb_putc(SB *b, char c) {
    if (sb_reserve(b, 1) != 0) return;
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
}

static void sb_puts(SB *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    if (sb_reserve(b, n) != 0) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

/* JSON-escape a UTF-8 string into the buffer (without surrounding quotes). */
static void sb_put_json_str(SB *b, const char *s) {
    if (!s) return;
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        unsigned char c = *q;
        switch (c) {
            case '"':  sb_puts(b, "\\\""); break;
            case '\\': sb_puts(b, "\\\\"); break;
            case '\n': sb_puts(b, "\\n");  break;
            case '\r': sb_puts(b, "\\r");  break;
            case '\t': sb_puts(b, "\\t");  break;
            case '\b': sb_puts(b, "\\b");  break;
            case '\f': sb_puts(b, "\\f");  break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    sb_puts(b, esc);
                } else {
                    sb_putc(b, (char)c);
                }
        }
    }
}

/* XML-escape into the buffer. */
static void sb_put_xml_str(SB *b, const char *s) {
    if (!s) return;
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        unsigned char c = *q;
        switch (c) {
            case '<':  sb_puts(b, "&lt;");   break;
            case '>':  sb_puts(b, "&gt;");   break;
            case '&':  sb_puts(b, "&amp;");  break;
            case '"':  sb_puts(b, "&quot;"); break;
            case '\'': sb_puts(b, "&apos;"); break;
            default:   sb_putc(b, (char)c);
        }
    }
}

/* ─── Dapper-style param binding (ABI v2) ──────────────────────────────
 *
 * Si el host expone arg_map_head (abi_version >= 2), aceptamos que un
 * argumento sea:
 *   - OBJECT_LITERAL inline:  { "@id": 5, "@name": "foo" }
 *   - Variable MAP            (let p = { ... }; sqlite_query(db, sql, p))
 *   - Instancia de clase      (sus atributos se exponen como @attr)
 *
 * Los placeholders en el SQL siguen la sintaxis nativa de sqlite3:
 *   @nombre, :nombre, $nombre, ?NNN
 * Internamente usamos sqlite3_bind_parameter_index(stmt, "@nombre") —
 * sqlite3 distingue el prefijo, así que aceptamos claves del mapa con o
 * sin '@' inicial: probamos primero la clave tal cual, luego con '@'
 * prepended.
 *
 * No interpolamos texto — usamos sqlite3_bind_* directamente, que es
 * inmune a inyección SQL y a quoting raro (`O'Reilly`).
 */

/* Detecta si el host expone la ABI v2 (arg_map_head no es NULL). */
static int host_has_v2(void) {
    return g_host.abi_version >= 2 && g_host.arg_map_head != NULL;
}

/* Devuelve 1 si `arg` resuelve a un mapa/objeto (params Dapper).
 * Sólo mira el tipo para evitar materializar el head innecesariamente. */
static int arg_looks_like_params(ASTNode *arg) {
    if (!arg || !arg->type) return 0;
    if (strcmp(arg->type, "OBJECT_LITERAL") == 0) return 1;
    if (strcmp(arg->type, "MAP") == 0) return 1;
    /* IDENTIFIER: sólo arg_map_head sabe si es MAP/OBJECT; lo dejamos
     * pasar y db_arg_as_map_head devolverá NULL si no aplica. */
    if (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0) return 1;
    return 0;
}

/* Bindea un valor (ASTNode) a un parámetro por índice 1-based.
 * Devuelve SQLITE_OK o un código de error. */
static int bind_value(sqlite3_stmt *stmt, int idx, ASTNode *val) {
    if (!val || !val->type) {
        return sqlite3_bind_null(stmt, idx);
    }
    const char *t = val->type;
    if (strcmp(t, "NULL") == 0) {
        return sqlite3_bind_null(stmt, idx);
    }
    if (strcmp(t, "NUMBER") == 0 || strcmp(t, "INT") == 0) {
        /* En ASTNode los enteros viven en `value`. */
        return sqlite3_bind_int64(stmt, idx, (sqlite3_int64)val->value);
    }
    if (strcmp(t, "BOOL") == 0) {
        /* Literal booleano (true/false): leaf "BOOL" con value=1/0. SQLite no
         * tiene tipo boolean nativo → se bindea como INTEGER 1/0. Sin esta
         * rama caía al fallback arg_string y se bindeaba como texto. */
        return sqlite3_bind_int64(stmt, idx, val->value ? 1 : 0);
    }
    if (strcmp(t, "FLOAT") == 0 || strcmp(t, "DB_RAW") == 0) {

        /* DB_RAW: float serializado a string por db_params (caso clase). */
        double d = 0;
        if (val->str_value && *val->str_value) d = atof(val->str_value);
        else d = (double)val->value;
        return sqlite3_bind_double(stmt, idx, d);
    }
    if (strcmp(t, "STRING") == 0) {
        const char *s = val->str_value ? val->str_value : "";
        return sqlite3_bind_text(stmt, idx, s, -1, SQLITE_TRANSIENT);
    }
    /* Para IDENTIFIER u otros, último recurso: pedir al host que lo
     * resuelva como string. */
    char *s = g_host.arg_string(val);
    int rc = sqlite3_bind_text(stmt, idx, s ? s : "", -1, SQLITE_TRANSIENT);
    if (s) free(s);
    return rc;
}

/* Camina la lista de KV_PAIR y bindea cada par al stmt.
 * Si el nombre del param es "@k", intenta primero "@k" en el SQL, luego
 * ":k" / "$k" / "k". Si la clave del mapa viene sin "@", también prueba
 * con "@" prepended. Imprime warning si un binding no matchea ningún
 * placeholder (útil para detectar typos). Devuelve SQLITE_OK o el primer
 * error encontrado. */
static int bind_params_from_head(sqlite3_stmt *stmt, ASTNode *head) {
    char nbuf[128];
    for (ASTNode *p = head; p; p = p->right) {
        const char *key = p->id ? p->id : "";
        ASTNode    *val = p->left;
        int idx = 0;

        /* Probar variaciones: tal cual, con '@', con ':', con '$'. */
        const char *prefixes[] = { "", "@", ":", "$", NULL };
        if (*key == '@' || *key == ':' || *key == '$') {
            /* Ya trae prefijo — probar tal cual primero. */
            idx = sqlite3_bind_parameter_index(stmt, key);
            if (idx == 0) {
                /* Sin prefijo (saltarlo). */
                idx = sqlite3_bind_parameter_index(stmt, key + 1);
            }
        } else {
            for (int i = 0; prefixes[i] && idx == 0; i++) {
                if (prefixes[i][0] == '\0') {
                    idx = sqlite3_bind_parameter_index(stmt, key);
                } else {
                    snprintf(nbuf, sizeof(nbuf), "%s%s", prefixes[i], key);
                    idx = sqlite3_bind_parameter_index(stmt, nbuf);
                }
            }
        }

        if (idx == 0) {
            /* No matchea ningún placeholder — no es error fatal, sólo aviso. */
            fprintf(stderr, "[sqlite] warning: param '%s' has no matching placeholder in SQL\n", key);
            continue;
        }
        int rc = bind_value(stmt, idx, val);
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

/* Resuelve el arg N como params (head de KV_PAIR) si aplica.
 * Devuelve NULL si no es un mapa o si la ABI del host es v1. Si
 * *out_owned == 1, el caller debe llamar a g_host.free_node(head). */
static ASTNode *arg_as_params(ASTNode *args, int idx, int *out_owned) {
    if (out_owned) *out_owned = 0;
    if (!host_has_v2()) return NULL;
    ASTNode *a = arg_at(args, idx);
    if (!arg_looks_like_params(a)) return NULL;
    return g_host.arg_map_head(a, out_owned);
}

/* ─── builtins ─────────────────────────────────────────────────────── */

static int te_sqlite_connect(ASTNode *node, ASTNode *args) {
    (void)node;
    char *path = H->arg_string(arg_at(args, 0));
    if (!path || !*path) {
        fprintf(stderr, "[sqlite_connect] missing path argument\n");
        if (path) free(path);
        H->set_ret_int(-1);
        return 1;
    }
    int slot = pool_alloc();
    if (slot < 0) {
        fprintf(stderr, "[sqlite_connect] pool exhausted (max %d)\n", POOL_SIZE);
        free(path);
        H->set_ret_int(-1);
        return 1;
    }
    sqlite3 *db = NULL;
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[sqlite_connect] open '%s' failed: %s\n",
                path, db ? sqlite3_errmsg(db) : "unknown");
        if (db) sqlite3_close(db);
        pool_free(slot);
        free(path);
        H->set_ret_int(-1);
        return 1;
    }
    /* Pragmas razonables para apps web: WAL + busy timeout. */
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    g_pool[slot] = db;
    g_req_scoped[slot] = (H->db_request_phase ? H->db_request_phase() : 1);
    free(path);
    H->set_ret_int(slot);
    return 1;
}

static int te_sqlite_close(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = H->arg_int(arg_at(args, 0), -1);
    pool_free(slot);
    H->set_ret_int(0);
    return 1;
}

static int te_sqlite_exec(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = H->arg_int(arg_at(args, 0), -1);
    char *sql = H->arg_string(arg_at(args, 1));

    if (slot < 0 || slot >= POOL_SIZE || !g_pool[slot]) {
        fprintf(stderr, "[sqlite_exec] invalid connection slot\n");
        if (sql) free(sql);
        H->set_ret_int(-1);
        return 1;
    }
    if (!sql || !*sql) {
        if (sql) free(sql);
        H->set_ret_int(0);
        return 1;
    }

    /* Detectar params Dapper-style (arg 2). */
    int   owned = 0;
    ASTNode *params = arg_as_params(args, 2, &owned);

    if (!params) {
        /* Fast path: SQL sin params → sqlite3_exec acepta múltiples sentencias
         * separadas por ';'. */
        char *errmsg = NULL;
        int rc = sqlite3_exec(g_pool[slot], sql, NULL, NULL, &errmsg);
        free(sql);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "[sqlite_exec] error: %s\n", errmsg ? errmsg : "unknown");
            if (errmsg) sqlite3_free(errmsg);
            H->set_ret_int(-1);
            return 1;
        }
        H->set_ret_int(sqlite3_changes(g_pool[slot]));
        return 1;
    }

    /* Slow path con params: una sola sentencia, prepare + bind + step. */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_pool[slot], sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        /* Devolvemos el error en __ret__ ademas de stderr para homogeneizar
         * con mysql_query/postgres_query: handlers que esperan SELECT pueden
         * detectar la falla con res.contains("\"error\"") en vez de recibir
         * silenciosamente -1 (que se confundia con 'no afecto filas'). */
        char ebuf[512];
        snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                 sqlite3_errmsg(g_pool[slot]));
        fprintf(stderr, "[sqlite_exec] prepare error: %s\n", sqlite3_errmsg(g_pool[slot]));
        if (stmt) sqlite3_finalize(stmt);
        if (owned) H->free_node(params);
        free(sql);
        H->set_ret_str(ebuf);
        return 1;
    }
    rc = bind_params_from_head(stmt, params);
    if (owned) H->free_node(params);
    if (rc != SQLITE_OK) {
        char ebuf[512];
        snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                 sqlite3_errmsg(g_pool[slot]));
        fprintf(stderr, "[sqlite_exec] bind error: %s\n", sqlite3_errmsg(g_pool[slot]));
        sqlite3_finalize(stmt);
        free(sql);
        H->set_ret_str(ebuf);
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        char ebuf[512];
        snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                 sqlite3_errmsg(g_pool[slot]));
        fprintf(stderr, "[sqlite_exec] step error: %s\n", sqlite3_errmsg(g_pool[slot]));
        sqlite3_finalize(stmt);
        free(sql);
        H->set_ret_str(ebuf);
        return 1;
    }
    int changes = sqlite3_changes(g_pool[slot]);
    sqlite3_finalize(stmt);
    free(sql);
    H->set_ret_int(changes);
    return 1;
}

static int te_sqlite_last_id(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = H->arg_int(arg_at(args, 0), -1);
    if (slot < 0 || slot >= POOL_SIZE || !g_pool[slot]) {
        H->set_ret_int(-1);
        return 1;
    }
    sqlite3_int64 id = sqlite3_last_insert_rowid(g_pool[slot]);
    H->set_ret_int((int)id);  /* TODO: si pasamos de 2^31 filas, ampliar API a int64 */
    return 1;
}

static int te_sqlite_query(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot   = H->arg_int(arg_at(args, 0), -1);
    char *sql  = H->arg_string(arg_at(args, 1));

    /* Detección de variantes:
     *   query(slot, sql)                       → fmt=json, params=NULL
     *   query(slot, sql, "json"|"xml")         → fmt=arg2, params=NULL
     *   query(slot, sql, {params})             → fmt=json, params=arg2
     *   query(slot, sql, {params}, "json"|"xml") → fmt=arg3, params=arg2
     */
    int   params_owned = 0;
    ASTNode *params = NULL;
    char *fmt = NULL;

    ASTNode *a2 = arg_at(args, 2);
    if (a2) {
        if (arg_looks_like_params(a2)) {
            params = arg_as_params(args, 2, &params_owned);
            /* Si arg_as_params devolvió NULL (no era map en realidad),
             * tratamos a2 como fmt — pero arg_looks_like_params es muy
             * laxo con IDENTIFIER, así que mejor: si params==NULL tras
             * intentar, considerarlo fmt sólo si el tipo es STRING. */
            if (!params) {
                if (a2->type && strcmp(a2->type, "STRING") == 0)
                    fmt = H->arg_string(a2);
            } else {
                fmt = H->arg_string(arg_at(args, 3));
            }
        } else {
            fmt = H->arg_string(a2);
        }
    }

    int as_xml = (fmt && (strcmp(fmt, "xml") == 0 || strcmp(fmt, "XML") == 0));

    if (slot < 0 || slot >= POOL_SIZE || !g_pool[slot]) {
        fprintf(stderr, "[sqlite_query] invalid connection slot\n");
        if (sql) free(sql);
        if (fmt) free(fmt);
        if (params_owned) H->free_node(params);
        H->set_ret_str(as_xml ? "<result/>" : "[]");
        return 1;
    }
    if (!sql || !*sql) {
        if (sql) free(sql);
        if (fmt) free(fmt);
        if (params_owned) H->free_node(params);
        H->set_ret_str(as_xml ? "<result/>" : "[]");
        return 1;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_pool[slot], sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        /* Devolvemos {"error":...} (NO "[]") para que el handler pueda
         * detectar la falla. Antes esto causaba un falso "sin resultados"
         * (p.ej. login devolvia "credenciales invalidas" en lugar de 500
         * cuando el SQL referenciaba una funcion inexistente como sha1()). */
        char ebuf[512];
        snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                 sqlite3_errmsg(g_pool[slot]));
        fprintf(stderr, "[sqlite_query] prepare error: %s\n", sqlite3_errmsg(g_pool[slot]));
        if (stmt) sqlite3_finalize(stmt);
        free(sql); if (fmt) free(fmt);
        if (params_owned) H->free_node(params);
        H->set_ret_str(ebuf);
        return 1;
    }
    if (params) {
        rc = bind_params_from_head(stmt, params);
        if (params_owned) H->free_node(params);
        if (rc != SQLITE_OK) {
            char ebuf[512];
            snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                     sqlite3_errmsg(g_pool[slot]));
            fprintf(stderr, "[sqlite_query] bind error: %s\n", sqlite3_errmsg(g_pool[slot]));
            sqlite3_finalize(stmt);
            free(sql); if (fmt) free(fmt);
            H->set_ret_str(ebuf);
            return 1;
        }
    }
    int ncols = sqlite3_column_count(stmt);

    SB b = {0};
    if (as_xml) sb_puts(&b, "<result>");
    else        sb_putc(&b, '[');

    int row_n = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (as_xml) {
            sb_puts(&b, "<row>");
            for (int i = 0; i < ncols; i++) {
                const char *name = sqlite3_column_name(stmt, i);
                if (!name) name = "col";
                sb_putc(&b, '<'); sb_puts(&b, name); sb_putc(&b, '>');
                int t = sqlite3_column_type(stmt, i);
                if (t != SQLITE_NULL) {
                    const unsigned char *v = sqlite3_column_text(stmt, i);
                    sb_put_xml_str(&b, (const char *)v);
                }
                sb_puts(&b, "</"); sb_puts(&b, name); sb_putc(&b, '>');
            }
            sb_puts(&b, "</row>");
        } else {
            if (row_n > 0) sb_putc(&b, ',');
            sb_putc(&b, '{');
            for (int i = 0; i < ncols; i++) {
                if (i > 0) sb_putc(&b, ',');
                sb_putc(&b, '"');
                sb_put_json_str(&b, sqlite3_column_name(stmt, i));
                sb_puts(&b, "\":");
                int t = sqlite3_column_type(stmt, i);
                switch (t) {
                    case SQLITE_NULL:
                        sb_puts(&b, "null");
                        break;
                    case SQLITE_INTEGER: {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%lld",
                                 (long long)sqlite3_column_int64(stmt, i));
                        sb_puts(&b, buf);
                        break;
                    }
                    case SQLITE_FLOAT: {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.17g",
                                 sqlite3_column_double(stmt, i));
                        sb_puts(&b, buf);
                        break;
                    }
                    case SQLITE_BLOB:
                        /* Serializa BLOB como string vacío por ahora; el caller
                         * puede usar hex() en SQL si necesita el contenido. */
                        sb_puts(&b, "\"\"");
                        break;
                    case SQLITE_TEXT:
                    default: {
                        sb_putc(&b, '"');
                        sb_put_json_str(&b, (const char *)sqlite3_column_text(stmt, i));
                        sb_putc(&b, '"');
                        break;
                    }
                }
            }
            sb_putc(&b, '}');
        }
        row_n++;
    }
    if (rc != SQLITE_DONE) {
        /* Step fallo a mitad de la iteracion: en vez de devolver filas
         * parciales + silencio, reportamos el error para no quedarnos con
         * datos a medias (consistente con mysql/postgres). */
        char ebuf[512];
        snprintf(ebuf, sizeof(ebuf), "{\"error\":\"%s\"}",
                 sqlite3_errmsg(g_pool[slot]));
        fprintf(stderr, "[sqlite_query] step error: %s\n", sqlite3_errmsg(g_pool[slot]));
        sqlite3_finalize(stmt);
        if (b.p) free(b.p);
        free(sql); if (fmt) free(fmt);
        H->set_ret_str(ebuf);
        return 1;
    }
    sqlite3_finalize(stmt);

    if (as_xml) sb_puts(&b, "</result>");
    else        sb_putc(&b, ']');

    /* OOM: en vez de devolver JSON/XML truncado en silencio, reportamos un
     * error explícito (consistente con los bridges mysql/postgres/sqlserver). */
    if (b.oom) {
        if (b.p) free(b.p);
        H->set_ret_str("{\"error\":\"memory_allocation_failed\"}");
        free(sql);
        if (fmt) free(fmt);
        return 1;
    }

    H->set_ret_str(b.p ? b.p : (as_xml ? "<result/>" : "[]"));
    if (b.p) free(b.p);
    free(sql);
    if (fmt) free(fmt);
    return 1;
}

/* ─── entry point ──────────────────────────────────────────────────── */

#ifdef _WIN32
__declspec(dllexport)
#endif
void te_module_register(const TEHostAPI *host) {
    if (!host) return;
    if (host->abi_version < TE_HOST_API_VERSION) {
        fprintf(stderr, "[libte_sqlite] ABI mismatch: host=%d, plugin requires >= %d\n",
                host->abi_version, TE_HOST_API_VERSION);
        return;
    }
    /* Nivel 2.1: guard append-only contra drift de layout. Si el host fue
     * recompilado con un TEHostAPI de tamaño distinto al que vio este plugin,
     * los punteros de la API quedarían desalineados y sqlite_* devolvería []
     * en silencio. Rechazamos RUIDOSAMENTE y pedimos recompilar el plugin. */
    if (host->struct_size != 0 && host->struct_size != (int)sizeof(TEHostAPI)) {
        fprintf(stderr,
            "[libte_sqlite] ABI layout mismatch: host TEHostAPI=%d bytes, "
            "plugin=%d bytes. Recompilá el plugin contra este binario "
            "(plugins/sqlite) antes de usar sqlite_*.\n",
            host->struct_size, (int)sizeof(TEHostAPI));
        return;
    }
    g_host = *host;   /* copia la API por valor (el original vive en el stack del host) */
    host->register_builtin("sqlite_connect", te_sqlite_connect);
    host->register_builtin("sqlite_exec",    te_sqlite_exec);
    host->register_builtin("sqlite_query",   te_sqlite_query);
    host->register_builtin("sqlite_last_id", te_sqlite_last_id);
    host->register_builtin("sqlite_close",   te_sqlite_close);
    /* ABI v3: auto-cierre de conexiones request-scoped al final de cada request. */
    if (host->register_request_cleanup)
        host->register_request_cleanup(sqlite_close_request_conns);
    fprintf(stderr,
        "[libte_sqlite] registered: sqlite_connect, sqlite_exec, sqlite_query, "
        "sqlite_last_id, sqlite_close\n");
}
