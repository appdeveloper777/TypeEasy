#include "mysql_bridge.h"
#include "db_params.h"
#include "typeeasy_http.h"   /* typeeasy_http_set_status() para sql_set_strict_errors */
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdint.h>    /* intptr_t */
#include <time.h>      /* clock_gettime */

/* Escape callback para db_substitute_params (estilo Dapper). */
char* mysql_escape_cb(const char* in, void* ctx) {
    MYSQL* c = (MYSQL*)ctx;
    size_t inl = in ? strlen(in) : 0;
    char* out = (char*)malloc(inl * 2 + 1);
    if (!out) return NULL;
    mysql_real_escape_string(c, out, in ? in : "", inl);
    return out;
}

// Pool de conexiones MySQL (máximo 10 conexiones concurrentes)
#define MYSQL_POOL_SIZE 10
static MYSQL* connections[MYSQL_POOL_SIZE] = {NULL};
static int next_conn_id = 0;

/* Auto-cleanup por request: marca qué slots se abrieron DURANTE un request
 * (g_db_request_phase==1) vs. en el load global (==0). Al final de cada
 * request, mysql_close_request_conns() libera/devuelve-al-pool los que el
 * script olvidó cerrar (return temprano, throw, error). Las conexiones
 * globales (abiertas antes del primer request) se preservan. */
extern int g_db_request_phase;
static int conn_req_scoped[MYSQL_POOL_SIZE] = {0};

/* Accessor público para obtener la conexión por id (usado por orm_bridge
 * para escape Dapper). Devuelve NULL si el id es inválido. */
void* mysql_get_conn(int conn_id) {
    if (conn_id < 0 || conn_id >= MYSQL_POOL_SIZE) return NULL;
    return (void*)connections[conn_id];
}

/* Pool keying — reuse conexiones por (host|user|db|port). Opt-in via
 * TYPEEASY_MYSQL_POOL=1. Cuando está activo, mysql_close() devuelve la
 * conexión al pool en vez de cerrarla, y mysql_connect() reusa una
 * existente con mysql_ping() para validarla. */
static char *pool_keys[MYSQL_POOL_SIZE] = {NULL};
static int   pool_in_use[MYSQL_POOL_SIZE] = {0};
static int pool_enabled_cached = -1;

static int pool_enabled(void) {
    if (pool_enabled_cached < 0) {
        const char *v = getenv("TYPEEASY_MYSQL_POOL");
        pool_enabled_cached = (v && (*v == '1' || *v == 't' || *v == 'T')) ? 1 : 0;
    }
    return pool_enabled_cached;
}

static int pool_acquire(const char *key) {
    if (!pool_enabled() || !key) return -1;
    for (int i = 0; i < MYSQL_POOL_SIZE; i++) {
        if (connections[i] && !pool_in_use[i] && pool_keys[i] && strcmp(pool_keys[i], key) == 0) {
            if (mysql_ping(connections[i]) != 0) {
                /* dead conn — dropearla y dejar el slot libre para reabrir */
                mysql_close(connections[i]);
                connections[i] = NULL;
                free(pool_keys[i]); pool_keys[i] = NULL;
                continue;
            }
            pool_in_use[i] = 1;
            return i;
        }
    }
    return -1;
}

static void pool_register(int conn_id, const char *key) {
    if (!pool_enabled() || conn_id < 0 || conn_id >= MYSQL_POOL_SIZE) return;
    if (pool_keys[conn_id]) free(pool_keys[conn_id]);
    pool_keys[conn_id] = key ? strdup(key) : NULL;
    pool_in_use[conn_id] = 1;
}

static int pool_release(int conn_id) {
    if (!pool_enabled() || conn_id < 0 || conn_id >= MYSQL_POOL_SIZE) return 0;
    if (pool_keys[conn_id]) {
        pool_in_use[conn_id] = 0; /* mantén viva la conexión */
        return 1; /* indica al caller "NO cierres" */
    }
    return 0;
}

/* ---- Buffer dinámico seguro para serializar result-sets (fix Exit 139) ----
 * Reemplaza el viejo buffer fijo de 2 MB con `offset += snprintf(...)`. El
 * patrón antiguo era inseguro: snprintf devuelve la longitud que *habría*
 * escrito (sin truncar), así que con columnas TEXT grandes (base64 ~52 KB) el
 * offset podía superar el buffer a mitad de fila → `buf+offset` fuera de rango
 * y `cap-offset` (size_t) en underflow → escritura OOB → corrupción de heap que
 * tumbaba todo el backend. Este SB crece con realloc y marca `oom` si falla,
 * permitiendo devolver un error en vez de caer. */
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

/* Escapa una cadena (con longitud explícita, soporta bytes binarios) según
 * RFC 8259 para que json_parse no se rompa al copiar TEXT/blobs con comillas,
 * backslashes o saltos de línea. */
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

/* ---------- ORM fast path (Paso A + B) -----------------------------------
 * Streaming MySQL → ObjectNode list usando arena/pool del fast-row CSV.
 * Diseñado para escalar a 1M+ filas:
 *   - mysql_use_result(): no buffer entero del lado cliente.
 *   - Pre-classify atributos + row template + memcpy por fila.
 *   - Arena allocator (sin millones de malloc/free).
 *   - AST node pool bump (wrapper por fila).
 *   - LIST node con value=1 → declare_variable salta clone + ctor por objeto.
 * Activa logs de timing con TE_ORM_TIMING=1.
 */
