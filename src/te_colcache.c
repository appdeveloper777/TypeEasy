/* te_colcache.c — TypeEasy columnar cache + lambda specializer.
 *
 * See te_colcache.h for the public surface.
 * Extracted from ast.c (Fase 2 paso 3).
 */
#define _GNU_SOURCE
#include "te_colcache.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TE_HAVE_OPENMP
#include <omp.h>
#endif

#if defined(__AVX2__)
#include <immintrin.h>
#endif

/* ============================================================================
 * Runtime gates
 * ========================================================================== */

int te_fastpath_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("TE_FASTPATH");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
}

int te_openmp_enabled(void) {
#ifdef TE_HAVE_OPENMP
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("TE_OPENMP");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
#else
    return 0;
#endif
}

/* ============================================================================
 * Columnar cache
 * ========================================================================== */

static void te_colcache_free(TeColCache *c) {
    if (!c) return;
    if (c->int_cols) { for (int k=0;k<c->nattr;k++) free(c->int_cols[k]); free(c->int_cols); }
    if (c->flt_cols) { for (int k=0;k<c->nattr;k++) free(c->flt_cols[k]); free(c->flt_cols); }
    if (c->str_cols) { for (int k=0;k<c->nattr;k++) free(c->str_cols[k]); free(c->str_cols); }
    free(c->kinds);
    free(c->items);
    free(c->children);
    free(c);
}

void te_colcache_invalidate(ASTNode *list_head) {
    if (!list_head || !list_head->col_cache) return;
    TeColCache *c = (TeColCache*)list_head->col_cache;
    list_head->col_cache = NULL;
    for (int i = 0; i < c->n_children; i++) {
        TeColCache *ch = c->children[i];
        if (ch && ch->owner) te_colcache_invalidate(ch->owner);
    }
    if (c->parent) {
        TeColCache *p = c->parent;
        for (int i = 0; i < p->n_children; i++) {
            if (p->children[i] == c) {
                p->children[i] = p->children[p->n_children - 1];
                p->n_children--;
                break;
            }
        }
    }
    te_colcache_free(c);
}

static void te_colcache_add_child(TeColCache *parent, TeColCache *child) {
    if (!parent || !child) return;
    if (parent->n_children >= parent->cap_children) {
        int nc = parent->cap_children ? parent->cap_children * 2 : 4;
        parent->children = (struct TeColCache**)realloc(parent->children, (size_t)nc * sizeof(struct TeColCache*));
        parent->cap_children = nc;
    }
    parent->children[parent->n_children++] = child;
    child->parent = parent;
}

static int te_colcache_kind_for_attr(ClassNode *cls, int attr_idx) {
    if (!cls || attr_idx < 0 || attr_idx >= cls->attr_count) return 3;
    const char *t = cls->attributes[attr_idx].type;
    if (!t) return 3;
    if (!strcmp(t,"int") || !strcmp(t,"INT") || !strcmp(t,"long") ||
        !strcmp(t,"Integer") || !strcmp(t,"bool") || !strcmp(t,"BOOL")) return 0;
    if (!strcmp(t,"float") || !strcmp(t,"FLOAT") || !strcmp(t,"double") ||
        !strcmp(t,"Double")) return 1;
    if (!strcmp(t,"string") || !strcmp(t,"STRING") || !strcmp(t,"String")) return 2;
    return 3;
}

