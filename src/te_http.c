/* te_http.c — Minimal HTTP client (libcurl + plain TCP fallback).
 *
 * Extracted from ast.c during Fase 1 modularization (see docs/REFACTOR_AST_C.md).
 * Functions kept byte-identical to the original ast.c implementation to
 * preserve behavior; only the surrounding includes/typedef were repackaged.
 */
#include "te_http.h"
#include "te_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* Headers POSIX/sockets: en Windows usamos winsock, en POSIX sockets BSD. */
#ifdef _WIN32
  #include <io.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

/* libcurl opcional: enabled with -DTE_HAVE_LIBCURL at compile time. */
#ifdef TE_HAVE_LIBCURL
  #include <curl/curl.h>
#endif

/* gotcha #11: soporte TLS para https:// en modo script SIN libcurl.
 * El build nativo de Windows (scripts/build_native_windows.sh) enlaza
 * -lssl -lcrypto (OpenSSL) para civetweb pero NO compila libcurl, así que
 * http_get("https://...") devolvía cadena vacía. Con -DTE_HAVE_OPENSSL el
 * fallback TCP de abajo envuelve el socket con OpenSSL para los https.
 * En Docker/Linux se usa libcurl (TE_HAVE_LIBCURL) y este bloque queda inerte. */
#ifdef TE_HAVE_OPENSSL
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

/* Feature: HTTP response status code of the most recent te_http_do() call.
 * 0 means "no response" (network failure / unreachable host); otherwise it is
 * the numeric HTTP status (200, 404, 500, ...). Exposed to scripts via the
 * http_last_status() builtin. The interpreter is single-threaded, so reading
 * this global right after the call is reliable. */
static int g_http_last_status = 0;
int te_http_last_status(void) { return g_http_last_status; }

/* Portable case-insensitive prefix compare (avoids depending on strncasecmp /
 * <strings.h> being available identically across MinGW and glibc). */
static int te_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) break;
    }
    return 0;
}

/* Minimal HTTP/1.0 client over plain TCP. Returns body as malloc'd string (caller frees), or NULL.
 * headers_str: "Name: value" lines separated by '\n' (a trailing '\r' is tolerated), or NULL. */
static char* te_http_request(const char *method, const char *url, const char *body,
                             const char *headers_str) {
    if (!url) return NULL;
#ifdef _WIN32
    /* En Windows toda llamada a sockets requiere WSAStartup previo. El servidor
     * --api lo inicializa, pero en modo script (typeeasy archivo.te) nadie lo
     * hace, así que http_get/http_post devolvían cadena vacía. Inicializamos
     * winsock de forma perezosa una sola vez; WSAStartup es refcounted, así que
     * coexiste sin problema con el WSAStartup del servidor API. */
    {
        static int wsa_ready = 0;
        if (!wsa_ready) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return NULL;
            wsa_ready = 1;
        }
    }
#endif
    /* Parse http://host[:port]/path  ó  https://host[:port]/path */
    const char *u = url;
    int use_tls = 0;
    if (strncmp(u, "http://", 7) == 0) u += 7;
    else if (strncmp(u, "https://", 8) == 0) {
#ifdef TE_HAVE_OPENSSL
        u += 8; use_tls = 1;
#else
        return NULL; /* TLS no soportado: compila con -DTE_HAVE_OPENSSL o usa libcurl */
#endif
    }
    char host[512] = {0};
    char port[16]  = "80";
    char path[1024] = "/";
    if (use_tls) snprintf(port, sizeof(port), "443"); /* default https port */
    const char *slash = strchr(u, '/');
    const char *colon = strchr(u, ':');
    size_t hostlen;
    if (colon && (!slash || colon < slash)) {
        hostlen = (size_t)(colon - u);
        if (hostlen >= sizeof(host)) hostlen = sizeof(host) - 1;
        memcpy(host, u, hostlen); host[hostlen] = 0;
        size_t pl = slash ? (size_t)(slash - colon - 1) : strlen(colon + 1);
        if (pl >= sizeof(port)) pl = sizeof(port) - 1;
        memcpy(port, colon + 1, pl); port[pl] = 0;
    } else {
        hostlen = slash ? (size_t)(slash - u) : strlen(u);
        if (hostlen >= sizeof(host)) hostlen = sizeof(host) - 1;
        memcpy(host, u, hostlen); host[hostlen] = 0;
    }
    if (slash) {
        size_t pl = strlen(slash);
        if (pl >= sizeof(path)) pl = sizeof(path) - 1;
        memcpy(path, slash, pl); path[pl] = 0;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return NULL;
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return NULL; }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        freeaddrinfo(res); return NULL;
    }
    freeaddrinfo(res);
