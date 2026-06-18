/* te_http.h — Minimal HTTP client wrapper.
 *
 * Extracted from ast.c during Fase 1 modularization (see docs/REFACTOR_AST_C.md).
 *
 * te_http_do() routes through libcurl when TE_HAVE_LIBCURL is defined
 * (supports HTTPS + custom headers + arbitrary methods) and falls back
 * to a plain HTTP/1.0 over TCP implementation otherwise.
 *
 * Returns a malloc'd response body (caller frees) or NULL on failure.
 *
 * headers_str format: lines "Header: value" separated by '\n'.
 *   "Authorization: Bearer XXX\nX-Custom: 1"
 */
#ifndef TE_HTTP_H
#define TE_HTTP_H

char* te_http_do(const char *method, const char *url,
                 const char *body, const char *headers_str);

/* HTTP status code of the most recent te_http_do() call.
 * 0 means "no response" (network failure / unreachable host); otherwise it is
 * the numeric HTTP status (200, 404, 500, ...). Exposed via http_last_status(). */
int te_http_last_status(void);

#endif /* TE_HTTP_H */
