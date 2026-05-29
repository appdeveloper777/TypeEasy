/* te_colcache.h — TypeEasy columnar cache + lambda specializer (LINQ fast-path).
 *
 * Extracted from ast.c (Fase 2 paso 3). Contains:
 *   - LambdaSpec enum + FastLambda struct (lambda body specialization)
 *   - TeColCache struct (columnar mirror for homogeneous OBJECT lists)
 *   - SIMD AVX2 int64 compare path
 *   - te_openmp_enabled / te_fastpath_enabled runtime gates
 *   - fast_lambda_analyze / fast_eval (used by all LINQ methods)
 *   - te_colcache_build / invalidate / build_from_mask / eval_pred / sum / count
 *
 * Both ast.c and te_csv.c consume this module: ast.c via interpret_call_method
 * LINQ paths (where/sumBy/countWhere/etc.); te_csv.c calls te_colcache_build
 * at the tail of from_csv_to_list.
 */
#ifndef TE_COLCACHE_H
#define TE_COLCACHE_H

#include "ast.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OpenMP threshold below which we don't parallelize column scans.
 * Empirically, pure aggregates over int64/double columns are memory-bound:
 * a single core saturates ~10GB/s, so 10M int64 (80MB) finishes in ~8ms
 * while 8-thread fork/join + barrier overhead is ~5–20ms on typical Linux.
 * Raising the threshold avoids slowdowns on small/medium datasets without
 * hurting truly large workloads. */
#define TE_OMP_MIN_N 50000000

int te_openmp_enabled(void);
int te_fastpath_enabled(void);

/* ---- Lambda specializer ---------------------------------------------------- */

typedef enum {
    SPEC_NONE = 0,
    SPEC_IDENT,
    SPEC_ATTR,
    SPEC_CMP_GT, SPEC_CMP_LT, SPEC_CMP_GE, SPEC_CMP_LE, SPEC_CMP_EQ, SPEC_CMP_NE,
    SPEC_MOD_K,
    SPEC_CMP_EQ_STR, SPEC_CMP_NE_STR,
    /* v0.0.14 Paso 2: aritmética simple sobre atributos del parámetro.
     *   ATTR_ATTR: p => p.a OP p.b  (segundo atributo en attr_name2)
     *   ATTR_K:    p => p.a OP k    (constante en k / k_d) */
    SPEC_MUL_ATTR_ATTR, SPEC_ADD_ATTR_ATTR, SPEC_SUB_ATTR_ATTR,
    SPEC_MUL_ATTR_K,    SPEC_ADD_ATTR_K,    SPEC_SUB_ATTR_K
} LambdaSpec;

typedef struct {
    LambdaSpec  spec;
    const char *attr_name;      /* borrowed from AST, do not free */
    long long   k;
    double      k_d;
    int         k_is_float;
    const char *k_str;          /* SPEC_CMP_EQ_STR/NE_STR: borrowed from AST */
    ClassNode  *cached_class;
    int         cached_idx;
    /* v0.0.14 Paso 2: segundo atributo para specs ATTR_ATTR. */
    const char *attr_name2;     /* borrowed from AST, do not free */
    ClassNode  *cached_class2;
    int         cached_idx2;
} FastLambda;

LambdaSpec fast_lambda_analyze(ASTNode *fn, FastLambda *out);

/* Inline accessors used by all LINQ hot loops. Defined in the header so the
 * compiler can fully inline them at every call site (ast.c + te_colcache.c). */
static inline int fl_attr_idx(FastLambda *fl, ObjectNode *obj) {
    if (!obj || !obj->class) return -1;
    if (fl->cached_class == obj->class && fl->cached_idx >= 0) return fl->cached_idx;
    for (int i = 0; i < obj->class->attr_count; i++) {
        const char *nm = obj->class->attributes[i].id;
        if (nm && strcmp(nm, fl->attr_name) == 0) {
            fl->cached_class = obj->class;
            fl->cached_idx = i;
            return i;
        }
    }
    return -1;
}

static inline int fl_read_attr(FastLambda *fl, ASTNode *item, double *out_d, int *out_is_int) {
    if (!item || !item->type) return 0;
    if (strcmp(item->type, "OBJECT") != 0 || !item->extra) return 0;
    ObjectNode *obj = (ObjectNode*)item->extra;
    int idx = fl_attr_idx(fl, obj);
    if (idx < 0) return 0;
    Variable *a = &obj->attributes[idx];
    if (a->vtype == VAL_INT)   { *out_d = (double)a->value.int_value; *out_is_int = 1; return 1; }
    if (a->vtype == VAL_FLOAT) { *out_d = a->value.float_value;       *out_is_int = 0; return 1; }
    if (a->vtype == VAL_STRING && a->value.string_value) {
        char *end = NULL;
        double d = strtod(a->value.string_value, &end);
        if (end && end != a->value.string_value) {
            *out_d = d;
            *out_is_int = (d == (double)(long long)d);
            return 1;
        }
    }
    return 0;
}

