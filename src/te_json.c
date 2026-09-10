/* te_json.c — JSON emit + parse, extracted from ast.c (Fase 1 modularization).
 *
 * Embedded evaluation of CALL_FUNC / CALL_METHOD inside JSON literals is
 * delegated to ast.c via the hooks registered through te_json_set_eval_hooks.
 */

#include "te_json.h"
#include "te_buf.h"
#include "ast.h"
#include "te_vm.h"
#include "te_value.h"
#include "te_decimal.h"
#include "te_value.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Eval hooks (registered by ast.c). NULL = embedded calls -> "null".
 * ------------------------------------------------------------------ */

/* Declared in ast.c (not in ast.h): tells whether an expression node
 * evaluates to a string (used to pick string vs numeric JSON emission). */
int is_string_type(ASTNode *node);

/* Declared in ast.c (not in ast.h): resolution helpers used to evaluate an
 * inline ACCESS_EXPR (map["k"] / list[i]) value to its underlying value node
 * so JSON emission preserves its type (gotcha #5). */
ASTNode* resolve_to_map(ASTNode *node);
ASTNode* resolve_to_list(ASTNode *node);
ASTNode* map_find_pair(ASTNode *map, const char *key);
ASTNode* list_get_item(ASTNode *list, int idx);
int      list_length(ASTNode *list);

void te_json_set_eval_hooks(te_json_eval_fn call_func,
                            te_json_eval_fn call_method) {
    g_vm.json_eval_call_func   = call_func;
    g_vm.json_eval_call_method = call_method;
}

/* ------------------------------------------------------------------
 * Emitter
 * ------------------------------------------------------------------ */

/* Emit JSON-escaped string content (with surrounding quotes). */
static void te_json_emit_str(TeBuf *b, const char *s) {
    tebuf_putc(b, '"');
    for (const unsigned char *p = (const unsigned char*)(s ? s : ""); *p; p++) {
        switch (*p) {
            case '"':  tebuf_puts(b, "\\\""); break;
            case '\\': tebuf_puts(b, "\\\\"); break;
            case '\n': tebuf_puts(b, "\\n");  break;
            case '\r': tebuf_puts(b, "\\r");  break;
            case '\t': tebuf_puts(b, "\\t");  break;
            case '\b': tebuf_puts(b, "\\b");  break;
            case '\f': tebuf_puts(b, "\\f");  break;
            default:
                if (*p < 0x20) { char tmp[8]; snprintf(tmp, sizeof(tmp), "\\u%04x", *p); tebuf_puts(b, tmp); }
                else tebuf_putc(b, (char)*p);
        }
    }
    tebuf_putc(b, '"');
}

/* Recursive JSON emitter for an AST node. */
/* Emite un VALOR de runtime (Variable/TeValue). Instancias de clase -> objeto con sus atributos. */
void te_json_emit_value(TeBuf *b, const Variable *v) {
    if (!v) { tebuf_puts(b, "null"); return; }
    const char *tag = v->type ? v->type : "";
    if (v->vtype == VAL_STRING) {
        if (te_var_is_decimal(v)) { tebuf_puts(b, v->value.string_value ? v->value.string_value : "0"); return; }
        te_json_emit_str(b, v->value.string_value ? v->value.string_value : "");
        return;
    }
    if (v->vtype == VAL_INT) {
        if (strcmp(tag, TE_T_BOOL) == 0) { tebuf_puts(b, v->value.int_value ? "true" : "false"); return; }
        char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)v->value.int_value); tebuf_puts(b, tmp); return;
    }
    if (v->vtype == VAL_FLOAT) { char tmp[64]; te_fmt_double(tmp, sizeof(tmp), v->value.float_value); tebuf_puts(b, tmp); return; }
    /* VAL_OBJECT */
    if (!v->value.object_value || strcmp(tag, TE_T_NULL) == 0) { tebuf_puts(b, "null"); return; }
    if (strcmp(tag, TE_T_LIST) == 0 || strcmp(tag, TE_T_MAP) == 0 || strcmp(tag, TE_T_OBJECT_LITERAL) == 0) {
        te_json_emit_node(b, (ASTNode *)(intptr_t)v->value.object_value);
        return;
    }
    if (strcmp(tag, TE_T_OBJECT) == 0) {
        ObjectNode *o = v->value.object_value;
        if (!o->class) { tebuf_puts(b, "null"); return; }
        tebuf_putc(b, '{');
        for (int i = 0; i < o->class->attr_count; i++) {
            if (i) tebuf_putc(b, ',');
            te_json_emit_str(b, o->class->attributes[i].id ? o->class->attributes[i].id : "");
            tebuf_putc(b, ':');
            te_json_emit_value(b, &o->attributes[i]);
        }
        tebuf_putc(b, '}');
        return;
    }
    tebuf_puts(b, "null");   /* LAMBDA / LAZY_ITER / ...: no serializable */
}

