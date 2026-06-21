/* te_xlsx.h — TypeEasy minimal XLSX → CSV in-memory converter.
 *
 * Permite usar `from "archivo.xlsx", Class;` igual que con CSV.
 * El convertidor lee la primera hoja del libro, materializa un buffer
 * CSV (RFC 4180) en memoria y lo entrega al parser CSV existente.
 *
 * Dependencias: zlib (-lz) para inflar streams DEFLATE de ZIP.
 *
 * Limitaciones (a propósito, mantener simple):
 *   - Solo la primera hoja (xl/worksheets/sheet1.xml).
 *   - Cell types soportados: shared string (s), inline string (inlineStr,
 *     str) y numérico (default). Booleanos se emiten como 0/1.
 *   - Sin estilos, sin fechas formateadas (se emite el serial OADate como
 *     número; si la clase TypeEasy declara la columna como string, viene
 *     el serial; convertir en el script si hace falta).
 *   - Sin fórmulas evaluadas — se usa <v> cacheado si está presente.
 */
#ifndef TE_XLSX_H
#define TE_XLSX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Devuelve 1 si filename termina en .xlsx / .xlsm (case-insensitive). */
int te_xlsx_filename_matches(const char *filename);

/* Lee un .xlsx desde disco y devuelve un buffer CSV (UTF-8) malloc'd.
 * `*out_len` recibe el tamaño en bytes (sin NUL terminator, aunque el
 * buffer está NUL-terminado para inspección con printf). Devuelve NULL
 * si no se pudo abrir o el archivo no es un XLSX válido — en ese caso
 * imprime un mensaje en stderr. El caller debe free()'ar el buffer. */
char *te_xlsx_to_csv_buf(const char *filename, size_t *out_len);

/* Variante bytes-in/bytes-out: el caller pasa un buffer ZIP (.xlsx) en
 * memoria y recibe el CSV equivalente. NO toma posesión de `zip`. Útil
 * para uploads HTTP donde el archivo llega en el body. */
char *te_xlsx_bytes_to_csv_buf(const char *zip, size_t zip_len, size_t *out_len);

/* ------------------------------------------------------------------------
 * Escritores de documentos (export). Producen los BYTES de un archivo en
 * memoria para que un endpoint pueda devolver una descarga binaria.
 * El caller toma posesión del buffer (free()). Devuelven NULL si falla.
 * ----------------------------------------------------------------------*/

/* Construye un .xlsx (OOXML, una hoja) a partir de un buffer CSV (RFC 4180).
 * Cada línea es una fila; las celdas puramente numéricas se escriben como
 * número, el resto como texto (inlineStr). `*out_len` recibe el tamaño en
 * bytes del .xlsx resultante. */
char *te_xlsx_from_csv_buf(const char *csv, size_t csv_len, size_t *out_len);

/* Construye un PDF (texto, fuente Helvetica, paginado) a partir de `text`
 * (líneas separadas por '\n'). `title` es opcional (se imprime como primera
 * línea en negrita-ish; puede ser NULL). `*out_len` recibe el tamaño del PDF. */
char *te_pdf_from_text(const char *text, const char *title, size_t *out_len);


#ifdef __cplusplus
}
#endif

#endif /* TE_XLSX_H */
