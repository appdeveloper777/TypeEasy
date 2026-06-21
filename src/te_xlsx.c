/* te_xlsx.c — TypeEasy XLSX → CSV in-memory converter.
 *
 * Reusa el lector CSV existente. Estrategia:
 *   1) Cargar el archivo XLSX completo a memoria (ZIP archive).
 *   2) Localizar el End-of-Central-Directory (EOCD).
 *   3) Iterar Central Directory; extraer:
 *        - xl/sharedStrings.xml  (opcional)
 *        - xl/worksheets/sheet1.xml
 *      Se descomprime con zlib (DEFLATE raw, sin wrapper).
 *   4) Parsear sharedStrings.xml: array de strings por <si><t>...</t></si>.
 *      Soporta rich text: concatena todos los <t> dentro del <si>.
 *   5) Parsear sheet1.xml fila a fila; emitir CSV (RFC 4180) en un buffer
 *      malloc'd. Maneja celdas dispersas (gaps según el atributo r= de la
 *      celda: "B2" → columna 1) rellenando con vacíos.
 *
 * Limitaciones documentadas en te_xlsx.h.
 */

#include "te_xlsx.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <zlib.h>

/* --------- helpers genéricos ---------------------------------------- */

static int te_xlsx_iendswith(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return 0;
    const char *p = s + (ls - lf);
    for (size_t i = 0; i < lf; i++) {
        char a = p[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        char b = suf[i]; if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

int te_xlsx_filename_matches(const char *filename) {
    if (!filename) return 0;
    return te_xlsx_iendswith(filename, ".xlsx")
        || te_xlsx_iendswith(filename, ".xlsm");
}

/* Buffer dinámico append (string builder). */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} TXBuf;

static int txbuf_reserve(TXBuf *b, size_t extra) {
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 1;
    size_t ncap = b->cap ? b->cap * 2 : 4096;
    while (ncap < need) ncap *= 2;
    char *p = (char*)realloc(b->buf, ncap);
    if (!p) return 0;
    b->buf = p; b->cap = ncap;
    return 1;
}
static int txbuf_putc(TXBuf *b, char c) {
    if (!txbuf_reserve(b, 1)) return 0;
    b->buf[b->len++] = c;
    b->buf[b->len] = '\0';
    return 1;
}
static int txbuf_puts(TXBuf *b, const char *s, size_t n) {
    if (!txbuf_reserve(b, n)) return 0;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 1;
}
static int txbuf_putcstr(TXBuf *b, const char *s) {
    return txbuf_puts(b, s, strlen(s));
}

/* Lee el archivo completo a memoria. */
static char *te_xlsx_slurp(const char *filename, size_t *out_len) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (r != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    *out_len = (size_t)sz;
    return buf;
}

/* --------- ZIP parsing --------------------------------------------- */

static uint16_t rd_u16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd_u32(const unsigned char *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Localiza EOCD: signature 0x06054b50 ("PK\x05\x06"). */
static const unsigned char *te_xlsx_find_eocd(const unsigned char *buf, size_t n) {
    if (n < 22) return NULL;
    /* EOCD min 22 bytes, comment puede ser hasta 65535. */
    size_t start = (n > 22 + 65535) ? (n - 22 - 65535) : 0;
    for (size_t i = n - 22; i > start; i--) {
        if (buf[i] == 0x50 && buf[i+1] == 0x4b
         && buf[i+2] == 0x05 && buf[i+3] == 0x06) {
            return buf + i;
        }
        if (i == 0) break;
    }
    /* Caso i==0 (chequeo aparte para no underflow). */
    if (buf[0] == 0x50 && buf[1] == 0x4b
     && buf[2] == 0x05 && buf[3] == 0x06) return buf;
    return NULL;
}

/* Inflar DEFLATE raw (sin wrapper zlib) usando inflateInit2(-MAX_WBITS).
 * Devuelve buffer malloc'd de tamaño uncompressed_size. */
static char *te_xlsx_inflate(const unsigned char *src, size_t comp_size,
                             size_t uncomp_size) {
    if (uncomp_size == 0) {
        char *out = (char*)malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    char *out = (char*)malloc(uncomp_size + 1);
    if (!out) return NULL;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)comp_size;
    zs.next_out = (Bytef*)out;
    zs.avail_out = (uInt)uncomp_size;

    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        free(out); return NULL;
    }
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    out[uncomp_size] = '\0';
    return out;
}

/* Extrae un archivo del ZIP cuyo nombre coincide exactamente (case-sensitive
 * — los nombres internos de XLSX son canónicos). Devuelve buffer descomprimido
 * + tamaño, o NULL si no existe. */
static char *te_xlsx_extract(const unsigned char *buf, size_t n,
                             const char *target_name, size_t *out_len) {
    *out_len = 0;
    const unsigned char *eocd = te_xlsx_find_eocd(buf, n);
    if (!eocd) return NULL;

    /* EOCD layout (offset desde el inicio del record):
     *  4  signature
     *  2  disk number
     *  2  disk where CD starts
     *  2  num CD entries on this disk
     *  2  total num CD entries
     *  4  size of CD
     *  4  offset of CD
     *  2  comment length
     */
    uint32_t cd_size   = rd_u32(eocd + 12);
    uint32_t cd_offset = rd_u32(eocd + 16);
    uint16_t cd_n      = rd_u16(eocd + 10);

    if ((size_t)cd_offset + (size_t)cd_size > n) return NULL;
    const unsigned char *p   = buf + cd_offset;
    const unsigned char *end = p + cd_size;
    size_t name_len = strlen(target_name);

    for (uint16_t i = 0; i < cd_n && p + 46 <= end; i++) {
        if (rd_u32(p) != 0x02014b50) return NULL; /* signature CD */
        uint16_t method      = rd_u16(p + 10);
        uint32_t comp_sz     = rd_u32(p + 20);
        uint32_t uncomp_sz   = rd_u32(p + 24);
        uint16_t fname_len   = rd_u16(p + 28);
        uint16_t extra_len   = rd_u16(p + 30);
        uint16_t cmt_len     = rd_u16(p + 32);
        uint32_t local_hdr   = rd_u32(p + 42);
        const unsigned char *fname = p + 46;
        const unsigned char *next  = fname + fname_len + extra_len + cmt_len;

        if (fname_len == name_len && memcmp(fname, target_name, name_len) == 0) {
            /* Encontrado: ir al Local File Header. */
            if ((size_t)local_hdr + 30 > n) return NULL;
            const unsigned char *lh = buf + local_hdr;
            if (rd_u32(lh) != 0x04034b50) return NULL;
            uint16_t lf_fname_len = rd_u16(lh + 26);
            uint16_t lf_extra_len = rd_u16(lh + 28);
            const unsigned char *data = lh + 30 + lf_fname_len + lf_extra_len;
            if (data + comp_sz > buf + n) return NULL;
            if (method == 0) {
                /* Stored. */
                char *out = (char*)malloc((size_t)uncomp_sz + 1);
                if (!out) return NULL;
                memcpy(out, data, uncomp_sz);
                out[uncomp_sz] = '\0';
                *out_len = uncomp_sz;
                return out;
            } else if (method == 8) {
                char *out = te_xlsx_inflate(data, comp_sz, uncomp_sz);
                if (!out) return NULL;
                *out_len = uncomp_sz;
                return out;
            } else {
                fprintf(stderr,
                    "XLSXError: método de compresión no soportado (%u) en '%s'.\n",
                    method, target_name);
                return NULL;
            }
        }
        p = next;
    }
    return NULL;
}

/* --------- XML helpers --------------------------------------------- */

/* Decodifica entidades XML básicas y &#NN; / &#xHH; en `s` (longitud `n`)
 * y appendea el resultado UTF-8 a `out`. */
static int te_xlsx_xml_unescape_append(TXBuf *out, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '&') {
            /* Buscar ';' */
            size_t j = i + 1;
            while (j < n && s[j] != ';' && (j - i) < 16) j++;
            if (j >= n || s[j] != ';') {
                if (!txbuf_putc(out, c)) return 0;
                i++; continue;
            }
            const char *ent = s + i + 1;
            size_t elen = j - i - 1;
            if (elen == 2 && !memcmp(ent, "lt", 2))      { if(!txbuf_putc(out,'<'))return 0; }
            else if (elen == 2 && !memcmp(ent, "gt", 2)) { if(!txbuf_putc(out,'>'))return 0; }
            else if (elen == 3 && !memcmp(ent, "amp", 3)){ if(!txbuf_putc(out,'&'))return 0; }
            else if (elen == 4 && !memcmp(ent, "quot",4)){ if(!txbuf_putc(out,'"'))return 0; }
            else if (elen == 4 && !memcmp(ent, "apos",4)){ if(!txbuf_putc(out,'\''))return 0; }
            else if (elen > 1 && ent[0] == '#') {
                /* Numeric char ref. */
                unsigned long cp = 0;
                if (ent[1] == 'x' || ent[1] == 'X') {
                    for (size_t k = 2; k < elen; k++) {
                        char ch = ent[k];
                        cp <<= 4;
                        if (ch >= '0' && ch <= '9') cp |= (unsigned)(ch - '0');
                        else if (ch >= 'a' && ch <= 'f') cp |= (unsigned)(ch - 'a' + 10);
                        else if (ch >= 'A' && ch <= 'F') cp |= (unsigned)(ch - 'A' + 10);
                        else { cp = 0; break; }
                    }
                } else {
                    for (size_t k = 1; k < elen; k++) {
                        char ch = ent[k];
                        if (ch < '0' || ch > '9') { cp = 0; break; }
                        cp = cp * 10 + (unsigned)(ch - '0');
                    }
                }
                /* Codificar UTF-8. */
                if (cp < 0x80) {
                    if (!txbuf_putc(out, (char)cp)) return 0;
                } else if (cp < 0x800) {
                    char b2[2] = { (char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F)) };
                    if (!txbuf_puts(out, b2, 2)) return 0;
                } else if (cp < 0x10000) {
                    char b3[3] = {
                        (char)(0xE0 | (cp >> 12)),
                        (char)(0x80 | ((cp >> 6) & 0x3F)),
                        (char)(0x80 | (cp & 0x3F)) };
                    if (!txbuf_puts(out, b3, 3)) return 0;
                } else if (cp < 0x110000) {
                    char b4[4] = {
                        (char)(0xF0 | (cp >> 18)),
                        (char)(0x80 | ((cp >> 12) & 0x3F)),
                        (char)(0x80 | ((cp >> 6) & 0x3F)),
                        (char)(0x80 | (cp & 0x3F)) };
                    if (!txbuf_puts(out, b4, 4)) return 0;
                }
            } else {
                /* Entidad desconocida: dejar literal. */
                if (!txbuf_puts(out, s + i, j - i + 1)) return 0;
            }
            i = j + 1;
        } else {
            if (!txbuf_putc(out, c)) return 0;
            i++;
        }
    }
    return 1;
}