#ifdef TE_HAVE_OPENSSL
    /* gotcha #11: handshake TLS sobre el socket ya conectado para https://.
     * SSL_CTX se crea una sola vez (estático). SNI vía SSL_set_tlsext_host_name
     * es obligatorio para hosts virtuales (CDNs, GitHub, etc.). Por defecto NO
     * verificamos el certificado contra el store del SO (Windows no expone uno
     * compatible con OpenSSL de fábrica); el objetivo es habilitar el transporte
     * cifrado en modo script. TE_HTTP_INSECURE se mantiene como no-op aquí. */
    SSL *ssl = NULL;
    if (use_tls) {
        static SSL_CTX *g_ssl_ctx = NULL;
        if (!g_ssl_ctx) {
            SSL_library_init();
            SSL_load_error_strings();
            g_ssl_ctx = SSL_CTX_new(TLS_client_method());
        }
        if (!g_ssl_ctx) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return NULL;
        }
        ssl = SSL_new(g_ssl_ctx);
        if (ssl) {
            SSL_set_tlsext_host_name(ssl, host); /* SNI */
            SSL_set_fd(ssl, (int)sock);
        }
        if (!ssl || SSL_connect(ssl) != 1) {
            if (ssl) SSL_free(ssl);
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return NULL;
        }
    }
