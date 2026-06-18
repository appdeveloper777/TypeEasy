/* te_stdlib.c — TypeEasy stdlib dispatcher + plugin host API.
 *
 * Extracted from ast.c during the Nivel A refactor (Fase 2 paso 4).
 * See te_stdlib.h for the public surface.
 *
 * Implementation notes:
 *  - This module relies on a number of ast.c symbols that were de-staticized
 *    so the dispatcher could be moved out cleanly: te_arg_string, te_arg_int,
 *    te_set_ret_string, te_set_ret_int, native_debug_log, list_get_item,
 *    list_length, resolve_to_list, resolve_to_map, map_find_pair.
 *  - Database bridges (native_mysql_*, native_postgres_*, native_sqlserver_*)
 *    are already public symbols defined in their respective bridge .c files.
 *  - HTTP/JSON helpers come from te_http.h / te_json.h / te_buf.h.
 */

#define _GNU_SOURCE
#include "te_stdlib.h"
#include "te_buf.h"
#include "te_http.h"
#include "te_json.h"
#include "db_params.h"
#include "te_bridge.h"
#include "te_async.h"
#include "te_evloop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

/* ---- Bridges to ast.c (de-staticized helpers) ---- */
extern void evaluate_native_args(ASTNode *arg);
extern ASTNode *list_get_item(ASTNode *list, int idx);
extern int list_length(ASTNode *list);
extern ASTNode *resolve_to_list(ASTNode *node);
extern ASTNode *resolve_to_map(ASTNode *node);
extern ASTNode *map_find_pair(ASTNode *map, const char *key);
extern const char *te_arg_string(ASTNode *arg);
extern int te_arg_int(ASTNode *arg, int dflt);
extern void te_set_ret_string(const char *s);
extern void te_set_ret_int(int n);
extern void native_debug_log(ASTNode *arg);

/* ---- DB bridges (public in their respective .c files) ---- */
extern void native_mysql_connect(ASTNode *args);
extern void native_mysql_query(ASTNode *args);
extern void native_mysql_close(ASTNode *args);
extern void native_postgres_connect(ASTNode *args);
extern void native_postgres_query(ASTNode *args);
extern void native_postgres_close(ASTNode *args);
extern void native_sqlserver_connect(ASTNode *args);
extern void native_sqlserver_query(ASTNode *args);
extern void native_sqlserver_close(ASTNode *args);

/* ---- Runtime flags shared with ast.c ---- */
extern int g_test_assertions;
extern int g_test_failed;
extern int throw_flag;
extern char *throw_message;

/* Cross-platform UTC mktime. timegm is GNU; Windows MSVC/MinGW uses _mkgmtime. */
#if defined(_WIN32)
  #define TE_TIMEGM(tm_ptr) _mkgmtime(tm_ptr)
#else
  #define TE_TIMEGM(tm_ptr) timegm(tm_ptr)
#endif

/* ============================================================
 * Base64URL helpers (RFC 7515) used by the JWT builtins.
 * Encode: standard base64 then '+'->'-', '/'->'_', drop '=' padding.
 * Decode: reverse mapping, re-pad to a multiple of 4, then EVP decode.
 * ============================================================ */
static char *te_b64url_encode(const unsigned char *data, size_t len) {
    size_t outcap = 4 * ((len + 2) / 3) + 1;
    char *out = (char*)malloc(outcap);
    int n = EVP_EncodeBlock((unsigned char*)out, data, (int)len);
    if (n < 0) n = 0;
    out[n] = 0;
    int w = 0;
    for (int i = 0; i < n; i++) {
        char c = out[i];
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
        else if (c == '=') continue;
        out[w++] = c;
    }
    out[w] = 0;
    return out;
}

static unsigned char *te_b64url_decode(const char *s, size_t *outlen) {
    size_t slen = s ? strlen(s) : 0;
    size_t padded = ((slen + 3) / 4) * 4;
    char *tmp = (char*)malloc(padded + 1);
    size_t i;
    for (i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
        tmp[i] = c;
    }
    for (; i < padded; i++) tmp[i] = '=';
    tmp[padded] = 0;
    size_t outcap = (padded / 4) * 3 + 1;
    unsigned char *out = (unsigned char*)malloc(outcap);
    int n = EVP_DecodeBlock(out, (const unsigned char*)tmp, (int)padded);
    if (n > 0 && padded >= 1 && tmp[padded - 1] == '=') n--;
    if (n > 0 && padded >= 2 && tmp[padded - 2] == '=') n--;
    if (n < 0) n = 0;
    out[n] = 0;
    free(tmp);
    if (outlen) *outlen = (size_t)n;
    return out;
}

/* ============================================================
 * te_jwt_verify_alloc — standalone HS256 JWT verification reused by both the
 * jwt_verify() builtin and the @auth endpoint decorator (src/typeeasy_api.c).
 * Returns a malloc'd payload JSON string when the signature is valid and the
 * optional "exp" claim is not in the past; otherwise a malloc'd empty string "".
 * Caller owns the result.
 * ============================================================ */
char *te_jwt_verify_alloc(const char *token_in, const char *secret) {
    char *result = strdup(""); /* default: invalid */
    char *t = token_in ? strdup(token_in) : strdup("");
    char *d1 = strchr(t, '.');
    if (d1) {
        char *d2 = strchr(d1 + 1, '.');
        if (d2) {
            *d1 = 0; *d2 = 0;
            const char *h64 = t;
            const char *p64 = d1 + 1;
            const char *s64 = d2 + 1;
            size_t silen = strlen(h64) + 1 + strlen(p64);
            char *signing = (char*)malloc(silen + 1);
            snprintf(signing, silen + 1, "%s.%s", h64, p64);
            unsigned char md[EVP_MAX_MD_SIZE]; unsigned int mdlen = 0;
            HMAC(EVP_sha256(), secret ? secret : "", secret ? (int)strlen(secret) : 0,
                 (const unsigned char*)signing, strlen(signing), md, &mdlen);
            char *expect = te_b64url_encode(md, mdlen);
            /* constant-time signature comparison */
            int ok = 0;
            size_t el = strlen(expect), sl = strlen(s64);
            if (el == sl) {
                volatile unsigned char diff = 0;
                for (size_t i = 0; i < el; i++)
                    diff |= (unsigned char)(expect[i] ^ s64[i]);
                ok = (diff == 0);
            }
            if (ok) {
                size_t plen = 0;
                unsigned char *payload = te_b64url_decode(p64, &plen);
                int expired = 0;
                const char *pp = (const char*)payload;
                ASTNode *root = te_json_parse_value(&pp);
                if (root && root->type && strcmp(root->type, "OBJECT_LITERAL") == 0) {
                    for (ASTNode *pair = root->left; pair; pair = pair->right) {
                        if (pair->id && strcmp(pair->id, "exp") == 0 && pair->left) {
                            long expv = 0;
                            const char *vt = pair->left->type ? pair->left->type : "";
                            if (strcmp(vt, "INT") == 0) expv = pair->left->value;
                            else if (strcmp(vt, "STRING") == 0) expv = atol(pair->left->str_value ? pair->left->str_value : "0");
                            else if (strcmp(vt, "FLOAT") == 0) expv = (long)atof(pair->left->str_value ? pair->left->str_value : "0");
                            if (expv > 0 && (long)time(NULL) >= expv) expired = 1;
                            break;
                        }
                    }
                }
                if (!expired) { free(result); result = strdup((char*)payload); }
                free(payload);
            }
            free(expect); free(signing);
        }
    }
    free(t);
    return result;
}