/* Busca substring en [p, end). Devuelve NULL si no encontrada. */
static const char *te_xlsx_strnstr(const char *p, const char *end, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0 || p + nl > end) return NULL;
    const char *limit = end - nl;
    for (const char *q = p; q <= limit; q++) {
        if (*q == needle[0] && memcmp(q, needle, nl) == 0) return q;
    }
    return NULL;
}

/* Extrae el valor del atributo `attr` dentro de la etiqueta [tag_start, tag_end).
 * Soporta comillas dobles o simples. Escribe puntero/longitud en *val/*vlen.
 * Devuelve 1 si lo encontró. */
static int te_xlsx_get_attr(const char *tag_start, const char *tag_end,
                            const char *attr,
                            const char **val, size_t *vlen) {
    size_t alen = strlen(attr);
    const char *p = tag_start;
    while (p + alen + 2 < tag_end) {
        if ((p == tag_start || isspace((unsigned char)*(p-1)))
            && !memcmp(p, attr, alen) && p[alen] == '=') {
            const char *q = p + alen + 1;
            if (q >= tag_end) return 0;
            char quote = *q;
            if (quote != '"' && quote != '\'') return 0;
            q++;
            const char *end = q;
            while (end < tag_end && *end != quote) end++;
            if (end >= tag_end) return 0;
            *val = q;
            *vlen = (size_t)(end - q);
            return 1;
        }
        p++;
    }
    return 0;
}

