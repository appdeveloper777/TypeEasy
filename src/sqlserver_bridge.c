#include "sqlserver_bridge.h"
#include "db_params.h"
#include <sybfront.h>
#include <sybdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MSSQL_POOL_SIZE 10
static DBPROCESS* mssql_connections[MSSQL_POOL_SIZE] = {NULL};

/* Auto-cleanup por request: ver mysql_bridge.c. Marca slots abiertos durante
 * un request para liberarlos al final si el script no llamó sqlserver_close(). */
extern int g_db_request_phase;
static int mssql_req_scoped[MSSQL_POOL_SIZE] = {0};
static int mssql_initialized = 0;

extern ASTNode* create_ast_leaf(char *type, int value, char *str_value, char *id);
extern void free_ast(ASTNode *node);

/* Escape para db_substitute_params: doble la comilla simple. SQL Server no
 * expone una API equivalente a mysql_real_escape_string en db-lib clásico, así
 * que hacemos el escape estándar `'` → `''`. (ctx ignorado.) */
static char* mssql_escape_cb(const char* in, void* ctx) {
    (void)ctx;
    if (!in) return strdup("");
    size_t inl = strlen(in);
    char* out = (char*)malloc(inl * 2 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < inl; i++) {
        if (in[i] == '\'') { out[o++] = '\''; out[o++] = '\''; }
        else out[o++] = in[i];
    }
    out[o] = '\0';
    return out;
}

/* ---- arg helpers (idénticos a postgres_bridge) ---- */
static const char* ms_arg_str(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) current = current->right;
    if (!current) return NULL;
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) return current->str_value;
    if (current->left && current->left->type &&
        strcmp(current->left->type, "STRING_LITERAL") == 0 && current->left->str_value)
        return current->left->str_value;
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        Variable* v = find_variable((char*)current->id);
        if (v && v->vtype == VAL_STRING) return v->value.string_value;
    }
    return NULL;
}

static int ms_arg_int(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) current = current->right;
    if (!current) return -1;
    if (current->type && strcmp(current->type, "NUMBER") == 0) return current->value;
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) {
        char* endptr = NULL;
        long val = strtol(current->str_value, &endptr, 10);
        if (endptr && *endptr == '\0') return (int)val;
        Variable* v = find_variable((char*)current->str_value);
        if (v && v->vtype == VAL_INT) return v->value.int_value;
    }
    if (current->left && current->left->type && strcmp(current->left->type, "NUMBER") == 0)
        return current->left->value;
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        Variable* v = find_variable(current->id);
        if (v && v->vtype == VAL_INT) return v->value.int_value;
    }
    return -1;
}

/* ---- Buffer dinámico seguro para serializar result-sets (fix Exit 139) ----
 * El buffer fijo de 2 MB con `off += snprintf(...)` desbordaba el heap cuando
 * una fila tenía varias columnas que sumaban más que el margen de 8 KB: snprintf
 * devuelve la longitud que *habría* escrito, así que `off` podía superar `cap` a
 * mitad de fila → `cap-off` (size_t) en underflow → escritura OOB → corrupción
 * de heap. Este SB crece con realloc y marca `oom` si falla. */
typedef struct { char *p; size_t len; size_t cap; int oom; } SB;

static void sb_init(SB *b) {
    b->cap = 65536; b->len = 0; b->oom = 0;
    b->p = (char*)malloc(b->cap);
    if (!b->p) { b->oom = 1; b->cap = 0; }
}
static int sb_reserve(SB *b, size_t extra) {
    if (b->oom) return 0;
    if (b->len + extra + 1 <= b->cap) return 1;
    size_t ncap = b->cap ? b->cap : 65536;
    while (ncap < b->len + extra + 1) {
        if (ncap > (SIZE_MAX / 2)) { b->oom = 1; return 0; }
        ncap *= 2;
    }
    char *np = (char*)realloc(b->p, ncap);
    if (!np) { b->oom = 1; return 0; }
    b->p = np; b->cap = ncap; return 1;
}
static void sb_putc(SB *b, char c) { if (sb_reserve(b, 1)) b->p[b->len++] = c; }
static void sb_putn(SB *b, const char *s, size_t n) {
    if (!n || !sb_reserve(b, n)) return;
    memcpy(b->p + b->len, s, n); b->len += n;
}
static void sb_puts(SB *b, const char *s) { if (s) sb_putn(b, s, strlen(s)); }

