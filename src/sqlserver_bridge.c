#include "sqlserver_bridge.h"
#include "db_params.h"
#include "typeeasy_http.h"   /* typeeasy_http_set_status() para strict-errors */
#include <sybfront.h>
#include <sybdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <process.h>
#define TE_GETPID _getpid
#else
#include <unistd.h>
#define TE_GETPID getpid
#endif

/* setenv portable (MinGW no expone setenv). */
static void mssql_setenv(const char* k, const char* v) {
#ifdef _WIN32
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s=%s", k, v ? v : "");
    _putenv(buf);
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}

#define MSSQL_POOL_SIZE 10
static DBPROCESS* mssql_connections[MSSQL_POOL_SIZE] = {NULL};

/* ---- Captura del motivo real del fallo de conexión (db-lib handlers) ----
 * Sin handlers, FreeTDS imprime los errores con su propio formato a stderr y el
 * intérprete solo veía "dbopen() falló". Con estos handlers capturamos el texto
 * exacto (handshake TLS, "Login failed for user", servidor inalcanzable, etc.)
 * para reportarlo en stderr con prefijo [SQLServer] y dejarlo en la variable de
 * script __sqlserver_error__ accesible desde el .te. */
static char g_mssql_last_err[1024] = {0};
static char g_mssql_last_msg[1024] = {0};

static int mssql_err_handler(DBPROCESS* dbproc, int severity, int dberr,
                             int oserr, char* dberrstr, char* oserrstr) {
    (void)dbproc; (void)severity; (void)dberr; (void)oserr;
    if (dberrstr) {
        if (oserrstr && *oserrstr)
            snprintf(g_mssql_last_err, sizeof(g_mssql_last_err), "%s (os: %s)", dberrstr, oserrstr);
        else
            snprintf(g_mssql_last_err, sizeof(g_mssql_last_err), "%s", dberrstr);
    }
    return INT_CANCEL;
}

static int mssql_msg_handler(DBPROCESS* dbproc, DBINT msgno, int msgstate,
                             int severity, char* msgtext, char* srvname,
                             char* procname, int line) {
    (void)dbproc; (void)msgstate; (void)srvname; (void)procname; (void)line;
    /* Mensajes informativos del servidor (severity 0) se ignoran; los de error
     * (login failed = 18456, etc.) se capturan. */
    if (msgtext && severity > 0)
        snprintf(g_mssql_last_msg, sizeof(g_mssql_last_msg), "%s (msg %ld)", msgtext, (long)msgno);
    return 0;
}

/* Construye un mensaje de causa combinando msg de servidor + error de db-lib. */
static const char* mssql_failure_cause(void) {
    static char cause[2200];
    if (g_mssql_last_msg[0] && g_mssql_last_err[0])
        snprintf(cause, sizeof(cause), "%s | %s", g_mssql_last_msg, g_mssql_last_err);
    else if (g_mssql_last_msg[0])
        snprintf(cause, sizeof(cause), "%s", g_mssql_last_msg);
    else if (g_mssql_last_err[0])
        snprintf(cause, sizeof(cause), "%s", g_mssql_last_err);
    else
        snprintf(cause, sizeof(cause), "unknown (sin detalle de FreeTDS)");
    return cause;
}

/* Publica la causa del fallo en stderr y en la variable de script
 * __sqlserver_error__ para que el .te pueda inspeccionarla. */
static void mssql_report_failure(const char* where) {
    const char* cause = mssql_failure_cause();
    fprintf(stderr, "[SQLServer] %s: %s\n", where, cause);
    ASTNode* e = create_ast_leaf("STRING", 0, strdup(cause), NULL);
    add_or_update_variable("__sqlserver_error__", e); free_ast(e);
}

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
    /* Las listas de argumentos se encadenan por ->next (ABI v0.0.12+).
     * Fallback a ->right por compat con listas construidas a mano. */
    for (int i = 0; i < index && current; i++)
        current = current->next ? current->next : current->right;
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
    for (int i = 0; i < index && current; i++)
        current = current->next ? current->next : current->right;
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

/* Resuelve el directorio temporal del sistema (Linux: TMPDIR|/tmp; Win: TEMP). */
static const char* mssql_tmpdir(void) {
    const char* t = getenv("TMPDIR");
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
    if (!t || !*t) t = "/tmp";
    return t;
}

/* Escribe un freetds.conf temporal con una sección [alias] para esta conexión y
 * apunta FREETDSCONF a él. Esto permite controlar TLS por conexión (encrypt /
 * ca cert) sin que el usuario tenga que editar /etc/freetds/freetds.conf.
 *   encrypt_mode: 0=off, 1=request(default), 2=require
 *   ca_path: si !=NULL, FreeTDS valida el cert contra ese PEM (sin él, acepta
 *            cualquier cert = TrustServerCertificate=True).
 * Devuelve 1 y escribe el alias en server_out; 0 si falla. */
