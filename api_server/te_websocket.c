/* te_websocket.c — TypeEasy native WebSocket runtime built on civetweb.
 *
 * Design:
 *   - On startup, te_ws_register_routes() walks global_methods, finds methods
 *     marked with http_method=="WS" and registers one civetweb websocket handler
 *     per route_path.
 *   - Each accepted connection is wrapped in a TeWsConn that lives in a global
 *     linked list protected by g_lock. The TeWsConn carries the .te handler
 *     MethodNode* and a linked list of subscribed channels.
 *   - On ws_ready the .te handler runs ONCE in the connection's thread; during
 *     that call, the thread-local "current connection" is set so that
 *     ws_subscribe(), ws_send() and request_param() resolve against it.
 *     The handler typically calls ws_subscribe("topic") and ws_send(initialState).
 *   - HTTP request handlers (or any code in the interpreter) can call
 *     ws_broadcast("topic", payload) at any time; we walk the conn list, send
 *     to those subscribed, and prune dead ones.
 *   - We hold a global interpreter lock (g_lock) around any invocation of
 *     typeeasy_embedded_invoke_method() because the AST runtime is not
 *     thread-safe. HTTP requests are already serialized through the same lock.
 *     (Reviewer note: this trades concurrency for correctness; the chess use
 *     case is fine — TypeEasy's runtime can't safely run two handlers at once
 *     anyway due to globals like g_req_query.)
 */
#include "te_websocket.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "civetweb.h"

/* Forward decls from the interpreter / api_server */
struct MethodNode;
extern struct MethodNode *global_methods;
char *typeeasy_embedded_invoke_method(struct MethodNode *m);

/* HTTP state setters (defined in src/ast.c) — we reuse the same machinery for
 * passing path params and query string into the .te handler. */
void typeeasy_http_reset(void);
void typeeasy_http_set_method(const char *m);
void typeeasy_http_set_path  (const char *p);
void typeeasy_http_add_query (const char *k, const char *v);
void typeeasy_http_add_header(const char *k, const char *v);
void typeeasy_http_add_param (const char *k, const char *v);

/* MethodNode layout (mirror from src/ast.h; we only need name/route_path/http_method) */
typedef struct MN {
    char *name;
    void *params;
    void *body;
    char *route_path;
    char *http_method;
    int cache_ttl;
    char *return_type;
    void *bc_body;
    struct MN *next;
} MN;

/* ===== Channel list per connection ===== */
typedef struct ChanNode {
    char *name;
    struct ChanNode *next;
} ChanNode;

/* ===== Connection record ===== */
typedef struct TeWsConn {
    struct mg_connection *mg_conn;
    unsigned int id;          /* monotonic id, only for debug/expose */
    MN *handler;              /* the .te handler MethodNode* */
    char *route_path;
    ChanNode *channels;
    int alive;
    struct TeWsConn *next;
} TeWsConn;

/* ===== Globals ===== */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;  /* protects the lists AND interpreter calls */
static TeWsConn *g_conns = NULL;
static unsigned int g_next_id = 1;

/* Thread-local current connection (set during .te handler invocation) */
static pthread_key_t  g_tls_key;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;
static void make_tls(void) { pthread_key_create(&g_tls_key, NULL); }
static TeWsConn *current_conn(void) {
    pthread_once(&g_tls_once, make_tls);
    return (TeWsConn*)pthread_getspecific(g_tls_key);
}
static void set_current_conn(TeWsConn *c) {
    pthread_once(&g_tls_once, make_tls);
    pthread_setspecific(g_tls_key, c);
}

/* ===== URI/route matching (subset of match_route_pattern in servidor_api.c) ==
 * We need our own because the civetweb websocket handler is registered for a
 * literal prefix; we still validate the full pattern and extract {name} params
 * into typeeasy_http_*. */
static int ws_match_pattern(const char *pattern, const char *uri) {
    const char *pp = pattern, *up = uri;
    while (*pp && *up) {
        if (*pp == '{') {
            const char *name_start = pp + 1;
            const char *name_end = strchr(name_start, '}');
            if (!name_end) return 0;
            const char *val_start = up;
            while (*up && *up != '/') up++;
            int nlen = name_end - name_start;
            int vlen = up - val_start;
            char name[64], val[256];
            if (nlen <= 0 || nlen >= (int)sizeof(name) || vlen >= (int)sizeof(val)) return 0;
            memcpy(name, name_start, nlen); name[nlen] = '\0';
            memcpy(val,  val_start,  vlen); val[vlen]   = '\0';
            typeeasy_http_add_param(name, val);
            pp = name_end + 1;
        } else if (*pp == *up) {
            pp++; up++;
        } else {
            return 0;
        }
    }
    return (*pp == '\0' && *up == '\0');
}

/* Populate query params from the request_info query_string (same parse loop
 * as manejadorApiDinamico). */