/* Recursive JSON emitter for an AST node. Nodos de DATOS (hojas, LIST, MAP, OBJECT construido)
 * se emiten directo; cualquier EXPRESIÓN (identificador, llamada, aritmética, acceso, ternario,
 * `new`, ...) se evalúa por el camino único te_eval_value y se emite su valor. */
void te_json_emit_node(TeBuf *b, ASTNode *n) {
    if (!n) { tebuf_puts(b, "null"); return; }
    if (!n->type) { tebuf_puts(b, "null"); return; }
    if (strcmp(n->type, TE_T_BOOL) == 0) {
        tebuf_puts(b, n->value ? "true" : "false");
        return;
    }
    if (strcmp(n->type, TE_T_STRING) == 0 || strcmp(n->type, TE_T_DATETIME) == 0 || strcmp(n->type, TE_T_UUID) == 0) {
        te_json_emit_str(b, n->str_value ? n->str_value : "");
        return;
    }
    if (strcmp(n->type, TE_T_INT) == 0 || strcmp(n->type, TE_T_NUMBER) == 0) {
        char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)n->value); tebuf_puts(b, tmp);
        return;
    }
    if (strcmp(n->type, TE_T_FLOAT) == 0 || strcmp(n->type, TE_T_DECIMAL) == 0) {
        const char *s = n->str_value ? n->str_value : "0";
        tebuf_puts(b, s);
        return;
    }
    if (strcmp(n->type, TE_T_NULL) == 0) { tebuf_puts(b, "null"); return; }
    if (strcmp(n->type, TE_T_LIST) == 0) {
        tebuf_putc(b, '[');
        ASTNode *cur = n->left;
        int first = 1;
        while (cur) {
            if (!first) tebuf_putc(b, ',');
            te_json_emit_node(b, cur);
            first = 0;
            cur = cur->next;
        }
        tebuf_putc(b, ']');
        return;
    }
    if (strcmp(n->type, TE_T_MAP) == 0 || strcmp(n->type, TE_T_OBJECT_LITERAL) == 0) {
        tebuf_putc(b, '{');
        ASTNode *cur = n->left;
        int first = 1;
        while (cur) {
            if (!first) tebuf_putc(b, ',');
            te_json_emit_str(b, cur->id ? cur->id : "");
            tebuf_putc(b, ':');
            te_json_emit_node(b, cur->left);
            first = 0;
            cur = cur->right;
        }
        tebuf_putc(b, '}');
        return;
    }
    if (strcmp(n->type, TE_T_OBJECT) == 0 && !n->is_new_expr) {
        TeValue v; te_leaf_to_value(n, &v);
        te_json_emit_value(b, &v);
        te_val_free(&v);
        return;
    }
    if (strcmp(n->type, TE_T_LAMBDA) == 0 || strcmp(n->type, TE_T_KV_PAIR) == 0) { tebuf_puts(b, "null"); return; }
    /* expresión: un solo camino */
    TeValue v;
    te_eval_value(n, &v);
    te_json_emit_value(b, &v);
    te_val_free(&v);
}

/* ------------------------------------------------------------------
 * Parser (recursive descent)
 * ------------------------------------------------------------------ */

static void te_json_skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
}

static char* te_json_parse_string(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;
    TeBuf b; tebuf_init(&b);
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            switch (**p) {
                case '"':  tebuf_putc(&b, '"');  (*p)++; break;
                case '\\': tebuf_putc(&b, '\\'); (*p)++; break;
                case '/':  tebuf_putc(&b, '/');  (*p)++; break;
                case 'n':  tebuf_putc(&b, '\n'); (*p)++; break;
                case 'r':  tebuf_putc(&b, '\r'); (*p)++; break;
                case 't':  tebuf_putc(&b, '\t'); (*p)++; break;
                case 'b':  tebuf_putc(&b, '\b'); (*p)++; break;
                case 'f':  tebuf_putc(&b, '\f'); (*p)++; break;
                case 'u': {
                    (*p)++;
                    unsigned int cp = 0;
                    for (int i = 0; i < 4 && **p; i++, (*p)++) {
                        char c = **p; cp <<= 4;
                        if (c>='0'&&c<='9') cp |= (c-'0');
                        else if (c>='a'&&c<='f') cp |= (c-'a'+10);
                        else if (c>='A'&&c<='F') cp |= (c-'A'+10);
                    }
                    if (cp < 0x80) tebuf_putc(&b, (char)cp);
                    else if (cp < 0x800) { tebuf_putc(&b, (char)(0xC0|(cp>>6))); tebuf_putc(&b, (char)(0x80|(cp&0x3F))); }
                    else { tebuf_putc(&b, (char)(0xE0|(cp>>12))); tebuf_putc(&b, (char)(0x80|((cp>>6)&0x3F))); tebuf_putc(&b, (char)(0x80|(cp&0x3F))); }
                    break;
                }
                default: tebuf_putc(&b, **p); if (**p) (*p)++; break;
            }
        } else {
            tebuf_putc(&b, **p); (*p)++;
        }
    }
    if (**p == '"') (*p)++;
    return b.p;
}

