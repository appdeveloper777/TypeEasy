#include "sqlserver_bridge.h"
#include "db_params.h"
#include <sybfront.h>
#include <sybdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSSQL_POOL_SIZE 10
static DBPROCESS* mssql_connections[MSSQL_POOL_SIZE] = {NULL};
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

static void ms_json_escape(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 8 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c < 0x20) { o += snprintf(out + o, out_size - o, "\\u%04x", c); }
        else out[o++] = c;
    }
    out[o] = '\0';
}

static void ms_xml_escape(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 8 < out_size; i++) {
        char c = in[i];
        switch (c) {
            case '&':  if (o + 5 < out_size) { strcpy(out + o, "&amp;");  o += 5; } break;
            case '<':  if (o + 4 < out_size) { strcpy(out + o, "&lt;");   o += 4; } break;
            case '>':  if (o + 4 < out_size) { strcpy(out + o, "&gt;");   o += 4; } break;
            case '"':  if (o + 6 < out_size) { strcpy(out + o, "&quot;"); o += 6; } break;
            case '\'': if (o + 6 < out_size) { strcpy(out + o, "&apos;"); o += 6; } break;
            default:   out[o++] = c;
        }
    }
    out[o] = '\0';
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
    printf("[SQLServer] Conexión exitosa (ID: %d)\n", slot); fflush(stdout);
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

    size_t cap = 2 * 1024 * 1024;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"oom\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }
    int off = 0;
    int first_result_set = 1;
    int had_result_set = 0;
    long long affected_total = 0;

    if (is_xml) off += snprintf(buf + off, cap - off,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
    else off += snprintf(buf + off, cap - off, "[");

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
            if ((size_t)off > cap - 8192) {
                snprintf(buf, cap, is_xml
                    ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n  <error>Result too large</error>\n</rows>"
                    : "{\"error\":\"Result too large\"}");
                off = (int)strlen(buf);
                free(cols);
                goto done;
            }
            if (is_xml) off += snprintf(buf + off, cap - off, "  <row>\n");
            else { if (row_idx > 0) off += snprintf(buf + off, cap - off, ","); off += snprintf(buf + off, cap - off, "{"); }

            for (int c = 0; c < ncols; c++) {
                int isnull = (cols[c].len == -1);
                const char* val = isnull ? "" : cols[c].data;
                int numeric = ms_type_is_numeric(cols[c].type);

                if (is_xml) {
                    off += snprintf(buf + off, cap - off, "    <%s>", cols[c].name);
                    if (!isnull) {
                        char esc[8192];
                        ms_xml_escape(val, esc, sizeof(esc));
                        off += snprintf(buf + off, cap - off, "%s", esc);
                    }
                    off += snprintf(buf + off, cap - off, "</%s>\n", cols[c].name);
                } else {
                    if (c > 0) off += snprintf(buf + off, cap - off, ",");
                    off += snprintf(buf + off, cap - off, "\"%s\":", cols[c].name);
                    if (isnull) off += snprintf(buf + off, cap - off, "null");
                    else if (numeric) off += snprintf(buf + off, cap - off, "%s", val);
                    else {
                        char esc[8192];
                        ms_json_escape(val, esc, sizeof(esc));
                        off += snprintf(buf + off, cap - off, "\"%s\"", esc);
                    }
                }
            }
            if (is_xml) off += snprintf(buf + off, cap - off, "  </row>\n");
            else off += snprintf(buf + off, cap - off, "}");
            row_idx++;
        }
        free(cols);
    }

done:
    if (is_xml) off += snprintf(buf + off, cap - off, "</rows>");
    else off += snprintf(buf + off, cap - off, "]");

    /* Si no hubo result-set (INSERT/UPDATE/DELETE puro), reemplazar por JSON de affected_rows */
    if (!had_result_set && !is_xml) {
        snprintf(buf, cap, "{\"affected_rows\":%lld}", affected_total);
        off = (int)strlen(buf);
    }

    ASTNode* ret = create_ast_leaf("STRING", 0, buf, NULL);
    add_or_update_variable("__ret__", ret); free_ast(ret);
    free(buf);
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
    fprintf(stderr, "[SQLServer] Conexión cerrada (ID: %d)\n", conn_id);
}