static void ws_populate_query(const char *qs) {
    if (!qs || !*qs) return;
    const char *p = qs;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!amp) amp = p + strlen(p);
        char key[128]; char val[1024];
        if (eq && eq < amp) {
            int kl = eq - p; int vl = amp - (eq + 1);
            if (kl > 0 && kl < (int)sizeof(key) && vl < (int)sizeof(val)) {
                memcpy(key, p, kl); key[kl] = '\0';
                memcpy(val, eq + 1, vl); val[vl] = '\0';
                typeeasy_http_add_query(key, val);
            }
        }
        if (*amp == '\0') break;
        p = amp + 1;
    }
}

/* ===== Connection registry helpers (caller holds g_lock) ===== */
static TeWsConn *conn_new(struct mg_connection *mg, MN *handler) {
    TeWsConn *c = (TeWsConn*)calloc(1, sizeof(TeWsConn));
    if (!c) return NULL;
    c->mg_conn = mg;
    c->id = g_next_id++;
    c->handler = handler;
    c->route_path = handler->route_path ? strdup(handler->route_path) : NULL;
    c->channels = NULL;
    c->alive = 1;
    c->next = g_conns;
    g_conns = c;
    return c;
}

static void conn_free_channels(TeWsConn *c) {
    ChanNode *ch = c->channels;
    while (ch) {
        ChanNode *n = ch->next;
        free(ch->name);
        free(ch);
        ch = n;
    }
    c->channels = NULL;
}

static void conn_remove(TeWsConn *target) {
    TeWsConn **pp = &g_conns;
    while (*pp) {
        if (*pp == target) {
            *pp = target->next;
            conn_free_channels(target);
            free(target->route_path);
            free(target);
            return;
        }
        pp = &(*pp)->next;
    }
}

static int conn_has_channel(TeWsConn *c, const char *ch) {
    for (ChanNode *n = c->channels; n; n = n->next) {
        if (strcmp(n->name, ch) == 0) return 1;
    }
    return 0;
}

/* ===== Civetweb callbacks ===== */
/* connect: invoked when WS handshake arrives. Return 0 to accept, !=0 to reject.
 * cbdata is the MethodNode* of the .te handler. */
