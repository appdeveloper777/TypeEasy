#include "db_params.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

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

    if (strcmp(tipo, "NUMBER") == 0) {
        kind = 1; vi = val->value;
    } else if (strcmp(tipo, "STRING") == 0 || strcmp(tipo, "STRING_LITERAL") == 0) {
        kind = 3; vs = val->str_value ? val->str_value : "";
    } else if (strcmp(tipo, "DB_RAW") == 0) {
        /* Valor crudo ya formateado (ej. float "%g"). Se interpola sin comillas. */
        const char* raw = val->str_value ? val->str_value : "NULL";
        buf_append(buf, len, cap, raw, strlen(raw));
        return 1;
    } else if (strcmp(tipo, "NULL") == 0 || strcmp(tipo, "NULLTOK") == 0) {
        kind = 4;
    } else if ((strcmp(tipo, "IDENTIFIER") == 0 || strcmp(tipo, "ID") == 0) && val->id) {
        Variable* v = find_variable(val->id);
        if (!v) { kind = 4; }
        else if (v->vtype == VAL_INT) { kind = 1; vi = v->value.int_value; }
        else if (v->vtype == VAL_FLOAT) { kind = 2; vf = v->value.float_value; }
        else if (v->vtype == VAL_STRING) { kind = 3; vs = v->value.string_value ? v->value.string_value : ""; }
        else { kind = 4; }
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
