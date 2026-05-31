#include "postgres_bridge.h"
#include "db_params.h"
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_POOL_SIZE 10
static PGconn* pg_connections[PG_POOL_SIZE] = {NULL};

extern ASTNode* create_ast_leaf(char *type, int value, char *str_value, char *id);
extern void free_ast(ASTNode *node);

/* Escape para db_substitute_params (estilo Dapper) */
static char* pg_escape_cb(const char* in, void* ctx) {
    PGconn* c = (PGconn*)ctx;
    size_t inl = in ? strlen(in) : 0;
    char* out = (char*)malloc(inl * 2 + 1);
    if (!out) return NULL;
    int err = 0;
    PQescapeStringConn(c, out, in ? in : "", inl, &err);
    return out;
}

/* ---- helpers para leer argumentos (copiados de mysql_bridge) ---- */
static const char* pg_arg_str(ASTNode* args, int index) {
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

static int pg_arg_int(ASTNode* args, int index) {
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

/* JSON-string-escape mínimo: backslash, comilla y control chars. */
static void json_escape_into(const char* in, char* out, size_t out_size) {
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

static void xml_escape_into(const char* in, char* out, size_t out_size) {
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

/* libpq oid → numeric flag (matches PG type OIDs in <catalog/pg_type_d.h>) */
static int pg_oid_is_numeric(Oid t) {
    switch ((int)t) {
        case 20: case 21: case 23:    /* int8, int2, int4 */
        case 700: case 701:            /* float4, float8 */
        case 1700:                     /* numeric */
        case 26: case 16:              /* oid, bool (treat bool as 0/1) */
            return 1;
        default: return 0;
    }
}

/* native_postgres_connect(host, user, password, database, [port]) → conn_id en __ret__ */
void native_postgres_connect(ASTNode* args) {
    const char* host = pg_arg_str(args, 0);
    const char* user = pg_arg_str(args, 1);
    const char* pass = pg_arg_str(args, 2);
    const char* db   = pg_arg_str(args, 3);
    int port = pg_arg_int(args, 4);
    if (port <= 0) port = 5432;

    if (!host || !user || !db) {
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    int slot = -1;
    for (int i = 0; i < PG_POOL_SIZE; i++) if (!pg_connections[i]) { slot = i; break; }
    if (slot < 0) {
        fprintf(stderr, "[Postgres] Pool lleno\n");
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    char conninfo[1024];
    snprintf(conninfo, sizeof(conninfo),
        "host=%s port=%d user=%s password=%s dbname=%s connect_timeout=10",
        host, port, user, pass ? pass : "", db);

    PGconn* conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "[Postgres] Error de conexión: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }

    pg_connections[slot] = conn;
    printf("[Postgres] Connection successful (ID: %d)\n", slot); fflush(stdout);
    ASTNode* r = create_ast_leaf("NUMBER", slot, NULL, NULL);
    add_or_update_variable("__ret__", r); free_ast(r);
}

/* native_postgres_query(conn_id, query, [params_map], [format=json|xml]) → string en __ret__ */
void native_postgres_query(ASTNode* args) {
    int conn_id = pg_arg_int(args, 0);
    const char* query = pg_arg_str(args, 1);

    /* Estilo Dapper: arg #2 puede ser un MAP de params */
    int params_owned = 0;
    ASTNode* params_head = db_arg_as_map_head(args, 2, &params_owned);
    const char* format = params_head ? pg_arg_str(args, 3) : pg_arg_str(args, 2);
    if (!format || !*format) format = "json";

    if (conn_id < 0 || conn_id >= PG_POOL_SIZE || !pg_connections[conn_id]) {
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

    PGconn* conn = pg_connections[conn_id];

    char* final_query = NULL;
    if (params_head) {
        final_query = db_substitute_params(query, params_head, pg_escape_cb, conn);
        query = final_query;
        if (params_owned) { free_ast(params_head); params_head = NULL; }
    }

    PGresult* res = PQexec(conn, query);
    ExecStatusType st = PQresultStatus(res);

    if (st == PGRES_COMMAND_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"affected_rows\":%s}", PQcmdTuples(res));
        PQclear(res);
        ASTNode* r = create_ast_leaf("STRING", 0, strdup(buf), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }
    if (st != PGRES_TUPLES_OK) {
        char err[512];
        snprintf(err, sizeof(err), "{\"error\":\"%s\"}", PQerrorMessage(conn));
        PQclear(res);
        ASTNode* r = create_ast_leaf("STRING", 0, strdup(err), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }

    int nrows = PQntuples(res);
    int nfields = PQnfields(res);
    size_t cap = 2 * 1024 * 1024;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        PQclear(res);
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"oom\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }
    int off = 0;
    int is_xml = (strcmp(format, "xml") == 0);

    if (is_xml) {
        off += snprintf(buf + off, cap - off,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
    } else {
        off += snprintf(buf + off, cap - off, "[");
    }

    for (int r = 0; r < nrows; r++) {
        if ((size_t)off > cap - 8192) {
            snprintf(buf, cap, is_xml
                ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n  <error>Result too large</error>\n</rows>"
                : "{\"error\":\"Result too large\"}");
            off = (int)strlen(buf);
            break;
        }
        if (is_xml) off += snprintf(buf + off, cap - off, "  <row>\n");
        else { if (r > 0) off += snprintf(buf + off, cap - off, ","); off += snprintf(buf + off, cap - off, "{"); }

        for (int f = 0; f < nfields; f++) {
            const char* name = PQfname(res, f);
            int isnull = PQgetisnull(res, r, f);
            const char* val = isnull ? "" : PQgetvalue(res, r, f);
            Oid t = PQftype(res, f);
            int numeric = pg_oid_is_numeric(t);

            if (is_xml) {
                off += snprintf(buf + off, cap - off, "    <%s>", name);
                if (!isnull) {
                    char esc[8192];
                    xml_escape_into(val, esc, sizeof(esc));
                    off += snprintf(buf + off, cap - off, "%s", esc);
                }
                off += snprintf(buf + off, cap - off, "</%s>\n", name);
            } else {
                if (f > 0) off += snprintf(buf + off, cap - off, ",");
                off += snprintf(buf + off, cap - off, "\"%s\":", name);
                if (isnull) off += snprintf(buf + off, cap - off, "null");
                else if (numeric) off += snprintf(buf + off, cap - off, "%s", val);
                else {
                    char esc[8192];
                    json_escape_into(val, esc, sizeof(esc));
                    off += snprintf(buf + off, cap - off, "\"%s\"", esc);
                }
            }
        }
        if (is_xml) off += snprintf(buf + off, cap - off, "  </row>\n");
        else off += snprintf(buf + off, cap - off, "}");
    }

    if (is_xml) off += snprintf(buf + off, cap - off, "</rows>");
    else off += snprintf(buf + off, cap - off, "]");

    PQclear(res);
    ASTNode* ret = create_ast_leaf("STRING", 0, buf, NULL);
    add_or_update_variable("__ret__", ret); free_ast(ret);
    free(buf);
    if (final_query) free(final_query);
}

/* native_postgres_close(conn_id) */
void native_postgres_close(ASTNode* args) {
    int conn_id = pg_arg_int(args, 0);
    if (conn_id < 0 || conn_id >= PG_POOL_SIZE || !pg_connections[conn_id]) {
        fprintf(stderr, "[Postgres] Conexión inválida (ID: %d)\n", conn_id);
        return;
    }
    PQfinish(pg_connections[conn_id]);
    pg_connections[conn_id] = NULL;
    fprintf(stderr, "[Postgres] Conexión cerrada (ID: %d)\n", conn_id);
}
