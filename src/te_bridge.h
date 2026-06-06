/* te_bridge.h — TypeEasy subprocess language bridge.
 *
 * Spawns external programs (Java, C#, Python, Node, Rust, ...) and talks to
 * them over a newline-delimited request/response protocol on stdin/stdout.
 * Each request is one line written to the child's stdin; each response is one
 * line read from the child's stdout. The payload is conventionally JSON but
 * the bridge itself is byte-agnostic (it only cares about the '\n' framing).
 *
 * Cross-platform: POSIX (pipe + fork + exec) and Windows (CreatePipe +
 * CreateProcess). See REGLA #1 in /memories/repo/typeeasy.md.
 *
 * .te builtins registered by te_register_bridge_builtins():
 *   lang_spawn(cmdline)        -> int slot (>=0) or -1 on failure
 *   lang_call(slot, request)   -> string response (one line, newline stripped)
 *   lang_send(slot, line)      -> int 1/0 (write only, no read)
 *   lang_recv(slot)            -> string (read one line, no write)
 *   lang_close(slot)           -> int 0
 * Aliases proc_spawn/proc_call/proc_send/proc_recv/proc_close are also
 * registered for users who prefer the generic "process" naming.
 */
#ifndef TE_BRIDGE_H
#define TE_BRIDGE_H

/* Registers the bridge builtins into the global builtin registry.
 * Called once from te_register_ast_builtins(). Idempotent. */
void te_register_bridge_builtins(void);

/* ---- Public API used by the async event loop (te_async.c) ---- */
#include <stddef.h>

/* Write one '\n'-terminated request line to the child's stdin without
 * waiting for a reply (non-blocking handoff). Returns 0 on success, -1 on
 * error. */
int te_bridge_write_line(int slot, const char *data, size_t len);

/* Non-blocking attempt to extract one complete '\n'-terminated line from the
 * child's stdout. Never blocks. Returns:
 *    1  -> *out set to a malloc'd line (caller frees), newline stripped
 *    0  -> no complete line available yet (poll again later)
 *   -1  -> EOF/error and nothing left to deliver
 */
int te_bridge_poll_line(int slot, char **out);

/* Block (efficiently) until at least one of the given child stdout streams is
 * readable, or `timeout_ms` elapses. Used by the async event loop to wait for
 * a slow worker's reply WITHOUT holding the global interpreter lock or busy-
 * spinning. Only reads each slot's fd/handle (owner-private state), so it is
 * safe to call with the lock released.
 *   Returns: the number of valid slots that were waited on (0 if none — the
 *   caller should then fall back to a short sleep). On POSIX it does a real
 *   poll(); on Windows (where pipes have no poll) it returns 0 so the caller
 *   keeps its existing 1ms-sleep behaviour unchanged. */
int te_bridge_wait_readable(const int *slots, int n, int timeout_ms);

#endif /* TE_BRIDGE_H */