ASTNode* mysql_query_to_objects_fast(int conn_id, const char* query, ClassNode* cls) {
    const char *te_timing = getenv("TE_ORM_TIMING");
    struct timespec t0 = {0,0}, t1 = {0,0};
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &t0);

    if (conn_id < 0 || conn_id >= MYSQL_POOL_SIZE || !connections[conn_id]) {
        fprintf(stderr, "[ORM-fast] invalid connection (ID: %d)\n", conn_id);
        return NULL;
    }
    if (!cls || !query) return NULL;

    MYSQL* conn = connections[conn_id];

    if (mysql_query(conn, query)) {
        fprintf(stderr, "[ORM-fast] Error: %s\n", mysql_error(conn));
        return NULL;
    }

    /* Streaming: no copia el resultset entero al cliente. */
    MYSQL_RES* res = mysql_use_result(conn);
    if (!res) {
        fprintf(stderr, "[ORM-fast] mysql_use_result NULL: %s\n", mysql_error(conn));
        return NULL;
    }

    int n_fields = (int)mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    int nattr = cls->attr_count;

    int *attr_kind = (int*)malloc((size_t)nattr * sizeof(int));
    int *attr_nullable = (int*)malloc((size_t)nattr * sizeof(int));
    char **shared_id   = (char**)te_orm_arena_alloc((size_t)nattr * sizeof(char*));
    char **shared_type = (char**)te_orm_arena_alloc((size_t)nattr * sizeof(char*));
    for (int a = 0; a < nattr; a++) {
        attr_kind[a]     = te_orm_attr_kind(cls->attributes[a].type);
        attr_nullable[a] = te_orm_attr_is_nullable(cls->attributes[a].type);
        shared_id[a]     = te_orm_arena_strdup(cls->attributes[a].id ? cls->attributes[a].id : "");
        shared_type[a]   = te_orm_arena_strdup(cls->attributes[a].type ? cls->attributes[a].type : "dynamic");
    }

    /* Mapeo columna→atributo (case-insensitive). */
    int *col_to_attr = (int*)malloc((size_t)n_fields * sizeof(int));
    for (int c = 0; c < n_fields; c++) {
        col_to_attr[c] = -1;
        for (int a = 0; a < nattr; a++) {
            if (cls->attributes[a].id && strcasecmp(cls->attributes[a].id, fields[c].name) == 0) {
                col_to_attr[c] = a;
                break;
            }
        }
    }

    /* Row template prebuilt: id/type/vtype/is_const seteados; value en cero. */
    Variable *row_template = (Variable*)te_orm_arena_alloc((size_t)nattr * sizeof(Variable));
    memset(row_template, 0, (size_t)nattr * sizeof(Variable));
    for (int a = 0; a < nattr; a++) {
        row_template[a].id       = shared_id[a];
        row_template[a].type     = shared_type[a];
        row_template[a].is_const = 0;
        row_template[a].vtype    = (attr_kind[a] == 0) ? VAL_INT :
                                   (attr_kind[a] == 3) ? VAL_FLOAT : VAL_STRING;
    }
    size_t row_template_bytes = (size_t)nattr * sizeof(Variable);

    char *shared_class_name = te_orm_arena_strdup(cls->name ? cls->name : "");
    const char *shared_obj_type = te_orm_wrapper_obj_type();

    ASTNode *first = NULL, *last = NULL;
    long row_count = 0;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        unsigned long *lengths = mysql_fetch_lengths(res);

        ObjectNode *obj = (ObjectNode*)te_orm_arena_alloc(sizeof(ObjectNode));
        obj->class = cls;
        obj->attributes = (Variable*)te_orm_arena_alloc(row_template_bytes);
        memcpy(obj->attributes, row_template, row_template_bytes);

        for (int c = 0; c < n_fields; c++) {
            int a = col_to_attr[c];
            if (a < 0) continue;
            const char *raw = row[c];
            unsigned long rl = lengths ? lengths[c] : 0;
            if (!raw) {
                /* NULL del servidor: deja value en zero (memset del template). */
                continue;
            }
            if (attr_kind[a] == 0) {
                /* int rápido (clon de la lógica CSV). */
                const char *p = raw;
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                else if (*p == '+') { p++; }
                long v = 0;
                int ok = (*p != '\0');
                while (*p) {
                    unsigned d = (unsigned)(*p - '0');
                    if (d > 9u) { ok = 0; break; }
                    v = v * 10 + (long)d;
                    p++;
                }
                if (!ok) {
                    char *endp = NULL;
                    v = strtol(raw, &endp, 10);
                }
                if (neg) v = -v;
                obj->attributes[a].value.int_value = (int)v;
            } else if (attr_kind[a] == 3) {
                /* FLOAT/DOUBLE: strtod. raw es NUL-terminated por MySQL C API. */
                char *endp = NULL;
                double dv = strtod(raw, &endp);
                obj->attributes[a].value.float_value = (endp == raw) ? 0.0 : dv;
            } else {
                /* STRING/OTHER: dup en arena con length conocido (sin strlen). */
                obj->attributes[a].value.string_value = te_orm_arena_dup(raw, (size_t)rl);
            }
        }

        ASTNode *item = te_orm_pool_alloc();
        item->type         = (char*)shared_obj_type;
        item->id           = shared_class_name;
        item->id_interned  = 1;
        item->from_pool    = 1;
        item->extra        = (struct ASTNode*)obj;
        item->value        = (int)(intptr_t)obj;
        item->left         = NULL;
        item->right        = NULL;
        item->next         = NULL;

        if (!first) { first = last = item; }
        else        { last->next = item; last = item; }
        row_count++;
    }

    mysql_free_result(res);
    free(col_to_attr);
    free(attr_kind);
    free(attr_nullable);

    if (te_timing) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000L;
        fprintf(stderr, "[ORM-fast] rows=%ld time=%ldus (%.3f us/row)\n",
                row_count, us, row_count > 0 ? (double)us / (double)row_count : 0.0);
    }

    ASTNode *list_node = (ASTNode*)calloc(1, sizeof(ASTNode));
    list_node->type      = strdup("LIST");
    list_node->left      = first;
    list_node->right     = NULL;
    list_node->next      = NULL;
    list_node->id        = NULL;
    list_node->str_value = NULL;
    list_node->value     = 1; /* PREINIT FLAG: declare_variable salta clone + ctor */
    return list_node;
}