/* --------- sharedStrings.xml parser -------------------------------- */

typedef struct {
    char  **items;   /* cada item es malloc'd (UTF-8 desescapado) */
    size_t  count;
    size_t  cap;
} TXSharedStrings;

static int txss_push(TXSharedStrings *ss, char *s) {
    if (ss->count >= ss->cap) {
        size_t nc = ss->cap ? ss->cap * 2 : 64;
        char **np = (char**)realloc(ss->items, nc * sizeof(char*));
        if (!np) return 0;
        ss->items = np; ss->cap = nc;
    }
    ss->items[ss->count++] = s;
    return 1;
}

static void txss_free(TXSharedStrings *ss) {
    if (!ss) return;
    for (size_t i = 0; i < ss->count; i++) free(ss->items[i]);
    free(ss->items);
    ss->items = NULL; ss->count = ss->cap = 0;
}

/* Parsea xl/sharedStrings.xml. Tolera <si><t>x</t></si> y rich text
 * (<si><r><t>a</t></r><r><t>b</t></r></si> → "ab"). */
static int te_xlsx_parse_shared_strings(const char *xml, size_t xml_len,
                                        TXSharedStrings *ss) {
    if (!xml || xml_len == 0) return 1; /* sin shared strings = OK */
    const char *p   = xml;
    const char *end = xml + xml_len;

    while ((p = te_xlsx_strnstr(p, end, "<si")) != NULL) {
        const char *si_open_end = memchr(p, '>', (size_t)(end - p));
        if (!si_open_end) break;
        /* Self-closing <si/> → string vacío. */
        if (si_open_end > p && *(si_open_end - 1) == '/') {
            char *empty = (char*)malloc(1); if (!empty) return 0;
            empty[0] = '\0';
            if (!txss_push(ss, empty)) { free(empty); return 0; }
            p = si_open_end + 1; continue;
        }
        const char *si_close = te_xlsx_strnstr(si_open_end + 1, end, "</si>");
        if (!si_close) break;

        /* Concatenar todos los <t>…</t> dentro de [si_open_end+1, si_close). */
        TXBuf concat = {0};
        const char *q = si_open_end + 1;
        while (q < si_close) {
            const char *t_open = te_xlsx_strnstr(q, si_close, "<t");
            if (!t_open) break;
            const char *t_open_end = memchr(t_open, '>', (size_t)(si_close - t_open));
            if (!t_open_end) break;
            if (t_open_end > t_open && *(t_open_end - 1) == '/') {
                /* <t/> vacío */
                q = t_open_end + 1; continue;
            }
            const char *t_close = te_xlsx_strnstr(t_open_end + 1, si_close, "</t>");
            if (!t_close) break;
            if (!te_xlsx_xml_unescape_append(&concat,
                    t_open_end + 1, (size_t)(t_close - (t_open_end + 1)))) {
                free(concat.buf); return 0;
            }
            q = t_close + 4;
        }
        char *s = concat.buf ? concat.buf : strdup("");
        if (!s) return 0;
        if (!txss_push(ss, s)) { free(s); return 0; }
        p = si_close + 5;
    }
    return 1;
}

/* --------- worksheet parser ---------------------------------------- */

/* Convierte "A1" / "AB12" → columna 0-based (parte antes del primer dígito). */
static int te_xlsx_colref_to_idx(const char *ref, size_t ref_len) {
    int col = 0;
    for (size_t i = 0; i < ref_len; i++) {
        char c = ref[i];
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z') col = col * 26 + (c - 'a' + 1);
        else break;
    }
    return col - 1; /* 0-based */
}

/* Appendea `s` (len `n`) como campo CSV en `out`. Quote si contiene
 * coma, comillas, CR o LF (RFC 4180). */
static int te_xlsx_csv_emit_field(TXBuf *out, const char *s, size_t n) {
    int need_quote = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r') { need_quote = 1; break; }
    }
    if (!need_quote) return txbuf_puts(out, s, n);
    if (!txbuf_putc(out, '"')) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"') { if (!txbuf_putc(out, '"')) return 0; }
        if (!txbuf_putc(out, c)) return 0;
    }
    return txbuf_putc(out, '"');
}

/* Mete el campo desde un C-string. */
static int te_xlsx_csv_emit_cstr(TXBuf *out, const char *s) {
    return te_xlsx_csv_emit_field(out, s, strlen(s));
}