/* ============================================================
 * te_resolve_arg — shared arg resolver.
 * Returns root node and writes resolved type to *out_type
 * ("LIST","MAP","STRING","INT","FLOAT","NUMBER",NULL).
 * ============================================================ */
ASTNode *te_resolve_arg(ASTNode *arg, const char **out_type) {
    if (out_type) *out_type = NULL;
    if (!arg) return NULL;
    if (arg->type && (strcmp(arg->type,"IDENTIFIER")==0 || strcmp(arg->type,"ID")==0) && arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) return NULL;
        if (v->type && (strcmp(v->type,"LIST")==0 || strcmp(v->type,"MAP")==0)) {
            if (out_type) *out_type = v->type;
            return (ASTNode*)(intptr_t)v->value.object_value;
        }
        if (v->vtype == VAL_STRING) {
            ASTNode *tmp = create_ast_leaf("STRING", 0, v->value.string_value ? v->value.string_value : "", NULL);
            if (out_type) *out_type = "STRING";
            return tmp;
        }
        if (v->vtype == VAL_FLOAT) {
            char buf[64]; te_fmt_double(buf, sizeof(buf), v->value.float_value);
            ASTNode *tmp = create_ast_leaf("FLOAT", 0, buf, NULL);
            if (out_type) *out_type = "FLOAT";
            return tmp;
        }
        if (v->vtype == VAL_INT) {
            /* Preserve BOOL tag so json_stringify(bool_var) emits true/false. */
            int is_bool = (v->type && strcmp(v->type, "BOOL") == 0);
            ASTNode *tmp = create_ast_leaf_number(is_bool ? "BOOL" : "INT",
                                                  v->value.int_value, NULL, NULL);
            if (out_type) *out_type = is_bool ? "BOOL" : "INT";
            return tmp;
        }
        return NULL;
    }
    if (arg->type) {
        if (strcmp(arg->type,"MAP")==0)  { if (out_type) *out_type = "MAP";  return arg; }
        if (strcmp(arg->type,"OBJECT_LITERAL")==0) { if (out_type) *out_type = "MAP"; return arg; }
        if (strcmp(arg->type,"LIST")==0) { if (out_type) *out_type = "LIST"; return arg; }
        if (strcmp(arg->type,"STRING")==0){ if (out_type) *out_type = "STRING"; return arg; }
        if (strcmp(arg->type,"INT")==0 || strcmp(arg->type,"NUMBER")==0) { if (out_type) *out_type = "INT"; return arg; }
        if (strcmp(arg->type,"FLOAT")==0){ if (out_type) *out_type = "FLOAT"; return arg; }
        /* v1.0.0: registros[0] (ACCESS_EXPR) — resolve to underlying item.
         * Without this, json_stringify(registros[0]) sees raw ACCESS_EXPR
         * and emits "null". */
        if (strcmp(arg->type,"ACCESS_EXPR")==0) {
            ASTNode *list = resolve_to_list(arg->left);
            if (list && arg->right) {
                int idx = (int)evaluate_expression(arg->right);
                int len = list_length(list);
                if (idx >= 0 && idx < len) {
                    ASTNode *item = list_get_item(list, idx);
                    if (item && item->type) {
                        if (strcmp(item->type, "OBJECT_LITERAL") == 0 ||
                            strcmp(item->type, "MAP") == 0) {
                            if (out_type) *out_type = "MAP";
                        } else if (strcmp(item->type, "LIST") == 0) {
                            if (out_type) *out_type = "LIST";
                        }
                        return item;
                    }
                }
            }
            ASTNode *map = resolve_to_map(arg->left);
            if (map && arg->right) {
                const char *key = NULL;
                if (arg->right->type && strcmp(arg->right->type, "STRING") == 0) key = arg->right->str_value;
                else if (arg->right->id) {
                    Variable *kv = find_variable(arg->right->id);
                    if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                }
                if (key) {
                    ASTNode *pair = map_find_pair(map, key);
                    if (pair) return pair->left;
                }
            }
        }
    }
    return arg;
}



/* Feature 2: normalize an HTTP-headers argument into the "Name: value\n..."
 * wire format te_http_do() expects. Accepts either:
 *   - a string already in "Name: value" lines (any '\n'/'\r\n' separators), or
 *   - a map / object literal { "Name": "value", ... } which is serialized here.
 * Returns a malloc'd string (caller frees), or NULL when there is no arg. */
static char* te_http_headers_arg(ASTNode *n) {
    if (!n) return NULL;
    ASTNode *map = resolve_to_map(n);
    if (map) {
        TeBuf b; tebuf_init(&b);
        for (ASTNode *pair = map->left; pair; pair = pair->right) {
            if (!pair->id) continue;
            char *val = get_node_string(pair->left);
            tebuf_puts(&b, pair->id);
            tebuf_puts(&b, ": ");
            if (val) { tebuf_puts(&b, val); free(val); }
            tebuf_puts(&b, "\n");
        }
        return b.p; /* "" for an empty map; te_http_do treats it as no headers */
    }
    return get_node_string(n);
}

/* ============================================================
 * te_builtin_dispatch — legacy if-chain dispatcher for builtins
 * not yet migrated to the registry. Returns 1 on hit.
 * ============================================================ */
