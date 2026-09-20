/* te_fmt.c -- `typeeasy --fmt <file.te> [--write] [--check]`
 *
 * Formateador CONSERVADOR: solo cambia espacios en blanco FUERA de strings,
 * strings interpoladas y comentarios. Nunca inserta ni quita tokens.
 *   - indentacion por profundidad de llaves (4 espacios; una llave de cierre cierra antes);
 *   - un espacio alrededor de operadores binarios/asignacion (a=b+c -> a = b + c),
 *     ninguno antes de coma, punto y coma, parentesis/corchete de cierre, uno tras la coma;
 *   - `if(` / `for(` / `while(` -> `if (`;  `){` -> `) {`;
 *   - sin espacios al final de linea; una sola linea en blanco consecutiva; newline final.
 * Los comentarios (de linea y de bloque) y el interior de strings se copian byte a byte;
 * un comentario de linea al final de una sentencia conserva UN espacio antes.
 *
 * Garantia verificable: te_fmt_tokens_equal(in, out) -- el texto sin espacios fuera de
 * strings/comentarios es identico; si no, --fmt NO escribe y sale con codigo 3.
 * Idempotente: fmt(fmt(x)) == fmt(x) (test tests/lang/09_diagnostics/fmt_*.te).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *buf; size_t len, cap; } FmtBuf;

static void fb_putc(FmtBuf *b, char c) {
    if (b->len + 2 > b->cap) { b->cap = b->cap ? b->cap * 2 : 4096; b->buf = (char *)realloc(b->buf, b->cap); }
    b->buf[b->len++] = c; b->buf[b->len] = 0;
}
static void fb_puts(FmtBuf *b, const char *s) { while (*s) fb_putc(b, *s++); }
static void fb_rtrim(FmtBuf *b) { while (b->len && (b->buf[b->len - 1] == ' ' || b->buf[b->len - 1] == '\t')) b->buf[--b->len] = 0; }
static char fb_last(const FmtBuf *b) { return b->len ? b->buf[b->len - 1] : '\n'; }

/* Operadores binarios que llevan espacio a ambos lados (los mas largos primero). */
static const char *BINOPS[] = { "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "??", "+=", "-=", "*=", "/=", "=>", "<<", ">>",
                                "=", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^", "?", ":", NULL };
static int is_ident(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
/* Ultima palabra emitida (identificador/keyword), "" si el ultimo token no es palabra. */
static const char *prev_word(const FmtBuf *o, char *out, size_t cap) {
    size_t e = o->len; while (e && o->buf[e - 1] == ' ') e--;
    size_t s = e; while (s && is_ident(o->buf[s - 1])) s--;
    size_t n = e - s; if (n >= cap) n = cap - 1;
    memcpy(out, o->buf + s, n); out[n] = 0;
    return out;
}
static int word_in(const char *w, const char **set) { for (int i = 0; set[i]; i++) if (strcmp(w, set[i]) == 0) return 1; return 0; }
static const char *KW_NO_OPERAND[] = { "return", "in", "else", "case", "throw", "await", "print", "println", NULL };
static const char *TYPE_WORDS[] = { "int", "string", "float", "bool", "datetime", "uuid", "decimal", "dynamic", "void", NULL };
static const char *KW_SAME_LINE_AFTER_BRACE[] = { "else", "catch", "finally", NULL };
/* Un `{` abre un BLOQUE si sigue a `)`, a `=>` o a una palabra (class X, else, fn...); tras
 * `return`/`in`/`throw`/`case` o cualquier operador/puntuacion es un MAP literal. */
static int brace_is_map(const FmtBuf *o) {
    size_t i = o->len; while (i && o->buf[i - 1] == ' ') i--;
    if (!i) return 0;
    char c = o->buf[i - 1];
    if (c == ')' || c == '>') return 0;
    if (is_ident(c)) { char w[64]; prev_word(o, w, sizeof w); return strcmp(w, "return") == 0 || strcmp(w, "in") == 0 || strcmp(w, "throw") == 0 || strcmp(w, "case") == 0; }
    return 1;
}
static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r') p++; return p; }
static int next_word_is(const char *p, const char **set) {
    p = skip_ws(p); char w[32]; size_t n = 0;
    while (is_ident(p[n]) && n < 31) { w[n] = p[n]; n++; }
    w[n] = 0; return n && word_in(w, set);
}
static void fb_indent(FmtBuf *o, int depth) { for (int i = 0; i < depth * 4; i++) fb_putc(o, ' '); }
static void fb_newline(FmtBuf *o) { fb_rtrim(o); if (o->len && fb_last(o) != '\n') fb_putc(o, '\n'); }
static int prev_is_operand(const FmtBuf *o) {
    /* el token anterior termina una expresion -> el siguiente '-'/'+' es binario, no unario */
    size_t i = o->len; while (i && (o->buf[i - 1] == ' ')) i--;
    if (!i) return 0;
    char c = o->buf[i - 1];
    if (is_ident(c)) { char w[64]; prev_word(o, w, sizeof w); return !word_in(w, KW_NO_OPERAND); }
    return c == ')' || c == ']' || c == '"';
}
static int ends_with_kw(const FmtBuf *o, const char *kw) {
    size_t n = strlen(kw); if (o->len < n) return 0;
    if (strncmp(o->buf + o->len - n, kw, n) != 0) return 0;
    return o->len == n || !is_ident(o->buf[o->len - n - 1]);
}

char *te_fmt_source(const char *src) {
    FmtBuf o = {0};
    int depth = 0, at_line_start = 1, blank_lines = 0, paren = 0, tern = 0;
    char kind[512]; int sp = 0;   /* pila de llaves: 'B' bloque, 'M' map literal */
    const char *p = src;
    while (*p) {
        /* ---- comentarios: copia literal ---- */
        if (p[0] == '/' && p[1] == '/') {
            if (!at_line_start) { fb_rtrim(&o); fb_putc(&o, ' '); }
            else for (int i = 0; i < depth * 4; i++) fb_putc(&o, ' ');
            while (*p && *p != '\n') fb_putc(&o, *p++);
            at_line_start = 0; continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            if (at_line_start) for (int i = 0; i < depth * 4; i++) fb_putc(&o, ' ');
            else if (fb_last(&o) != ' ') fb_putc(&o, ' ');
            fb_puts(&o, "/*"); p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) fb_putc(&o, *p++);
            if (*p) { fb_puts(&o, "*/"); p += 2; }
            at_line_start = 0; continue;
        }
        /* ---- strings ("..." y $"...") ---- */
        if (*p == '"' || (*p == '$' && p[1] == '"')) {
            if (at_line_start) fb_indent(&o, depth + (paren > 0 ? 1 : 0));
            else if (o.len && (is_ident(fb_last(&o)) || fb_last(&o) == ')' || fb_last(&o) == ']' || fb_last(&o) == '"')) fb_putc(&o, ' ');   /* import "x" / return "x" */
            at_line_start = 0;
            int interp = (*p == '$');
            if (interp) fb_putc(&o, *p++);
            fb_putc(&o, *p++);
            int br = 0;   /* $"...{ m["k"] }...": las comillas dentro de {} no cierran la string */
            while (*p) {
                if (*p == '\\' && p[1]) { fb_putc(&o, *p++); fb_putc(&o, *p++); continue; }
                if (interp && *p == '{') br++;
                else if (interp && *p == '}' && br > 0) br--;
                else if (*p == '"') {
                    if (br == 0) break;
                    fb_putc(&o, *p++);                       /* string anidada dentro de {}: copiar entera */
                    while (*p && *p != '"') { if (*p == '\\' && p[1]) fb_putc(&o, *p++); fb_putc(&o, *p++); }
                    if (*p) fb_putc(&o, *p++);
                    continue;
                }
                fb_putc(&o, *p++);
            }
            if (*p) fb_putc(&o, *p++);
            continue;
        }
        /* ---- fin de linea ---- */
        if (*p == '\n') {
            fb_rtrim(&o);
            if (at_line_start) { if (++blank_lines <= 1 && o.len) fb_putc(&o, '\n'); }
            else { fb_putc(&o, '\n'); blank_lines = 0; }
            at_line_start = 1; p++; continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r') { p++; continue; }
        /* ---- primer token de la linea: indentar (un `}` cierra antes; dentro de ( [ un nivel extra) ---- */
        if (at_line_start) {
            if (*p == '}' && depth > 0) depth--;
            fb_indent(&o, depth + ((paren > 0 && *p != ')' && *p != ']') ? 1 : 0));
            at_line_start = 0;
            if (*p == '}') {
                if (sp > 0) sp--;
                fb_putc(&o, '}'); p++;
                const char *q = skip_ws(p);
                if (!(*q == ';' || *q == ')' || *q == ',' || *q == '.' || *q == '\n' || *q == 0 || (q[0] == '/' && q[1] == '/') || next_word_is(q, KW_SAME_LINE_AFTER_BRACE))) { fb_newline(&o); at_line_start = 1; blank_lines = 0; }
                continue;
            }
            if (*p == ')' || *p == ']') { fb_putc(&o, *p++); if (paren > 0) paren--; if (sp > 0 && kind[sp - 1] == 'P') sp--; continue; }   /* cierre multilinea */
        }
        /* ---- llaves ---- */
        if (*p == '{') {
            int is_map = brace_is_map(&o);
            if (o.len && fb_last(&o) != ' ' && fb_last(&o) != '(' && fb_last(&o) != '[' && fb_last(&o) != '\n') fb_putc(&o, ' ');
            fb_putc(&o, '{'); depth++; p++;
            if (sp < (int)sizeof kind) kind[sp++] = is_map ? 'M' : 'B';
            if (is_map) { const char *q = skip_ws(p); if (*q != '}' && *q != '\n') fb_putc(&o, ' '); }
            else { const char *q = skip_ws(p); if (*q != '\n' && *q != '}' && !(q[0] == '/' && q[1] == '/')) { fb_newline(&o); at_line_start = 1; blank_lines = 0; } }
            continue;
        }
        if (*p == '}') {
            char k = sp > 0 ? kind[--sp] : 'B';
            if (depth > 0) depth--;
            fb_rtrim(&o);
            if (k == 'B') { if (fb_last(&o) != '{') { fb_newline(&o); fb_indent(&o, depth + (paren > 0 ? 1 : 0)); } }
            else if (fb_last(&o) != '{') fb_putc(&o, ' ');
            fb_putc(&o, '}'); p++;
            if (k == 'B') {
                const char *q = skip_ws(p);
                if (!(*q == ';' || *q == ')' || *q == ',' || *q == '.' || *q == '\n' || *q == 0 || (q[0] == '/' && q[1] == '/') || next_word_is(q, KW_SAME_LINE_AFTER_BRACE))) { fb_newline(&o); at_line_start = 1; blank_lines = 0; }
            }
            continue;
        }
        /* ---- puntuacion sin espacio antes ---- */
        if (*p == ';') {
            fb_rtrim(&o); fb_putc(&o, *p++);
            const char *q = skip_ws(p);
            if (sp > 0 && kind[sp - 1] == 'P') { if (*q != '\n' && *q) fb_putc(&o, ' '); continue; }   /* for (a; b; c) */
            if (q[0] == '/' && q[1] == '/') { fb_putc(&o, ' '); continue; }             /* stmt; // comentario */
            if (*q != '\n' && *q != 0) { fb_newline(&o); at_line_start = 1; blank_lines = 0; }
            continue;
        }
        if (*p == ',') { fb_rtrim(&o); fb_putc(&o, *p++); if (*p != '\n' && *p != '\r' && *p) fb_putc(&o, ' '); continue; }
        if (*p == ')' || *p == ']') { fb_rtrim(&o); fb_putc(&o, *p++); if (paren > 0) paren--; if (sp > 0 && kind[sp - 1] == 'P') sp--; continue; }
        if (*p == '(' || *p == '[') {
            /* `if(` `for(` `while(` `catch(` -> con espacio; llamadas f( sin espacio; `in [..]`/`return [..]` con espacio */
            if (*p == '(' && (ends_with_kw(&o, "if") || ends_with_kw(&o, "for") || ends_with_kw(&o, "while") ||
                              ends_with_kw(&o, "catch") || ends_with_kw(&o, "return") || ends_with_kw(&o, "in"))) fb_putc(&o, ' ');
            if (*p == '[') { char w[64]; prev_word(&o, w, sizeof w); if (word_in(w, KW_NO_OPERAND) || (o.len && (fb_last(&o) == ',' || fb_last(&o) == ':'))) { if (fb_last(&o) != ' ') fb_putc(&o, ' '); } }
            fb_putc(&o, *p++); paren++;
            if (sp < (int)sizeof kind) kind[sp++] = 'P';
            continue;
        }
        if (*p == '.' && !isdigit((unsigned char)p[1])) { fb_rtrim(&o); fb_putc(&o, *p++); continue; }
        /* ---- operadores ---- */
        {
            int matched = 0;
            for (int k = 0; BINOPS[k]; k++) {
                size_t n = strlen(BINOPS[k]);
                if (strncmp(p, BINOPS[k], n) != 0) continue;
                /* `++`/`--` postfijo o prefijo: sin espacios */
                if ((p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-')) { fb_rtrim(&o); fb_puts(&o, p[0] == '+' ? "++" : "--"); p += 2; matched = 1; break; }
                /* unario - / + : sin espacio despues; espacio antes si sigue a una palabra (return -a) */
                if (n == 1 && (p[0] == '-' || p[0] == '+') && !prev_is_operand(&o)) { if (o.len && is_ident(fb_last(&o))) fb_putc(&o, ' '); fb_putc(&o, *p++); matched = 1; break; }
                /* `?.` (null-aware) y `??`: `?.` sin espacios */
                if (p[0] == '?' && p[1] == '.') { fb_rtrim(&o); fb_puts(&o, "?."); p += 2; matched = 1; break; }
                /* tipo nullable `int?` / `Clase?` (tras un tipo): pegado; si no, es ternario */
                if (p[0] == '?' && n == 1) {
                    char w[64]; prev_word(&o, w, sizeof w);
                    int nullable = word_in(w, TYPE_WORDS) || (w[0] && isupper((unsigned char)w[0]) && prev_is_operand(&o) && (p[1] == ';' || p[1] == ',' || p[1] == ')' || p[1] == ' ' || p[1] == '='));
                    if (nullable) { fb_rtrim(&o); fb_putc(&o, '?'); p++; matched = 1; break; }
                    tern++;
                    fb_rtrim(&o); fb_puts(&o, " ? "); p++; matched = 1; break;
                }
                /* `:` ternario -> ` : `; clave de map -> `: `; anotacion de tipo -> ` : ` */
                if (p[0] == ':' && n == 1) {
                    fb_rtrim(&o);
                    if (tern > 0) { tern--; fb_puts(&o, " : "); }
                    else if (sp > 0 && kind[sp - 1] == 'M') fb_puts(&o, ": ");
                    else fb_puts(&o, " : ");
                    p++; matched = 1; break;
                }
                fb_rtrim(&o); fb_putc(&o, ' '); fb_puts(&o, BINOPS[k]); fb_putc(&o, ' '); p += n; matched = 1; break;
            }
            if (matched) continue;
        }
        if (*p == '!' || *p == '~') { fb_putc(&o, *p++); continue; }
        /* ---- identificadores / numeros / resto ---- */
        if (is_ident(*p) || *p == '@' || *p == '#') {
            if (o.len && (is_ident(fb_last(&o)) || fb_last(&o) == '"' || fb_last(&o) == ')' || fb_last(&o) == ']' || fb_last(&o) == '}')) fb_putc(&o, ' ');
            while (*p && (is_ident(*p) || *p == '@' || *p == '#' || (*p == '.' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[-1])))) fb_putc(&o, *p++);
            continue;
        }
        fb_putc(&o, *p++);
    }
    fb_rtrim(&o);
    if (o.len && fb_last(&o) != '\n') fb_putc(&o, '\n');
    if (!o.buf) { o.buf = (char *)calloc(1, 1); }
    return o.buf;
}

/* Texto sin espacios fuera de strings/comentarios (los comentarios se comparan
 * tambien sin espacios, porque el fmt solo mueve su indentacion). */
static char *fmt_token_stream(const char *s) {
    FmtBuf o = {0};
    const char *p = s;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') { if (!isspace((unsigned char)*p)) fb_putc(&o, *p); p++; } continue; }
        if (p[0] == '/' && p[1] == '*') { fb_puts(&o, "/*"); p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) { if (!isspace((unsigned char)*p)) fb_putc(&o, *p); p++; } if (*p) { fb_puts(&o, "*/"); p += 2; } continue; }
        if (*p == '"' || (*p == '$' && p[1] == '"')) {
            int interp = (*p == '$'); if (interp) fb_putc(&o, *p++);
            fb_putc(&o, *p++);
            int br = 0;
            while (*p) {
                if (*p == '\\' && p[1]) { fb_putc(&o, *p++); fb_putc(&o, *p++); continue; }
                if (interp && *p == '{') br++;
                else if (interp && *p == '}' && br > 0) br--;
                else if (*p == '"') {
                    if (br == 0) break;
                    fb_putc(&o, *p++);
                    while (*p && *p != '"') { if (*p == '\\' && p[1]) fb_putc(&o, *p++); fb_putc(&o, *p++); }
                    if (*p) fb_putc(&o, *p++);
                    continue;
                }
                fb_putc(&o, *p++);
            }
            if (*p) fb_putc(&o, *p++);
            continue;
        }
        if (isspace((unsigned char)*p)) { p++; continue; }
        fb_putc(&o, *p++);
    }
    if (!o.buf) o.buf = (char *)calloc(1, 1);
    return o.buf;
}

int te_fmt_tokens_equal(const char *a, const char *b) {
    char *ta = fmt_token_stream(a), *tb = fmt_token_stream(b);
    int eq = strcmp(ta, tb) == 0;
    free(ta); free(tb);
    return eq;
}

/* Modo CLI. Devuelve 0 ok (o ya formateado con --check), 1 no formateado (--check),
 * 2 error de E/S, 3 el formateador alteraria tokens (no se escribe nada). */
int te_fmt_main(const char *path, int write, int check) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "fmt: cannot open %s\n", path); return 2; }
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)n + 1);
    if (!src || fread(src, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); free(src); fprintf(stderr, "fmt: read error %s\n", path); return 2; }
    src[n] = 0; fclose(fp);
    char *out = te_fmt_source(src);
    if (!te_fmt_tokens_equal(src, out)) {
        fprintf(stderr, "fmt: refusing to format %s: token stream would change (formatter bug; nothing written)\n", path);
        free(src); free(out); return 3;
    }
    int changed = strcmp(src, out) != 0;
    if (check) { printf("%s: %s\n", path, changed ? "needs formatting" : "ok"); free(src); free(out); return changed ? 1 : 0; }
    if (write) {
        if (changed) {
            FILE *w = fopen(path, "wb");
            if (!w) { fprintf(stderr, "fmt: cannot write %s\n", path); free(src); free(out); return 2; }
            fwrite(out, 1, strlen(out), w); fclose(w);
        }
        printf("%s: %s\n", path, changed ? "formatted" : "unchanged");
    } else {
        fputs(out, stdout);
    }
    free(src); free(out);
    return 0;
}