/* Parsea xl/worksheets/sheet1.xml y emite CSV en `out`. */
static int te_xlsx_parse_sheet(const char *xml, size_t xml_len,
                               const TXSharedStrings *ss,
                               TXBuf *out) {
    const char *p   = xml;
    const char *end = xml + xml_len;
    int first_row = 1;

    while ((p = te_xlsx_strnstr(p, end, "<row")) != NULL) {
        const char *row_open_end = memchr(p, '>', (size_t)(end - p));
        if (!row_open_end) break;
        int self_closing_row =
            (row_open_end > p && *(row_open_end - 1) == '/');
        const char *row_close = self_closing_row
            ? row_open_end + 1
            : te_xlsx_strnstr(row_open_end + 1, end, "</row>");
        if (!row_close) break;

        /* Saltar línea inicial — no separador antes de la primera fila. */
        if (!first_row) {
            if (!txbuf_putc(out, '\n')) return 0;
        }
        first_row = 0;

        int next_col = 0;   /* siguiente columna esperada (relleno de gaps) */
        const char *cur = self_closing_row ? row_close : row_open_end + 1;

        while (cur < row_close) {
            const char *c_open = te_xlsx_strnstr(cur, row_close, "<c");
            if (!c_open) break;
            /* Asegurar que es <c> y no <cell-otra-cosa>. */
            char next_ch = c_open[2];
            if (next_ch != ' ' && next_ch != '>' && next_ch != '/' && next_ch != '\t' && next_ch != '\n' && next_ch != '\r') {
                cur = c_open + 1; continue;
            }
            const char *c_open_end = memchr(c_open, '>', (size_t)(row_close - c_open));
            if (!c_open_end) break;
            int self_closing_c = (c_open_end > c_open && *(c_open_end - 1) == '/');

            /* atributos r="A1" y t="s"|"str"|"inlineStr"|"b"|"n" */
            const char *ref = NULL, *typ = NULL;
            size_t ref_len = 0, typ_len = 0;
            te_xlsx_get_attr(c_open, c_open_end, "r", &ref, &ref_len);
            te_xlsx_get_attr(c_open, c_open_end, "t", &typ, &typ_len);

            int col_idx = (ref && ref_len) ? te_xlsx_colref_to_idx(ref, ref_len) : next_col;
            if (col_idx < next_col) col_idx = next_col; /* defensa */

            /* Emitir separadores para gaps. */
            while (next_col < col_idx) {
                if (!txbuf_putc(out, ',')) return 0;
                next_col++;
            }
            if (next_col > 0) {
                if (!txbuf_putc(out, ',')) return 0;
            }
            next_col++;

            const char *c_close = NULL;
            if (!self_closing_c) {
                c_close = te_xlsx_strnstr(c_open_end + 1, row_close, "</c>");
            }
            const char *c_body_end = c_close ? c_close : c_open_end + 1;
            const char *c_body_start = c_open_end + 1;

            if (self_closing_c || c_body_start >= c_body_end) {
                /* Celda vacía: nada que emitir. */
                cur = self_closing_c ? c_open_end + 1 : c_close + 4;
                continue;
            }

            /* Decidir tipo. */
            int is_shared = (typ_len == 1 && typ[0] == 's');
            int is_inline = (typ_len == 10 && !memcmp(typ, "inlineStr", 9)); /* "inlineStr" -> 9 chars */
            /* Nota: strlen("inlineStr")==9 ; ajustamos: */
            if (!is_inline) is_inline = (typ_len == 9 && !memcmp(typ, "inlineStr", 9));
            int is_str    = (typ_len == 3 && !memcmp(typ, "str", 3));
            int is_bool   = (typ_len == 1 && typ[0] == 'b');

            if (is_inline) {
                /* <c t="inlineStr"><is><t>...</t></is></c> — concatenar <t>. */
                TXBuf tmp = {0};
                const char *q = c_body_start;
                while (q < c_body_end) {
                    const char *t_open = te_xlsx_strnstr(q, c_body_end, "<t");
                    if (!t_open) break;
                    const char *t_open_end = memchr(t_open, '>', (size_t)(c_body_end - t_open));
                    if (!t_open_end) break;
                    if (t_open_end > t_open && *(t_open_end - 1) == '/') {
                        q = t_open_end + 1; continue;
                    }
                    const char *t_close = te_xlsx_strnstr(t_open_end + 1, c_body_end, "</t>");
                    if (!t_close) break;
                    if (!te_xlsx_xml_unescape_append(&tmp,
                            t_open_end + 1, (size_t)(t_close - (t_open_end + 1)))) {
                        free(tmp.buf); return 0;
                    }
                    q = t_close + 4;
                }
                if (tmp.buf) {
                    if (!te_xlsx_csv_emit_field(out, tmp.buf, tmp.len)) { free(tmp.buf); return 0; }
                    free(tmp.buf);
                }
            } else {
                /* Buscar <v>...</v> dentro de la celda. */
                const char *v_open = te_xlsx_strnstr(c_body_start, c_body_end, "<v");
                if (v_open) {
                    const char *v_open_end = memchr(v_open, '>', (size_t)(c_body_end - v_open));
                    if (!v_open_end) { cur = c_close ? c_close + 4 : c_open_end + 1; continue; }
                    const char *v_close = te_xlsx_strnstr(v_open_end + 1, c_body_end, "</v>");
                    if (!v_close) { cur = c_close ? c_close + 4 : c_open_end + 1; continue; }
                    const char *v_str = v_open_end + 1;
                    size_t v_len = (size_t)(v_close - v_str);

                    if (is_shared) {
                        /* índice numérico en el array de shared strings. */
                        char tmp[32];
                        size_t cn = v_len < sizeof(tmp) - 1 ? v_len : sizeof(tmp) - 1;
                        memcpy(tmp, v_str, cn); tmp[cn] = '\0';
                        long idx = strtol(tmp, NULL, 10);
                        if (ss && idx >= 0 && (size_t)idx < ss->count && ss->items[idx]) {
                            if (!te_xlsx_csv_emit_cstr(out, ss->items[idx])) return 0;
                        }
                        /* fuera de rango → vacío (no error fatal). */
                    } else if (is_bool) {
                        /* 0/1 — emitirlo crudo. */
                        if (!te_xlsx_csv_emit_field(out, v_str, v_len)) return 0;
                    } else if (is_str) {
                        /* Resultado de fórmula como string: decodificar entidades. */
                        TXBuf tmp = {0};
                        if (!te_xlsx_xml_unescape_append(&tmp, v_str, v_len)) { free(tmp.buf); return 0; }
                        if (tmp.buf) {
                            if (!te_xlsx_csv_emit_field(out, tmp.buf, tmp.len)) { free(tmp.buf); return 0; }
                            free(tmp.buf);
                        }
                    } else {
                        /* Numérico (o tipo desconocido) — emitir crudo. */
                        if (!te_xlsx_csv_emit_field(out, v_str, v_len)) return 0;
                    }
                }
                /* Si no hay <v> y no es inline → celda sin valor cacheado. */
            }

            cur = c_close ? c_close + 4 : c_open_end + 1;
        }

        p = self_closing_row ? row_open_end + 1 : row_close + 6;
    }

    /* Newline final por convención. */
    if (out->len > 0 && out->buf[out->len - 1] != '\n') {
        if (!txbuf_putc(out, '\n')) return 0;
    }
    return 1;
}