int te_builtin_dispatch(ASTNode *node) {
    if (!node || !node->id) return 0;
    const char *fn = node->id;
    /* CALL_FUNC stores args on node->left; METHOD_CALL_ALONE on node->right.
     * Pick the non-null side. */
    ASTNode *a0 = node->left ? node->left : node->right;
    ASTNode *a1 = a0 ? a0->next : NULL; /* gotcha #1: 2nd arg via ->next */

    /* Fase 1: registry first. New builtins (and plugins loaded via
     * load_native) live in the hash table; the legacy if-chain below
     * remains as transparent fallback for builtins not yet migrated. */
    if (te_builtin_dispatch_registry(node, a0)) return 1;

    /* ---- len(x): string | list | map ---- */
    if (strcmp(fn, "len") == 0) {
        int n = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type, "STRING") == 0) {
                n = (int)strlen(a0->str_value ? a0->str_value : "");
            } else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *v = find_variable(a0->id);
                if (v) {
                    if (v->vtype == VAL_STRING) n = (int)strlen(v->value.string_value ? v->value.string_value : "");
                    else if (v->type && (strcmp(v->type,"LIST")==0 || strcmp(v->type,"MAP")==0)) {
                        ASTNode *root = (ASTNode*)(intptr_t)v->value.object_value;
                        if (root) {
                            ASTNode *cur = root->left;
                            if (strcmp(v->type,"LIST")==0) { while (cur) { n++; cur = cur->next; } }
                            else { while (cur) { n++; cur = cur->right; } }
                        }
                    } else if (v->vtype == VAL_INT) n = v->value.int_value;
                }
            } else {
                /* fallback: number length is just its int value semantics */
                n = (int)evaluate_expression(a0);
            }
        }
        ASTNode *r = create_ast_leaf_number("INT", n, NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ---- range(n) / range(start, end) / range(start, end, step) ---- */
    if (strcmp(fn, "range") == 0) {
        int start = 0, end = 0, step = 1;
        if (a0 && !a1) { end = (int)evaluate_expression(a0); }
        else if (a0 && a1) {
            start = (int)evaluate_expression(a0);
            end   = (int)evaluate_expression(a1);
            if (a1->next) step = (int)evaluate_expression(a1->next); /* gotcha #1: 3rd arg via ->next */
        }
        if (step == 0) step = 1;
        ASTNode *list = create_list_node(NULL);
        ASTNode *tail = NULL;
        for (int i = start; (step > 0 ? i < end : i > end); i += step) {
            ASTNode *item = (ASTNode*)calloc(1, sizeof(ASTNode));
            item->type = strdup("NUMBER");
            item->value = i;
            item->next = NULL;
            if (!list->left) list->left = item;
            else tail->next = item;
            tail = item;
        }
        add_or_update_variable("__ret__", list);
        return 1;
    }

    /* ---- read_file(path) -> string  /  write_file(path, content) -> int ---- */
    if (strcmp(fn, "read_file") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        char *out = NULL;
        if (path) {
            FILE *fp = fopen(path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (sz < 0) sz = 0;
                out = (char*)malloc((size_t)sz + 1);
                if (out) {
                    size_t r = fread(out, 1, (size_t)sz, fp);
                    out[r] = 0;
                }
                fclose(fp);
            }
            free(path);
        }
        ASTNode *r = create_ast_leaf("STRING", 0, out ? out : "", NULL);
        if (out) free(out);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "write_file") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        char *content = a1 ? get_node_string(a1) : NULL;
        int ok = 0;
        if (path && content) {
            FILE *fp = fopen(path, "wb");
            if (fp) {
                size_t L = strlen(content);
                ok = (fwrite(content, 1, L, fp) == L) ? 1 : 0;
                fclose(fp);
            }
        }
        if (path) free(path);
        if (content) free(content);
        ASTNode *r = create_ast_leaf_number("INT", ok, NULL, NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "file_exists") == 0) {
        char *path = a0 ? get_node_string(a0) : NULL;
        int ok = 0;
        if (path) { FILE *fp = fopen(path, "rb"); if (fp) { ok = 1; fclose(fp); } free(path); }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", ok, NULL, NULL));
        return 1;
    }

    /* ---- type conversions: to_int, to_str, to_float ---- */
    if (strcmp(fn, "to_int") == 0) {
        int v = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type,"STRING")==0) v = atoi(a0->str_value ? a0->str_value : "0");
            else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *vv = find_variable(a0->id);
                if (vv) {
                    if (vv->vtype == VAL_STRING) v = atoi(vv->value.string_value ? vv->value.string_value : "0");
                    else if (vv->vtype == VAL_FLOAT) v = (int)vv->value.float_value;
                    else v = vv->value.int_value;
                }
            } else v = (int)evaluate_expression(a0);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", v, NULL, NULL));
        return 1;
    }
    if (strcmp(fn, "to_float") == 0) {
        double v = 0;
        if (a0) {
            if (a0->type && strcmp(a0->type,"STRING")==0) v = atof(a0->str_value ? a0->str_value : "0");
            else if (a0->type && (strcmp(a0->type,"IDENTIFIER")==0 || strcmp(a0->type,"ID")==0)) {
                Variable *vv = find_variable(a0->id);
                if (vv) {
                    if (vv->vtype == VAL_STRING) v = atof(vv->value.string_value ? vv->value.string_value : "0");
                    else if (vv->vtype == VAL_FLOAT) v = vv->value.float_value;
                    else v = (double)vv->value.int_value;
                }
            } else v = evaluate_expression(a0);
        }
        char buf[64]; te_fmt_double(buf, sizeof(buf), v);
        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
        return 1;
    }
    if (strcmp(fn, "to_str") == 0) {
        char *s = a0 ? get_node_string(a0) : strdup("");
        ASTNode *r = create_ast_leaf("STRING", 0, s ? s : "", NULL);
        if (s) free(s);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ---- print_err(msg) -> writes to stderr (returns 0) ---- */
    if (strcmp(fn, "print_err") == 0) {
        char *s = a0 ? get_node_string(a0) : strdup("");
        fprintf(stderr, "%s\n", s ? s : "");
        if (s) free(s);
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", 0, NULL, NULL));
        return 1;
    }

    /* ---- abs(x), min(a,b), max(a,b): top-level convenience ---- */
    if (strcmp(fn, "abs") == 0) {
        double v = a0 ? evaluate_expression(a0) : 0;
        if (v == (int)v) add_or_update_variable("__ret__", create_ast_leaf_number("INT", abs((int)v), NULL, NULL));
        else { char buf[64]; te_fmt_double(buf, sizeof(buf), v < 0 ? -v : v); add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL)); }
        return 1;
    }
    if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
        double va = a0 ? evaluate_expression(a0) : 0;
        double vb = a1 ? evaluate_expression(a1) : va;
        double r = (strcmp(fn,"min")==0) ? (va < vb ? va : vb) : (va > vb ? va : vb);
        if (r == (int)r) add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)r, NULL, NULL));
        else { char buf[64]; te_fmt_double(buf, sizeof(buf), r); add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL)); }
        return 1;
    }

    /* ---- Phase F: assert / assert_eq for test runner ---- */
    if (strcmp(fn, "assert") == 0) {
        extern int g_test_assertions; extern int g_test_failed;
        double cond = a0 ? evaluate_expression(a0) : 0;
        g_test_assertions++;
        if (cond == 0) {
            g_test_failed = 1;
            char *msg = a1 ? get_node_string(a1) : NULL;
            fprintf(stderr, "    ASSERT FAILED: %s (line %d)\n", msg ? msg : "condition is false", node->line);
            if (msg) free(msg);
        }
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (int)(cond != 0), NULL, NULL));
        return 1;
    }
    if (strcmp(fn, "assert_eq") == 0) {
        extern int g_test_assertions; extern int g_test_failed;
        g_test_assertions++;
        int eq = 0;
        if (a0 && a1 && a0->type && a1->type) {
            int sa = (strcmp(a0->type,"STRING")==0) || (a0->type && (strcmp(a0->type,"IDENTIFIER")==0||strcmp(a0->type,"ID")==0) && find_variable(a0->id) && find_variable(a0->id)->vtype==VAL_STRING);
            int sb = (strcmp(a1->type,"STRING")==0) || (a1->type && (strcmp(a1->type,"IDENTIFIER")==0||strcmp(a1->type,"ID")==0) && find_variable(a1->id) && find_variable(a1->id)->vtype==VAL_STRING);
            if (sa || sb) {
                char *s1 = get_node_string(a0); char *s2 = get_node_string(a1);
                eq = (s1 && s2 && strcmp(s1, s2) == 0) ? 1 : 0;
                if (!eq) fprintf(stderr, "    ASSERT_EQ FAILED: \"%s\" != \"%s\" (line %d)\n", s1?s1:"", s2?s2:"", node->line);
                if (s1) free(s1); if (s2) free(s2);
            } else {
                double va = evaluate_expression(a0);
                double vb = evaluate_expression(a1);
                eq = (va == vb) ? 1 : 0;
                if (!eq) fprintf(stderr, "    ASSERT_EQ FAILED: %g != %g (line %d)\n", va, vb, node->line);
            }
        }
        if (!eq) g_test_failed = 1;
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", eq, NULL, NULL));
        return 1;
    }

    /* ---- Phase D: stdlib JSON / HTTP ---- */
    if (strcmp(fn, "json_stringify") == 0) {
        const char *rt = NULL;
        ASTNode *root = te_resolve_arg(a0, &rt);
        TeBuf b; tebuf_init(&b);
        te_json_emit_node(&b, root);
        ASTNode *r = create_ast_leaf("STRING", 0, b.p, NULL);
        free(b.p);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "json_parse") == 0) {
        char *s = a0 ? get_node_string(a0) : NULL;
        if (!s) {
            add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, "", NULL));
            return 1;
        }
        const char *p = s;
        ASTNode *r = te_json_parse_value(&p);
        free(s);
        if (!r) r = create_ast_leaf("STRING", 0, "", NULL);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ===== Crypto / encoding stdlib (mayo 2026) =====
     * sha256(s), md5_hex(s), hmac_sha256(key, msg),
     * base64_encode(s), base64_decode(s). Devuelven STRING. */
    if (strcmp(fn, "sha256") == 0) {
        char *s = a0 ? get_node_string(a0) : NULL;
        unsigned char md[SHA256_DIGEST_LENGTH];
        SHA256((const unsigned char*)(s ? s : ""), s ? strlen(s) : 0, md);
        char hex[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) sprintf(hex + i*2, "%02x", md[i]);
        hex[SHA256_DIGEST_LENGTH*2] = 0;
        if (s) free(s);
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, hex, NULL));
        return 1;
    }
    if (strcmp(fn, "md5_hex") == 0) {
        char *s = a0 ? get_node_string(a0) : NULL;
        unsigned char md[MD5_DIGEST_LENGTH];
        MD5((const unsigned char*)(s ? s : ""), s ? strlen(s) : 0, md);
        char hex[MD5_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) sprintf(hex + i*2, "%02x", md[i]);
        hex[MD5_DIGEST_LENGTH*2] = 0;
        if (s) free(s);
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, hex, NULL));
        return 1;
    }
    if (strcmp(fn, "hmac_sha256") == 0) {
        char *key = a0 ? get_node_string(a0) : NULL;
        char *msg = a1 ? get_node_string(a1) : NULL;
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int  mdlen = 0;
        HMAC(EVP_sha256(),
             key ? key : "", key ? (int)strlen(key) : 0,
             (const unsigned char*)(msg ? msg : ""), msg ? strlen(msg) : 0,
             md, &mdlen);
        char hex[EVP_MAX_MD_SIZE * 2 + 1];
        for (unsigned int i = 0; i < mdlen; i++) sprintf(hex + i*2, "%02x", md[i]);
        hex[mdlen*2] = 0;
        if (key) free(key); if (msg) free(msg);
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, hex, NULL));
        return 1;
    }
    if (strcmp(fn, "base64_encode") == 0) {
        char *s = a0 ? get_node_string(a0) : NULL;
        size_t slen = s ? strlen(s) : 0;
        size_t outcap = 4 * ((slen + 2) / 3) + 1;
        char *out = (char*)malloc(outcap);
        int n = EVP_EncodeBlock((unsigned char*)out, (const unsigned char*)(s ? s : ""), (int)slen);
        out[n] = 0;
        if (s) free(s);
        ASTNode *r = create_ast_leaf("STRING", 0, out, NULL);
        free(out);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "base64_decode") == 0) {
        char *s = a0 ? get_node_string(a0) : NULL;
        size_t slen = s ? strlen(s) : 0;
        size_t outcap = (slen / 4) * 3 + 4;
        unsigned char *out = (unsigned char*)malloc(outcap + 1);
        int n = EVP_DecodeBlock(out, (const unsigned char*)(s ? s : ""), (int)slen);
        /* EVP_DecodeBlock no descuenta el padding '=' — hacerlo aquí. */
        if (n > 0 && slen >= 1 && s[slen-1] == '=') n--;
        if (n > 0 && slen >= 2 && s[slen-2] == '=') n--;
        if (n < 0) n = 0;
        out[n] = 0;
        ASTNode *r = create_ast_leaf("STRING", 0, (char*)out, NULL);
        free(out); if (s) free(s);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    /* ===== JWT (HS256) =====
     * jwt_sign(payload_json, secret)  -> "header.payload.signature" (base64url).
     * jwt_verify(token, secret)       -> payload JSON if the signature is valid
     *                                    and the token is not expired (checks the
     *                                    optional numeric "exp" claim), else "".
     * Typical use:
     *   let token = jwt_sign("{\"sub\":\"ana\",\"exp\":1900000000}", secret);
     *   let claims = jwt_verify(token, secret);
     *   if (claims == "") { return 401; } */
    if (strcmp(fn, "jwt_sign") == 0) {
        char *payload = a0 ? get_node_string(a0) : strdup("{}");
        char *secret  = a1 ? get_node_string(a1) : strdup("");
        const char *header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
        char *h64 = te_b64url_encode((const unsigned char*)header, strlen(header));
        char *p64 = te_b64url_encode((const unsigned char*)(payload ? payload : ""),
                                     payload ? strlen(payload) : 0);
        size_t silen = strlen(h64) + 1 + strlen(p64);
        char *signing = (char*)malloc(silen + 1);
        snprintf(signing, silen + 1, "%s.%s", h64, p64);
        unsigned char md[EVP_MAX_MD_SIZE]; unsigned int mdlen = 0;
        HMAC(EVP_sha256(), secret ? secret : "", secret ? (int)strlen(secret) : 0,
             (const unsigned char*)signing, strlen(signing), md, &mdlen);
        char *s64 = te_b64url_encode(md, mdlen);
        size_t tlen = strlen(signing) + 1 + strlen(s64);
        char *token = (char*)malloc(tlen + 1);
        snprintf(token, tlen + 1, "%s.%s", signing, s64);
        ASTNode *r = create_ast_leaf("STRING", 0, token, NULL);
        free(token); free(s64); free(signing); free(p64); free(h64);
        if (payload) free(payload); if (secret) free(secret);
        add_or_update_variable("__ret__", r);
        free_ast(r); /* __ret__ copia el valor; el nodo temporal no se reusa */
        return 1;
    }
    if (strcmp(fn, "jwt_verify") == 0) {
        char *token  = a0 ? get_node_string(a0) : strdup("");
        char *secret = a1 ? get_node_string(a1) : strdup("");
        char *result = te_jwt_verify_alloc(token, secret);
        ASTNode *r = create_ast_leaf("STRING", 0, result, NULL);
        free(result);
        if (token) free(token); if (secret) free(secret);
        add_or_update_variable("__ret__", r);
        free_ast(r); /* __ret__ copia el valor; el nodo temporal no se reusa */
        return 1;
    }

    /* ===== Rate limiting (mayo 2026) =====
     * rate_limit(key, max_requests, window_seconds) → 1 si permitido, 0 si excedido.
     * Bucket array de 1024 slots con FNV-1a + linear probe. Sin sync (single-thread
     * por petición). Uso típico:
     *   let ip = request_header("X-Real-IP");
     *   if (rate_limit(ip + ":/api/foo", 100, 60) == 0) return "429"; */
    if (strcmp(fn, "rate_limit") == 0) {
        static struct { uint64_t hash; int count; long window_start; } buckets[1024];
        char *key = a0 ? get_node_string(a0) : strdup("");
        int max  = a1 ? (int)evaluate_expression(a1) : 60;
        ASTNode *a2 = a1 ? a1->next : NULL; /* gotcha #1: 3rd arg via ->next */
        int win  = a2 ? (int)evaluate_expression(a2) : 60;
        if (max <= 0) max = 1;
        if (win <= 0) win = 1;
        uint64_t h = 1469598103934665603ULL;
        for (const char *p2 = key; *p2; p2++) { h ^= (unsigned char)*p2; h *= 1099511628211ULL; }
        if (h == 0) h = 1; /* 0 reservado para "slot vacío" */
        long now = (long)time(NULL);
        int allowed = 0;
        for (int probe = 0; probe < 8; probe++) {
            int idx = (int)((h + probe) & 1023);
            if (buckets[idx].hash == 0 || buckets[idx].hash == h) {
                if (buckets[idx].hash == 0 || (now - buckets[idx].window_start) >= win) {
                    buckets[idx].hash = h;
                    buckets[idx].window_start = now;
                    buckets[idx].count = 0;
                }
                if (buckets[idx].count < max) {
                    buckets[idx].count++;
                    allowed = 1;
                }
                break;
            }
        }
        free(key);
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", allowed, NULL, NULL));
        return 1;
    }

    if (strcmp(fn, "http_get") == 0) {
        /* http_get(url) | http_get(url, headers) */
        char *url   = a0 ? get_node_string(a0) : NULL;
        char *hdrs  = te_http_headers_arg(a1);
        char *body  = url ? te_http_do("GET", url, NULL, hdrs) : NULL;
        if (url) free(url);
        if (hdrs) free(hdrs);
        ASTNode *r = create_ast_leaf("STRING", 0, body ? body : "", NULL);
        if (body) free(body);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "http_post") == 0) {
        /* http_post(url, body) | http_post(url, body, headers) */
        char *url  = a0 ? get_node_string(a0) : NULL;
        char *post = a1 ? get_node_string(a1) : NULL;
        ASTNode *a2 = a1 ? a1->next : NULL; /* gotcha #1: 3rd arg via ->next */
        char *hdrs = te_http_headers_arg(a2);
        char *body = url ? te_http_do("POST", url, post ? post : "", hdrs) : NULL;
        if (url) free(url);
        if (post) free(post);
        if (hdrs) free(hdrs);
        ASTNode *r = create_ast_leaf("STRING", 0, body ? body : "", NULL);
        if (body) free(body);
        add_or_update_variable("__ret__", r);
        return 1;
    }
    if (strcmp(fn, "http_request") == 0) {
        /* http_request(method, url) |
         * http_request(method, url, body) |
         * http_request(method, url, body, headers) */
        char *method = a0 ? get_node_string(a0) : NULL;
        char *url    = a1 ? get_node_string(a1) : NULL;
        ASTNode *a2  = a1 ? a1->next : NULL; /* gotcha #1: 3rd arg via ->next */
        ASTNode *a3  = a2 ? a2->next : NULL; /* gotcha #1: 4th arg via ->next */
        char *post   = a2 ? get_node_string(a2) : NULL;
        char *hdrs   = te_http_headers_arg(a3);
        char *body   = (method && url) ? te_http_do(method, url, post, hdrs) : NULL;
        if (method) free(method);
        if (url) free(url);
        if (post) free(post);
        if (hdrs) free(hdrs);
        ASTNode *r = create_ast_leaf("STRING", 0, body ? body : "", NULL);
        if (body) free(body);
        add_or_update_variable("__ret__", r);
        return 1;
    }

    if (strcmp(fn, "http_last_status") == 0) {
        /* Feature 1: HTTP status code of the last http_get/post/request call.
         * 0 = no response (network failure / unreachable host). */
        add_or_update_variable("__ret__",
            create_ast_leaf_number("INT", te_http_last_status(), NULL, NULL));
        return 1;
    }

    return 0;
}