static void sb_put_json_escaped_n(SB *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  sb_putn(b, "\\\"", 2); break;
            case '\\': sb_putn(b, "\\\\", 2); break;
            case '\b': sb_putn(b, "\\b", 2); break;
            case '\f': sb_putn(b, "\\f", 2); break;
            case '\n': sb_putn(b, "\\n", 2); break;
            case '\r': sb_putn(b, "\\r", 2); break;
            case '\t': sb_putn(b, "\\t", 2); break;
            default:
                if (c < 0x20) { char u[8]; snprintf(u, sizeof u, "\\u%04x", c); sb_putn(b, u, 6); }
                else sb_putc(b, (char)c);
        }
    }
}
static void sb_put_xml_escaped_n(SB *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '&':  sb_putn(b, "&amp;", 5); break;
            case '<':  sb_putn(b, "&lt;", 4); break;
            case '>':  sb_putn(b, "&gt;", 4); break;
            case '"':  sb_putn(b, "&quot;", 6); break;
            case '\'': sb_putn(b, "&apos;", 6); break;
            default:   sb_putc(b, s[i]); break;
        }
    }
}

/* FreeTDS column types numéricos */
static int ms_type_is_numeric(int t) {
    switch (t) {
        case SYBINT1: case SYBINT2: case SYBINT4: case SYBINT8:
        case SYBFLT8: case SYBREAL:
        case SYBNUMERIC: case SYBDECIMAL:
        case SYBMONEY: case SYBMONEY4:
        case SYBBIT:
            return 1;
        default:
            return 0;
    }
}