/* --------- API pública --------------------------------------------- */

/* Núcleo bytes-in/bytes-out. NO libera `zip` (es del caller). */
char *te_xlsx_bytes_to_csv_buf(const char *zip, size_t zip_len, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!zip || zip_len < 22) {
        fprintf(stderr, "XLSXError: buffer XLSX vacío o demasiado corto.\n");
        return NULL;
    }
    const unsigned char *ubuf = (const unsigned char*)zip;

    /* Validar firma local (PK\x03\x04) o EOCD presente. */
    if (zip[0] != 'P' || zip[1] != 'K') {
        fprintf(stderr, "XLSXError: el buffer no parece un archivo ZIP/XLSX.\n");
        return NULL;
    }

    /* Extraer shared strings (opcional). */
    size_t ss_len = 0;
    char *ss_xml = te_xlsx_extract(ubuf, zip_len, "xl/sharedStrings.xml", &ss_len);
    TXSharedStrings ss = {0};
    if (ss_xml) {
        if (!te_xlsx_parse_shared_strings(ss_xml, ss_len, &ss)) {
            free(ss_xml); txss_free(&ss);
            fprintf(stderr, "XLSXError: error parseando sharedStrings.xml.\n");
            return NULL;
        }
        free(ss_xml);
    }

    /* Extraer la primera hoja. Intentar sheet1.xml; si no, sheet.xml. */
    size_t sh_len = 0;
    char *sh_xml = te_xlsx_extract(ubuf, zip_len, "xl/worksheets/sheet1.xml", &sh_len);
    if (!sh_xml) {
        sh_xml = te_xlsx_extract(ubuf, zip_len, "xl/worksheets/sheet.xml", &sh_len);
    }
    if (!sh_xml) {
        fprintf(stderr,
            "XLSXError: el buffer no contiene xl/worksheets/sheet1.xml.\n");
        txss_free(&ss); return NULL;
    }

    TXBuf csv = {0};
    int ok = te_xlsx_parse_sheet(sh_xml, sh_len, &ss, &csv);
    free(sh_xml);
    txss_free(&ss);

    if (!ok) {
        free(csv.buf);
        fprintf(stderr, "XLSXError: error parseando worksheet.\n");
        return NULL;
    }
    if (out_len) *out_len = csv.len;
    /* Si parse_sheet no emitió nada, devolver buffer vacío válido. */
    if (!csv.buf) {
        csv.buf = (char*)malloc(1);
        if (csv.buf) csv.buf[0] = '\0';
    }
    return csv.buf;
}

char *te_xlsx_to_csv_buf(const char *filename, size_t *out_len) {
    if (out_len) *out_len = 0;
    size_t zip_len = 0;
    char *zip = te_xlsx_slurp(filename, &zip_len);
    if (!zip) {
        fprintf(stderr, "XLSXError: no se pudo abrir '%s'.\n", filename);
        return NULL;
    }
    char *out = te_xlsx_bytes_to_csv_buf(zip, zip_len, out_len);
    free(zip);
    return out;
}

/* ===========================================================================
 * Escritores de documentos (export): CSV -> XLSX y texto -> PDF.
 *
 * Viven aquí (no en un archivo nuevo) a propósito: te_xlsx.c ya está enlazado
 * en TODAS las rutas de build con zlib (-lz), así que añadir un .c separado
 * arriesgaría errores de linker en CI por olvidar alguna ruta. crc32() de
 * zlib se reutiliza para el ZIP.
 * ======================================================================== */

/* ---- little-endian writers sobre TXBuf ---------------------------------- */
static int txb_u16(TXBuf *b, unsigned v) {
    return txbuf_putc(b, (char)(v & 0xff)) && txbuf_putc(b, (char)((v >> 8) & 0xff));
}
static int txb_u32(TXBuf *b, unsigned long v) {
    return txbuf_putc(b, (char)(v & 0xff))
        && txbuf_putc(b, (char)((v >> 8) & 0xff))
        && txbuf_putc(b, (char)((v >> 16) & 0xff))
        && txbuf_putc(b, (char)((v >> 24) & 0xff));
}