/* ─── Fase 1/3: bootstrap + plugin host API ───────────────────────────── */

/* Adapter helpers: convert legacy native_*(arg) into the registry signature.
 * EVAL variants pre-evaluate native args (identifier resolution, etc.); the
 * raw variant skips that for builtins that handle args themselves. */
#define TE_WRAP_EVAL(NAME, FN) \
    static int adapt_##NAME(ASTNode *node, ASTNode *args) { \
        (void)node; if (args) evaluate_native_args(args); FN(args); return 1; \
    }
#define TE_WRAP_RAW(NAME, FN) \
    static int adapt_##NAME(ASTNode *node, ASTNode *args) { \
        (void)node; FN(args); return 1; \
    }

/* DB bridges — pre-evaluate args (need resolved literals). */
TE_WRAP_EVAL(mysql_connect,     native_mysql_connect)
TE_WRAP_EVAL(mysql_query,       native_mysql_query)
TE_WRAP_EVAL(mysql_close,       native_mysql_close)
TE_WRAP_EVAL(postgres_connect,  native_postgres_connect)
TE_WRAP_EVAL(postgres_query,    native_postgres_query)
TE_WRAP_EVAL(postgres_close,    native_postgres_close)
TE_WRAP_EVAL(sqlserver_connect, native_sqlserver_connect)
TE_WRAP_EVAL(sqlserver_query,   native_sqlserver_query)
TE_WRAP_EVAL(sqlserver_close,   native_sqlserver_close)

