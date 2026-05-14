#include "mysql_bridge.h"
#include "db_params.h"
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

// Helper: Escape XML special characters
static void xml_escape(const char* input, char* output, size_t output_size) {
    size_t out_pos = 0;
    for (size_t i = 0; input[i] != '\0' && out_pos < output_size - 6; i++) {
        switch (input[i]) {
            case '&':
                if (out_pos + 5 < output_size) {
                    strcpy(output + out_pos, "&amp;");
                    out_pos += 5;
                }
                break;
            case '<':
                if (out_pos + 4 < output_size) {
                    strcpy(output + out_pos, "&lt;");
                    out_pos += 4;
                }
                break;
            case '>':
                if (out_pos + 4 < output_size) {
                    strcpy(output + out_pos, "&gt;");
                    out_pos += 4;
                }
                break;
            case '"':
                if (out_pos + 6 < output_size) {
                    strcpy(output + out_pos, "&quot;");
                    out_pos += 6;
                }
                break;
            case '\'':
                if (out_pos + 6 < output_size) {
                    strcpy(output + out_pos, "&apos;");
                    out_pos += 6;
                }
                break;
            default:
                output[out_pos++] = input[i];
                break;
        }
    }
    output[out_pos] = '\0';
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
        fprintf(stderr, "[ORM-fast] Conexión inválida (ID: %d)\n", conn_id);
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
        row_template[a].vtype    = (attr_kind[a] == 0) ? VAL_INT : VAL_STRING;
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
        printf("[ORM] Error: Conexión inválida (ID: %d)\n", conn_id);
        return NULL;
    }
    
    MYSQL* conn = connections[conn_id];
    
    // Ejecutar query
    if (mysql_query(conn, query)) {
        printf("[ORM] Error en query: %s\n", mysql_error(conn));
        return NULL;
    }
    
    // Obtener resultados
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        printf("[ORM] No hay resultados para la consulta\n");
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
        current = current->right;
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
    
    return NULL;
}