static int mssql_apply_tls_conf(const char* host, int port, int encrypt_mode,
                                const char* ca_path, int slot,
                                char* server_out, size_t server_sz) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.typeeasy_freetds_%d_%d.conf",
             mssql_tmpdir(), (int)TE_GETPID(), slot);
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[SQLServer] no se pudo crear conf temporal '%s'\n", path);
        return 0;
    }
    snprintf(server_out, server_sz, "typeeasy_mssql_%d", slot);
    fprintf(f, "[%s]\n", server_out);
    fprintf(f, "\thost = %s\n", host);
    fprintf(f, "\tport = %d\n", port);
    fprintf(f, "\ttds version = 7.4\n");
    if (encrypt_mode == 0)      fprintf(f, "\tencrypt = off\n");
    else if (encrypt_mode == 2) fprintf(f, "\tencrypt = require\n");
    /* encrypt_mode==1 -> default de FreeTDS (request); no se escribe la clave. */
    if (ca_path && *ca_path)    fprintf(f, "\tca cert = %s\n", ca_path);
    fclose(f);
    mssql_setenv("FREETDSCONF", path);
    return 1;
}

/* native_sqlserver_connect(host, user, password, database, [port=1433], [opts])
 *   opts (mapa, opcional): controla TLS, equivalente a la cadena de conexión
 *   "Encrypt=...;TrustServerCertificate=...":
 *     - tls | ssl | encrypt : 1/true/"require"/"on" -> exige TLS (encrypt=require);
 *                             0/"off" -> deshabilita cifrado (encrypt=off).
 *     - tls_skip_verify | tls_insecure | tls_no_verify | insecure |
 *       trust_cert | trust_server_certificate : 1/true -> acepta cert
 *       self-signed sin validar (= TrustServerCertificate=yes / sqlcmd -C).
 *     - tls_ca | ssl_ca | ca : ruta a un PEM con la CA para validar la cadena.
 *     - tls_fp : NO soportado por FreeTDS; se ignora (se trata como skip_verify).
 *   Devuelve conn_id (>=0) o -1 en error. En error deja la causa real en la
 *   variable de script __sqlserver_error__. */
