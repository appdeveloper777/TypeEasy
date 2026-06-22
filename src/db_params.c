#include "db_params.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* === Flag opt-in: tratar el STRING vacio ("") como SQL NULL ================
 * Por default 0 = comportamiento legacy (interpola ''). Cuando se enciende
 * con sql_set_empty_as_null(true) (o via env TYPEEASY_SQL_EMPTY_AS_NULL=1),
 * un parametro STRING cuyo valor es "" se emite como SQL NULL en vez de ''.
 *
 * Por que existe: forms HTML mandan "" para columnas opcionales numericas/
 * fecha; con STRICT_TRANS_TABLES, '' en INT/DATE lanza ERROR 1292 y aborta
 * el INSERT/UPDATE. La forma legacy obliga a envolver cada columna en
 *   COALESCE(NULLIF(@col,''),<default>)
 * en cada query. Activar este flag elimina toda esa ceremonia.
 *
 * Ademas, hace consistente el comportamiento con la rama ACCESS_EXPR (que
 * YA tratasta el vacio como NULL via db_emit_resolved): con el flag, ambos
 * caminos coinciden. */
int g_db_empty_as_null = 0;
/* Flag opt-in: en modo --api, si un query falla, ademas de devolver el
 * string JSON con {"error":...} fijamos response_status(500) para que el
 * server no responda 200 OK con un 'falso exito'. */
int g_db_strict_errors = 0;
/* Flag opt-in: si != 0, sql_query/sql_exec (la fachada generica) envuelven su
 * resultado en { success:bool, data|error }. Se activa con sql_set_envelope()
 * o env TYPEEASY_SQL_ENVELOPE=1. OFF por defecto. */
int g_db_envelope = 0;

ASTNode* db_arg_as_map_head(ASTNode* args, int idx, int* out_owned) {
    if (out_owned) *out_owned = 0;
    ASTNode* cur = args;
    /* v0.0.12: la lista de argumentos del call se encadena por ->next
     * (fallback ->right por compat con listas construidas a mano). */
    for (int i = 0; i < idx && cur; i++) cur = cur->next ? cur->next : cur->right;
    if (!cur || !cur->type) return NULL;

    /* Caso 1: literal { "k": v, ... } */
    if (strcmp(cur->type, "OBJECT_LITERAL") == 0) return cur->left;

    /* Caso 2: IDENTIFIER → Variable */
    if ((strcmp(cur->type, "IDENTIFIER") == 0 || strcmp(cur->type, "ID") == 0) && cur->id) {
        Variable* v = find_variable(cur->id);
        if (!v) return NULL;

        /* 2a: variable de tipo MAP */
        if (v->type && strcmp(v->type, "MAP") == 0) {
            ASTNode* m = (ASTNode*)(intptr_t)v->value.object_value;
            return m ? m->left : NULL;
        }

        /* 2b: instancia de clase → sintetizar KV_PAIR desde sus atributos.
         * Cada atributo del objeto se vuelve un parámetro nombrado.
         * `db_substitute_params` ya prueba con y sin '@', así que `@id`
         * matchea con un atributo `id`. */
        if (v->vtype == VAL_OBJECT && v->value.object_value && v->value.object_value->attributes) {
            ObjectNode* obj = v->value.object_value;
            int n = obj->class ? obj->class->attr_count : 0;
            ASTNode* head = NULL;
            ASTNode* tail = NULL;
            for (int i = 0; i < n; i++) {
                Variable* a = &obj->attributes[i];
                if (!a->id) continue;
                ASTNode* leaf = NULL;
                if (a->vtype == VAL_INT) {
                    leaf = create_ast_leaf("NUMBER", a->value.int_value, NULL, NULL);
                } else if (a->vtype == VAL_STRING) {
                    leaf = create_ast_leaf("STRING", 0,
                                           a->value.string_value ? a->value.string_value : "",
                                           NULL);
                } else if (a->vtype == VAL_FLOAT) {
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "%g", a->value.float_value);
                    leaf = create_ast_leaf("DB_RAW", 0, tmp, NULL);
                } else {
                    leaf = create_ast_leaf("NULL", 0, NULL, NULL);
                }
                ASTNode* pair = create_kv_pair_node(a->id, leaf);
                if (!head) head = pair;
                else tail->right = pair;
                tail = pair;
            }
            if (head && out_owned) *out_owned = 1;
            return head;
        }
    }
    return NULL;
}

static ASTNode* find_param(ASTNode* head, const char* key) {
    for (ASTNode* p = head; p; p = p->right) {
        if (p->id && strcmp(p->id, key) == 0) return p->left;
    }
    return NULL;
}