// Devuelve lista de resultados para ORM
ASTNode* mysql_query_result(int conn_id, const char* query) {

   // fflush(stdout);
    
    // Validar conexión
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
        fprintf(stderr, "[ORM] Error: invalid connection (ID: %d)\n", conn_id);
        return NULL;
    }
    
    MYSQL* conn = connections[conn_id];
    
    // Ejecutar query
    if (mysql_query(conn, query)) {
        printf("[ORM] query error: %s\n", mysql_error(conn));
        return NULL;
    }
    
    // Obtener resultados
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        printf("[ORM] no results for the query\n");
        return NULL;
    }
    
    int num_fields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    
    //printf("[ORM] Query ejecutado exitosamente. Campos: %d\n", num_fields);
    
    // Construir lista de resultados
    ASTNode* first_row = NULL;
    ASTNode* last_row = NULL;
    MYSQL_ROW row;
    
    while ((row = mysql_fetch_row(res))) {
        // Crear nodo de argumentos para esta fila
        ASTNode* row_args = NULL;
        
        // Debug: mostrar campos de la primera fila
        /*if (!first_row) {
            printf("[MySQL DEBUG] Campos de la primera fila:\n");
            for (int i = 0; i < num_fields; i++) {
                printf("  Campo %d: name='%s', value='%s', type=%d\n", 
                       i, fields[i].name, row[i] ? row[i] : "NULL", fields[i].type);
            }
        }*/
        for (int i = 0; i < num_fields; i++) {
            const char* field_name = fields[i].name;
            const char* field_value = row[i] ? row[i] : "";
            ASTNode* field_node = NULL;
            if (IS_NUM(fields[i].type)) {
                int int_value = row[i] ? atoi(row[i]) : 0;
                field_node = create_ast_leaf("NUMBER", int_value, NULL, (char*)field_name);
            } else {
                field_node = create_ast_leaf("STRING", 0, (char*)field_value, (char*)field_name);
            }
            row_args = append_to_list(row_args, field_node);
        }
        ASTNode* row_node = create_ast_node("ARGS", row_args, NULL);
        if (!first_row) {
            first_row = row_node;
            last_row = row_node;
        } else {
            last_row->right = row_node;
            last_row = row_node;
        }
    }
    
    mysql_free_result(res);
    
    //printf("[ORM] Resultados procesados exitosamente\n");
   // printf("[MySQL DEBUG] mysql_query_result returning: first_row=%p\n", (void*)first_row);
    //fflush(stdout);
    
    return first_row;
}

// Prototipos de funciones auxiliares de ast.c
extern ASTNode* create_ast_leaf(char *type, int value, char *str_value, char *id);
extern void free_ast(ASTNode *node);

// Helper: Extrae argumento string de un nodo de argumentos
static const char* get_arg_string(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) {
        /* v0.0.12: las listas de argumentos se encadenan por ->next.
         * Fallback a ->right por compat con listas construidas a mano. */
        current = current->next ? current->next : current->right;
    }
    if (!current) return NULL;
    
    // Primero verificar si el nodo actual es directamente un STRING
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) {
        return current->str_value;
    }
    
    // Si no, verificar si tiene un left que sea STRING_LITERAL
    if (current->left && current->left->type && 
        strcmp(current->left->type, "STRING_LITERAL") == 0 && 
        current->left->str_value) {
        return current->left->str_value;
    }

    // Si es un identificador (variable), resolver su valor string.
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        Variable* v = find_variable(current->id);
        if (v && v->vtype == VAL_STRING && v->value.string_value) {
            return v->value.string_value;
        }
    }

    // También verificar current->left si es IDENTIFIER.
    if (current->left && current->left->type &&
        strcmp(current->left->type, "IDENTIFIER") == 0 && current->left->id) {
        Variable* v = find_variable(current->left->id);
        if (v && v->vtype == VAL_STRING && v->value.string_value) {
            return v->value.string_value;
        }
    }
    
    return NULL;
}

// Helper: Extrae argumento int de un nodo de argumentos
static int get_arg_int(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) {
        /* v0.0.12: args encadenados por ->next (fallback ->right). */
        current = current->next ? current->next : current->right;
    }
    if (!current) return -1;

    // Si es un número
    if (current->type && strcmp(current->type, "NUMBER") == 0) {
        return current->value;
    }

    // Si es un string que representa un número
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) {
        char* endptr = NULL;
        long val = strtol(current->str_value, &endptr, 10);
        if (endptr && *endptr == '\0') {
            // Es un número válido
            return (int)val;
        } else {
            // No es un número, intentar buscar como nombre de variable
            //printf("[DEBUG] String no numérico '%s', buscando como variable\n", current->str_value);
            Variable* v = find_variable(current->str_value);
            if (v) {
               // printf("[DEBUG] Variable encontrada: %s, tipo=%d, valor=%d\n", current->str_value, v->vtype, v->value.int_value);
                if (v->vtype == VAL_INT) {
                    return v->value.int_value;
                }
            }
            /*
            else {
                printf("[DEBUG] Variable NO encontrada: %s\n", current->str_value);
            }*/
        }
    }

    // Si no, verificar si tiene un left que sea NUMBER
    if (current->left && current->left->type && strcmp(current->left->type, "NUMBER") == 0) {
        return current->left->value;
    }

    // Si el left es string numérico
    if (current->left && current->left->type && strcmp(current->left->type, "STRING") == 0 && current->left->str_value) {
        char* endptr = NULL;
        long val = strtol(current->left->str_value, &endptr, 10);
        if (endptr && *endptr == '\0') {
            return (int)val;
        } else {
            // No es un número, intentar buscar como nombre de variable
            Variable* v = find_variable(current->left->str_value);
            if (v && v->vtype == VAL_INT) {
                return v->value.int_value;
            }
        }
    }

    // Si es un identificador, buscar la variable
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        // printf("[DEBUG] looking up IDENTIFIER variable: %s\n", current->id);
        Variable* v = find_variable(current->id);
        /*if (v) {
            printf("[DEBUG] Variable encontrada: %s, tipo=%d, valor=%d\n", current->id, v->vtype, v->value.int_value);
        } else {
            printf("[DEBUG] Variable NO encontrada: %s\n", current->id);
        }*/
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }

    // También verificar current->left si es IDENTIFIER
    if (current->left && current->left->type && strcmp(current->left->type, "IDENTIFIER") == 0 && current->left->id) {
        Variable* v = find_variable(current->left->id);
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }

    return -1;
}