void te_colcache_build(ASTNode *list_head, ClassNode *cls) {
    if (!list_head || !cls) return;
    if (list_head->col_cache) { te_colcache_free((TeColCache*)list_head->col_cache); list_head->col_cache = NULL; }
    int n = 0;
    for (ASTNode *it = list_head->left; it; it = it->next) n++;
    if (n <= 0) return;

    int nattr = cls->attr_count;
    if (nattr <= 0) return;

    TeColCache *c = (TeColCache*)calloc(1, sizeof(TeColCache));
    c->cls = cls;
    c->n_rows = n;
    c->nattr = nattr;
    c->kinds = (int*)calloc(nattr, sizeof(int));
    c->int_cols = (int64_t**)calloc(nattr, sizeof(int64_t*));
    c->flt_cols = (double**)calloc(nattr, sizeof(double*));
    c->str_cols = (const char***)calloc(nattr, sizeof(const char**));
    c->items = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));

    for (int k = 0; k < nattr; k++) {
        c->kinds[k] = te_colcache_kind_for_attr(cls, k);
        if (c->kinds[k] == 0) c->int_cols[k] = (int64_t*)malloc((size_t)n * sizeof(int64_t));
        else if (c->kinds[k] == 1) c->flt_cols[k] = (double*)malloc((size_t)n * sizeof(double));
        else if (c->kinds[k] == 2) c->str_cols[k] = (const char**)malloc((size_t)n * sizeof(const char*));
    }

    /* v0.0.13 (perf): two-pass build. Pass 1 (serial, fast): walk linked
     * list ONCE to populate items[] and validate row classes. Pass 2
     * (parallel OpenMP): fill column buffers from items[] using random
     * access (each iteration independent).
     *
     * Before this change, the single-pass walk dominated CSV-load time
     * (~650ms for 10M rows). Now Pass 2 scales near-linearly with cores
     * (target ~100ms on 8 cores). */
    int i = 0;
    for (ASTNode *it = list_head->left; it; it = it->next, i++) {
        if (!it->type || strcmp(it->type, "OBJECT") != 0 || !it->extra) {
            te_colcache_free(c);
            return;
        }
        ObjectNode *obj = (ObjectNode*)it->extra;
        if (obj->class != cls) {
            te_colcache_free(c);
            return;
        }
        c->items[i] = it;
        obj->owning_list = (void*)list_head;
    }
#ifdef TE_HAVE_OPENMP
    if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < n; j++) {
            ObjectNode *obj = (ObjectNode*)c->items[j]->extra;
            for (int k = 0; k < nattr; k++) {
                Variable *a = &obj->attributes[k];
                switch (c->kinds[k]) {
                    case 0: {
                        int64_t v = 0;
                        if (a->vtype == VAL_INT) v = (int64_t)a->value.int_value;
                        else if (a->vtype == VAL_FLOAT) v = (int64_t)a->value.float_value;
                        else if (a->vtype == VAL_STRING && a->value.string_value) {
                            char *end = NULL; long long ll = strtoll(a->value.string_value, &end, 10);
                            if (end != a->value.string_value) v = (int64_t)ll;
                        }
                        c->int_cols[k][j] = v;
                        break;
                    }
                    case 1: {
                        double v = 0.0;
                        if (a->vtype == VAL_FLOAT) v = a->value.float_value;
                        else if (a->vtype == VAL_INT) v = (double)a->value.int_value;
                        else if (a->vtype == VAL_STRING && a->value.string_value) {
                            char *end = NULL; double d = strtod(a->value.string_value, &end);
                            if (end != a->value.string_value) v = d;
                        }
                        c->flt_cols[k][j] = v;
                        break;
                    }
                    case 2: {
                        c->str_cols[k][j] = (a->vtype == VAL_STRING) ? a->value.string_value : NULL;
                        break;
                    }
                    default: break;
                }
            }
        }
    } else
#endif
    {
        for (int j = 0; j < n; j++) {
            ObjectNode *obj = (ObjectNode*)c->items[j]->extra;
            for (int k = 0; k < nattr; k++) {
                Variable *a = &obj->attributes[k];
                switch (c->kinds[k]) {
                    case 0: {
                        int64_t v = 0;
                        if (a->vtype == VAL_INT) v = (int64_t)a->value.int_value;
                        else if (a->vtype == VAL_FLOAT) v = (int64_t)a->value.float_value;
                        else if (a->vtype == VAL_STRING && a->value.string_value) {
                            char *end = NULL; long long ll = strtoll(a->value.string_value, &end, 10);
                            if (end != a->value.string_value) v = (int64_t)ll;
                        }
                        c->int_cols[k][j] = v;
                        break;
                    }
                    case 1: {
                        double v = 0.0;
                        if (a->vtype == VAL_FLOAT) v = a->value.float_value;
                        else if (a->vtype == VAL_INT) v = (double)a->value.int_value;
                        else if (a->vtype == VAL_STRING && a->value.string_value) {
                            char *end = NULL; double d = strtod(a->value.string_value, &end);
                            if (end != a->value.string_value) v = d;
                        }
                        c->flt_cols[k][j] = v;
                        break;
                    }
                    case 2: {
                        c->str_cols[k][j] = (a->vtype == VAL_STRING) ? a->value.string_value : NULL;
                        break;
                    }
                    default: break;
                }
            }
        }
    }
    list_head->col_cache = c;
    c->owner = list_head;
}