/* Fase 3: load_native("name") — dynamically load a `.so` plugin which
 * registers its own builtins via te_module_register(host_api). */
static int adapt_load_native(ASTNode *node, ASTNode *args) {
    (void)node;
    const char *name = NULL;
    char *owned = NULL;
    if (args) {
        if (args->type && strcmp(args->type, "STRING") == 0) {
            name = args->str_value;
        } else if (args->type && (strcmp(args->type, "IDENTIFIER")==0 || strcmp(args->type,"ID")==0)) {
            owned = get_node_string(args);
            name = owned;
        }
    }
    int rc = name ? te_load_native_module(name) : -1;
    if (owned) free(owned);
    add_or_update_variable("__ret__", create_ast_leaf_number("INT", rc == 0 ? 1 : 0, NULL, NULL));
    return 1;
}

/* debug_log("...") -> int(0). Always prints to stderr (bypasses HTTP
 * response capture). Useful for diagnostics in --api handlers. */
static int adapt_debug_log(ASTNode *node, ASTNode *args) {
    (void)node;
    if (args) evaluate_native_args(args);
    native_debug_log(args);
    return 1;
}

/* env("KEY")            -> string ("" if unset)
 * env("KEY", "default") -> string (default if unset/empty)
 * The 2-arg form is the idiomatic way to provide a fallback. */