static inline const char* fl_read_attr_str(FastLambda *fl, ASTNode *item) {
    if (!item || !item->type) return NULL;
    if (strcmp(item->type, "OBJECT") != 0 || !item->extra) return NULL;
    ObjectNode *obj = (ObjectNode*)item->extra;
    int idx = fl_attr_idx(fl, obj);
    if (idx < 0) return NULL;
    Variable *a = &obj->attributes[idx];
    if (a->vtype == VAL_STRING) return a->value.string_value;
    return NULL;
}

/* v0.0.14 Paso 2: lector del SEGUNDO atributo (specs ATTR_ATTR). Idéntico a
 * fl_attr_idx/fl_read_attr pero usando attr_name2/cached_class2/cached_idx2. */
static inline int fl_attr_idx2(FastLambda *fl, ObjectNode *obj) {
    if (!obj || !obj->class) return -1;
    if (fl->cached_class2 == obj->class && fl->cached_idx2 >= 0) return fl->cached_idx2;
    for (int i = 0; i < obj->class->attr_count; i++) {
        const char *nm = obj->class->attributes[i].id;
        if (nm && fl->attr_name2 && strcmp(nm, fl->attr_name2) == 0) {
            fl->cached_class2 = obj->class;
            fl->cached_idx2 = i;
            return i;
        }
    }
    return -1;
}

static inline int fl_read_attr2(FastLambda *fl, ASTNode *item, double *out_d, int *out_is_int) {
    if (!item || !item->type) return 0;
    if (strcmp(item->type, "OBJECT") != 0 || !item->extra) return 0;
    ObjectNode *obj = (ObjectNode*)item->extra;
    int idx = fl_attr_idx2(fl, obj);
    if (idx < 0) return 0;
    Variable *a = &obj->attributes[idx];
    if (a->vtype == VAL_INT)   { *out_d = (double)a->value.int_value; *out_is_int = 1; return 1; }
    if (a->vtype == VAL_FLOAT) { *out_d = a->value.float_value;       *out_is_int = 0; return 1; }
    if (a->vtype == VAL_STRING && a->value.string_value) {
        char *end = NULL;
        double d = strtod(a->value.string_value, &end);
        if (end && end != a->value.string_value) {
            *out_d = d;
            *out_is_int = (d == (double)(long long)d);
            return 1;
        }
    }
    return 0;
}

static inline int fast_eval(FastLambda *fl, ASTNode *item, double *out_d, int *out_is_int) {
    if (fl->spec == SPEC_NONE) return 0;
    if (fl->spec == SPEC_IDENT) {
        if (!item || !item->type) return 0;
        if (strcmp(item->type, "NUMBER") == 0 || strcmp(item->type, "INT") == 0) {
            *out_d = (double)item->value; *out_is_int = 1; return 1;
        }
        if (strcmp(item->type, "FLOAT") == 0 && item->str_value) {
            *out_d = atof(item->str_value); *out_is_int = 0; return 1;
        }
        return 0;
    }
    if (fl->spec == SPEC_CMP_EQ_STR || fl->spec == SPEC_CMP_NE_STR) {
        const char *s = fl_read_attr_str(fl, item);
        if (!s) return 0;
        int eq = (fl->k_str && strcmp(s, fl->k_str) == 0);
        int b = (fl->spec == SPEC_CMP_EQ_STR) ? eq : !eq;
        *out_d = (double)b; *out_is_int = 1; return 1;
    }
    double av; int av_is_int;
    if (!fl_read_attr(fl, item, &av, &av_is_int)) return 0;
    if (fl->spec == SPEC_ATTR) { *out_d = av; *out_is_int = av_is_int; return 1; }
    /* v0.0.14 Paso 2: aritmética attr OP attr. */
    if (fl->spec == SPEC_MUL_ATTR_ATTR || fl->spec == SPEC_ADD_ATTR_ATTR ||
        fl->spec == SPEC_SUB_ATTR_ATTR) {
        double bv; int bv_is_int;
        if (!fl_read_attr2(fl, item, &bv, &bv_is_int)) return 0;
        double r = (fl->spec == SPEC_MUL_ATTR_ATTR) ? av * bv
                 : (fl->spec == SPEC_ADD_ATTR_ATTR) ? av + bv
                 :                                    av - bv;
        *out_d = r;
        *out_is_int = (av_is_int && bv_is_int && r == (double)(long long)r);
        return 1;
    }
    /* v0.0.14 Paso 2: aritmética attr OP constante. */
    if (fl->spec == SPEC_MUL_ATTR_K || fl->spec == SPEC_ADD_ATTR_K ||
        fl->spec == SPEC_SUB_ATTR_K) {
        double kk = fl->k_is_float ? fl->k_d : (double)fl->k;
        double r = (fl->spec == SPEC_MUL_ATTR_K) ? av * kk
                 : (fl->spec == SPEC_ADD_ATTR_K) ? av + kk
                 :                                  av - kk;
        *out_d = r;
        *out_is_int = (av_is_int && !fl->k_is_float && r == (double)(long long)r);
        return 1;
    }
    if (fl->spec == SPEC_MOD_K) {
        if (fl->k == 0) return 0;
        *out_d = (double)((long long)av % fl->k);
        *out_is_int = 1;
        return 1;
    }
    double k = fl->k_is_float ? fl->k_d : (double)fl->k;
    int b = 0;
    switch (fl->spec) {
        case SPEC_CMP_GT: b = (av >  k); break;
        case SPEC_CMP_LT: b = (av <  k); break;
        case SPEC_CMP_GE: b = (av >= k); break;
        case SPEC_CMP_LE: b = (av <= k); break;
        case SPEC_CMP_EQ: b = (av == k); break;
        case SPEC_CMP_NE: b = (av != k); break;
        default: return 0;
    }
    *out_d = (double)b; *out_is_int = 1; return 1;
}