void te_colcache_attach_prebuilt(ASTNode *list_head, TeColCache *c) {
    if (!list_head || !c) return;
    if (list_head->col_cache) {
        te_colcache_free((TeColCache*)list_head->col_cache);
        list_head->col_cache = NULL;
    }
    c->owner = list_head;
    c->parent = NULL;
    c->children = NULL;
    c->n_children = 0;
    c->cap_children = 0;
    list_head->col_cache = c;
    /* NOTA: NO seteamos obj->owning_list aquí. Caches prebuilt provienen
     * de cargas CSV de sólo-lectura; pagar el walk de 10M objects (~230ms)
     * para soportar mutación post-load que casi nunca ocurre es mal trade.
     * Si en el futuro se permite mutar tras carga, hacer el setup en la
     * primera mutación o gatear por flag. */
}

TeColCache *te_colcache_build_from_mask(TeColCache *parent, const char *mask,
                                        ASTNode *result_list_head,
                                        ASTNode **selected_items, int new_n) {
    if (!parent || !mask || new_n <= 0) return NULL;
    TeColCache *c = (TeColCache*)calloc(1, sizeof(TeColCache));
    c->cls = parent->cls;
    c->n_rows = new_n;
    c->nattr = parent->nattr;
    c->kinds = (int*)malloc((size_t)c->nattr * sizeof(int));
    memcpy(c->kinds, parent->kinds, (size_t)c->nattr * sizeof(int));
    c->int_cols = (int64_t**)calloc(c->nattr, sizeof(int64_t*));
    c->flt_cols = (double**)calloc(c->nattr, sizeof(double*));
    c->str_cols = (const char***)calloc(c->nattr, sizeof(const char**));
    c->items = (ASTNode**)malloc((size_t)new_n * sizeof(ASTNode*));
    if (selected_items) memcpy(c->items, selected_items, (size_t)new_n * sizeof(ASTNode*));
    for (int k = 0; k < c->nattr; k++) {
        if (c->kinds[k] == 0 && parent->int_cols[k]) {
            c->int_cols[k] = (int64_t*)malloc((size_t)new_n * sizeof(int64_t));
            int w = 0;
            const int64_t *src = parent->int_cols[k];
            for (int r = 0; r < parent->n_rows; r++) if (mask[r]) c->int_cols[k][w++] = src[r];
        } else if (c->kinds[k] == 1 && parent->flt_cols[k]) {
            c->flt_cols[k] = (double*)malloc((size_t)new_n * sizeof(double));
            int w = 0;
            const double *src = parent->flt_cols[k];
            for (int r = 0; r < parent->n_rows; r++) if (mask[r]) c->flt_cols[k][w++] = src[r];
        } else if (c->kinds[k] == 2 && parent->str_cols[k]) {
            c->str_cols[k] = (const char**)malloc((size_t)new_n * sizeof(const char*));
            int w = 0;
            const char **src = parent->str_cols[k];
            for (int r = 0; r < parent->n_rows; r++) if (mask[r]) c->str_cols[k][w++] = src[r];
        }
    }
    c->owner = result_list_head;
    if (result_list_head) result_list_head->col_cache = c;
    te_colcache_add_child(parent, c);
    return c;
}

int te_class_attr_idx(ClassNode *cls, const char *name) {
    if (!cls || !name) return -1;
    for (int i = 0; i < cls->attr_count; i++) {
        if (cls->attributes[i].id && !strcmp(cls->attributes[i].id, name)) return i;
    }
    return -1;
}

/* ============================================================================
 * SIMD AVX2 int64 compare
 * ========================================================================== */