#endif
    /* Build request */
    TeBuf req; tebuf_init(&req);
    tebuf_puts(&req, method); tebuf_putc(&req, ' ');
    tebuf_puts(&req, path); tebuf_puts(&req, " HTTP/1.0\r\n");
    tebuf_puts(&req, "Host: "); tebuf_puts(&req, host);
    if (strcmp(port, use_tls ? "443" : "80") != 0) { tebuf_putc(&req, ':'); tebuf_puts(&req, port); }
    tebuf_puts(&req, "\r\n");
    tebuf_puts(&req, "User-Agent: TypeEasy/1.0\r\nAccept: */*\r\nConnection: close\r\n");
    /* Feature 2: forward custom request headers. Each non-empty "Name: value"
     * line (separated by '\n', tolerating a trailing '\r') is sent verbatim.
     * If the caller provides its own Content-Type we skip the default below. */
    int caller_set_ctype = 0;
    if (headers_str && *headers_str) {
        const char *p = headers_str;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t L = nl ? (size_t)(nl - p) : strlen(p);
            while (L > 0 && (p[L-1] == '\r' || p[L-1] == ' ' || p[L-1] == '\t')) L--;
            const char *s = p;
            while (L > 0 && (*s == ' ' || *s == '\t')) { s++; L--; }
            if (L > 0) {
                if (L >= 13 && te_strncasecmp(s, "Content-Type:", 13) == 0) caller_set_ctype = 1;
                char *line = (char*)malloc(L + 1);
                if (line) {
                    memcpy(line, s, L); line[L] = 0;
                    tebuf_puts(&req, line);
                    tebuf_puts(&req, "\r\n");
                    free(line);
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (body && *body) {
        char clbuf[64]; snprintf(clbuf, sizeof(clbuf), "Content-Length: %zu\r\n", strlen(body));
        tebuf_puts(&req, clbuf);
        if (!caller_set_ctype) tebuf_puts(&req, "Content-Type: application/json\r\n");
    }
    tebuf_puts(&req, "\r\n");
    if (body && *body) tebuf_puts(&req, body);
    /* Send */
    size_t sent = 0; size_t total = req.len;
    while (sent < total) {
#ifdef TE_HAVE_OPENSSL
        int w;
        if (ssl) {
            w = SSL_write(ssl, req.p + sent, (int)(total - sent));
        } else {
#ifdef _WIN32
            w = send(sock, req.p + sent, (int)(total - sent), 0);
#else
            w = (int)send(sock, req.p + sent, total - sent, 0);
#endif
        }
#else
#ifdef _WIN32
        int w = send(sock, req.p + sent, (int)(total - sent), 0);
#else
        ssize_t w = send(sock, req.p + sent, total - sent, 0);
#endif
#endif
        if (w <= 0) break;
        sent += (size_t)w;
    }
    free(req.p);
    /* Receive */
    TeBuf resp; tebuf_init(&resp);
    char chunk[4096];
#ifdef TE_HAVE_OPENSSL
    int r;
    for (;;) {
        if (ssl) {
            r = SSL_read(ssl, chunk, (int)sizeof(chunk));
        } else {
#ifdef _WIN32
            r = recv(sock, chunk, (int)sizeof(chunk), 0);
#else
            r = (int)recv(sock, chunk, sizeof(chunk), 0);
#endif
        }
        if (r <= 0) break;
#else
#ifdef _WIN32
    int r;
    while ((r = recv(sock, chunk, (int)sizeof(chunk), 0)) > 0) {
#else
    ssize_t r;
    while ((r = recv(sock, chunk, sizeof(chunk), 0)) > 0) {
#endif
#endif
        if (resp.len + (size_t)r + 1 > resp.cap) {
            while (resp.len + (size_t)r + 1 > resp.cap) resp.cap *= 2;
            resp.p = (char*)realloc(resp.p, resp.cap);
        }
        memcpy(resp.p + resp.len, chunk, (size_t)r);
        resp.len += (size_t)r;
        resp.p[resp.len] = 0;
    }
#ifdef TE_HAVE_OPENSSL
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
#endif
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    /* Feature 1: capture the HTTP status code from the status line
     * ("HTTP/1.x NNN reason"). Stays 0 (set by te_http_do) when no response. */
    if (resp.p && resp.len > 0) {
        const char *sp = strchr(resp.p, ' ');
        if (sp) { int st = atoi(sp + 1); if (st > 0) g_http_last_status = st; }
    }
    /* Strip headers: find \r\n\r\n */
    char *sep = strstr(resp.p, "\r\n\r\n");
    if (!sep) sep = strstr(resp.p, "\n\n");
    char *out;
    if (sep) {
        sep += (sep[0] == '\r') ? 4 : 2;
        out = strdup(sep);
    } else {
        out = strdup(resp.p);
    }
    free(resp.p);
    return out;
}

/* ─── HTTP client (libcurl backend, opcional) ───────────────────────────
 * Si TE_HAVE_LIBCURL esta definido, te_http_do() rutea por libcurl
 * (HTTPS + headers custom + cualquier metodo). Si no, cae al fallback
 * te_http_request() que solo soporta HTTP plano sin headers.
 *
 * Formato de headers_str: lineas "Header: value" separadas por '\n'.
 *   "Authorization: Bearer XXX\nX-Custom: 1"
 */
#ifdef TE_HAVE_LIBCURL
struct te_curl_buf { char *p; size_t len, cap; };

static size_t te_curl_write_cb(void *ptr, size_t sz, size_t nm, void *ud) {
    struct te_curl_buf *b = (struct te_curl_buf*)ud;
    size_t add = sz * nm;
    if (b->len + add + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->len + add + 1) nc *= 2;
        char *np = (char*)realloc(b->p, nc);
        if (!np) return 0;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->len, ptr, add);
    b->len += add;
    b->p[b->len] = 0;
    return add;
}

static char* te_http_request_curl(const char *method, const char *url,
                                   const char *body, const char *headers_str) {
    if (!url) return NULL;
    static int curl_inited = 0;
    if (!curl_inited) { curl_global_init(CURL_GLOBAL_DEFAULT); curl_inited = 1; }
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    struct te_curl_buf buf = {0};
    struct curl_slist *hdrs = NULL;
    int caller_set_ctype = 0;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "TypeEasy/1.0");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, te_curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    /* Por compatibilidad: aceptar certs autofirmados desactivado por default;
     * si el usuario lo necesita puede setear TE_HTTP_INSECURE=1 en el env. */
    if (getenv("TE_HTTP_INSECURE") && strcmp(getenv("TE_HTTP_INSECURE"), "1") == 0) {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (method && strcmp(method, "GET") != 0) {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    }
    if (body && *body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    if (headers_str && *headers_str) {
        const char *p = headers_str;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t L = nl ? (size_t)(nl - p) : strlen(p);
            while (L > 0 && (p[L-1] == '\r' || p[L-1] == ' ' || p[L-1] == '\t')) L--;
            if (L > 0) {
                char *line = (char*)malloc(L + 1);
                if (line) {
                    memcpy(line, p, L); line[L] = 0;
                    if (strncasecmp(line, "Content-Type:", 13) == 0) caller_set_ctype = 1;
                    hdrs = curl_slist_append(hdrs, line);
                    free(line);
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    /* Default Content-Type para POST/PUT/PATCH si caller no lo seteo. */
    if (body && *body && !caller_set_ctype) {
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    }
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
        g_http_last_status = (int)http_code; /* Feature 1: surface status code */
    }
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        if (buf.p) free(buf.p);
        return NULL;
    }
    return buf.p ? buf.p : strdup("");
}
#endif /* TE_HAVE_LIBCURL */

/* Wrapper unico que decide entre libcurl (full features) y fallback TCP. */
char* te_http_do(const char *method, const char *url,
                 const char *body, const char *headers_str) {
    g_http_last_status = 0; /* reset; stays 0 on network failure / no response */
#ifdef TE_HAVE_LIBCURL
    return te_http_request_curl(method, url, body, headers_str);
#else
    return te_http_request(method, url, body, headers_str);
#endif
}