static int adapt_env(ASTNode *node, ASTNode *args) {
    (void)node;
    if (args) evaluate_native_args(args);
    char *key = args ? get_node_string(args) : NULL;
    const char *val = (key && *key) ? getenv(key) : NULL;
    if (key) free(key);
    if (val && *val) {
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, val, NULL));
    } else {
        /* unset/empty: usar el 2º arg como default si se dio. Si NO hay 2º
         * arg, devolver null (no "") para que `env("X") ?? def` funcione
         * (gotcha #12). El check de null sobre la CALL_FUNC lo hace
         * te_expr_is_null. */
        ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
        if (a1) {
            char *def = get_node_string(a1);
            add_or_update_variable("__ret__",
                create_ast_leaf("STRING", 0, def ? def : "", NULL));
            if (def) free(def);
        } else {
            add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
        }
    }
    return 1;
}

/* env_required("KEY") -> string. Same as env(), but throws when the
 * variable is missing or empty so the script aborts (or is caught by
 * a surrounding try { } catch). */
static int adapt_env_required(ASTNode *node, ASTNode *args) {
    (void)node;
    extern int throw_flag;
    extern char *throw_message;
    if (args) evaluate_native_args(args);
    char *key = args ? get_node_string(args) : NULL;
    const char *val = (key && *key) ? getenv(key) : NULL;
    if (!val || !*val) {
        char buf[256];
        snprintf(buf, sizeof(buf), "env_required: missing environment variable '%s'", key ? key : "(null)");
        if (throw_message) { free(throw_message); throw_message = NULL; }
        throw_message = strdup(buf);
        throw_flag = 1;
        if (key) free(key);
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, "", NULL));
        return 1;
    }
    if (key) free(key);
    add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, val, NULL));
    return 1;
}

/* ============================================================
 * v1.0.0 — datetime + uuid builtins
 *
 * datetime is stored as ISO-8601 string ("YYYY-MM-DDTHH:MM:SSZ", UTC).
 * uuid is stored as canonical lower-case string
 * ("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx", v4 variant 10).
 *
 * All adapters set __ret__ via add_or_update_variable, matching the
 * existing adapter pattern (adapt_env, adapt_env_required).
 * ============================================================ */

/* Cross-platform UTC mktime. timegm is GNU; Windows MSVC/MinGW uses _mkgmtime. */
#if defined(_WIN32)
  #define TE_TIMEGM(tm_ptr) _mkgmtime(tm_ptr)
#else
  #define TE_TIMEGM(tm_ptr) timegm(tm_ptr)
#endif

static void te_format_iso_utc(time_t t, char *out, size_t out_sz) {
    struct tm g;
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    strftime(out, out_sz, "%Y-%m-%dT%H:%M:%SZ", &g);
}

/* Parse ISO-8601 "YYYY-MM-DDTHH:MM:SSZ" (or with space instead of T, and
 * with optional trailing Z) into time_t (UTC). Returns 0 on success. */
static int te_parse_iso_utc(const char *s, time_t *out) {
    if (!s || !*s || !out) return -1;
    int Y, M, D, h = 0, mi = 0, se = 0;
    char sep = 'T';
    int n = sscanf(s, "%d-%d-%d%c%d:%d:%d", &Y, &M, &D, &sep, &h, &mi, &se);
    if (n < 3) return -1;
    struct tm tmv; memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = Y - 1900;
    tmv.tm_mon  = M - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = h;
    tmv.tm_min  = mi;
    tmv.tm_sec  = se;
    time_t t = TE_TIMEGM(&tmv);
    if (t == (time_t)-1) return -1;
    *out = t;
    return 0;
}

/* now() -> string (ISO-8601 UTC) */
static int adapt_now(ASTNode *node, ASTNode *args) {
    (void)node; (void)args;
    char buf[32];
    te_format_iso_utc(time(NULL), buf, sizeof(buf));
    add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, buf, NULL));
    return 1;
}

/* now_epoch() -> int (seconds since 1970-01-01 UTC) */
static int adapt_now_epoch(ASTNode *node, ASTNode *args) {
    (void)node; (void)args;
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", (int)time(NULL), NULL, NULL));
    return 1;
}

/* now_ms() -> int (milliseconds since first call, monotonic).
 * Safe for benchmark deltas: fits in int32 for ~24 days of uptime. */