#if defined(__AVX2__)
static inline void te_simd_cmp_i64(const int64_t *col, int n, int spec, int64_t k, char *mask) {
    int i = 0;
    __m256i vk = _mm256_set1_epi64x(k);
    for (; i + 4 <= n; i += 4) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(col + i));
        __m256i cmp;
        if (spec == 7)      cmp = _mm256_cmpeq_epi64(va, vk);
        else if (spec == 8){ cmp = _mm256_cmpeq_epi64(va, vk); cmp = _mm256_xor_si256(cmp, _mm256_set1_epi64x(-1)); }
        else if (spec == 3) cmp = _mm256_cmpgt_epi64(va, vk);
        else if (spec == 4) cmp = _mm256_cmpgt_epi64(vk, va);
        else if (spec == 5){ __m256i lt = _mm256_cmpgt_epi64(vk, va); cmp = _mm256_xor_si256(lt, _mm256_set1_epi64x(-1)); }
        else if (spec == 6){ __m256i gt = _mm256_cmpgt_epi64(va, vk); cmp = _mm256_xor_si256(gt, _mm256_set1_epi64x(-1)); }
        else { for (int j=0;j<4;j++) mask[i+j]=0; continue; }
        int64_t lanes[4];
        _mm256_storeu_si256((__m256i*)lanes, cmp);
        mask[i  ] = (lanes[0] != 0);
        mask[i+1] = (lanes[1] != 0);
        mask[i+2] = (lanes[2] != 0);
        mask[i+3] = (lanes[3] != 0);
    }
    for (; i < n; i++) {
        int b = 0;
        if (spec == 7) b = (col[i] == k);
        else if (spec == 8) b = (col[i] != k);
        else if (spec == 3) b = (col[i] >  k);
        else if (spec == 4) b = (col[i] <  k);
        else if (spec == 5) b = (col[i] >= k);
        else if (spec == 6) b = (col[i] <= k);
        mask[i] = (char)b;
    }
}
#endif

/* ============================================================================
 * Lambda specializer
 * ========================================================================== */

static int fl_is_attr_of_param(ASTNode *N, const char *pname, size_t plen) {
    if (!N || !N->type || strcmp(N->type, "ACCESS_ATTR") != 0) return 0;
    ASTNode *L = N->left, *R = N->right;
    if (!L || !L->type || !L->id) return 0;
    if (strcmp(L->type, "IDENTIFIER") != 0 && strcmp(L->type, "ID") != 0) return 0;
    if (strlen(L->id) != plen || memcmp(L->id, pname, plen) != 0) return 0;
    if (!R || !R->id) return 0;
    return 1;
}

LambdaSpec fast_lambda_analyze(ASTNode *fn, FastLambda *out) {
    out->spec = SPEC_NONE;
    out->cached_class = NULL;
    out->cached_idx = -1;
    out->attr_name = NULL;
    out->k = 0; out->k_d = 0.0; out->k_is_float = 0;
    out->k_str = NULL;
    if (!te_fastpath_enabled()) return SPEC_NONE;
    if (!fn || !fn->left) return SPEC_NONE;
    if (!fn->id || !fn->id[0]) return SPEC_NONE;

    const char *params = fn->id;
    int npar = 1;
    for (const char *q = params; *q; q++) if (*q == '\1') npar++;
    if (npar != 1) return SPEC_NONE;
    const char *p_end = params;
    while (*p_end && *p_end != '\1') p_end++;
    size_t plen = (size_t)(p_end - params);
    if (plen == 0 || plen >= 128) return SPEC_NONE;

    ASTNode *body = fn->left;
    if (body->type && strcmp(body->type, "STATEMENT_LIST") == 0) return SPEC_NONE;

    if (body->type && (strcmp(body->type, "IDENTIFIER") == 0 || strcmp(body->type, "ID") == 0)) {
        if (body->id && strlen(body->id) == plen && memcmp(body->id, params, plen) == 0) {
            out->spec = SPEC_IDENT;
            return SPEC_IDENT;
        }
        return SPEC_NONE;
    }

    if (fl_is_attr_of_param(body, params, plen)) {
        out->spec = SPEC_ATTR;
        out->attr_name = body->right->id;
        return SPEC_ATTR;
    }

    if (body->type && body->left && body->right) {
        LambdaSpec s = SPEC_NONE;
        if      (strcmp(body->type, "GT")    == 0) s = SPEC_CMP_GT;
        else if (strcmp(body->type, "LT")    == 0) s = SPEC_CMP_LT;
        else if (strcmp(body->type, "GT_EQ") == 0) s = SPEC_CMP_LE;
        else if (strcmp(body->type, "LT_EQ") == 0) s = SPEC_CMP_GE;
        else if (strcmp(body->type, "EQ")    == 0) s = SPEC_CMP_EQ;
        else if (strcmp(body->type, "DIFF")  == 0) s = SPEC_CMP_NE;
        else if (strcmp(body->type, "MOD")   == 0) s = SPEC_MOD_K;
        if (s != SPEC_NONE && fl_is_attr_of_param(body->left, params, plen)) {
            ASTNode *R = body->right;
            if (R->type && (strcmp(R->type, "NUMBER") == 0 || strcmp(R->type, "INT") == 0)) {
                out->spec = s;
                out->attr_name = body->left->right->id;
                out->k = (long long)R->value;
                out->k_d = (double)R->value;
                out->k_is_float = 0;
                return s;
            }
            if (R->type && strcmp(R->type, "FLOAT") == 0 && R->str_value) {
                out->spec = s;
                out->attr_name = body->left->right->id;
                out->k_d = atof(R->str_value);
                out->k = (long long)out->k_d;
                out->k_is_float = 1;
                return s;
            }
            if ((s == SPEC_CMP_EQ || s == SPEC_CMP_NE) &&
                R->type && strcmp(R->type, "STRING") == 0 && R->str_value) {
                LambdaSpec ss = (s == SPEC_CMP_EQ) ? SPEC_CMP_EQ_STR : SPEC_CMP_NE_STR;
                out->spec = ss;
                out->attr_name = body->left->right->id;
                out->k_str = R->str_value;
                return ss;
            }
        }
    }
    return SPEC_NONE;
}

