/* te_buf.h — Dynamic string buffer utility shared across TypeEasy modules.
 *
 * Extracted from ast.c during Fase 1 modularization (see docs/REFACTOR_AST_C.md).
 * Header-only with `static inline` so every translation unit gets its own copy
 * without link conflicts and with no .o file to wire into build scripts.
 */
#ifndef TE_BUF_H
#define TE_BUF_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t len; size_t cap; } TeBuf;

static inline void tebuf_init(TeBuf *b) {
    b->cap = 256;
    b->p = (char*)malloc(b->cap);
    b->len = 0;
    if (b->p) b->p[0] = 0;
}

static inline void tebuf_putc(TeBuf *b, char c) {
    if (b->len + 2 > b->cap) {
        b->cap *= 2;
        b->p = (char*)realloc(b->p, b->cap);
    }
    b->p[b->len++] = c;
    b->p[b->len] = 0;
}

static inline void tebuf_puts(TeBuf *b, const char *s) {
    if (!s) return;
    size_t L = strlen(s);
    if (b->len + L + 1 > b->cap) {
        while (b->len + L + 1 > b->cap) b->cap *= 2;
        b->p = (char*)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, L);
    b->len += L;
    b->p[b->len] = 0;
}

#endif /* TE_BUF_H */