// native_mysql_connect(host, user, password, database, [port])
// Retorna connection_id en __ret__
void native_mysql_connect(ASTNode* args) {
        // Imprimir todos los argumentos recibidos
        ASTNode* curr = args;
        int idx = 0;
        while (curr) {
            //printf("[DEBUG][native_mysql_connect] Arg #%d: type=%s, id=%s, str_value=%s, value=%d\n", idx, curr->type ? curr->type : "NULL", curr->id ? curr->id : "NULL", curr->str_value ? curr->str_value : "NULL", curr->value);
            curr = curr->next ? curr->next : curr->right;
            idx++;
        }
    // Debug: imprimir estructura de argumentos
   // printf("[MySQL DEBUG] native_mysql_connect called\n");
   // printf("[MySQL DEBUG] args=%p\n", (void*)args);
    //printf("[MySQL] native_mysql_connect called\n"); fflush(stdout);
    
    const char* host = get_arg_string(args, 0);
    const char* user = get_arg_string(args, 1);
    const char* pass = get_arg_string(args, 2);
    const char* db   = get_arg_string(args, 3);
    int port = get_arg_int(args, 4);  // Optional 5th parameter

    /* Pool: intentar reusar una conexión libre con el mismo key. */
    char pool_key[512];
    snprintf(pool_key, sizeof(pool_key), "%s|%s|%s|%d",
             host ? host : "", user ? user : "", db ? db : "", port);
    int reused = pool_acquire(pool_key);
    if (reused >= 0) {
        conn_req_scoped[reused] = g_db_request_phase;
        ASTNode* ret_node = create_ast_leaf("NUMBER", reused, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }

    // Debug: imprimir los valores recibidos
   /* printf("[MySQL DEBUG] Valores recibidos en native_mysql_connect:\n");
    printf("  host: '%s'\n", host ? host : "NULL");
    printf("  user: '%s'\n", user ? user : "NULL");
    printf("  pass: '%s'\n", pass ? pass : "NULL");
    printf("  db:   '%s'\n", db ? db : "NULL");
    printf("  port: %d\n", port);
    fflush(stdout);*/

    // If port is -1 (not provided or invalid), use default 3306
    if (port <= 0) {
        port = 3306;
    }

   /* printf("[MySQL] Args extracted: host=%s, user=%s, pass=%s, db=%s, port=%d\n", 
           host ? host : "NULL", 
           user ? user : "NULL", 
           pass ? pass : "NULL", 
           db ? db : "NULL",
           port);
    fflush(stdout);*/

    if (!host || !user || !pass || !db) {
        //printf("[MySQL] Error: Argumentos inválidos para mysql_connect\n"); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    // Find a free connection slot
    int conn_id = -1;
    for (int i = 0; i < 10; i++) {
        if (connections[i] == NULL) {
            conn_id = i;
            break;
        }
    }

    if (conn_id == -1) {
       // printf("[MySQL] Error: Max conexiones alcanzado\n"); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "[MySQL] Error: mysql_init failed\n"); fflush(stderr);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }

    /* 6to parametro opcional: map de opciones { "tls": 1, "tls_version": "TLSv1.3" }.
     * Default: tls=0 (sin SSL, funciona contra MySQL/MariaDB local de XAMPP).
     * Para servicios cloud (TiDB, PlanetScale, Aiven) pasar tls=1. */
    int opt_tls = -1;            /* -1 = no especificado por el script */
    const char *opt_tls_version = NULL;
    const char *opt_tls_fp = NULL;   /* SHA1 fingerprint del cert del server */
    const char *opt_tls_ca = NULL;   /* Ruta a PEM con la CA del server */
    int opt_tls_insecure = 0;        /* 1 = TLS sin verificar cadena ni hostname */
    int opts_owned = 0;
    ASTNode* opts_head = db_arg_as_map_head(args, 5, &opts_owned);
    for (ASTNode* p = opts_head; p; p = p->right) {
        if (!p->id || !p->left) continue;
        const char *k = p->id;
        ASTNode *v = p->left;
        const char *vt = v->type ? v->type : "";
        /* Resolver el valor: puede ser un literal (NUMBER/STRING) o una
         * referencia a variable (IDENTIFIER -> p.ej. tls_fp: DB_FP). Sin esto,
         * los valores por variable se ignoraban y opciones como tls_fp/tls_ca
         * quedaban sin efecto (se caia al modo verify por defecto). */
        const char *v_str = (strcmp(vt, "STRING") == 0) ? v->str_value : NULL;
        long v_num = v->value;
        int v_is_str = (strcmp(vt, "STRING") == 0 && v->str_value);
        int v_is_num = (strcmp(vt, "NUMBER") == 0 || strcmp(vt, "INT") == 0);
        if ((strcmp(vt, "IDENTIFIER") == 0 || strcmp(vt, "ID") == 0) && v->id) {
            Variable* rv = find_variable(v->id);
            if (rv) {
                if (rv->vtype == VAL_STRING) {
                    v_str = rv->value.string_value; v_is_str = (v_str != NULL); v_is_num = 0;
                } else if (rv->vtype == VAL_INT) {
                    v_num = rv->value.int_value; v_is_num = 1; v_is_str = 0;
                } else if (rv->vtype == VAL_FLOAT) {
                    v_num = (long)rv->value.float_value; v_is_num = 1; v_is_str = 0;
                }
            }
        }
        if (strcmp(k, "tls") == 0 || strcmp(k, "ssl") == 0) {
            if (v_is_num) {
                opt_tls = (v_num != 0) ? 1 : 0;
            } else if (v_is_str && v_str) {
                opt_tls = (strcmp(v_str, "true") == 0 ||
                           strcmp(v_str, "1") == 0 ||
                           strcmp(v_str, "require") == 0 ||
                           strcmp(v_str, "on") == 0) ? 1 : 0;
            }
        } else if (strcmp(k, "tls_version") == 0 || strcmp(k, "ssl_version") == 0) {
            if (v_is_str && v_str) {
                opt_tls_version = v_str;
            }
        } else if (strcmp(k, "tls_fp") == 0 || strcmp(k, "tls_peer_fp") == 0 ||
                   strcmp(k, "fingerprint") == 0) {
            /* Fijar el SHA1 del cert del server. Permite conectar por TLS a
             * servidores con cert self-signed / root no confiable (p.ej.
             * MySQL 8 con caching_sha2_password que fuerza TLS) sin tener que
             * confiar en la CA: Schannel/OpenSSL validan contra esta huella. */
            if (v_is_str && v_str) {
                opt_tls_fp = v_str;
            }
        } else if (strcmp(k, "tls_ca") == 0 || strcmp(k, "ssl_ca") == 0 ||
                   strcmp(k, "ca") == 0) {
            /* Ruta a un PEM con la CA (o la cadena) del server. Permite que
             * Schannel (Windows) y OpenSSL (Linux) confien en un cert
             * self-signed SOLO para esta conexion, sin tocar el trust store
             * del sistema ni la base de datos. Funciona igual en .exe y Linux. */
            if (v_is_str && v_str) {
                opt_tls_ca = v_str;
            }
        } else if (strcmp(k, "tls_insecure") == 0 || strcmp(k, "tls_no_verify") == 0 ||
                   strcmp(k, "insecure") == 0 || strcmp(k, "tls_skip_verify") == 0) {
            /* Desactiva TODA verificacion TLS (cadena + hostname). Con el
             * backend OpenSSL (libmariadb del instalador 0.0.15) equivale a
             * SSL_VERIFY_NONE: se cifra el canal pero se acepta cualquier cert.
             * Necesario para MySQL 8 con cert auto-generado self-signed cuyo CN
             * no coincide con la IP. NO usar en produccion contra hosts no
             * confiables (vulnerable a MITM); para eso usar tls_ca con la CA real. */
            if (v_is_num) {
                opt_tls_insecure = (v_num != 0) ? 1 : 0;
            } else if (v_is_str && v_str) {
                opt_tls_insecure = (strcmp(v_str, "true") == 0 ||
                                    strcmp(v_str, "1") == 0 ||
                                    strcmp(v_str, "on") == 0) ? 1 : 0;
            }
        }
    }

    /* TLS: el script controla via opciones { "tls": 1, "tls_version": "..." }.
     * Default: tls=0 (sin SSL) -> funciona contra MySQL/MariaDB local sin TLS
     * (XAMPP, instalacion Windows estandar). El script DEBE pasar tls=1 para
     * conectar a servicios cloud (TiDB, PlanetScale, Aiven) que requieren TLS.
     * Verificacion de cert: desactivada (no bundleamos CA bundle en el
     * instalador Win).
     *
     * Backwards-compat por env var (sin documentar):
     *   TYPEEASY_MYSQL_SSL=require  -> forzar TLS
     *   TYPEEASY_MYSQL_TLS_VERSION  -> override version (default "TLSv1.2") */
    int tls_enabled = 0;
    if (opt_tls >= 0) {
        tls_enabled = opt_tls;
    } else {
        const char *ssl_mode_env = getenv("TYPEEASY_MYSQL_SSL");
        if (ssl_mode_env && strcmp(ssl_mode_env, "require") == 0) tls_enabled = 1;
        /* default: sin TLS */
    }
    /* Una huella fijada implica TLS (validacion por fingerprint). */
    if (opt_tls_fp && *opt_tls_fp) tls_enabled = 1;
    /* Una CA provista implica TLS (validacion contra esa CA). */
    if (opt_tls_ca && *opt_tls_ca) tls_enabled = 1;
    /* tls_insecure implica TLS (canal cifrado, sin verificacion). */
    if (opt_tls_insecure) tls_enabled = 1;

    unsigned long client_flags = 0;

    /* Auth caching_sha2_password (MySQL 8 por defecto) en conexion PLANA:
     * pedir al servidor su clave publica RSA para cifrar el password e
     * intercambiarlo sobre TCP sin TLS (igual que hace PHP mysqlnd / pymysql).
     * Sin esto, libmariadb intenta negociar TLS solo para enviar el password,
     * y en Windows (Schannel) eso falla con errno 2026 contra certs
     * self-signed. Con esto, el .exe de Windows y el binario Linux conectan
     * sin TLS y sin tocar la base de datos. */
    if (!tls_enabled) {
#ifdef MYSQL_OPT_GET_SERVER_PUBLIC_KEY
        {
            unsigned char get_pubkey = 1;
            mysql_options(conn, MYSQL_OPT_GET_SERVER_PUBLIC_KEY, &get_pubkey);
        }
#endif
    }

    /* IMPORTANTE (MariaDB Connector/C): fijar CUALQUIER opcion SSL —incluso
     * MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0— marca la conexion para negociar TLS.
     * Por eso, cuando el script pide tls=0 NO debemos tocar ninguna opcion SSL:
     * asi la conexion queda en texto plano (como hace pymysql) y no falla con
     * errno 2026 contra servidores con cert self-signed/untrusted-root. */
    if (tls_enabled) {
        /* NOTA CRITICA: MYSQL_OPT_SSL_ENFORCE, MYSQL_OPT_TLS_VERSION,
         * MYSQL_OPT_SSL_CA, MYSQL_OPT_SSL_VERIFY_SERVER_CERT y
         * MARIADB_OPT_TLS_PEER_FP son VALORES DE ENUM, no macros #define.
         * Por eso NO se pueden guardar con #ifdef (siempre seria falso y el
         * codigo se compilaria fuera, dejando la conexion sin ninguna opcion
         * TLS aplicada: el bug que hacia fallar los certs self-signed con
         * tls_fp/tls_ca). libmariadb (>=3.x) define estos enums siempre. */
        {
            unsigned char ssl_on = 1;
            mysql_options(conn, MYSQL_OPT_SSL_ENFORCE, &ssl_on);
        }
        /* En Windows libmariadb.dll de MSYS2 usa Schannel; su TLS 1.3 falla
         * contra los NLB de AWS (TiDB, PlanetScale, Aiven) con
         * SEC_E_DECRYPT_FAILURE (0x80090330). TLS 1.2 funciona universal. */
        {
            const char *tls_ver = opt_tls_version;
            if (!tls_ver) tls_ver = getenv("TYPEEASY_MYSQL_TLS_VERSION");
            if (!tls_ver || !*tls_ver) tls_ver = "TLSv1.2";
            mysql_options(conn, MYSQL_OPT_TLS_VERSION, tls_ver);
        }

        /* Modos de verificacion del cert del server, mutuamente excluyentes:
         *
         *  (1) tls_fp -> pin por huella SHA1 (MARIADB_OPT_TLS_PEER_FP). Es EL
         *      mecanismo de MariaDB Connector/C para certs self-signed: compara
         *      la huella del cert presentado y omite cadena + hostname. SEGURO.
         *  (2) tls_ca -> CA explicita (PEM). Valida la cadena contra esa CA;
         *      verify=0 ademas omite el chequeo de hostname (necesario porque el
         *      cert auto-generado de MySQL tiene un CN generico, no la IP). SEGURO.
         *  (3) tls_insecure -> verify=0 sin CA: acepta cualquier cert (canal
         *      cifrado pero vulnerable a MITM). Solo para dev/diagnostico.
         *  (4) por defecto -> verify=1 (CA publica del sistema, como TiDB). */
        int handled = 0;
        if (opt_tls_fp && *opt_tls_fp) {
            mysql_optionsv(conn, MARIADB_OPT_TLS_PEER_FP, opt_tls_fp);
            mysql_ssl_set(conn, NULL, NULL, NULL, NULL, NULL);
            /* La huella SHA1 ES el gate de seguridad: pinea el cert EXACTO del
             * server. Hay que apagar la verificacion de cadena+hostname; si no,
             * el backend (OpenSSL o Schannel) rechaza el cert self-signed por
             * cadena no confiable o CN que no coincide, ANTES de comparar la
             * huella. Con verify=0 + PEER_FP la huella es el unico gate: seguro,
             * porque un MITM tendria que presentar un cert con la misma huella
             * SHA1 (computacionalmente inviable). */
            {
                unsigned char verify_off = 0;
                mysql_options(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify_off);
            }
            handled = 1;
        }
        if (!handled) {
            int want_verify_off = 0;
            if (opt_tls_ca && *opt_tls_ca) {
                mysql_options(conn, MYSQL_OPT_SSL_CA, opt_tls_ca);
                /* CA self-signed de MySQL: el CN del cert es generico (no la IP),
                 * asi que hay que omitir el chequeo de hostname. La cadena se
                 * valida igual contra la CA provista. */
                want_verify_off = 1;
            }
            if (opt_tls_insecure) want_verify_off = 1;  /* acepta cualquier cert */
            mysql_ssl_set(conn, NULL, NULL,
                          (opt_tls_ca && *opt_tls_ca) ? opt_tls_ca : NULL,
                          NULL, NULL);
            unsigned char verify_val = want_verify_off ? 0 : 1;
            /* Default { tls: 1 } -> verify=1: valida la cadena contra el trust
             * store del sistema (acepta CA publica como TiDB/PlanetScale/RDS y
             * rechaza self-signed). Es la conducta historica segura. Para certs
             * self-signed usar tls_fp (recomendado) o tls_ca. */
            mysql_options(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify_val);
        }
        client_flags |= CLIENT_SSL;
    }

    if (opts_owned && opts_head) free_ast(opts_head);

    if (!mysql_real_connect(conn, host, user, pass, db, port, NULL, client_flags)) {
        unsigned int err_no = mysql_errno(conn);
        const char* err_msg = mysql_error(conn);
        fprintf(stderr, "[MySQL] connection error (errno=%u): %s\n",
                err_no, err_msg ? err_msg : "(no message)"); fflush(stderr);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        mysql_close(conn);
        return;
    }
    
    connections[conn_id] = conn;
    conn_req_scoped[conn_id] = g_db_request_phase;
    pool_register(conn_id, pool_key);
    // next_conn_id is no longer used for allocation logic
    
    printf("[MySQL] Connection successful (ID: %d)%s\n", conn_id, pool_enabled() ? " [pool]" : ""); fflush(stdout);
    ASTNode* ret_node = create_ast_leaf("NUMBER", conn_id, NULL, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
}

// native_mysql_query(connection_id, query_string, [format])
// format can be "json" (default) or "xml"
// Retorna JSON or XML string en __ret__
extern void orm_run_mapped_query(int conn_id, const char* query, ClassNode* cls);

/* Devuelve el ASTNode en posicion idx (0-based) sin resolverlo. */
static ASTNode* mysql_arg_at(ASTNode* args, int idx) {
    ASTNode* cur = args;
    /* v0.0.12: args encadenados por ->next (fallback ->right). */
    for (int i = 0; i < idx && cur; i++) cur = cur->next ? cur->next : cur->right;
    return cur;
}

/* Si args[idx] es un IDENTIFIER que matchea una ClassNode, devuelve la clase. */
static ClassNode* mysql_arg_as_class(ASTNode* args, int idx) {
    ASTNode* a = mysql_arg_at(args, idx);
    if (!a || !a->type) return NULL;
    if ((strcmp(a->type, "IDENTIFIER") == 0 || strcmp(a->type, "ID") == 0) && a->id) {
        /* Solo es clase si NO existe una variable con ese nombre. */
        Variable* v = find_variable(a->id);
        if (v) return NULL;
        return find_class(a->id);
    }
    return NULL;
}

void native_mysql_query(ASTNode* args) {
    int conn_id = get_arg_int(args, 0);
    const char* query = get_arg_string(args, 1);

    /* v0.0.21: si el SQL llega como concatenación/expresión (ADD), llamada
     * anidada (CALL_FUNC) o ternario, get_arg_string() no lo resuelve y
     * devuelve NULL → antes esto producía {"error":"invalid_query"}. Ahora lo
     * evaluamos con get_node_string() (heap). Su ciclo de vida se enlaza más
     * abajo al de final_query para liberarlo en todas las salidas. */
    char* query_owned = NULL;
    if (!query) {
        ASTNode* qn = mysql_arg_at(args, 1);
        if (qn) {
            query_owned = get_node_string(qn);
            if (query_owned && query_owned[0] != '\0') {
                query = query_owned;
            } else {
                free(query_owned);
                query_owned = NULL;
            }
        }
    }

    /* Estilo Dapper: arg #2 puede ser un MAP de params; en ese caso arg #3 es el formato/clase. */
    int params_owned = 0;
    ASTNode* params_head = db_arg_as_map_head(args, 2, &params_owned);

    /* Detectar si el slot del 4° (con params) o 3° (sin params) arg es una clase → modo ORM. */
    ClassNode* orm_class = mysql_arg_as_class(args, params_head ? 3 : 2);

    const char* format = NULL;
    if (!orm_class) {
        if (params_head) {
            format = get_arg_string(args, 3);
        } else {
            format = get_arg_string(args, 2);
        }
        // Default to JSON if format not specified
        if (!format || strlen(format) == 0) {
            format = "json";
        }
    }
    
   // printf("[MySQL] Query format: %s\n", format);
    
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
       // printf("[MySQL] Error: Conexión inválida (ID: %d)\n", conn_id);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_connection\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        if (params_owned && params_head) free_ast(params_head);
        if (query_owned) free(query_owned);
        return;
    }
    
    if (!query) {
        //printf("[MySQL] Error: Query inválido\n");
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_query\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        if (params_owned && params_head) free_ast(params_head);
        return;
    }
    
    MYSQL* conn = connections[conn_id];

    /* Sustituir @placeholders si hay params */
    char* final_query = NULL;
    if (params_head) {
        final_query = db_substitute_params(query, params_head, mysql_escape_cb, conn);
        query = final_query;
        if (query_owned) { free(query_owned); query_owned = NULL; }
        if (params_owned) { free_ast(params_head); params_head = NULL; }
    } else if (query_owned) {
        /* Sin params: reutilizamos el ciclo de vida de final_query para que la
         * cadena evaluada se libere en cada return posterior. */
        final_query = query_owned;
        query_owned = NULL;
    }

    /* Modo ORM: si el caller pasó una clase como formato, mapeamos cada fila
     * a una instancia y devolvemos una LIST en __ret__. */
    if (orm_class) {
        orm_run_mapped_query(conn_id, query, orm_class);
        if (final_query) free(final_query);
        return;
    }

    if (mysql_query(conn, query)) {
      //  printf("[MySQL] Error en query: %s\n", mysql_error(conn));
        char error_buffer[512];
        snprintf(error_buffer, sizeof(error_buffer), "{\"error\":\"%s\"}", mysql_error(conn));
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup(error_buffer), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        /* Strict mode (opt-in, solo modo --api): que un fallo de query NO
         * termine respondiendo 200 OK silenciosamente. Fijamos response_status
         * (500) para que el server responda 500 si el handler no toca el
         * status. El handler sigue pudiendo inspeccionar el string y
         * sobreescribir con un response_status() distinto (p.ej. 409 para
         * UNIQUE violation). En CLI (g_api_mode=0) el flag es no-op: no hay
         * respuesta HTTP que cambiar, el script ve el mismo {"error":...}. */
        extern int g_api_mode;
        if (g_db_strict_errors && g_api_mode) typeeasy_http_set_status(500);
        if (final_query) free(final_query);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        // Query sin resultados (INSERT, UPDATE, DELETE) o error al obtener resultados
        if (mysql_field_count(conn) == 0) { // No result set for this query
            char ar_buf[64];
            snprintf(ar_buf, sizeof(ar_buf),
                     "{\"affected_rows\":%llu,\"insert_id\":%llu}",
                     (unsigned long long)mysql_affected_rows(conn),
                     (unsigned long long)mysql_insert_id(conn));
            ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup(ar_buf), NULL);
            add_or_update_variable("__ret__", ret_node);
            free_ast(ret_node);
            if (final_query) free(final_query);
            return;
        } else { // Error fetching result set for a query that should have one
           // printf("[MySQL] Error al obtener resultado: %s\n", mysql_error(conn));
            ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"no_result\"}"), NULL);
            add_or_update_variable("__ret__", ret_node);
            free_ast(ret_node);
            if (final_query) free(final_query);
            return;
        }
    }
    
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    /* v0.0.21 (fix segfault Exit 139): serializamos en un buffer que crece de
     * forma segura (SB). El buffer fijo anterior con `offset += snprintf(...)`
     * desbordaba el heap cuando una fila tenía columnas TEXT grandes (base64
     * ~52 KB). Ya no hay límite artificial de 2 MB ("Result too large"): el
     * tamaño lo acota la memoria real, y si malloc/realloc falla devolvemos un
     * error en vez de tumbar el proceso. */
    SB sb; sb_init(&sb);

    if (strcmp(format, "xml") == 0) {
        sb_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            unsigned long* lens = mysql_fetch_lengths(result);
            sb_puts(&sb, "  <row>\n");
            for (int i = 0; i < num_fields; i++) {
                sb_puts(&sb, "    <");
                sb_puts(&sb, fields[i].name);
                sb_putc(&sb, '>');
                if (row[i]) sb_put_xml_escaped_n(&sb, row[i], lens ? lens[i] : strlen(row[i]));
                sb_puts(&sb, "</");
                sb_puts(&sb, fields[i].name);
                sb_puts(&sb, ">\n");
            }
            sb_puts(&sb, "  </row>\n");
        }
        sb_puts(&sb, "</rows>");
    } else {
        // Generate JSON (default)
        sb_putc(&sb, '[');
        MYSQL_ROW row;
        int first_row = 1;
        while ((row = mysql_fetch_row(result))) {
            unsigned long* lens = mysql_fetch_lengths(result);
            if (!first_row) sb_putc(&sb, ',');
            first_row = 0;
            sb_putc(&sb, '{');
            for (int i = 0; i < num_fields; i++) {
                if (i > 0) sb_putc(&sb, ',');
                sb_putc(&sb, '"');
                sb_put_json_escaped_n(&sb, fields[i].name, strlen(fields[i].name));
                sb_puts(&sb, "\":");
                if (row[i]) {
                    size_t vlen = lens ? lens[i] : strlen(row[i]);
                    // IS_NUM: campo numérico → sin comillas ni escape (valores ASCII seguros)
                    if (IS_NUM(fields[i].type)) {
                        sb_putn(&sb, row[i], vlen);
                    } else {
                        sb_putc(&sb, '"');
                        sb_put_json_escaped_n(&sb, row[i], vlen);
                        sb_putc(&sb, '"');
                    }
                } else {
                    sb_puts(&sb, "null");
                }
            }
            sb_putc(&sb, '}');
        }
        sb_putc(&sb, ']');
    }

    mysql_free_result(result);

    if (sb.oom) {
        free(sb.p);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"memory_allocation_failed\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        if (final_query) free(final_query);
        return;
    }

    sb.p[sb.len] = '\0';  /* sb_reserve garantiza espacio para el NUL */
    ASTNode* ret_node = create_ast_leaf("STRING", 0, sb.p, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
    free(sb.p);
    if (final_query) free(final_query);
}