/* ============================================================================
 * Columnar evaluators
 * ========================================================================== */

int te_colcache_eval_pred(TeColCache *c, int attr_idx, const FastLambda *fl, char *mask) {
    if (!c || attr_idx < 0 || attr_idx >= c->nattr || !fl) return 0;
    int n = c->n_rows;
    int kind = c->kinds[attr_idx];
    int spec = (int)fl->spec;

    if (spec == SPEC_CMP_EQ_STR || spec == SPEC_CMP_NE_STR) {
        if (kind != 2 || !c->str_cols[attr_idx] || !fl->k_str) return 0;
        const char **col = c->str_cols[attr_idx];
        const char *kstr = fl->k_str;
        int want_eq = (spec == SPEC_CMP_EQ_STR);
#ifdef TE_HAVE_OPENMP
        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < n; i++) {
                const char *s = col[i];
                int eq = (s && kstr && strcmp(s, kstr) == 0);
                mask[i] = (char)(want_eq ? eq : !eq);
            }
            return 1;
        }
#endif
        for (int i = 0; i < n; i++) {
            const char *s = col[i];
            int eq = (s && kstr && strcmp(s, kstr) == 0);
            mask[i] = (char)(want_eq ? eq : !eq);
        }
        return 1;
    }

    if (kind == 0 && (spec == SPEC_CMP_GT || spec == SPEC_CMP_LT ||
                      spec == SPEC_CMP_GE || spec == SPEC_CMP_LE ||
                      spec == SPEC_CMP_EQ || spec == SPEC_CMP_NE)) {
        const int64_t *col = c->int_cols[attr_idx];
        int64_t k = fl->k_is_float ? (int64_t)fl->k_d : (int64_t)fl->k;
#ifdef TE_HAVE_OPENMP
        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
            #pragma omp parallel
            {
                int nthr = omp_get_num_threads();
                int tid  = omp_get_thread_num();
                int chunk = (n + nthr - 1) / nthr;
                int s = tid * chunk;
                int e = s + chunk; if (e > n) e = n;
                if (s < e) {
#if defined(__AVX2__)
                    te_simd_cmp_i64(col + s, e - s, spec, k, mask + s);
#else
                    for (int i = s; i < e; i++) {
                        int b = 0;
                        switch (spec) {
                            case SPEC_CMP_EQ: b = (col[i] == k); break;
                            case SPEC_CMP_NE: b = (col[i] != k); break;
                            case SPEC_CMP_GT: b = (col[i] >  k); break;
                            case SPEC_CMP_LT: b = (col[i] <  k); break;
                            case SPEC_CMP_GE: b = (col[i] >= k); break;
                            case SPEC_CMP_LE: b = (col[i] <= k); break;
                        }
                        mask[i] = (char)b;
                    }
#endif
                }
            }
            return 1;
        }
#endif
#if defined(__AVX2__)
        te_simd_cmp_i64(col, n, spec, k, mask);
#else
        for (int i = 0; i < n; i++) {
            int b = 0;
            switch (spec) {
                case SPEC_CMP_EQ: b = (col[i] == k); break;
                case SPEC_CMP_NE: b = (col[i] != k); break;
                case SPEC_CMP_GT: b = (col[i] >  k); break;
                case SPEC_CMP_LT: b = (col[i] <  k); break;
                case SPEC_CMP_GE: b = (col[i] >= k); break;
                case SPEC_CMP_LE: b = (col[i] <= k); break;
            }
            mask[i] = (char)b;
        }