static int adapt_now_ms(ASTNode *node, ASTNode *args) {
    (void)node; (void)args;
    static struct timespec t0 = {0, 0};
    static int initialized = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!initialized) { t0 = ts; initialized = 1; }
    long long ms = (long long)(ts.tv_sec - t0.tv_sec) * 1000LL
                 + (long long)(ts.tv_nsec - t0.tv_nsec) / 1000000LL;
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", (int)ms, NULL, NULL));
    return 1;
}

/* date_parse("YYYY-MM-DDTHH:MM:SSZ") -> int (epoch seconds, 0 on error) */
static int adapt_date_parse(ASTNode *node, ASTNode *args) {
    (void)node;
    /* NOTE: do NOT call evaluate_native_args here — it permanently mutates
     * IDENTIFIER nodes into STRING/NUMBER leafs, which breaks reuse inside
     * LINQ lambdas (every iteration would read the first item's value).
     * get_node_string() resolves identifiers safely on each call. */
    char *s = args ? get_node_string(args) : NULL;
    time_t t = 0;
    int rc = te_parse_iso_utc(s, &t);
    if (s) free(s);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", rc == 0 ? (int)t : 0, NULL, NULL));
    return 1;
}

/* date_format(epoch_int [, fmt_str]) -> string. Default fmt = ISO-8601 UTC. */
static int adapt_date_format(ASTNode *node, ASTNode *args) {
    (void)node;
    /* NOTE: evaluate_native_args mutates AST permanently — skip it. */
    time_t t = (time_t)(args ? (int)evaluate_expression(args) : 0);
    ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
    char *fmt = a1 ? get_node_string(a1) : NULL;
    struct tm g;
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char buf[128];
    strftime(buf, sizeof(buf), (fmt && *fmt) ? fmt : "%Y-%m-%dT%H:%M:%SZ", &g);
    if (fmt) free(fmt);
    add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, buf, NULL));
    return 1;
}

/* date_add(iso_string, unit, n) -> string. unit in {seconds,minutes,hours,days}. */
static int adapt_date_add(ASTNode *node, ASTNode *args) {
    (void)node;
    /* NOTE: evaluate_native_args mutates AST permanently — skip it. */
    char *s = args ? get_node_string(args) : NULL;
    ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
    char *unit = a1 ? get_node_string(a1) : NULL;
    ASTNode *a2 = a1 ? a1->next : NULL; /* gotcha #1: 3rd arg via ->next */
    long long n = a2 ? (long long)evaluate_expression(a2) : 0;
    time_t t = 0;
    int rc = te_parse_iso_utc(s, &t);
    if (rc != 0) {
        if (s) free(s); if (unit) free(unit);
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, "", NULL));
        return 1;
    }
    long long mult = 1;
    if (unit) {
        if (!strcmp(unit, "seconds") || !strcmp(unit, "second")) mult = 1;
        else if (!strcmp(unit, "minutes") || !strcmp(unit, "minute")) mult = 60;
        else if (!strcmp(unit, "hours") || !strcmp(unit, "hour")) mult = 3600;
        else if (!strcmp(unit, "days") || !strcmp(unit, "day")) mult = 86400;
    }
    t += (time_t)(n * mult);
    char buf[32];
    te_format_iso_utc(t, buf, sizeof(buf));
    if (s) free(s); if (unit) free(unit);
    add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, buf, NULL));
    return 1;
}

/* date_diff(iso_a, iso_b, unit) -> int (a - b in unit; 0 on parse error). */
static int adapt_date_diff(ASTNode *node, ASTNode *args) {
    (void)node;
    /* NOTE: evaluate_native_args mutates AST permanently — skip it. */
    char *sa = args ? get_node_string(args) : NULL;
    ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
    char *sb = a1 ? get_node_string(a1) : NULL;
    ASTNode *a2 = a1 ? a1->next : NULL; /* gotcha #1: 3rd arg via ->next */
    char *unit = a2 ? get_node_string(a2) : NULL;
    time_t ta = 0, tb = 0;
    int ra = te_parse_iso_utc(sa, &ta);
    int rb = te_parse_iso_utc(sb, &tb);
    long long diff = 0;
    if (ra == 0 && rb == 0) {
        diff = (long long)ta - (long long)tb;
        long long div = 1;
        if (unit) {
            if (!strcmp(unit, "minutes") || !strcmp(unit, "minute")) div = 60;
            else if (!strcmp(unit, "hours") || !strcmp(unit, "hour")) div = 3600;
            else if (!strcmp(unit, "days") || !strcmp(unit, "day")) div = 86400;
        }
        diff /= div;
    }
    if (sa) free(sa); if (sb) free(sb); if (unit) free(unit);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", (int)diff, NULL, NULL));
    return 1;
}

/* uuid_v4() -> string (RFC 4122 v4, random). Uses rand(); seeded lazily. */
static int adapt_uuid_v4(ASTNode *node, ASTNode *args) {
    (void)node; (void)args;
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)(time(NULL) ^ (uintptr_t)&seeded));
        seeded = 1;
    }
    unsigned char b[16];
    for (int i = 0; i < 16; i++) b[i] = (unsigned char)(rand() & 0xFF);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40); /* version 4 */
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80); /* variant 10 */
    char out[37];
    snprintf(out, sizeof(out),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7], b[8],b[9],
        b[10],b[11],b[12],b[13],b[14],b[15]);
    add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, out, NULL));
    return 1;
}

/* uuid_valid("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx") -> bool (true/false). */
static int adapt_uuid_valid(ASTNode *node, ASTNode *args) {
    (void)node;
    /* NOTE: evaluate_native_args mutates AST permanently — skip it. */
    char *s = args ? get_node_string(args) : NULL;
    int ok = 0;
    if (s && strlen(s) == 36) {
        ok = 1;
        for (int i = 0; i < 36 && ok; i++) {
            char c = s[i];
            if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') ok = 0; }
            else { if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) ok = 0; }
        }
    }
    if (s) free(s);
    /* gotcha #6: uuid_valid es un predicado; devolver BOOL (true/false) en vez
     * de INT 1/0 para que `if (uuid_valid(x))` y println muestren booleanos. */
    add_or_update_variable("__ret__",
        create_ast_leaf_number("BOOL", ok, NULL, NULL));
    return 1;
}

/* Called once by te_builtins_ensure_loaded(). Idempotent — overwrites are OK.
 * Add new core builtins here as one-liners. */
