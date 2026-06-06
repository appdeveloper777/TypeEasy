/* te_evloop.h — TypeEasy real async runtime (fiber-based event loop).
 *
 * Stage 1 of the "async real" work. Unlike te_async.c (which runs spawned
 * lambdas to completion sequentially), this module provides TRUE suspend /
 * resume: a task can pause mid-execution at an `await` point and the event
 * loop will run other ready tasks until it can be resumed.
 *
 * It does so with stackful coroutines ("fibers"):
 *   - Windows : CreateFiber / SwitchToFiber (native OS fibers).
 *   - POSIX   : ucontext (getcontext / makecontext / swapcontext).
 *
 * Each fiber owns its own C call stack, so the recursive tree-walking
 * interpreter (interpret_ast) keeps working untouched — a paused handler is
 * simply a parked C stack. A small per-fiber snapshot of the interpreter's
 * variable scope keeps tasks from clobbering each other's locals while they
 * are interleaved on the single interpreter thread.
 *
 * .te builtins registered by te_register_evloop_builtins():
 *   go(lambda)              -> int task   (schedule a coroutine; runs on await)
 *   sleep_async(ms)         -> 0          (suspend this task ~ms, yield to loop)
 *   await_async(task)       -> result     (drive the loop until `task` is done)
 *   await_async_all(t, ...) -> list       (run several tasks concurrently)
 *
 * The headline win is overlap: N tasks that each sleep_async(100) finish in
 * ~100 ms total, not N*100 ms, because the loop advances all of them while
 * they wait. Stage 3 extends the same yield mechanism to real I/O (DB/HTTP)
 * by running the blocking call on a background thread-pool and yielding the
 * fiber until it completes.
 */
#ifndef TE_EVLOOP_H
#define TE_EVLOOP_H

/* Registers the event-loop builtins into the global builtin registry.
 * Called once from te_register_ast_builtins() / te_builtins bootstrap.
 * Idempotent. */
void te_register_evloop_builtins(void);

#endif /* TE_EVLOOP_H */