#endif
        return 1;
    }

    if (kind == 1 && (spec == SPEC_CMP_GT || spec == SPEC_CMP_LT ||
                      spec == SPEC_CMP_GE || spec == SPEC_CMP_LE ||
                      spec == SPEC_CMP_EQ || spec == SPEC_CMP_NE)) {
        const double *col = c->flt_cols[attr_idx];
        double k = fl->k_is_float ? fl->k_d : (double)fl->k;
#ifdef TE_HAVE_OPENMP
        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < n; i++) {
                int b = 0;
                switch (spec) {
                    case SPEC_CMP_EQ: b = (col[i] == k); break;
                    case SPEC_CMP_NE: b = (col[i] != k); break;
                    case SPEC_CMP_GT: b = (col[i] >  k); break;
                    case SPEC_CMP_LT: b = (col[i] <  k); break;
                    case SPEC_CMP_GE: b = (col[i] >= k); break;
                    case SPEC_CMP_LE: b = (col[i] <= k); break;
                }
                mask[i] = (char)b;
            }
            return 1;
        }
#endif
        for (int i = 0; i < n; i++) {
            int b = 0;
            switch (spec) {
                case SPEC_CMP_EQ: b = (col[i] == k); break;
                case SPEC_CMP_NE: b = (col[i] != k); break;
                case SPEC_CMP_GT: b = (col[i] >  k); break;
                case SPEC_CMP_LT: b = (col[i] <  k); break;
                case SPEC_CMP_GE: b = (col[i] >= k); break;
                case SPEC_CMP_LE: b = (col[i] <= k); break;
            }
            mask[i] = (char)b;
        }
        return 1;
    }

    return 0;
}

int te_colcache_sum(TeColCache *c, int attr_idx, const char *mask,
                    long long *out_i, double *out_d, int *out_is_int) {
    if (!c || attr_idx < 0 || attr_idx >= c->nattr) return 0;
    int n = c->n_rows;
    int kind = c->kinds[attr_idx];
    if (kind == 0) {
        const int64_t *col = c->int_cols[attr_idx];
        long long total = 0;
#ifdef TE_HAVE_OPENMP
        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
            if (mask) {
                #pragma omp parallel for reduction(+:total) schedule(static)
                for (int i = 0; i < n; i++) if (mask[i]) total += (long long)col[i];
            } else {
                #pragma omp parallel for reduction(+:total) schedule(static)
                for (int i = 0; i < n; i++) total += (long long)col[i];
            }
            *out_i = total; *out_d = 0.0; *out_is_int = 1; return 1;
        }
#endif
        if (mask) { for (int i=0;i<n;i++) if (mask[i]) total += (long long)col[i]; }
        else      { for (int i=0;i<n;i++) total += (long long)col[i]; }
        *out_i = total; *out_d = 0.0; *out_is_int = 1; return 1;
    }
    if (kind == 1) {
        const double *col = c->flt_cols[attr_idx];
        double total = 0.0;
#ifdef TE_HAVE_OPENMP
        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
            if (mask) {
                #pragma omp parallel for reduction(+:total) schedule(static)
                for (int i = 0; i < n; i++) if (mask[i]) total += col[i];
            } else {
                #pragma omp parallel for reduction(+:total) schedule(static)
                for (int i = 0; i < n; i++) total += col[i];
            }
            *out_d = total; *out_i = 0; *out_is_int = 0; return 1;
        }
#endif
        if (mask) { for (int i=0;i<n;i++) if (mask[i]) total += col[i]; }
        else      { for (int i=0;i<n;i++) total += col[i]; }
        *out_d = total; *out_i = 0; *out_is_int = 0; return 1;
    }
    return 0;
}

long long te_colcache_count(TeColCache *c, const char *mask) {
    if (!c) return 0;
    int n = c->n_rows;
    if (!mask) return n;
    long long total = 0;
#ifdef TE_HAVE_OPENMP
    if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
        #pragma omp parallel for reduction(+:total) schedule(static)
        for (int i = 0; i < n; i++) total += mask[i] ? 1 : 0;
        return total;
    }
#endif
    for (int i = 0; i < n; i++) if (mask[i]) total++;
    return total;
}
