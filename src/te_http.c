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

/* Minimal HTTP/1.0 client over plain TCP. Returns body as malloc'd string (caller frees), or NULL. */
static char* te_http_request(const char *method, const char *url, const char *body) {
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
    /* Parse http://host[:port]/path */
    const char *u = url;
    if (strncmp(u, "http://", 7) == 0) u += 7;
    else if (strncmp(u, "https://", 8) == 0) return NULL; /* TLS no soportado en stdlib mínima */
    char host[512] = {0};
    char port[16]  = "80";
    char path[1024] = "/";
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
    /* Build request */
    TeBuf req; tebuf_init(&req);
    tebuf_puts(&req, method); tebuf_putc(&req, ' ');
    tebuf_puts(&req, path); tebuf_puts(&req, " HTTP/1.0\r\n");
    tebuf_puts(&req, "Host: "); tebuf_puts(&req, host);
    if (strcmp(port, "80") != 0) { tebuf_putc(&req, ':'); tebuf_puts(&req, port); }
    tebuf_puts(&req, "\r\n");
    tebuf_puts(&req, "User-Agent: TypeEasy/1.0\r\nAccept: */*\r\nConnection: close\r\n");
    if (body && *body) {
        char clbuf[64]; snprintf(clbuf, sizeof(clbuf), "Content-Length: %zu\r\n", strlen(body));
        tebuf_puts(&req, clbuf);
        tebuf_puts(&req, "Content-Type: application/json\r\n");
    }
    tebuf_puts(&req, "\r\n");
    if (body && *body) tebuf_puts(&req, body);
    /* Send */
    size_t sent = 0; size_t total = req.len;
    while (sent < total) {
#ifdef _WIN32
        int w = send(sock, req.p + sent, (int)(total - sent), 0);
#else
        ssize_t w = send(sock, req.p + sent, total - sent, 0);
#endif
        if (w <= 0) break;
        sent += (size_t)w;
    }
    free(req.p);
    /* Receive */
    TeBuf resp; tebuf_init(&resp);
    char chunk[4096];
#ifdef _WIN32
    int r;
    while ((r = recv(sock, chunk, (int)sizeof(chunk), 0)) > 0) {
#else
    ssize_t r;
    while ((r = recv(sock, chunk, sizeof(chunk), 0)) > 0) {
#endif
        if (resp.len + (size_t)r + 1 > resp.cap) {
            while (resp.len + (size_t)r + 1 > resp.cap) resp.cap *= 2;
            resp.p = (char*)realloc(resp.p, resp.cap);
        }
        memcpy(resp.p + resp.len, chunk, (size_t)r);
        resp.len += (size_t)r;
        resp.p[resp.len] = 0;
    }
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
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
#ifdef TE_HAVE_LIBCURL
    return te_http_request_curl(method, url, body, headers_str);
#else
    (void)headers_str;
    return te_http_request(method, url, body);
#endif
}
