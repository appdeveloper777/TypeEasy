/* te_async.h — TypeEasy cooperative async runtime.
 *
 * A small, real cooperative event loop. Task handles are plain integers
 * (like bridge slots), so they store cleanly in TypeEasy variables.
 *
 * .te builtins registered by te_register_async_builtins():
 *   spawn(lambda)              -> int task   (defers `lambda` until awaited)
 *   lang_call_async(slot, req) -> int task   (writes `req` now, reply later)
 *   await_task(task)           -> result     (drives the loop until done)
 *   await(task)                -> alias of await_task
 *   await_all(t1, t2, ... | [t1,t2]) -> list of results (run concurrently)
 *
 * The headline win is overlap: several lang_call_async() handoffs run while
 * the event loop polls every child's stdout without blocking, so awaiting N
 * subprocess calls costs ~max(latency_i), not the sum. See te_bridge.h for
 * the non-blocking write/poll primitives this builds on.
 */
#ifndef TE_ASYNC_H
#define TE_ASYNC_H

/* Registers the async builtins into the global builtin registry.
 * Called once from te_register_ast_builtins(). Idempotent. */
void te_register_async_builtins(void);

#endif /* TE_ASYNC_H */
