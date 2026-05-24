/*
 * libte_sqlite — Plugin nativo de SQLite para TypeEasy.
 *
 * Builtins expuestos:
 *   sqlite_connect("path/to.db" | ":memory:")      -> int slot   (-1 en error)
 *   sqlite_exec(slot, "INSERT ... ; UPDATE ...")    -> int rows_affected (sum) o -1
 *   sqlite_query(slot, "SELECT ...", "json"|"xml") -> string en __ret__
 *   sqlite_last_id(slot)                            -> int last_insert_rowid
 *   sqlite_close(slot)                              -> int 0
 *
 * Uso desde un script .te:
 *   load_native("sqlite");
 *   let db = sqlite_connect("game.db");
 *   sqlite_exec(db, "CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, name TEXT)");
 *   sqlite_exec(db, "INSERT INTO t(name) VALUES('alice')");
 *   let rows = sqlite_query(db, "SELECT id, name FROM t", "json");
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
}

static ASTNode *arg_at(ASTNode *args, int n) {
    ASTNode *p = args;
    for (int i = 0; p && i < n; i++) p = p->right;
    return p;
}

/* ─── string buffer ────────────────────────────────────────────────── */

typedef struct { char *p; size_t len, cap; } SB;

static int sb_reserve(SB *b, size_t add) {
    if (b->len + add + 1 <= b->cap) return 0;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < b->len + add + 1) nc *= 2;
    char *np = (char *)realloc(b->p, nc);
    if (!np) return -1;
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
    char *fmt  = H->arg_string(arg_at(args, 2));

    int as_xml = (fmt && (strcmp(fmt, "xml") == 0 || strcmp(fmt, "XML") == 0));

    if (slot < 0 || slot >= POOL_SIZE || !g_pool[slot]) {
        fprintf(stderr, "[sqlite_query] invalid connection slot\n");
        if (sql) free(sql);
        if (fmt) free(fmt);
        H->set_ret_str(as_xml ? "<result/>" : "[]");
        return 1;
    }
    if (!sql || !*sql) {
        if (sql) free(sql);
        if (fmt) free(fmt);
        H->set_ret_str(as_xml ? "<result/>" : "[]");
        return 1;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_pool[slot], sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[sqlite_query] prepare error: %s\n", sqlite3_errmsg(g_pool[slot]));
        if (stmt) sqlite3_finalize(stmt);
        free(sql); if (fmt) free(fmt);
        H->set_ret_str(as_xml ? "<result/>" : "[]");
        return 1;
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
        fprintf(stderr, "[sqlite_query] step error: %s\n", sqlite3_errmsg(g_pool[slot]));
    }
    sqlite3_finalize(stmt);

    if (as_xml) sb_puts(&b, "</result>");
    else        sb_putc(&b, ']');

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
    if (host->abi_version != TE_HOST_API_VERSION) {
        fprintf(stderr, "[libte_sqlite] ABI mismatch: host=%d plugin=%d\n",
                host->abi_version, TE_HOST_API_VERSION);
        return;
    }
    g_host = *host;   /* copia la API por valor (el original vive en el stack del host) */
    host->register_builtin("sqlite_connect", te_sqlite_connect);
    host->register_builtin("sqlite_exec",    te_sqlite_exec);
    host->register_builtin("sqlite_query",   te_sqlite_query);
    host->register_builtin("sqlite_last_id", te_sqlite_last_id);
    host->register_builtin("sqlite_close",   te_sqlite_close);
    fprintf(stderr,
        "[libte_sqlite] registered: sqlite_connect, sqlite_exec, sqlite_query, "
        "sqlite_last_id, sqlite_close\n");
}
