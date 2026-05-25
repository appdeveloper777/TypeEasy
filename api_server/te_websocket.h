/* te_websocket.h — Native WebSocket support for TypeEasy
 *
 * Exposes:
 *   - te_ws_register_routes(ctx)   : install civetweb WS callbacks for routes
 *                                    in global_methods that have http_method=="WS"
 *   - te_ws_send_current(msg)      : send to the connection currently being handled
 *   - te_ws_broadcast(channel, msg): send to every conn subscribed to <channel>
 *   - te_ws_subscribe_current(ch)  : subscribe the currently-handling conn to <channel>
 *   - te_ws_current_id_str(buf,n)  : write the id of the currently-handling conn
 *
 * The currently-handling connection is set by the WS callbacks before invoking
 * the .te handler (running in the connection's thread), so builtins called from
 * inside the .te handler resolve to that connection.
 */
#ifndef TE_WEBSOCKET_H
#define TE_WEBSOCKET_H

#include "civetweb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Discover routes with http_method=="WS" from global_methods and register a
 * civetweb websocket handler per route. Call AFTER discover_routes() and AFTER
 * mg_start() returns. */
void te_ws_register_routes(struct mg_context *ctx);

/* Builtins (called from .te runtime) */
int  te_ws_subscribe_current(const char *channel);
int  te_ws_send_current(const char *msg);
int  te_ws_broadcast(const char *channel, const char *msg);
int  te_ws_current_id_str(char *out, int cap);

/* Lifecycle */
void te_ws_init(void);
void te_ws_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_WEBSOCKET_H */