/* Columna 0-based -> letra(s) de Excel (0->A, 25->Z, 26->AA). */
static void te_col_letter(int col0, char *out) {
    char tmp[8]; int i = 0; int n = col0 + 1;
    while (n > 0) { int r = (n - 1) % 26; tmp[i++] = (char)('A' + r); n = (n - 1) / 26; }
    int j; for (j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    out[j] = '\0';
}

/* Escapa &<>"' y descarta caracteres de control no imprimibles (XML 1.0). */
static int te_xml_escape_append(TXBuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '&': if (!txbuf_putcstr(b, "&amp;"))  return 0; break;
            case '<': if (!txbuf_putcstr(b, "&lt;"))   return 0; break;
            case '>': if (!txbuf_putcstr(b, "&gt;"))   return 0; break;
            case '"': if (!txbuf_putcstr(b, "&quot;")) return 0; break;
            case '\'':if (!txbuf_putcstr(b, "&apos;")) return 0; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break; /* skip */
                if (!txbuf_putc(b, (char)c)) return 0;
        }
    }
    return 1;
}

/* ¿La celda es un número simple? (entero o decimal, signo opcional). Se trata
 * como texto si tiene ceros a la izquierda ("007") para no perder el formato. */
static int te_cell_is_number(const char *s, size_t n) {
    if (n == 0 || n > 18) return 0;
    size_t i = 0;
    if (s[i] == '-') { i++; if (i >= n) return 0; }
    if (s[i] == '0' && (n - i) > 1 && s[i + 1] != '.') return 0; /* leading zero */
    int digits = 0, dot = 0;
    for (; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') digits++;
        else if (s[i] == '.') { if (dot) return 0; dot = 1; }
        else return 0;
    }
    return digits > 0;
}