static int cb_connect(const struct mg_connection *conn, void *cbdata) {
    MN *handler = (MN*)cbdata;
    const struct mg_request_info *req = mg_get_request_info(conn);
    const char *uri = req->local_uri;

    /* Reset shared HTTP state and re-populate so the handler invocation in
     * cb_ready can see request_param/request_query. */
    pthread_mutex_lock(&g_lock);
    typeeasy_http_reset();
    typeeasy_http_set_method("WS");
    typeeasy_http_set_path(uri);

    int ok = ws_match_pattern(handler->route_path, uri);
    if (!ok) {
        fprintf(stderr, "[WS] reject: pattern %s != uri %s\n", handler->route_path, uri);
        typeeasy_http_reset();
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    ws_populate_query(req->query_string);
    for (int i = 0; i < req->num_headers; i++) {
        typeeasy_http_add_header(req->http_headers[i].name,
                                 req->http_headers[i].value);
    }
    /* leave lock held? No — we'll re-lock in cb_ready. Other handlers may run
     * between cb_connect and cb_ready; this isn't a hot path. */
    pthread_mutex_unlock(&g_lock);
    fprintf(stderr, "[WS] accept %s\n", uri);
    return 0;
}

/* ready: invoked once the WS connection is fully established. Now we can
 * register the connection and run the .te handler. */
static void cb_ready(struct mg_connection *conn, void *cbdata) {
    MN *handler = (MN*)cbdata;

    pthread_mutex_lock(&g_lock);
    TeWsConn *c = conn_new(conn, handler);
    if (!c) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    /* Re-populate HTTP state for the handler invocation (cb_connect's state
     * may have been clobbered by another request). */
    const struct mg_request_info *req = mg_get_request_info(conn);
    typeeasy_http_reset();
    typeeasy_http_set_method("WS");
    typeeasy_http_set_path(req->local_uri);
    ws_match_pattern(handler->route_path, req->local_uri); /* re-extract params */
    ws_populate_query(req->query_string);
    for (int i = 0; i < req->num_headers; i++) {
        typeeasy_http_add_header(req->http_headers[i].name,
                                 req->http_headers[i].value);
    }

    set_current_conn(c);
    char *result = typeeasy_embedded_invoke_method((struct MethodNode*)handler);
    set_current_conn(NULL);

    if (result) free(result);
    typeeasy_http_reset();
    pthread_mutex_unlock(&g_lock);
}

/* data: invoked when the client sends a frame. We only handle text frames;
 * we don't expose them to .te yet (handler is one-shot at connect). Just
 * keep the connection alive. Return 1 to keep, 0 to close. */
static int cb_data(struct mg_connection *conn, int bits, char *data, size_t len, void *cbdata) {
    (void)conn; (void)data; (void)len; (void)cbdata;
    int opcode = bits & 0x0F;
    if (opcode == 0x8) return 0; /* close */
    return 1;
}

/* close: connection closed. Remove from registry. */
static void cb_close(const struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    pthread_mutex_lock(&g_lock);
    for (TeWsConn *c = g_conns; c; c = c->next) {
        if (c->mg_conn == conn) {
            fprintf(stderr, "[WS] close id=%u %s\n", c->id, c->route_path ? c->route_path : "?");
            conn_remove(c);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* ===== Public API ===== */
void te_ws_init(void) {
    pthread_once(&g_tls_once, make_tls);
}

void te_ws_shutdown(void) {
    pthread_mutex_lock(&g_lock);
    while (g_conns) conn_remove(g_conns);
    pthread_mutex_unlock(&g_lock);
}

/* Civetweb pattern syntax uses '*' for "anything but / and ?". Our route uses
 * '{name}'. Translate '/ws/game/{id}' -> '/ws/game/*'. Caller frees the
 * returned buffer. */
static char *translate_to_civetweb_pattern(const char *src) {
    if (!src) return NULL;
    size_t cap = strlen(src) + 8;
    char *out = (char*)malloc(cap);
    if (!out) return NULL;
    size_t o = 0;
    for (const char *p = src; *p; ) {
        if (*p == '{') {
            const char *end = strchr(p, '}');
            if (!end) { p++; continue; }
            if (o + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            out[o++] = '*';
            p = end + 1;
        } else {
            if (o + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return out;
}

void te_ws_register_routes(struct mg_context *ctx) {
    te_ws_init();
    if (!ctx) return;
    int count = 0;
    for (MN *m = (MN*)global_methods; m; m = m->next) {
        if (!m->http_method || strcmp(m->http_method, "WS") != 0) continue;
        if (!m->route_path) continue;
        char *cw_pattern = translate_to_civetweb_pattern(m->route_path);
        if (!cw_pattern) continue;
        mg_set_websocket_handler(ctx, cw_pattern,
                                 cb_connect, cb_ready, cb_data, cb_close,
                                 (void*)m);
        fprintf(stderr, "[WS] registered %s (civetweb=%s) -> %s()\n",
                m->route_path, cw_pattern, m->name ? m->name : "?");
        free(cw_pattern);
        count++;
    }
    if (count == 0) {
        fprintf(stderr, "[WS] no WebSocket routes found\n");
    }
}

int te_ws_subscribe_current(const char *channel) {
    if (!channel || !*channel) return 0;
    TeWsConn *c = current_conn();
    if (!c) {
        fprintf(stderr, "[WS] ws_subscribe outside a WS handler — ignored\n");
        return 0;
    }
    /* caller is inside cb_ready which already holds g_lock */
    if (conn_has_channel(c, channel)) return 1;
    ChanNode *n = (ChanNode*)malloc(sizeof(ChanNode));
    if (!n) return 0;
    n->name = strdup(channel);
    n->next = c->channels;
    c->channels = n;
    fprintf(stderr, "[WS] id=%u subscribed to %s\n", c->id, channel);
    return 1;
}

int te_ws_send_current(const char *msg) {
    if (!msg) return 0;
    TeWsConn *c = current_conn();
    if (!c) return 0;
    int rc = mg_websocket_write(c->mg_conn, MG_WEBSOCKET_OPCODE_TEXT,
                                msg, strlen(msg));
    return rc > 0;
}

int te_ws_broadcast(const char *channel, const char *msg) {
    if (!channel || !msg) return 0;
    int sent = 0;
    /* Snapshot the list while holding the lock so we can write outside it.
     * (mg_websocket_write may block on a slow client; we don't want to
     * block other handlers.) */
    struct mg_connection **targets = NULL;
    int n = 0, cap = 0;
    pthread_mutex_lock(&g_lock);
    for (TeWsConn *c = g_conns; c; c = c->next) {
        if (!c->alive) continue;
        if (!conn_has_channel(c, channel)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            targets = (struct mg_connection**)realloc(targets, cap * sizeof(*targets));
            if (!targets) { pthread_mutex_unlock(&g_lock); return 0; }
        }
        targets[n++] = c->mg_conn;
    }
    pthread_mutex_unlock(&g_lock);

    size_t mlen = strlen(msg);
    for (int i = 0; i < n; i++) {
        int rc = mg_websocket_write(targets[i], MG_WEBSOCKET_OPCODE_TEXT, msg, mlen);
        if (rc > 0) sent++;
    }
    free(targets);
    if (sent) fprintf(stderr, "[WS] broadcast %s -> %d client(s)\n", channel, sent);
    return sent;
}

int te_ws_current_id_str(char *out, int cap) {
    TeWsConn *c = current_conn();
    if (!c) { if (cap) out[0] = 0; return 0; }
    return snprintf(out, cap, "%u", c->id);
}
