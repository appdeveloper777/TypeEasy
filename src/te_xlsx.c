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