static int te_xlsx_emit_cell(TXBuf *sheet, int row1, int col0, const char *txt, size_t n) {
    if (n == 0) return 1; /* celda vacía: se omite */
    char colL[8]; te_col_letter(col0, colL);
    char ref[24]; snprintf(ref, sizeof ref, "%s%d", colL, row1);
    if (te_cell_is_number(txt, n)) {
        if (!txbuf_putcstr(sheet, "<c r=\"") || !txbuf_putcstr(sheet, ref)
            || !txbuf_putcstr(sheet, "\"><v>") || !txbuf_puts(sheet, txt, n)
            || !txbuf_putcstr(sheet, "</v></c>")) return 0;
    } else {
        if (!txbuf_putcstr(sheet, "<c r=\"") || !txbuf_putcstr(sheet, ref)
            || !txbuf_putcstr(sheet, "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">")
            || !te_xml_escape_append(sheet, txt, n)
            || !txbuf_putcstr(sheet, "</t></is></c>")) return 0;
    }
    return 1;
}

/* Añade una entrada STORED (sin comprimir) al ZIP en construcción. Registra
 * crc/size/offset/name para la central directory posterior. */
typedef struct { unsigned long crc, size, offset; const char *name; size_t name_len; } TeZipEnt;

static int te_zip_add(TXBuf *zip, TeZipEnt *e, const char *name,
                      const char *data, size_t len) {
    e->name = name; e->name_len = strlen(name);
    e->offset = (unsigned long)zip->len;
    e->size = (unsigned long)len;
    e->crc = (unsigned long)crc32(0L, (const unsigned char*)data, (unsigned)len);
    if (!txb_u32(zip, 0x04034b50UL)) return 0;     /* local file header sig */
    if (!txb_u16(zip, 20)) return 0;               /* version needed */
    if (!txb_u16(zip, 0)) return 0;                /* flags */
    if (!txb_u16(zip, 0)) return 0;                /* method STORED */
    if (!txb_u16(zip, 0)) return 0;                /* mod time */
    if (!txb_u16(zip, 0)) return 0;                /* mod date */
    if (!txb_u32(zip, e->crc)) return 0;
    if (!txb_u32(zip, e->size)) return 0;          /* compressed == uncompressed */
    if (!txb_u32(zip, e->size)) return 0;
    if (!txb_u16(zip, (unsigned)e->name_len)) return 0;
    if (!txb_u16(zip, 0)) return 0;                /* extra len */
    if (!txbuf_puts(zip, name, e->name_len)) return 0;
    if (len && !txbuf_puts(zip, data, len)) return 0;
    return 1;
}

char *te_xlsx_from_csv_buf(const char *csv, size_t csv_len, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!csv) { csv = ""; csv_len = 0; }

    /* 1) CSV -> sheet1.xml (inline strings / números). */
    TXBuf sheet = {0};
    if (!txbuf_putcstr(&sheet,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<sheetData>")) { free(sheet.buf); return NULL; }

    TXBuf cell = {0};
    int inq = 0, row_open = 0, col = 0, row1 = 1, any = 0;
    size_t i = 0;
    int ok = 1;
#define OPEN_ROW() do { if (!row_open) { char rb[16]; snprintf(rb,sizeof rb,"%d",row1); \
        if(!txbuf_putcstr(&sheet,"<row r=\"")||!txbuf_putcstr(&sheet,rb)||!txbuf_putcstr(&sheet,"\">")){ok=0;} row_open=1; } } while(0)
#define FLUSH_CELL() do { OPEN_ROW(); if(!te_xlsx_emit_cell(&sheet,row1,col,cell.buf?cell.buf:"",cell.len)){ok=0;} col++; cell.len=0; } while(0)
#define CLOSE_ROW() do { if(row_open){ if(!txbuf_putcstr(&sheet,"</row>")){ok=0;} row_open=0; } row1++; col=0; any=0; } while(0)
    for (i = 0; ok && i < csv_len; i++) {
        char c = csv[i];
        if (inq) {
            if (c == '"') {
                if (i + 1 < csv_len && csv[i + 1] == '"') { if(!txbuf_putc(&cell,'"')){ok=0;} i++; }
                else inq = 0;
            } else { if(!txbuf_putc(&cell,c)){ok=0;} }
            continue;
        }
        if (c == '"') { inq = 1; any = 1; continue; }
        if (c == ',') { FLUSH_CELL(); any = 1; continue; }
        if (c == '\r') continue;            /* CRLF: el \n cierra la fila */
        if (c == '\n') {
            if (row_open || cell.len > 0 || col > 0 || any) { FLUSH_CELL(); CLOSE_ROW(); }
            else { row1++; }                /* línea totalmente vacía */
            continue;
        }
        if(!txbuf_putc(&cell,c)){ok=0;} any = 1;
    }
    if (ok && (cell.len > 0 || col > 0 || any || row_open)) { FLUSH_CELL(); CLOSE_ROW(); }
#undef OPEN_ROW
#undef FLUSH_CELL
#undef CLOSE_ROW
    free(cell.buf);
    if (ok) ok = txbuf_putcstr(&sheet, "</sheetData></worksheet>");
    if (!ok) { free(sheet.buf); return NULL; }

    /* 2) Partes estáticas del paquete OOXML. */
    static const char *CT =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "</Types>";
    static const char *RELS =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    static const char *WB =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
    static const char *WBRELS =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "</Relationships>";

    struct { const char *name; const char *data; size_t len; } parts[5] = {
        { "[Content_Types].xml",        CT,        strlen(CT) },
        { "_rels/.rels",                RELS,      strlen(RELS) },
        { "xl/workbook.xml",            WB,        strlen(WB) },
        { "xl/_rels/workbook.xml.rels", WBRELS,    strlen(WBRELS) },
        { "xl/worksheets/sheet1.xml",   sheet.buf ? sheet.buf : "", sheet.len },
    };

    /* 3) Ensamblar el ZIP (STORED). */
    TXBuf zip = {0};
    TeZipEnt ents[5];
    for (int k = 0; k < 5; k++) {
        if (!te_zip_add(&zip, &ents[k], parts[k].name, parts[k].data, parts[k].len)) {
            free(sheet.buf); free(zip.buf); return NULL;
        }
    }
    unsigned long cd_off = (unsigned long)zip.len;
    for (int k = 0; k < 5; k++) {
        TeZipEnt *e = &ents[k];
        if (!txb_u32(&zip, 0x02014b50UL) ||           /* central dir sig */
            !txb_u16(&zip, 20) || !txb_u16(&zip, 20) ||/* made by / needed */
            !txb_u16(&zip, 0)  || !txb_u16(&zip, 0)  ||/* flags / method */
            !txb_u16(&zip, 0)  || !txb_u16(&zip, 0)  ||/* time / date */
            !txb_u32(&zip, e->crc) || !txb_u32(&zip, e->size) || !txb_u32(&zip, e->size) ||
            !txb_u16(&zip, (unsigned)e->name_len) ||
            !txb_u16(&zip, 0) || !txb_u16(&zip, 0) || /* extra / comment */
            !txb_u16(&zip, 0) || !txb_u16(&zip, 0) || /* disk / internal attrs */
            !txb_u32(&zip, 0) || !txb_u32(&zip, e->offset) ||
            !txbuf_puts(&zip, e->name, e->name_len)) {
            free(sheet.buf); free(zip.buf); return NULL;
        }
    }
    unsigned long cd_size = (unsigned long)zip.len - cd_off;
    if (!txb_u32(&zip, 0x06054b50UL) ||               /* EOCD sig */
        !txb_u16(&zip, 0) || !txb_u16(&zip, 0) ||      /* disk numbers */
        !txb_u16(&zip, 5) || !txb_u16(&zip, 5) ||      /* entries */
        !txb_u32(&zip, cd_size) || !txb_u32(&zip, cd_off) ||
        !txb_u16(&zip, 0)) {                           /* comment len */
        free(sheet.buf); free(zip.buf); return NULL;
    }

    free(sheet.buf);
    if (out_len) *out_len = zip.len;
    return zip.buf; /* puede ser NULL solo si todo falló (ya cubierto) */
}

/* ---- PDF (texto paginado, Helvetica) ------------------------------------ */

/* Escapa ( ) \ y normaliza no-ASCII a '?' para un PDF Latin-1 simple. */
static int te_pdf_escape_append(TXBuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '(' || c == ')' || c == '\\') { if (!txbuf_putc(b, '\\')) return 0; if (!txbuf_putc(b, (char)c)) return 0; }
        else if (c == '\t') { if (!txbuf_putcstr(b, "    ")) return 0; }
        else if (c < 0x20 || c > 0x7e) { if (!txbuf_putc(b, '?')) return 0; }
        else { if (!txbuf_putc(b, (char)c)) return 0; }
    }
    return 1;
}

char *te_pdf_from_text(const char *text, const char *title, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!text) text = "";

    /* 1) Partir en líneas lógicas y envolver a ~95 chars. */
    const int WRAP = 95;
    char **lines = NULL; int nlines = 0, cap = 0;