/* Buffer dinámico mínimo */
static void buf_append(char** buf, size_t* len, size_t* cap, const char* s, size_t n) {
    if (*len + n + 1 >= *cap) {
        size_t nc = (*cap) * 2;
        while (nc < *len + n + 1) nc *= 2;
        *buf = (char*)realloc(*buf, nc);
        *cap = nc;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/* ¿Es `s` un literal numérico (entero o flotante, con signo/exponente)? Se usa
 * para decidir si un valor resuelto vía get_node_string puede interpolarse SIN
 * comillas. Importa para `LIMIT @n` (MySQL rechaza `LIMIT '5'`) y para mantener
 * columnas numéricas como números, no strings entrecomillados. */
static int db_str_is_number(const char* s) {
    if (!s || !*s) return 0;
    int i = 0, digits = 0, dot = 0, exp = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    for (; s[i]; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { digits++; continue; }
        if (c == '.' && !dot && !exp) { dot = 1; continue; }
        if ((c == 'e' || c == 'E') && digits && !exp) {
            exp = 1;
            if (s[i+1] == '+' || s[i+1] == '-') i++;
            continue;
        }
        return 0;
    }
    return digits > 0;
}

/* Emite un valor cuya forma escalar no es un leaf simple (indexación
 * `m["k"]`/`arr[i]`, acceso a miembro sobre un MAP) resolviéndolo a su forma
 * string vía get_node_string —que EJECUTA el acceso en el runtime— y aplicando:
 *   vacío/NULL  -> SQL NULL  (clave ausente, índice fuera de rango, elemento null)
 *   numérico    -> sin comillas (LIMIT/columnas numéricas correctas)
 *   resto       -> escapado + entrecomillado
 * Mismo enfoque que la rama CALL_FUNC; cubre la variante inline del bug donde
 * un valor numérico de una colección se perdía como NULL. */
static int db_emit_resolved(char** buf, size_t* len, size_t* cap,
                            ASTNode* val, db_escape_fn escape, void* ctx) {
    char* s = get_node_string(val);
    if (!s || !*s) { if (s) free(s); buf_append(buf, len, cap, "NULL", 4); return 1; }
    if (db_str_is_number(s)) {
        buf_append(buf, len, cap, s, strlen(s));
        free(s);
        return 1;
    }
    char* esc = escape(s, ctx);
    free(s);
    if (!esc) { buf_append(buf, len, cap, "NULL", 4); return 1; }
    buf_append(buf, len, cap, "'", 1);
    buf_append(buf, len, cap, esc, strlen(esc));
    buf_append(buf, len, cap, "'", 1);
    free(esc);
    return 1;
}

/* Formatea un valor TypeEasy para inlining en SQL.
 * Devuelve 1 si OK, 0 si no se pudo (caller deja literal). */
static int append_value(char** buf, size_t* len, size_t* cap,
                        ASTNode* val, db_escape_fn escape, void* ctx) {
    if (!val || !val->type) {
        buf_append(buf, len, cap, "NULL", 4);
        return 1;
    }
    /* Resolver IDENTIFIER → variable */
    const char* tipo = val->type;
    int     vi = 0;
    double  vf = 0;
    const char* vs = NULL;
    int kind = 0; /* 1=int 2=float 3=str 4=null */

    if (strcmp(tipo, "NUMBER") == 0 || strcmp(tipo, "INT") == 0) {
        /* "NUMBER" = literal entero del parser TypeEasy; "INT" = entero
         * producido por json_parse() (te_json crea leaves "INT", no "NUMBER").
         * Sin la rama "INT" un objeto reusado como bind-params —p.ej.
         * `json_parse(mysql_query(...,"json"))[0]`— enlazaba sus columnas
         * numéricas como SQL NULL (caían al `else` final, kind=4), mientras
         * las columnas string sí se enlazaban. Ambos guardan el entero en
         * val->value. */
        kind = 1; vi = val->value;
    } else if (strcmp(tipo, "STRING") == 0 || strcmp(tipo, "STRING_LITERAL") == 0) {
        kind = 3; vs = val->str_value ? val->str_value : "";
    } else if (strcmp(tipo, "DB_RAW") == 0) {
        /* Valor crudo ya formateado (ej. float "%g"). Se interpola sin comillas. */
        const char* raw = val->str_value ? val->str_value : "NULL";
        buf_append(buf, len, cap, raw, strlen(raw));
        return 1;
    } else if (strcmp(tipo, "FLOAT") == 0 || strcmp(tipo, "FLOAT_LITERAL") == 0) {
        /* Literal flotante (p.ej. 19.9): el parser lo guarda como un leaf
         * "FLOAT" con el texto numérico en str_value (value=0). Sin esta rama
         * caía al `else` final (kind=4) y se interpolaba NULL, perdiendo el
         * valor en INSERT/UPDATE con params { "@price": 19.9 }. Se interpola
         * crudo, sin comillas, igual que un número. */
        const char* raw = (val->str_value && *val->str_value) ? val->str_value : "NULL";
        buf_append(buf, len, cap, raw, strlen(raw));
        return 1;
    } else if (strcmp(tipo, "BOOL") == 0) {
        /* Literal booleano (true/false): el parser lo guarda como un leaf
         * "BOOL" con value=1/0 (sin str_value). SQL estándar no tiene un tipo
         * boolean portable en parámetros, así que se interpola como entero
         * 1/0 (compatible con TINYINT/BOOLEAN de MySQL y INTEGER de SQLite).
         * Sin esta rama caía al `else` final (kind=4) y se perdía como NULL. */
        kind = 1; vi = val->value ? 1 : 0;
    } else if (strcmp(tipo, "NULL") == 0 || strcmp(tipo, "NULLTOK") == 0) {

        kind = 4;
    } else if ((strcmp(tipo, "IDENTIFIER") == 0 || strcmp(tipo, "ID") == 0) && val->id) {
        Variable* v = find_variable(val->id);
        if (!v) { kind = 4; }
        else if (v->vtype == VAL_INT) { kind = 1; vi = v->value.int_value; }
        else if (v->vtype == VAL_FLOAT) { kind = 2; vf = v->value.float_value; }
        else if (v->vtype == VAL_STRING) { kind = 3; vs = v->value.string_value ? v->value.string_value : ""; }
        else { kind = 4; }
    } else if (strcmp(tipo, "ACCESS_ATTR") == 0 && val->left && val->right) {
        /* Acceso a miembro `obj.attr` (p.ej. body.codcontacto en un INSERT).
         * Sin esto el valor se ignoraba (kind=4 -> NULL) y la fila se insertaba
         * vacia. Resolvemos la variable OBJECT y leemos el atributo por su id,
         * usando su vtype para decidir el formato (con/sin comillas). */
        ASTNode* o = val->left;
        ASTNode* a = val->right;
        const char* attr_name = a->id ? a->id : a->str_value;
        kind = 4;
        int handled = 0;
        Variable* ov = (o->id) ? find_variable(o->id) : NULL;
        /* Solo recorrer attributes[] si es un OBJECT de clase. Para un MAP
         * (p.ej. una fila de json_parse) value.object_value es un ASTNode*, no
         * un ObjectNode*, así que leer ->class sería un acceso mal tipado; esos
         * casos (mapVar.clave) se resuelven abajo vía get_node_string. */
        if (ov && ov->vtype == VAL_OBJECT && ov->type &&
            strcmp(ov->type, "OBJECT") == 0 &&
            ov->value.object_value && ov->value.object_value->class && attr_name) {
            ObjectNode* obj = ov->value.object_value;
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (obj->class->attributes[i].id &&
                    strcmp(obj->class->attributes[i].id, attr_name) == 0) {
                    Variable* attr = &obj->attributes[i];
                    handled = 1; /* atributo encontrado: null/objeto -> NULL (kind=4) */
                    if (attr->vtype == VAL_INT) { kind = 1; vi = attr->value.int_value; }
                    else if (attr->vtype == VAL_FLOAT) { kind = 2; vf = attr->value.float_value; }
                    else if (attr->vtype == VAL_STRING) { kind = 3; vs = attr->value.string_value ? attr->value.string_value : ""; }
                    break;
                }
            }
        }
        if (!handled) {
            /* Acceso a miembro sobre un MAP (mapVar.clave) o no resuelto:
             * resolver por la forma string del runtime (numérico sin comillas,
             * texto entrecomillado, ausente/null -> NULL). */
            return db_emit_resolved(buf, len, cap, val, escape, ctx);
        }
    } else if (strcmp(tipo, "ACCESS_EXPR") == 0) {
        /* Indexación inline como valor de bind-param: `{ "@x": row["activo"] }`,
         * `{ "@x": arr[i] }`, `{ "@x": m["k"] }`. Sin esta rama caía al `else`
         * final (kind=4) y se interpolaba NULL, perdiendo valores numéricos de
         * una colección (variante inline del bug "número de objeto -> NULL").
         * Se resuelve por la forma string del runtime con detección numérica. */
        return db_emit_resolved(buf, len, cap, val, escape, ctx);
    } else if (strcmp(tipo, "CALL_FUNC") == 0 || strcmp(tipo, "CALL_METHOD") == 0) {
        /* Valor que es una llamada inline en el map, p.ej.
         * { "@c": now(), "@u": uuid_v4() }. get_node_string EJECUTA la llamada
         * (corre la función/método y lee __ret__) y devuelve su forma string.
         * Sin esta rama caía al `else` final (kind=4) y se interpolaba NULL,
         * perdiendo now()/uuid_v4() en INSERT/UPDATE. Se trata como string
         * (escapado + entrecomillado), igual que el fallback de texto del
         * plugin SQLite; si la función devuelve un número MySQL lo coacciona
         * desde la forma entrecomillada. (Pasar el valor por una variable ya
         * funcionaba vía la rama IDENTIFIER; esto cubre el caso inline.) */
        char* s = get_node_string(val);
        if (!s) { buf_append(buf, len, cap, "NULL", 4); return 1; }
        char* esc = escape(s, ctx);
        free(s);
        if (!esc) { buf_append(buf, len, cap, "NULL", 4); return 1; }
        buf_append(buf, len, cap, "'", 1);
        buf_append(buf, len, cap, esc, strlen(esc));
        buf_append(buf, len, cap, "'", 1);
        free(esc);
        return 1;
    } else {
        kind = 4;
    }

    char tmp[64];
    int n;
    switch (kind) {
        case 1:
            n = snprintf(tmp, sizeof(tmp), "%d", vi);
            buf_append(buf, len, cap, tmp, (size_t)n);
            return 1;
        case 2:
            n = snprintf(tmp, sizeof(tmp), "%g", vf);
            buf_append(buf, len, cap, tmp, (size_t)n);
            return 1;
        case 3: {
            /* Opt-in: STRING vacio "" -> NULL (vease g_db_empty_as_null arriba). */
            if (g_db_empty_as_null && (!vs || !*vs)) {
                buf_append(buf, len, cap, "NULL", 4);
                return 1;
            }
            char* esc = escape(vs, ctx);
            if (!esc) { buf_append(buf, len, cap, "NULL", 4); return 1; }
            buf_append(buf, len, cap, "'", 1);
            buf_append(buf, len, cap, esc, strlen(esc));
            buf_append(buf, len, cap, "'", 1);
            free(esc);
            return 1;
        }
        case 4:
        default:
            buf_append(buf, len, cap, "NULL", 4);
            return 1;
    }
}

char* db_substitute_params(const char* sql, ASTNode* params_head,
                           db_escape_fn escape, void* ctx) {
    if (!sql) return NULL;
    if (!params_head) return strdup(sql);

    size_t cap = strlen(sql) * 2 + 64;
    char* out = (char*)malloc(cap);
    size_t len = 0;
    out[0] = '\0';

    const char* p = sql;
    int in_str = 0;
    char str_quote = 0;

    while (*p) {
        char c = *p;
        if (in_str) {
            buf_append(&out, &len, &cap, p, 1);
            /* SQL escape de comilla: '' → sigue dentro */
            if (c == str_quote) {
                if (p[1] == str_quote) {
                    buf_append(&out, &len, &cap, p + 1, 1);
                    p += 2;
                    continue;
                }
                in_str = 0;
            }
            p++;
            continue;
        }
        if (c == '\'' || c == '"') {
            in_str = 1; str_quote = c;
            buf_append(&out, &len, &cap, p, 1);
            p++;
            continue;
        }
        if (c == '@' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
            const char* start = p + 1;
            const char* q = start;
            while (*q && (isalnum((unsigned char)*q) || *q == '_')) q++;
            size_t nl = (size_t)(q - start);
            char name[128];
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, start, nl);
            name[nl] = '\0';

            /* Buscar con y sin @ */
            ASTNode* val = find_param(params_head, name);
            if (!val) {
                char with_at[130];
                snprintf(with_at, sizeof(with_at), "@%s", name);
                val = find_param(params_head, with_at);
            }
            if (val) {
                append_value(&out, &len, &cap, val, escape, ctx);
                p = q;
                continue;
            }
            /* No encontrado: dejar @nombre literal */
            buf_append(&out, &len, &cap, p, 1 + nl);
            p = q;
            continue;
        }
        buf_append(&out, &len, &cap, p, 1);
        p++;
    }
    return out;
}
