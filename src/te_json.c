/* te_json.c — JSON emit + parse, extracted from ast.c (Fase 1 modularization).
 *
 * Embedded evaluation of CALL_FUNC / CALL_METHOD inside JSON literals is
 * delegated to ast.c via the hooks registered through te_json_set_eval_hooks.
 */

#include "te_json.h"
#include "te_buf.h"
#include "ast.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Eval hooks (registered by ast.c). NULL = embedded calls -> "null".
 * ------------------------------------------------------------------ */
static te_json_eval_fn g_eval_call_func   = NULL;
static te_json_eval_fn g_eval_call_method = NULL;

/* Declared in ast.c (not in ast.h): tells whether an expression node
 * evaluates to a string (used to pick string vs numeric JSON emission). */
int is_string_type(ASTNode *node);

void te_json_set_eval_hooks(te_json_eval_fn call_func,
                            te_json_eval_fn call_method) {
    g_eval_call_func   = call_func;
    g_eval_call_method = call_method;
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
void te_json_emit_node(TeBuf *b, ASTNode *n) {
    if (!n) { tebuf_puts(b, "null"); return; }
    if (!n->type) { tebuf_puts(b, "null"); return; }
    /* v1.0.0: evaluate embedded CALL_FUNC/CALL_METHOD (e.g. map literal
     * values like `{"id": uuid_v4()}` stored raw in KV_PAIR->left).
     * Requires hooks; otherwise emit null. */
    if (strcmp(n->type, "CALL_FUNC") == 0 || strcmp(n->type, "CALL_METHOD") == 0) {
        te_json_eval_fn hook = (strcmp(n->type, "CALL_FUNC") == 0)
                                ? g_eval_call_func
                                : g_eval_call_method;
        if (!hook) { tebuf_puts(b, "null"); return; }
        hook(n);
        Variable *r = find_variable("__ret__");
        if (!r) { tebuf_puts(b, "null"); return; }
        if (r->vtype == VAL_STRING) { te_json_emit_str(b, r->value.string_value ? r->value.string_value : ""); return; }
        if (r->vtype == VAL_INT) {
            if (r->type && strcmp(r->type, "BOOL") == 0) { tebuf_puts(b, r->value.int_value ? "true" : "false"); return; }
            char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", r->value.int_value); tebuf_puts(b, tmp); return;
        }
        if (r->vtype == VAL_FLOAT) { char tmp[64]; snprintf(tmp, sizeof(tmp), "%g", r->value.float_value); tebuf_puts(b, tmp); return; }
        tebuf_puts(b, "null"); return;
    }
    if (strcmp(n->type, "BOOL") == 0) {
        tebuf_puts(b, n->value ? "true" : "false");
        return;
    }
    if (strcmp(n->type, "STRING") == 0) {
        te_json_emit_str(b, n->str_value ? n->str_value : "");
        return;
    }
    if (strcmp(n->type, "INT") == 0 || strcmp(n->type, "NUMBER") == 0) {
        char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", n->value); tebuf_puts(b, tmp);
        return;
    }
    if (strcmp(n->type, "FLOAT") == 0) {
        const char *s = n->str_value ? n->str_value : "0";
        tebuf_puts(b, s);
        return;
    }
    if (strcmp(n->type, "LIST") == 0) {
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
    if (strcmp(n->type, "MAP") == 0 || strcmp(n->type, "OBJECT_LITERAL") == 0) {
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
    if (strcmp(n->type, "IDENTIFIER") == 0 && n->id) {
        Variable *v = find_variable(n->id);
        if (v) {
            if (v->type && strcmp(v->type, "BOOL") == 0) {
                tebuf_puts(b, v->value.int_value ? "true" : "false");
                return;
            }
            if (v->type && strcmp(v->type, "NULL") == 0) { tebuf_puts(b, "null"); return; }
            if (v->vtype == VAL_STRING) { te_json_emit_str(b, v->value.string_value ? v->value.string_value : ""); return; }
            if (v->vtype == VAL_INT)    { char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", v->value.int_value); tebuf_puts(b, tmp); return; }
            if (v->vtype == VAL_FLOAT)  { char tmp[64]; snprintf(tmp, sizeof(tmp), "%g", v->value.float_value); tebuf_puts(b, tmp); return; }
            if (v->type && (strcmp(v->type,"LIST")==0 || strcmp(v->type,"MAP")==0)) {
                te_json_emit_node(b, (ASTNode*)(intptr_t)v->value.object_value);
                return;
            }
        }
    }
    /* v1.0.1: evaluate expression nodes used as inline object-literal values,
     * e.g. { mensaje: "Hola " + nombre } (ADD string concat) or an
     * interpolated string { mensaje: $"Hola {nombre}" }. Strings go through
     * get_node_string(); numeric arithmetic through evaluate_expression(). */
    if (strcmp(n->type, "ADD") == 0 || strcmp(n->type, "SUB") == 0 ||
        strcmp(n->type, "MUL") == 0 || strcmp(n->type, "DIV") == 0 ||
        strcmp(n->type, "MOD") == 0 || strcmp(n->type, "NEG") == 0 ||
        strcmp(n->type, "STRING_INTERP") == 0) {
        if (strcmp(n->type, "STRING_INTERP") == 0 || is_string_type(n)) {
            char *s = get_node_string(n);
            te_json_emit_str(b, s ? s : "");
            if (s) free(s);
        } else {
            double d = evaluate_expression(n);
            if (d == (long long)d) {
                char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)d);
                tebuf_puts(b, tmp);
            } else {
                char tmp[64]; snprintf(tmp, sizeof(tmp), "%g", d);
                tebuf_puts(b, tmp);
            }
        }
        return;
    }
    tebuf_puts(b, "null");
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
    if (c == '"') {
        char *s = te_json_parse_string(p);
        ASTNode *r = create_ast_leaf("STRING", 0, s ? s : "", NULL);
        if (s) free(s);
        return r;
    }
    if (c == '{') {
        (*p)++;
        ASTNode *map = (ASTNode*)calloc(1, sizeof(ASTNode));
        map->type = strdup("OBJECT_LITERAL");
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
        ASTNode *tail = NULL;
        te_json_skip_ws(p);
        if (**p == ']') { (*p)++; return list; }
        while (**p) {
            ASTNode *val = te_json_parse_value(p);
            if (!list->left) list->left = val; else tail->next = val;
            tail = val;
            te_json_skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; break; }
            break;
        }
        return list;
    }
    if (c == 't' && strncmp(*p, "true", 4) == 0) { *p += 4; return create_ast_leaf_number("INT", 1, NULL, NULL); }
    if (c == 'f' && strncmp(*p, "false", 5) == 0) { *p += 5; return create_ast_leaf_number("INT", 0, NULL, NULL); }
    if (c == 'n' && strncmp(*p, "null", 4) == 0) { *p += 4; return create_ast_leaf_number("INT", 0, NULL, NULL); }
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
    if (L >= sizeof(tmp)) L = sizeof(tmp) - 1;
    memcpy(tmp, start, L); tmp[L] = 0;
    if (is_float) return create_ast_leaf("FLOAT", 0, tmp, NULL);
    return create_ast_leaf_number("INT", atoi(tmp), NULL, NULL);
}