void native_sqlserver_connect(ASTNode* args) {
    const char* host = ms_arg_str(args, 0);
    const char* user = ms_arg_str(args, 1);
    const char* pass = ms_arg_str(args, 2);
    const char* db   = ms_arg_str(args, 3);
    int port = ms_arg_int(args, 4);
    if (port <= 0) port = 1433;

    /* ---- 6º argumento opcional: mapa de opciones TLS ---- */
    int opt_tls = -1;            /* -1 = no especificado */
    int opt_skip_verify = 0;     /* 1 = aceptar cualquier cert (trust) */
    const char* opt_ca = NULL;   /* ruta PEM de la CA */
    int has_opts = 0;
    int opts_owned = 0;
    ASTNode* opts_head = db_arg_as_map_head(args, 5, &opts_owned);
    for (ASTNode* p = opts_head; p; p = p->right) {
        if (!p->id || !p->left) continue;
        has_opts = 1;
        const char* k = p->id;
        ASTNode* v = p->left;
        const char* vt = v->type ? v->type : "";
        const char* v_str = (strcmp(vt, "STRING") == 0) ? v->str_value : NULL;
        long v_num = v->value;
        int v_is_str = (strcmp(vt, "STRING") == 0 && v->str_value);
        int v_is_num = (strcmp(vt, "NUMBER") == 0 || strcmp(vt, "INT") == 0);
        if ((strcmp(vt, "IDENTIFIER") == 0 || strcmp(vt, "ID") == 0) && v->id) {
            Variable* rv = find_variable(v->id);
            if (rv) {
                if (rv->vtype == VAL_STRING) { v_str = rv->value.string_value; v_is_str = (v_str != NULL); v_is_num = 0; }
                else if (rv->vtype == VAL_INT) { v_num = rv->value.int_value; v_is_num = 1; v_is_str = 0; }
                else if (rv->vtype == VAL_FLOAT) { v_num = (long)rv->value.float_value; v_is_num = 1; v_is_str = 0; }
            }
        }
        int truthy = 0;
        if (v_is_num) truthy = (v_num != 0);
        else if (v_is_str && v_str)
            truthy = (strcmp(v_str, "1") == 0 || strcmp(v_str, "true") == 0 ||
                      strcmp(v_str, "on") == 0 || strcmp(v_str, "require") == 0 ||
                      strcmp(v_str, "yes") == 0 || strcmp(v_str, "strict") == 0);

        if (strcmp(k, "tls") == 0 || strcmp(k, "ssl") == 0 || strcmp(k, "encrypt") == 0) {
            if (v_is_str && v_str && (strcmp(v_str, "off") == 0 || strcmp(v_str, "no") == 0 ||
                                      strcmp(v_str, "false") == 0 || strcmp(v_str, "0") == 0))
                opt_tls = 0;
            else opt_tls = truthy ? 1 : 0;
        } else if (strcmp(k, "tls_skip_verify") == 0 || strcmp(k, "tls_insecure") == 0 ||
                   strcmp(k, "tls_no_verify") == 0 || strcmp(k, "insecure") == 0 ||
                   strcmp(k, "trust_cert") == 0 || strcmp(k, "trust_server_certificate") == 0) {
            opt_skip_verify = truthy ? 1 : 0;
        } else if (strcmp(k, "tls_ca") == 0 || strcmp(k, "ssl_ca") == 0 || strcmp(k, "ca") == 0) {
            if (v_is_str && v_str) opt_ca = v_str;
        } else if (strcmp(k, "tls_fp") == 0 || strcmp(k, "tls_peer_fp") == 0 || strcmp(k, "fingerprint") == 0) {
            fprintf(stderr, "[SQLServer] aviso: 'tls_fp' no está soportado por FreeTDS; "
                            "use 'tls_ca' (validar contra CA) o 'tls_skip_verify' (aceptar self-signed).\n");
            opt_skip_verify = 1;  /* mejor esfuerzo: cifrar y aceptar el cert */
        }
    }
    if (opts_owned && opts_head) { free_ast(opts_head); opts_head = NULL; }

    /* Una CA implica TLS. skip_verify implica TLS. */
    int encrypt_mode;  /* 0=off, 1=request(default), 2=require */
    if (opt_tls == 0)               encrypt_mode = 0;
    else if (opt_tls == 1 || opt_skip_verify || (opt_ca && *opt_ca)) encrypt_mode = 2;
    else                            encrypt_mode = 1;
    /* Si el usuario pidió validar contra una CA, NO saltamos verificación. */
    const char* ca_path = (opt_ca && *opt_ca) ? opt_ca : NULL;

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
        /* Handlers para capturar el motivo real del fallo (TLS / login). */
        dberrhandle(mssql_err_handler);
        dbmsghandle(mssql_msg_handler);
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

    /* Limpiar capturas de fallos previos antes de intentar conectar. */
    g_mssql_last_err[0] = '\0';
    g_mssql_last_msg[0] = '\0';

    LOGINREC* login = dblogin();
    if (!login) {
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }
    DBSETLUSER(login, user);
    DBSETLPWD(login, pass ? pass : "");
    DBSETLAPP(login, "typeeasy");

    /* Selección del nombre de servidor para dbopen:
     *  - Sin opciones TLS: comportamiento histórico (host:port directo), respeta
     *    cualquier /etc/freetds.conf del usuario. Sin regresión.
     *  - Con opciones TLS: escribimos un freetds.conf temporal con encrypt/ca y
     *    pasamos el alias, dándonos control total de TLS por conexión. */
    char server[512];
    if (has_opts) {
        if (!mssql_apply_tls_conf(host, port, encrypt_mode, ca_path, slot, server, sizeof(server))) {
            dbloginfree(login);
            mssql_report_failure("conf TLS");
            ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
            add_or_update_variable("__ret__", r); free_ast(r);
            return;
        }
    } else {
        snprintf(server, sizeof(server), "%s:%d", host, port);
    }

    DBPROCESS* dbproc = dbopen(login, server);
    dbloginfree(login);
    if (!dbproc) {
        char where[160];
        snprintf(where, sizeof(where), "dbopen() falló (host=%s:%d, encrypt=%s)",
                 host, port, encrypt_mode == 0 ? "off" : (encrypt_mode == 2 ? "require" : "request"));
        mssql_report_failure(where);
        ASTNode* r = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", r); free_ast(r);
        return;
    }
    if (dbuse(dbproc, (char*)db) == FAIL) {
        char where[160];
        snprintf(where, sizeof(where), "dbuse(%s) falló", db);
        mssql_report_failure(where);
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
        /* Strict mode solo aplica en --api; en CLI es no-op. */
        { extern int g_api_mode; if (g_db_strict_errors && g_api_mode) typeeasy_http_set_status(500); }
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