#define PUSH_LINE(PTR,LEN) do { \
        if (nlines == cap) { cap = cap ? cap*2 : 64; char **g = (char**)realloc(lines, (size_t)cap*sizeof(char*)); if(!g){goto pdf_oom;} lines = g; } \
        char *cp = (char*)malloc((size_t)(LEN)+1); if(!cp){goto pdf_oom;} if(LEN)memcpy(cp,(PTR),(size_t)(LEN)); cp[LEN]='\0'; lines[nlines++]=cp; } while(0)

    if (title && *title) { size_t tl = strlen(title); PUSH_LINE(title, (int)tl); PUSH_LINE("", 0); }

    {
        const char *p = text;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t ll = nl ? (size_t)(nl - p) : strlen(p);
            if (ll > 0 && p[ll ? ll - 1 : 0] == '\r') ll--; /* strip trailing CR handled below */
            const char *line = p; size_t rem = nl ? (size_t)(nl - p) : strlen(p);
            if (rem > 0 && line[rem - 1] == '\r') rem--;
            if (rem == 0) { PUSH_LINE("", 0); }
            else {
                size_t off = 0;
                while (off < rem) {
                    size_t chunk = rem - off; if (chunk > (size_t)WRAP) chunk = WRAP;
                    PUSH_LINE(line + off, (int)chunk);
                    off += chunk;
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
        if (*text == '\0') PUSH_LINE("", 0);
    }

    /* 2) Paginar. */
    const int LPP = 50;            /* líneas por página */
    const int Y_TOP = 750, X = 50, LEAD = 14;
    int npages = (nlines + LPP - 1) / LPP; if (npages < 1) npages = 1;

    /* Objetos: 1 catalog, 2 pages, 3 font, luego (page,content) por página. */
    int nobj = 3 + npages * 2;
    unsigned long *offs = (unsigned long*)calloc((size_t)nobj + 1, sizeof(unsigned long));
    if (!offs) goto pdf_oom;

    TXBuf pdf = {0};
    if (!txbuf_putcstr(&pdf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n")) { free(offs); goto pdf_oom; }
#define BEGIN_OBJ(N) do { offs[(N)] = (unsigned long)pdf.len; char hb[32]; snprintf(hb,sizeof hb,"%d 0 obj\n",(N)); if(!txbuf_putcstr(&pdf,hb)){free(offs);free(pdf.buf);goto pdf_oom;} } while(0)
#define END_OBJ() do { if(!txbuf_putcstr(&pdf,"endobj\n")){free(offs);free(pdf.buf);goto pdf_oom;} } while(0)

    /* obj 1: catalog */
    BEGIN_OBJ(1); txbuf_putcstr(&pdf, "<< /Type /Catalog /Pages 2 0 R >>\n"); END_OBJ();

    /* obj 2: pages (con Kids) */
    BEGIN_OBJ(2);
    txbuf_putcstr(&pdf, "<< /Type /Pages /Kids [");
    for (int pg = 0; pg < npages; pg++) {
        char kb[24]; snprintf(kb, sizeof kb, " %d 0 R", 4 + pg * 2);
        txbuf_putcstr(&pdf, kb);
    }
    { char cb[48]; snprintf(cb, sizeof cb, " ] /Count %d >>\n", npages); txbuf_putcstr(&pdf, cb); }
    END_OBJ();

    /* obj 3: font Helvetica */
    BEGIN_OBJ(3);
    txbuf_putcstr(&pdf, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\n");
    END_OBJ();

    for (int pg = 0; pg < npages; pg++) {
        int page_obj = 4 + pg * 2;
        int cont_obj = 5 + pg * 2;

        /* Stream de contenido de la página. */
        TXBuf cs = {0};
        char head[96];
        snprintf(head, sizeof head, "BT\n/F1 11 Tf\n%d TL\n%d %d Td\n", LEAD, X, Y_TOP);
        txbuf_putcstr(&cs, head);
        int start = pg * LPP, end = start + LPP; if (end > nlines) end = nlines;
        for (int li = start; li < end; li++) {
            if (li > start) txbuf_putcstr(&cs, "T* ");
            txbuf_putc(&cs, '(');
            te_pdf_escape_append(&cs, lines[li], strlen(lines[li]));
            txbuf_putcstr(&cs, ") Tj\n");
        }
        txbuf_putcstr(&cs, "ET\n");

        BEGIN_OBJ(cont_obj);
        { char lb[48]; snprintf(lb, sizeof lb, "<< /Length %lu >>\nstream\n", (unsigned long)cs.len); txbuf_putcstr(&pdf, lb); }
        txbuf_puts(&pdf, cs.buf ? cs.buf : "", cs.len);
        txbuf_putcstr(&pdf, "\nendstream\n");
        END_OBJ();
        free(cs.buf);

        BEGIN_OBJ(page_obj);
        { char pb[160]; snprintf(pb, sizeof pb,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 3 0 R >> >> /Contents %d 0 R >>\n", cont_obj);
          txbuf_putcstr(&pdf, pb); }
        END_OBJ();
    }
#undef BEGIN_OBJ
#undef END_OBJ

    /* xref + trailer */
    unsigned long xref_off = (unsigned long)pdf.len;
    { char xb[32]; snprintf(xb, sizeof xb, "xref\n0 %d\n", nobj + 1); txbuf_putcstr(&pdf, xb); }
    txbuf_putcstr(&pdf, "0000000000 65535 f\r\n");
    for (int o = 1; o <= nobj; o++) {
        char eb[24]; snprintf(eb, sizeof eb, "%010lu 00000 n\r\n", offs[o]);
        txbuf_putcstr(&pdf, eb);
    }
    { char tb[96]; snprintf(tb, sizeof tb,
        "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", nobj + 1, xref_off);
      txbuf_putcstr(&pdf, tb); }

    free(offs);
    for (int k = 0; k < nlines; k++) free(lines[k]);
    free(lines);
    if (out_len) *out_len = pdf.len;
    return pdf.buf;

pdf_oom:
#undef PUSH_LINE
    for (int k = 0; k < nlines; k++) free(lines[k]);
    free(lines);
    return NULL;
}