// Helper: Extrae argumento int de un nodo de argumentos
static int get_arg_int(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) {
        current = current->right;
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
        printf("[DEBUG] Buscando variable IDENTIFIER: %s\n", current->id);
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
            curr = curr->right;
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
    int opts_owned = 0;
    ASTNode* opts_head = db_arg_as_map_head(args, 5, &opts_owned);
    for (ASTNode* p = opts_head; p; p = p->right) {
        if (!p->id || !p->left) continue;
        const char *k = p->id;
        ASTNode *v = p->left;
        const char *vt = v->type ? v->type : "";
        if (strcmp(k, "tls") == 0 || strcmp(k, "ssl") == 0) {
            if (strcmp(vt, "NUMBER") == 0 || strcmp(vt, "INT") == 0) {
                opt_tls = (v->value != 0) ? 1 : 0;
            } else if (strcmp(vt, "STRING") == 0 && v->str_value) {
                opt_tls = (strcmp(v->str_value, "true") == 0 ||
                           strcmp(v->str_value, "1") == 0 ||
                           strcmp(v->str_value, "require") == 0 ||
                           strcmp(v->str_value, "on") == 0) ? 1 : 0;
            }
        } else if (strcmp(k, "tls_version") == 0 || strcmp(k, "ssl_version") == 0) {
            if (strcmp(vt, "STRING") == 0 && v->str_value) {
                opt_tls_version = v->str_value;
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

    unsigned long client_flags = 0;

#ifdef MYSQL_OPT_SSL_ENFORCE
    {
        unsigned char ssl_on = tls_enabled ? 1 : 0;
        mysql_options(conn, MYSQL_OPT_SSL_ENFORCE, &ssl_on);
    }
#endif
#ifdef MYSQL_OPT_SSL_VERIFY_SERVER_CERT
    {
        unsigned char verify_off = 0;
        mysql_options(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify_off);
    }
#endif

    if (tls_enabled) {
        /* En Windows libmariadb.dll de MSYS2 usa Schannel; su TLS 1.3 falla
         * contra los NLB de AWS (TiDB, PlanetScale, Aiven) con
         * SEC_E_DECRYPT_FAILURE (0x80090330). TLS 1.2 funciona universal. */
#ifdef MYSQL_OPT_TLS_VERSION
        {
            const char *tls_ver = opt_tls_version;
            if (!tls_ver) tls_ver = getenv("TYPEEASY_MYSQL_TLS_VERSION");
            if (!tls_ver || !*tls_ver) tls_ver = "TLSv1.2";
            mysql_options(conn, MYSQL_OPT_TLS_VERSION, tls_ver);
        }
#endif
        mysql_ssl_set(conn, NULL, NULL, NULL, NULL, NULL);
        client_flags |= CLIENT_SSL;
    }

    if (opts_owned && opts_head) free_ast(opts_head);

    if (!mysql_real_connect(conn, host, user, pass, db, port, NULL, client_flags)) {
        unsigned int err_no = mysql_errno(conn);
        const char* err_msg = mysql_error(conn);
        fprintf(stderr, "[MySQL] Error de conexion (errno=%u): %s\n",
                err_no, err_msg ? err_msg : "(sin mensaje)"); fflush(stderr);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        mysql_close(conn);
        return;
    }
    
    connections[conn_id] = conn;
    pool_register(conn_id, pool_key);
    // next_conn_id is no longer used for allocation logic
    
    printf("[MySQL] Conexión exitosa (ID: %d)%s\n", conn_id, pool_enabled() ? " [pool]" : ""); fflush(stdout);
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
    for (int i = 0; i < idx && cur; i++) cur = cur->right;
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
        if (params_owned) { free_ast(params_head); params_head = NULL; }
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
    
    // Allocate buffer for result (2MB for large queries)
    char* result_buffer = (char*)malloc(2097152);
    if (!result_buffer) {
        mysql_free_result(result);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"memory_allocation_failed\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        if (final_query) free(final_query);
        return;
    }
    
    int offset = 0;
    
    // Check format and generate accordingly
    size_t buffer_size = 2097152;
    if (strcmp(format, "xml") == 0) {
        // Generate XML
        offset += snprintf(result_buffer + offset, buffer_size - offset, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            if (offset > buffer_size - 8192) {
                // Clear buffer and write only valid XML error message
                snprintf(result_buffer, buffer_size, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n  <error>Result too large</error>\n</rows>");
                offset = strlen(result_buffer);
                break;
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "  <row>\n");
            for (int i = 0; i < num_fields; i++) {
                offset += snprintf(result_buffer + offset, buffer_size - offset, "    <%s>", fields[i].name);
            if (row[i]) {
                // Escape XML special characters
                char escaped[8192];
                xml_escape(row[i], escaped, sizeof(escaped));
                offset += snprintf(result_buffer + offset, buffer_size - offset, "%s", escaped);
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "</%s>\n", fields[i].name);
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "  </row>\n");
        }
        offset += snprintf(result_buffer + offset, buffer_size - offset, "</rows>");
    } else {
        // Generate JSON (default)
        offset += snprintf(result_buffer + offset, buffer_size - offset, "[");
        MYSQL_ROW row;
        int first_row = 1;
        while ((row = mysql_fetch_row(result))) {
            if (offset > buffer_size - 4096) {
                // Clear buffer and write only error message
                snprintf(result_buffer, buffer_size, "{\"error\":\"Result too large\"}");
                offset = strlen(result_buffer);
                break;
            }
            if (!first_row) {
                offset += snprintf(result_buffer + offset, buffer_size - offset, ",");
            }
            first_row = 0;
            offset += snprintf(result_buffer + offset, buffer_size - offset, "{");
            for (int i = 0; i < num_fields; i++) {
                if (i > 0) {
                    offset += snprintf(result_buffer + offset, buffer_size - offset, ",");
                }
                offset += snprintf(result_buffer + offset, buffer_size - offset, "\"%s\":", fields[i].name);
                if (row[i]) {
                    // Check if field is numeric using IS_NUM flag
                    // This is more reliable than checking field type
                    if (IS_NUM(fields[i].type)) {
                        // Numeric field - no quotes
                        offset += snprintf(result_buffer + offset, buffer_size - offset, "%s", row[i]);
                    } else {
                        // String field - add quotes
                        offset += snprintf(result_buffer + offset, buffer_size - offset, "\"%s\"", row[i]);
                    }
                } else {
                    offset += snprintf(result_buffer + offset, buffer_size - offset, "null");
                }
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "}");
        }
        offset += snprintf(result_buffer + offset, buffer_size - offset, "]");
    }
    
    mysql_free_result(result);
    
    //printf("[MySQL] Query ejecutado exitosamente\n");
    ASTNode* ret_node = create_ast_leaf("STRING", 0, result_buffer, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
    free(result_buffer);
    if (final_query) free(final_query);
}

// native_mysql_close(connection_id)
void native_mysql_close(ASTNode* args) {
    int conn_id = get_arg_int(args, 0);
    
    if (conn_id < 0 || conn_id >= MYSQL_POOL_SIZE || !connections[conn_id]) {
        printf("[MySQL] Error: Conexión inválida (ID: %d)\n", conn_id);
        return;
    }
    
    /* Si el pool está activo, devolver al pool en vez de cerrar. */
    if (pool_release(conn_id)) {
        fprintf(stderr, "[MySQL] Conexión devuelta al pool (ID: %d)\n", conn_id);
        return;
    }

    mysql_close(connections[conn_id]);
    connections[conn_id] = NULL;
    fprintf(stderr, "[MySQL] Conexión cerrada (ID: %d)\n", conn_id);
}