/* native_sqlserver_connect(host, user, password, database, [port=1433]) → conn_id */
void native_sqlserver_connect(ASTNode* args) {
    const char* host = ms_arg_str(args, 0);
    const char* user = ms_arg_str(args, 1);
    const char* pass = ms_arg_str(args, 2);
    const char* db   = ms_arg_str(args, 3);
    int port = ms_arg_int(args, 4);
    if (port <= 0) port = 1433;

    if (!host || !user || !db) {
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    if (!mssql_initialized) {
        if (dbinit() == FAIL) {
            fprintf(stderr, "[SQLServer] dbinit() falló\n");
            ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
            add_or_update_variable("__ret__", r); free_ast(r);
            return;
        }
        mssql_initialized = 1;
    }

    int slot = -1;
    for (int i = 0; i < MSSQL_POOL_SIZE; i++) if (!mssql_connections[i]) { slot = i; break; }
    if (slot < 0) {
        fprintf(stderr, "[SQLServer] Pool lleno\n");
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    LOGINREC* login = dblogin();
    if (!login) {
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }
    DBSETLUSER(login, user);
    DBSETLPWD(login, pass ? pass : "");
    DBSETLAPP(login, "typeeasy");

    /* Servidor en formato host:port para FreeTDS */
    char server[512];
    snprintf(server, sizeof(server), "%s:%d", host, port);

    DBPROCESS* dbproc = dbopen(login, server);
    dbloginfree(login);
    if (!dbproc) {
        fprintf(stderr, "[SQLServer] dbopen() falló (server=%s)\n", server);
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }
    if (dbuse(dbproc, (char*)db) == FAIL) {
        fprintf(stderr, "[SQLServer] dbuse(%s) falló\n", db);
        dbclose(dbproc);
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    mssql_connections[slot] = dbproc;
    mssql_req_scoped[slot] = g_db_request_phase;
    printf("[SQLServer] Connection successful (ID: %d)\n", slot); fflush(stdout);
    ASTNode* r = create_ast_leaf("NUMBER", slot, NULL, NULL);
    add_or_update_variable("__ret__", r); free_ast(r);
}

/* native_sqlserver_query(conn_id, query, [params_map], [format=json|xml]) → string */
void native_sqlserver_query(ASTNode* args) {
    int conn_id = ms_arg_int(args, 0);
    const char* query = ms_arg_str(args, 1);
    int params_owned = 0;
    ASTNode* params_head = db_arg_as_map_head(args, 2, &params_owned);
    const char* format = params_head ? ms_arg_str(args, 3) : ms_arg_str(args, 2);
    if (!format || !*format) format = "json";
    int is_xml = (strcmp(format, "xml") == 0);

    if (conn_id < 0 || conn_id >= MSSQL_POOL_SIZE || !mssql_connections[conn_id]) {
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_connection\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (params_owned && params_head) free_ast(params_head);
        return;
    }
    if (!query) {
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_query\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (params_owned && params_head) free_ast(params_head);
        return;
    }

    DBPROCESS* db = mssql_connections[conn_id];

    char* final_query = NULL;
    if (params_head) {
        final_query = db_substitute_params(query, params_head, mssql_escape_cb, db);
        query = final_query;
        if (params_owned) { free_ast(params_head); params_head = NULL; }
    }

    if (dbcmd(db, (char*)query) == FAIL || dbsqlexec(db) == FAIL) {
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"query_failed\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }

    /* Serialización con buffer dinámico seguro (fix segfault): sin tope de 2 MB
     * ni "Result too large"; si la memoria falla devolvemos un error. */
    SB sb; sb_init(&sb);
    int first_result_set = 1;
    int had_result_set = 0;
    long long affected_total = 0;

    if (is_xml) sb_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
    else sb_putc(&sb, '[');

    /* Procesar todos los result-sets (normalmente uno) */
    RETCODE rc;
    while ((rc = dbresults(db)) != NO_MORE_RESULTS) {
        if (rc == FAIL) break;
        if (!first_result_set) { /* concatenar como filas extra */ }
        first_result_set = 0;

        int ncols = dbnumcols(db);
        if (ncols <= 0) {
            /* INSERT/UPDATE/DELETE: drenar y acumular affected rows */
            while (dbnextrow(db) != NO_MORE_ROWS) { /* no rows expected */ }
            DBINT n = dbcount(db);
            if (n >= 0) affected_total += n;
            continue;
        }
        had_result_set = 1;

        /* Bind cada columna a buffer string (CHARBIND) — máx 4096 chars/celda */
        typedef struct { char data[4096]; DBINT len; int type; const char* name; } Col;
        Col* cols = (Col*)calloc(ncols, sizeof(Col));
        if (!cols) break;
        for (int c = 0; c < ncols; c++) {
            cols[c].name = (const char*)dbcolname(db, c + 1);
            cols[c].type = dbcoltype(db, c + 1);
            dbbind(db, c + 1, NTBSTRINGBIND, sizeof(cols[c].data), (BYTE*)cols[c].data);
            dbnullbind(db, c + 1, &cols[c].len);
        }

        int row_idx = 0;
        while (dbnextrow(db) != NO_MORE_ROWS) {
            if (is_xml) sb_puts(&sb, "  <row>\n");
            else { if (row_idx > 0) sb_putc(&sb, ','); sb_putc(&sb, '{'); }

            for (int c = 0; c < ncols; c++) {
                int isnull = (cols[c].len == -1);
                const char* val = isnull ? "" : cols[c].data;
                int numeric = ms_type_is_numeric(cols[c].type);

                if (is_xml) {
                    sb_puts(&sb, "    <"); sb_puts(&sb, cols[c].name); sb_putc(&sb, '>');
                    if (!isnull) sb_put_xml_escaped_n(&sb, val, strlen(val));
                    sb_puts(&sb, "</"); sb_puts(&sb, cols[c].name); sb_puts(&sb, ">\n");
                } else {
                    if (c > 0) sb_putc(&sb, ',');
                    sb_putc(&sb, '"');
                    sb_put_json_escaped_n(&sb, cols[c].name, strlen(cols[c].name));
                    sb_puts(&sb, "\":");
                    if (isnull) sb_puts(&sb, "null");
                    else if (numeric) sb_puts(&sb, val);
                    else {
                        sb_putc(&sb, '"');
                        sb_put_json_escaped_n(&sb, val, strlen(val));
                        sb_putc(&sb, '"');
                    }
                }
            }
            if (is_xml) sb_puts(&sb, "  </row>\n");
            else sb_putc(&sb, '}');
            row_idx++;
        }
        free(cols);
    }

    if (is_xml) sb_puts(&sb, "</rows>");
    else sb_putc(&sb, ']');

    if (sb.oom) {
        free(sb.p);
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"memory_allocation_failed\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }

    /* Si no hubo result-set (INSERT/UPDATE/DELETE puro), devolver affected_rows */
    if (!had_result_set && !is_xml) {
        free(sb.p);
        char abuf[64];
        snprintf(abuf, sizeof(abuf), "{\"affected_rows\":%lld}", affected_total);
        ASTNode* ret = create_ast_leaf("STRING", 0, strdup(abuf), NULL);
        add_or_update_variable("__ret__", ret); free_ast(ret);
        if (final_query) free(final_query);
        return;
    }

    sb.p[sb.len] = '\0';
    ASTNode* ret = create_ast_leaf("STRING", 0, sb.p, NULL);
    add_or_update_variable("__ret__", ret); free_ast(ret);
    free(sb.p);
    if (final_query) free(final_query);
}

/* native_sqlserver_close(conn_id) */
void native_sqlserver_close(ASTNode* args) {
    int conn_id = ms_arg_int(args, 0);
    if (conn_id < 0 || conn_id >= MSSQL_POOL_SIZE || !mssql_connections[conn_id]) {
        fprintf(stderr, "[SQLServer] Conexión inválida (ID: %d)\n", conn_id);
        return;
    }
    dbclose(mssql_connections[conn_id]);
    mssql_connections[conn_id] = NULL;
    mssql_req_scoped[conn_id] = 0;
    fprintf(stderr, "[SQLServer] Conexión cerrada (ID: %d)\n", conn_id);
}

/* Cierre automático al final de cada request (ver mysql_bridge.c). Cierra solo
 * las conexiones abiertas durante este request que no se cerraron. */
void sqlserver_close_request_conns(void) {
    for (int i = 0; i < MSSQL_POOL_SIZE; i++) {
        if (!mssql_connections[i] || !mssql_req_scoped[i]) continue;
        dbclose(mssql_connections[i]);
        mssql_connections[i] = NULL;
        mssql_req_scoped[i] = 0;
    }
}