ASTNode *te_json_parse_value(const char **p) {
    te_json_skip_ws(p);
    char c = **p;
    /* Empty / whitespace-only input is not JSON: yield null (was NUMBER 0,
     * which then failed for-in with a misleading "it is a number"). */
    if (c == '\0') return create_ast_leaf(TE_T_NULL, 0, NULL, NULL);
    if (c == '"') {
        char *s = te_json_parse_string(p);
        ASTNode *r = create_ast_leaf(TE_T_STRING, 0, s ? s : "", NULL);
        if (s) free(s);
        return r;
    }
    if (c == '{') {
        (*p)++;
        ASTNode *map = (ASTNode*)calloc(1, sizeof(ASTNode));
        map->type = strdup(TE_T_OBJECT_LITERAL);
        ASTNode *tail = NULL;
        te_json_skip_ws(p);
        if (**p == '}') { (*p)++; return map; }
        while (**p) {
            te_json_skip_ws(p);
            char *k = te_json_parse_string(p);
            te_json_skip_ws(p);
            if (**p == ':') (*p)++;
            ASTNode *val = te_json_parse_value(p);
            ASTNode *pair = create_kv_pair_node(k ? k : "", val);
            if (k) free(k);
            if (!map->left) map->left = pair; else tail->right = pair;
            tail = pair;
            te_json_skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; break; }
            break;
        }
        return map;
    }
    if (c == '[') {
        (*p)++;
        ASTNode *list = create_list_node(NULL);
        te_json_skip_ws(p);
        if (**p == ']') { (*p)++; return list; }
        while (**p) {
            ASTNode *val = te_json_parse_value(p);
            /* Usar te_list_append mantiene el índice lateral (TEListIdx) en
             * sincronía. Encadenar a mano dejaba la lista con longitud 0 según
             * list_length()/list_get_item(), rompiendo el indexado arr[i]. */
            te_list_append(list, val);
            te_json_skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; break; }
            break;
        }
        return list;
    }
    if (c == 't' && strncmp(*p, "true", 4) == 0) { *p += 4; return create_ast_leaf_number(TE_T_INT, 1, NULL, NULL); }
    if (c == 'f' && strncmp(*p, "false", 5) == 0) { *p += 5; return create_ast_leaf_number(TE_T_INT, 0, NULL, NULL); }
    /* JSON null → nodo NULL propio (no INT 0). Antes se colapsaba a INT 0, lo
     * que era indistinguible de un 0 real: el model-binding de un body tipado
     * lo convertía en el string "0" y el binder de @params lo interpolaba como
     * '0' (rompía columnas DATE/DATETIME/ENUM bajo STRICT_TRANS_TABLES). Con un
     * nodo NULL, te_object_from_json lo enlaza como SQL NULL y los emisores
     * (te_json_emit_node, get_node_string) lo serializan como "null"/"". */
    if (c == 'n' && strncmp(*p, "null", 4) == 0) { *p += 4; return create_ast_leaf(TE_T_NULL, 0, NULL, NULL); }
    /* number */
    const char *start = *p;
    if (**p == '-' || **p == '+') (*p)++;
    int is_float = 0;
    while (**p && (isdigit((unsigned char)**p) || **p == '.' || **p == 'e' || **p == 'E' || **p == '-' || **p == '+')) {
        if (**p == '.' || **p == 'e' || **p == 'E') is_float = 1;
        (*p)++;
    }
    char tmp[64];
    size_t L = (size_t)(*p - start);
    /* Anti-hang: if nothing was consumed the current byte is not a valid JSON
     * value start (e.g. stray ',' '}' or garbage from malformed input). Skip
     * one byte so any caller looping over te_json_parse_value() always makes
     * forward progress instead of spinning forever. Guarded by **p so we never
     * run past the NUL terminator. */
    if (L == 0 && **p) (*p)++;
    if (L >= sizeof(tmp)) L = sizeof(tmp) - 1;
    memcpy(tmp, start, L); tmp[L] = 0;
    if (is_float) return create_ast_leaf(TE_T_FLOAT, 0, tmp, NULL);
    return create_ast_leaf_number(TE_T_INT, strtoll(tmp, NULL, 10), NULL, NULL);
}