void te_register_ast_builtins(void) {
    te_builtin_register("mysql_connect",     adapt_mysql_connect);
    te_builtin_register("mysql_query",       adapt_mysql_query);
    te_builtin_register("mysql_close",       adapt_mysql_close);
    te_builtin_register("postgres_connect",  adapt_postgres_connect);
    te_builtin_register("postgres_query",    adapt_postgres_query);
    te_builtin_register("postgres_close",    adapt_postgres_close);
    te_builtin_register("sqlserver_connect", adapt_sqlserver_connect);
    te_builtin_register("sqlserver_query",   adapt_sqlserver_query);
    te_builtin_register("sqlserver_close",   adapt_sqlserver_close);
    te_builtin_register("load_native",       adapt_load_native);
    te_builtin_register("env",               adapt_env);
    te_builtin_register("env_required",      adapt_env_required);
    te_builtin_register("debug_log",         adapt_debug_log);
    /* datetime + uuid (v1.0.0) */
    te_builtin_register("now",               adapt_now);
    te_builtin_register("now_epoch",         adapt_now_epoch);
    te_builtin_register("now_ms",            adapt_now_ms);
    te_builtin_register("date_parse",        adapt_date_parse);
    te_builtin_register("date_format",       adapt_date_format);
    te_builtin_register("date_add",          adapt_date_add);
    te_builtin_register("date_diff",         adapt_date_diff);
    te_builtin_register("uuid_v4",           adapt_uuid_v4);
    te_builtin_register("uuid_valid",        adapt_uuid_valid);
    /* subprocess language bridge (lang_spawn/lang_call/...) */
    te_register_bridge_builtins();
    /* cooperative async runtime (spawn/await_task/await_all/lang_call_async) */
    te_register_async_builtins();
    /* real async runtime: fiber-based event loop (go/sleep_async/await_async) */
    te_register_evloop_builtins();
    /* Other builtins (json, xml, request_*, response_*, len, range,
     * read_file, http_get, etc.) still served by the legacy if-chains
     * in call_native_function / te_builtin_dispatch. They can be migrated
     * here incrementally — this is now backwards-compatible by design. */
}

/* Plugin ABI: fill the host API struct that `.so` modules receive. The
 * pointers below are to file-static helpers earlier in this translation
 * unit, so plugins get a stable surface without linking the host binary. */
static char  *host_arg_string_dup(ASTNode *arg) {
    const char *s = te_arg_string(arg);
    if (s) return strdup(s);
    /* Fallback: te_arg_string sólo cubre STRING literal e IDENTIFIER->string.
     * Para expresiones (ACCESS_ATTR `obj.campo`, CALL_FUNC, concatenaciones)
     * delegamos en get_node_string, que evalúa el nodo y devuelve memoria
     * propia. Esto permite usar `obj.campo` como valor en un mapa de
     * parámetros Dapper de cualquier bridge DB (mysql/postgres/sqlite/...). */
    if (arg) return get_node_string(arg);
    return NULL;
}
static int    host_arg_int(ASTNode *arg, int defv) {
    if (!arg) return defv;
    /* Tipos directos (NUMBER/INT o IDENTIFIER->int) por la vía rápida. */
    if (arg->type &&
        (strcmp(arg->type, "NUMBER") == 0 || strcmp(arg->type, "INT") == 0 ||
         strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0))
        return te_arg_int(arg, defv);
    /* Fallback para ACCESS_ATTR numérico (`obj.campo`) y otras expresiones. */
    return (int)evaluate_expression(arg);
}
static double host_arg_float(ASTNode *arg, double defv) {
    if (!arg) return defv;
    return evaluate_expression(arg);
}
static void   host_set_ret_int(int v)            { te_set_ret_int(v); }
static void   host_set_ret_str(const char *s)    { te_set_ret_string(s); }
static void   host_set_ret_float(double v) {
    char buf[64]; te_fmt_double(buf, sizeof(buf), v);
    ASTNode *r = create_ast_leaf("FLOAT", 0, buf, NULL);
    add_or_update_variable("__ret__", r);
    free_ast(r);
}

/* ABI v2 — Dapper-style params para plugins DB.
 * Reusa db_arg_as_map_head (mismo helper que usan los bridges core
 * mysql/postgres/sqlserver). El plugin recibe el head crudo y lo
 * camina via ast.h. Si `*owned == 1` el plugin debe llamar a
 * host->free_node(head) cuando termine.
 *
 * IMPORTANTE: aquí recibimos `arg` (un único ASTNode) en lugar de la
 * lista de args al estilo (args, idx). db_arg_as_map_head espera una
 * lista — le pasamos `arg` directamente como si fuese una lista de un
 * solo elemento e idx=0 (su iteración `for (i<idx) cur=cur->right` con
 * idx=0 termina inmediatamente y devuelve el primer nodo). */
static ASTNode *host_arg_map_head(ASTNode *arg, int *out_owned) {
    return db_arg_as_map_head(arg, 0, out_owned);
}
static void host_free_node(ASTNode *n) {
    if (n) free_ast(n);
}

/* ABI v3 — auto-cleanup de conexiones por request.
 * Registro de callbacks que los plugins DB (sqlite, ...) registran para que
 * el runtime los invoque al final de cada request HTTP. Almacenamiento aquí
 * (en el host), invocado desde runtime_reset_vars_to_initial_state (ast.c). */
#define TE_DB_CLEANUP_MAX 16
static void (*g_db_cleanup_hooks[TE_DB_CLEANUP_MAX])(void);
static int g_db_cleanup_count = 0;

void te_db_register_request_cleanup(void (*fn)(void)) {
    if (!fn) return;
    for (int i = 0; i < g_db_cleanup_count; i++)
        if (g_db_cleanup_hooks[i] == fn) return;   /* dedupe */
    if (g_db_cleanup_count < TE_DB_CLEANUP_MAX)
        g_db_cleanup_hooks[g_db_cleanup_count++] = fn;
}

void te_db_run_request_cleanup_hooks(void) {
    for (int i = 0; i < g_db_cleanup_count; i++)
        if (g_db_cleanup_hooks[i]) g_db_cleanup_hooks[i]();
}

static void host_register_request_cleanup(void (*fn)(void)) {
    te_db_register_request_cleanup(fn);
}
extern int g_db_request_phase;
static int host_db_request_phase(void) { return g_db_request_phase; }

void te_fill_host_api(TEHostAPI *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->abi_version      = TE_HOST_API_VERSION;
    out->struct_size      = (int)sizeof(TEHostAPI);
    out->register_builtin = te_builtin_register;
    out->set_ret_int      = host_set_ret_int;
    out->set_ret_str      = host_set_ret_str;
    out->set_ret_float    = host_set_ret_float;
    out->arg_string       = host_arg_string_dup;
    out->arg_int          = host_arg_int;
    out->arg_float        = host_arg_float;
    /* ABI v2 */
    out->arg_map_head     = host_arg_map_head;
    out->free_node        = host_free_node;
    /* ABI v3 */
    out->register_request_cleanup = host_register_request_cleanup;
    out->db_request_phase         = host_db_request_phase;
}