/* ---- Columnar cache ------------------------------------------------------- */

typedef struct TeColCache {
    ClassNode *cls;
    int        n_rows;
    int        nattr;
    int       *kinds;          /* per-attr: 0=INT, 1=FLOAT, 2=STRING, 3=OTHER */
    int64_t  **int_cols;       /* int_cols[k] or NULL */
    double   **flt_cols;       /* flt_cols[k] or NULL */
    const char ***str_cols;    /* str_cols[k] or NULL; entries borrowed */
    ASTNode  **items;          /* parallel item pointer array, length n_rows */
    ASTNode   *owner;          /* the LIST ASTNode this cache belongs to */
    struct TeColCache **children;
    int        n_children;
    int        cap_children;
    struct TeColCache *parent; /* NULL if root (CSV-load cache) */
    /* v0.0.14 pulimiento #5 (LINQ pipeline fusion): vista LAZY de un parent
     * sin compactaci\u00f3n f\u00edsica. lazy_mask es un buffer de bytes de tama\u00f1o
     * parent->n_rows; n_rows = popcount(lazy_mask). int_cols/flt_cols/
     * str_cols/items son TODOS NULL hasta materializaci\u00f3n.
     * Aggregates (sum/count/length) operan directo sobre parent + mask.
     * Eval_pred fuerza materializaci\u00f3n (poblar *_cols filtrando parent). */
    char      *lazy_mask;      /* owned; NULL si no es vista lazy */
} TeColCache;

/* Attribute index lookup helper (also used by ast.c). */
int te_class_attr_idx(ClassNode *cls, const char *name);

/* Public colcache API. */
void       te_colcache_build(ASTNode *list_head, ClassNode *cls);
/* v0.0.13 (perf): adjuntar un TeColCache YA construido (p.ej. por te_csv.c
 * durante el parseo) sin tener que recorrer la lista de objetos. Asume
 * c->cls/n_rows/nattr/kinds/*_cols/items ya poblados. Inicializa owner y
 * children y cuelga `c` del listNode. */
void       te_colcache_attach_prebuilt(ASTNode *list_head, TeColCache *c);
void       te_colcache_invalidate(ASTNode *list_head);
TeColCache *te_colcache_build_from_mask(TeColCache *parent, const char *mask,
                                        ASTNode *result_list_head,
                                        ASTNode **selected_items, int new_n);
/* v0.0.14 pulimiento #5: vista LAZY (sin compactaci\u00f3n) — el caller cede
 * ownership del buffer mask. result_list_head->col_cache queda apuntando a
 * la vista; aggregates redirigen a parent + mask. */
TeColCache *te_colcache_attach_lazy(TeColCache *parent, char *mask,
                                    ASTNode *result_list_head, int new_n);
/* Materializa una vista lazy in-place: puebla *_cols filtrando parent por
 * lazy_mask, libera lazy_mask. items[] NO se crea (no se necesita para
 * aggregates). No-op si la vista no es lazy. */
void        te_colcache_materialize(TeColCache *view);
int        te_colcache_eval_pred(TeColCache *c, int attr_idx,
                                 const FastLambda *fl, char *mask);
int        te_colcache_sum(TeColCache *c, int attr_idx, const char *mask,
                           long long *out_i, double *out_d, int *out_is_int);
long long  te_colcache_count(TeColCache *c, const char *mask);
/* v0.0.14 Paso 3: \u00edndice de fila con el valor m\u00ednimo (want_max=0) o m\u00e1ximo
 * (want_max=1) en una columna INT/FLOAT. mask opcional. Devuelve 1 y setea
 * *out_idx (\u00edndice en el espacio de filas de `c`). No soporta vistas lazy
 * (requiere items[] del caller para devolver el objeto). */
int        te_colcache_minmax(TeColCache *c, int attr_idx, int want_max,
                              const char *mask, int *out_idx);

#ifdef __cplusplus
}
#endif

#endif /* TE_COLCACHE_H */
