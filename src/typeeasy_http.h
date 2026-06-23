#ifndef TYPEEASY_HTTP_H
#define TYPEEASY_HTTP_H

#include <stddef.h>  /* size_t (typeeasy_http_set_body_n) */

/* Phase H: API for the HTTP server to communicate request/response state to
 * the interpreted endpoint body. Implementation lives in ast.c. */

#ifdef __cplusplus
extern "C" {
#endif

void typeeasy_http_reset(void);
void typeeasy_http_set_method(const char *m);
void typeeasy_http_set_path  (const char *p);
void typeeasy_http_set_body  (const char *b);
void typeeasy_http_set_body_n(const void *buf, size_t len);
void typeeasy_http_add_query (const char *k, const char *v);
void typeeasy_http_add_header(const char *k, const char *v);
void typeeasy_http_add_param (const char *k, const char *v);
int  typeeasy_http_get_status(void);
void typeeasy_http_set_status(int s);
int  typeeasy_http_iter_response_header(int idx, const char **k, const char **v);

/* Binary download channel: a handler can stage raw file bytes (xlsx/pdf/etc.)
 * to be sent as the response body instead of the textual return value. The
 * server reads these AFTER invoking the handler; NULL/0 means "no binary body,
 * use the textual result". set_response_bytes copies the buffer. */
#include <stddef.h>
void        typeeasy_http_set_response_bytes(const void *buf, size_t len, const char *content_type);
const char *typeeasy_http_get_response_bytes(size_t *out_len);
const char *typeeasy_http_get_response_content_type(void);

/* --- Debugger introspection: read live request data ---- */
const char *typeeasy_http_get_method(void);
const char *typeeasy_http_get_path(void);
const char *typeeasy_http_get_body(void);
int  typeeasy_http_iter_param (int idx, const char **k, const char **v);
int  typeeasy_http_iter_query (int idx, const char **k, const char **v);
int  typeeasy_http_iter_header(int idx, const char **k, const char **v);

#ifdef __cplusplus
}
#endif

#endif
