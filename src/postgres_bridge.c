#include "postgres_bridge.h"
#include "db_params.h"
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

/* ---- Buffer dinámico seguro para serializar result-sets (fix Exit 139) ----
 * Reemplaza el viejo buffer fijo de 2 MB con `off += snprintf(...)`, que era
 * inseguro: snprintf devuelve la longitud que *habría* escrito (sin truncar),
 * así que con columnas TEXT grandes el offset podía superar el buffer a mitad
 * de fila → `buf+off` fuera de rango y `cap-off` (size_t) en underflow →
 * escritura OOB → corrupción de heap. Este SB crece con realloc y marca `oom`
 * si falla, permitiendo devolver un error en vez de caer. */
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

/* Escape JSON (RFC 8259) con longitud explícita para que json_parse no se
 * rompa al copiar TEXT con comillas, backslashes o saltos de línea. */
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
    int is_xml = (strcmp(format, "xml") == 0);

    /* Serialización con buffer dinámico seguro (fix segfault): sin tope de 2 MB
     * ni "Result too large"; si la memoria falla devolvemos un error. */
    SB sb; sb_init(&sb);

    if (is_xml) sb_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
    else        sb_putc(&sb, '[');

    for (int r = 0; r < nrows; r++) {
        if (is_xml) sb_puts(&sb, "  <row>\n");
        else { if (r > 0) sb_putc(&sb, ','); sb_putc(&sb, '{'); }

        for (int f = 0; f < nfields; f++) {
            const char* name = PQfname(res, f);
            int isnull = PQgetisnull(res, r, f);
            const char* val = isnull ? "" : PQgetvalue(res, r, f);
            Oid t = PQftype(res, f);
            int numeric = pg_oid_is_numeric(t);

            if (is_xml) {
                sb_puts(&sb, "    <"); sb_puts(&sb, name); sb_putc(&sb, '>');
                if (!isnull) sb_put_xml_escaped_n(&sb, val, strlen(val));
                sb_puts(&sb, "</"); sb_puts(&sb, name); sb_puts(&sb, ">\n");
            } else {
                if (f > 0) sb_putc(&sb, ',');
                sb_putc(&sb, '"');
                sb_put_json_escaped_n(&sb, name, strlen(name));
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
    }

    if (is_xml) sb_puts(&sb, "</rows>");
    else sb_putc(&sb, ']');

    PQclear(res);

    if (sb.oom) {
        free(sb.p);
        ASTNode* r = create_ast_leaf("STRING", 0, strdup("{\"error\":\"memory_allocation_failed\"}"), NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        if (final_query) free(final_query);
        return;
    }

    sb.p[sb.len] = '\0';
    ASTNode* ret = create_ast_leaf("STRING", 0, sb.p, NULL);
    add_or_update_variable("__ret__", ret); free_ast(ret);
    free(sb.p);
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