// native_mysql_close(connection_id)
void native_mysql_close(ASTNode* args) {
    int conn_id = get_arg_int(args, 0);
    
    if (conn_id < 0 || conn_id >= MYSQL_POOL_SIZE || !connections[conn_id]) {
        fprintf(stderr, "[MySQL] Error: invalid connection (ID: %d)\n", conn_id);
        return;
    }
    
    /* Si el pool está activo, devolver al pool en vez de cerrar. */
    if (pool_release(conn_id)) {
        conn_req_scoped[conn_id] = 0;
        fprintf(stderr, "[MySQL] connection returned to pool (ID: %d)\n", conn_id);
        return;
    }

    mysql_close(connections[conn_id]);
    connections[conn_id] = NULL;
    conn_req_scoped[conn_id] = 0;
    fprintf(stderr, "[MySQL] connection closed (ID: %d)\n", conn_id);
}

/* Cierre automático al final de cada request (llamado desde
 * runtime_reset_vars_to_initial_state en ast.c). Recorre los slots y libera
 * SOLO las conexiones abiertas durante este request que no se cerraron
 * explícitamente. Con el pool activo, las devuelve al pool para reuso; sin
 * pool, las cierra físicamente. Las conexiones globales no se tocan. */
void mysql_close_request_conns(void) {
    for (int i = 0; i < MYSQL_POOL_SIZE; i++) {
        if (!connections[i] || !conn_req_scoped[i]) continue;
        if (pool_enabled() && pool_keys[i]) {
            pool_in_use[i] = 0;   /* devolver al pool para reuso */
        } else {
            mysql_close(connections[i]);
            connections[i] = NULL;
        }
        conn_req_scoped[i] = 0;
    }
}
