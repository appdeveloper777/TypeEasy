/* te_evloop.c — TypeEasy real async runtime (fiber-based event loop).
 *
 * See te_evloop.h for the public surface and the rationale.
 *
 * ---------------------------------------------------------------------------
 * HOW IT WORKS
 * ---------------------------------------------------------------------------
 * A "fiber" is a stackful coroutine: it owns a private C call stack, so the
 * recursive interpreter (interpret_ast -> evaluate_expression -> call_lambda
 * -> interpret_ast ...) runs inside it without any change. Pausing a task is
 * just parking its C stack; resuming is switching back to it.
 *
 *   Windows : CreateFiber / SwitchToFiber  (the OS gives us fibers directly).
 *   POSIX   : ucontext (getcontext/makecontext/swapcontext).
 *
 * The scheduler runs on the interpreter thread itself. Only ONE fiber runs at
 * a time (cooperative), and a fiber only ever yields at an explicit await
 * point (sleep_async today; real I/O in Stage 3). Because the interpreter's
 * variable scope lives in process globals (vars[]/var_count/...), we snapshot
 * that slice into each fiber on every context switch, so interleaved tasks do
 * not clobber each other's locals. Globals defined before the loop started are
 * copied into each fiber's starting scope (read-shared), matching the usual
 * "async function sees module globals" mental model.
 */

#include "te_evloop.h"
#include "ast.h"
#include "te_builtins.h"
#include "te_bytecode.h"   /* BC_NOT_COMPILABLE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* ---- interpreter globals we snapshot per fiber (defined in ast.c) -------- */
#define MAX_VARS 100
extern Variable vars[MAX_VARS];
extern int      var_count;
extern Variable __ret_var;
extern int      __ret_var_active;
extern int      return_flag;
extern int      throw_flag;
extern int      g_call_depth;

/* ---- platform: fibers, monotonic clock, sleep ---------------------------- */
#if defined(_WIN32)
  #include <windows.h>
  #define TE_FIBER_STACK_SIZE_WIN (512 * 1024)
  static long long te_now_ms(void) { return (long long)GetTickCount64(); }
  static void      te_msleep(int ms) { Sleep((DWORD)(ms < 0 ? 0 : ms)); }
#else
  #include <ucontext.h>
  #include <time.h>
  #include <unistd.h>
  static long long te_now_ms(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  }
  static void te_msleep(int ms) {
      if (ms <= 0) return;
      struct timespec ts;
      ts.tv_sec  = ms / 1000;
      ts.tv_nsec = (long)(ms % 1000) * 1000000L;
      nanosleep(&ts, NULL);
  }
  #define TE_FIBER_STACK_SIZE (512 * 1024)
#endif

/* ---- per-fiber interpreter context snapshot ------------------------------ */
typedef struct {
    Variable vars_copy[MAX_VARS];
    int      var_count;
    Variable ret;
    int      ret_active;
    int      return_flag;
    int      throw_flag;
    int      call_depth;
} CtxSnapshot;

/* A Variable owns three heap char* fields: id, type, and (when VAL_STRING)
 * value.string_value. object_value (lists/maps/objects) is NOT owned here —
 * those live for the whole program run and are shared by pointer, never freed
 * mid-run by the interpreter (add_or_update_variable only frees string_value +
 * type on reassign). So a memory-safe snapshot must deep-copy exactly those
 * three char* fields and leave object_value aliased. Stage 1 used a shallow
 * memcpy which aliased the char* fields across the caller and every fiber,
 * so reassigning a string/typed variable inside a coroutine would free a
 * pointer still held elsewhere (double-free at exit). Stage 2 fixes that by
 * giving every snapshot fully independent string storage. */
static void var_dup_owned(Variable *v) {
    if (v->id)   v->id   = strdup(v->id);
    if (v->type) v->type = strdup(v->type);
    if (v->vtype == VAL_STRING && v->value.string_value)
        v->value.string_value = strdup(v->value.string_value);
}

static void var_free_owned(Variable *v) {
    if (v->id)   { free(v->id);   v->id = NULL; }
    if (v->type) { free(v->type); v->type = NULL; }
    if (v->vtype == VAL_STRING && v->value.string_value) {
        free(v->value.string_value);
        v->value.string_value = NULL;
    }
}

/* Release a snapshot's owned strings (call before discarding/overwriting it). */
static void ctx_free(CtxSnapshot *s) {
    int n = s->var_count;
    if (n < 0) n = 0;
    if (n > MAX_VARS) n = MAX_VARS;
    for (int i = 0; i < n; i++) var_free_owned(&s->vars_copy[i]);
    s->var_count = 0;
    if (s->ret_active) { var_free_owned(&s->ret); }
    s->ret_active = 0;
}

/* Snapshot the live interpreter scope into `s`, deep-copying owned strings so
 * the snapshot shares nothing freeable with the live scope. Frees any prior
 * contents of `s` first. */
static void ctx_save(CtxSnapshot *s) {
    int n = var_count;
    if (n < 0) n = 0;
    if (n > MAX_VARS) n = MAX_VARS;
    ctx_free(s);                       /* release previous snapshot storage */
    memcpy(s->vars_copy, vars, (size_t)n * sizeof(Variable));
    for (int i = 0; i < n; i++) var_dup_owned(&s->vars_copy[i]);
    s->var_count   = n;
    s->ret         = __ret_var;
    s->ret_active  = __ret_var_active;
    if (s->ret_active) var_dup_owned(&s->ret);
    s->return_flag = return_flag;
    s->throw_flag  = throw_flag;
    s->call_depth  = g_call_depth;
}

/* Make the live interpreter scope equal to `s` (deep-copied), freeing whatever
 * the live scope currently owns. The snapshot keeps its own copies intact, so
 * it can be restored again later. The name->index side-index is rebuilt because
 * vars[] is rewritten in place. */
static void ctx_restore(const CtxSnapshot *s) {
    int n = s->var_count;
    if (n < 0) n = 0;
    if (n > MAX_VARS) n = MAX_VARS;
    /* free what the live scope currently owns */
    int live = var_count;
    if (live < 0) live = 0;
    if (live > MAX_VARS) live = MAX_VARS;
    for (int i = 0; i < live; i++) var_free_owned(&vars[i]);
    if (__ret_var_active) var_free_owned(&__ret_var);
    /* copy in the snapshot, giving the live scope its own fresh strings */
    memcpy(vars, s->vars_copy, (size_t)n * sizeof(Variable));
    for (int i = 0; i < n; i++) var_dup_owned(&vars[i]);
    var_count        = n;
    __ret_var        = s->ret;
    __ret_var_active = s->ret_active;
    if (__ret_var_active) var_dup_owned(&__ret_var);
    return_flag      = s->return_flag;
    throw_flag       = s->throw_flag;
    g_call_depth     = s->call_depth;
    te_runtime_rebuild_symtab();       /* keep name->index lookups correct */
}

/* ---- fiber pool ---------------------------------------------------------- */
#define TE_FIBER_MAX 256

enum { FB_FREE = 0, FB_READY, FB_SLEEPING, FB_DONE, FB_IO_WAIT };

/* Stage 3: a unit of blocking I/O handed to a background worker thread. The
 * worker runs ONLY pure C I/O (no interpreter state) and stores the raw bytes;
 * the owning fiber converts them to an ASTNode after it is resumed on the main
 * thread. `done` and the result buffer are the only fields shared across
 * threads and are published under g_io_mu. */
typedef struct IoJob {
    int            kind;       /* IO_KIND_* */
    char          *in_str;     /* input (e.g. file path), owned */
    char          *out_buf;    /* result bytes, owned (malloc) */
    long           out_len;    /* result length */
    int            ok;         /* 1 = success, 0 = error */
    int            done;       /* set by worker under g_io_mu when finished */
    int            fiber_id;   /* fiber to wake */
    struct IoJob  *qnext;      /* job-queue link */
} IoJob;

enum { IO_KIND_READ_FILE = 1 };

typedef struct {
    int          in_use;
    int          state;
    ASTNode     *lambda;    /* the .te lambda to run (owned by the AST) */
    ASTNode     *result;    /* DONE: handed to __ret__ on await */
    long long    wake_at;   /* SLEEPING: monotonic ms deadline */
    int          started;   /* 0 until the fiber body has begun */
    IoJob       *io_job;    /* IO_WAIT: the pending blocking job (NULL otherwise) */
    CtxSnapshot  ctx;       /* parked interpreter scope */
#if defined(_WIN32)
    void        *os_fiber;  /* LPVOID from CreateFiber */
#else
    ucontext_t   uctx;
    char        *stack;
#endif
} Fiber;

static Fiber g_fibers[TE_FIBER_MAX];
static Fiber *g_current = NULL;       /* fiber currently running, NULL = scheduler */
static int    g_loop_active = 0;      /* re-entrancy guard for the driver */

#if defined(_WIN32)
static void *g_sched_fiber = NULL;    /* the scheduler's fiber (this drain) */
static int   g_we_converted = 0;      /* did WE ConvertThreadToFiber this drain? */
#else
static ucontext_t g_sched_uctx;       /* scheduler resume point */
#endif

static int fiber_find_free(void) {
    for (int i = 0; i < TE_FIBER_MAX; i++)
        if (!g_fibers[i].in_use) return i;
    return -1;
}

static int fiber_valid(int id) {
    return id >= 0 && id < TE_FIBER_MAX && g_fibers[id].in_use;
}

/* Run the current fiber's lambda body, then return to the scheduler for good. */
static void fiber_body(void) {
    Fiber *f = g_current;
    if (f) {
        f->started = 1;
        ASTNode *r = f->lambda ? call_lambda(f->lambda, NULL) : NULL;
        f->result  = r;
        f->state   = FB_DONE;
    }
    /* Hand control back to the scheduler; this fiber is never resumed again. */
#if defined(_WIN32)
    SwitchToFiber(g_sched_fiber);
    /* Defensive: a finished fiber must never fall off the end of its proc. */
    for (;;) SwitchToFiber(g_sched_fiber);
#else
    /* uc_link (set at makecontext) returns us to the scheduler automatically. */
#endif
}

#if defined(_WIN32)
static VOID CALLBACK fiber_trampoline(LPVOID param) {
    (void)param;
    fiber_body();
}
#else
static void fiber_trampoline(void) {
    fiber_body();
}
#endif

/* Suspend the running fiber and return to the scheduler. On resume, the
 * scheduler has already restored this fiber's interpreter context. */
static void fiber_yield(void) {
    Fiber *f = g_current;
    if (!f) return;            /* not inside a fiber: nothing to yield */
    ctx_save(&f->ctx);
#if defined(_WIN32)
    SwitchToFiber(g_sched_fiber);
#else
    swapcontext(&f->uctx, &g_sched_uctx);
#endif
    /* resumed here later */
}

/* Scheduler -> fiber. Restores the fiber's context, runs it until it yields
 * or finishes, then control returns here. */
static void fiber_resume(Fiber *f) {
    ctx_restore(&f->ctx);
    g_current = f;
#if defined(_WIN32)
    if (!f->os_fiber) {
        f->os_fiber = CreateFiber(TE_FIBER_STACK_SIZE_WIN, fiber_trampoline, f);
    }
    SwitchToFiber(f->os_fiber);
#else
    if (!f->started && !f->stack) {
        f->stack = (char *)malloc(TE_FIBER_STACK_SIZE);
        getcontext(&f->uctx);
        f->uctx.uc_stack.ss_sp   = f->stack;
        f->uctx.uc_stack.ss_size = TE_FIBER_STACK_SIZE;
        f->uctx.uc_link          = &g_sched_uctx;
        makecontext(&f->uctx, fiber_trampoline, 0);
    }
    swapcontext(&g_sched_uctx, &f->uctx);
#endif
    g_current = NULL;
}

/* Make sure the scheduler context exists (Windows must convert the thread to a
 * fiber before SwitchToFiber works). Windows fibers are thread-affine, so the
 * conversion is established per top-level drain on whatever thread is running
 * it (the API server serializes drains behind a global invoke lock but may use
 * a different pool thread for each request). scheduler_release() undoes it so
 * the thread is left exactly as it was found. */
static void scheduler_ensure(void) {
#if defined(_WIN32)
    if (IsThreadAFiber()) {
        g_sched_fiber  = GetCurrentFiber();
        g_we_converted = 0;            /* already a fiber: do not convert back */
    } else {
        g_sched_fiber  = ConvertThreadToFiber(NULL);
        g_we_converted = 1;            /* we own the conversion; undo it later */
    }
#endif
}

/* Undo a conversion we performed in scheduler_ensure(). A thread converted with
 * ConvertThreadToFiber MUST be converted back before it exits, or the CRT/OpenMP
 * teardown crashes; doing it at the end of every drain keeps each server worker
 * thread in its original (non-fiber) state. */
static void scheduler_release(void) {
#if defined(_WIN32)
    if (g_we_converted && IsThreadAFiber()) {
        ConvertFiberToThread();
        g_we_converted = 0;
    }
    g_sched_fiber = NULL;
#endif
}

/* Free a finished fiber's OS resources and slot, returning its result. */
/* ------------------------------------------------------------------------
 * Result ownership: call_lambda() returns a value node that, for a literal
 * `return 1`, is a *borrowed pointer into the program AST* (the `1` leaf of
 * the source). Handing that borrowed node to the variable store and then
 * linking it via ->next (as await_all does to build its result list) would
 * mutate the program AST in place, cross-linking sibling statements. At
 * program exit free_ast() would then walk the corrupted tree and free nodes
 * twice (SIGSEGV). To stay sound we hand back a *fully owned, independent*
 * deep clone of the value, with its top-level ->next detached, so the caller
 * may freely relink and the engine may free it without touching the AST.
 * ------------------------------------------------------------------------ */

/* Deep-clone a value subtree, following child ->next chains (e.g. list items)
 * but NOT the node's own sibling chain unless `with_next` is set. */
static ASTNode *evloop_clone_rec(ASTNode *n, int with_next) {
    if (!n) return NULL;
    ASTNode *c = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!c) return NULL;
    c->type         = n->type ? strdup(n->type) : NULL;
    c->kind         = n->kind;
    c->id           = n->id ? strdup(n->id) : NULL;
    c->value        = n->value;
    c->str_value    = n->str_value ? strdup(n->str_value) : NULL;
    c->str_interned = 0;
    c->id_interned  = 0;
    c->from_pool    = 0;
    c->line         = n->line;
    c->bc           = BC_NOT_COMPILABLE;
    /* caches must not be carried over (stale pointers into the source AST) */
    c->cached_var      = NULL;
    c->cached_class    = NULL;
    c->cached_attr_idx = 0;
    c->col_cache       = NULL;
    c->left  = evloop_clone_rec(n->left,  1); /* children: full chains */
    c->right = evloop_clone_rec(n->right, 1);
    c->extra = evloop_clone_rec(n->extra, 1);
    c->next  = with_next ? evloop_clone_rec(n->next, 1) : NULL;
    return c;
}

/* Produce an owned copy of a task result value (top-level ->next detached). */
static ASTNode *evloop_clone_value(ASTNode *r) {
    if (!r) return create_ast_leaf("STRING", 0, "", NULL);
    ASTNode *c = evloop_clone_rec(r, 0);
    return c ? c : create_ast_leaf("STRING", 0, "", NULL);
}

static ASTNode *fiber_take_result(int id) {
    ASTNode *r = NULL;
    if (fiber_valid(id)) {
        Fiber *f = &g_fibers[id];
        r = f->result ? evloop_clone_value(f->result) : NULL;
#if defined(_WIN32)
        if (f->os_fiber) { DeleteFiber(f->os_fiber); f->os_fiber = NULL; }
#else
        if (f->stack) { free(f->stack); f->stack = NULL; }
#endif
        ctx_free(&f->ctx);     /* release the fiber's parked scope copy */
        f->in_use  = 0;
        f->state   = FB_FREE;
        f->result  = NULL;
        f->lambda  = NULL;
        f->started = 0;
    }
    if (!r) r = create_ast_leaf("STRING", 0, "", NULL);
    return r;
}

/* ---- Stage 3: blocking-I/O thread pool ----------------------------------- */
/* A small pool of OS threads runs the blocking parts of I/O so the scheduler
 * thread never stalls: a fiber that starts I/O parks in FB_IO_WAIT and yields,
 * the loop keeps running other ready fibers, and when a worker finishes it
 * publishes the result and wakes the loop. Workers touch ONLY their IoJob
 * (pure C I/O), never the interpreter — so there is no shared mutable engine
 * state and no lock around the interpreter is needed. */

#define TE_IO_WORKERS 4

static pthread_mutex_t g_io_mu  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_io_cv  = PTHREAD_COND_INITIALIZER;  /* job available */
static pthread_cond_t  g_io_done_cv = PTHREAD_COND_INITIALIZER; /* job finished */
static IoJob          *g_io_q_head = NULL;   /* FIFO of pending jobs */
static IoJob          *g_io_q_tail = NULL;
static int             g_io_pending = 0;     /* jobs submitted but not yet done */
static int             g_io_pool_started = 0;

/* Perform the actual blocking work for a job (runs on a worker thread). */
static void io_execute(IoJob *job) {
    job->ok = 0;
    job->out_buf = NULL;
    job->out_len = 0;
    if (job->kind == IO_KIND_READ_FILE && job->in_str) {
        FILE *fp = fopen(job->in_str, "rb");
        if (fp) {
            if (fseek(fp, 0, SEEK_END) == 0) {
                long sz = ftell(fp);
                if (sz >= 0) {
                    rewind(fp);
                    char *buf = (char *)malloc((size_t)sz + 1);
                    if (buf) {
                        size_t got = fread(buf, 1, (size_t)sz, fp);
                        buf[got] = '\0';
                        job->out_buf = buf;
                        job->out_len = (long)got;
                        job->ok = 1;
                    }
                }
            }
            fclose(fp);
        }
    }
}

static void *io_worker_main(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_io_mu);
        while (g_io_q_head == NULL) pthread_cond_wait(&g_io_cv, &g_io_mu);
        IoJob *job = g_io_q_head;
        g_io_q_head = job->qnext;
        if (g_io_q_head == NULL) g_io_q_tail = NULL;
        pthread_mutex_unlock(&g_io_mu);

        io_execute(job);                 /* pure I/O, no locks held */

        pthread_mutex_lock(&g_io_mu);
        job->done = 1;
        pthread_cond_signal(&g_io_done_cv);  /* wake the scheduler */
        pthread_mutex_unlock(&g_io_mu);
    }
    return NULL;
}

static void io_pool_ensure(void) {
    if (g_io_pool_started) return;
    g_io_pool_started = 1;
    for (int i = 0; i < TE_IO_WORKERS; i++) {
        pthread_t th;
        if (pthread_create(&th, NULL, io_worker_main, NULL) == 0)
            pthread_detach(th);
    }
}

/* Submit a job (main thread). The job is owned by the caller fiber until it
 * collects the result. */
static void io_submit(IoJob *job) {
    io_pool_ensure();
    pthread_mutex_lock(&g_io_mu);
    job->qnext = NULL;
    job->done  = 0;
    if (g_io_q_tail) g_io_q_tail->qnext = job;
    else             g_io_q_head = job;
    g_io_q_tail = job;
    g_io_pending++;
    pthread_cond_signal(&g_io_cv);
    pthread_mutex_unlock(&g_io_mu);
}

/* True if any in-use fiber is parked on I/O. */
static int io_any_waiting(void) {
    for (int i = 0; i < TE_FIBER_MAX; i++)
        if (g_fibers[i].in_use && g_fibers[i].state == FB_IO_WAIT) return 1;
    return 0;
}

/* Wait (on the scheduler thread) until at least one outstanding I/O job is done
 * or `timeout_ms` elapses (timeout_ms < 0 = wait indefinitely). Then promote
 * every IO_WAIT fiber whose job finished to FB_READY. */
static void io_wait_and_collect(int timeout_ms) {
    pthread_mutex_lock(&g_io_mu);
    /* Only block if work is outstanding AND nothing has finished yet. A worker
     * that completed before we reached pthread_cond_wait would otherwise make
     * us miss its signal and hang forever (lost wakeup). job->done is published
     * by the worker under g_io_mu, which we hold here. */
    int any_done = 0;
    for (int i = 0; i < TE_FIBER_MAX; i++) {
        Fiber *f = &g_fibers[i];
        if (f->in_use && f->state == FB_IO_WAIT && f->io_job && f->io_job->done) {
            any_done = 1; break;
        }
    }
    if (g_io_pending > 0 && !any_done) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&g_io_done_cv, &g_io_mu);
        } else {
            struct timespec ts;
#if defined(_WIN32)
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            ts = now;
#else
            clock_gettime(CLOCK_REALTIME, &ts);
#endif
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&g_io_done_cv, &g_io_mu, &ts);
        }
    }
    /* Promote finished jobs' fibers to READY (state mutated on main thread). */
    for (int i = 0; i < TE_FIBER_MAX; i++) {
        Fiber *f = &g_fibers[i];
        if (f->in_use && f->state == FB_IO_WAIT && f->io_job && f->io_job->done) {
            f->state = FB_READY;
            g_io_pending--;
        }
    }
    pthread_mutex_unlock(&g_io_mu);
}

/* ---- the event loop ------------------------------------------------------ */
/* Drive ready/sleeping fibers until every id in `ids` reaches DONE. The
 * caller's interpreter context is saved on entry and restored on exit so that
 * resuming fibers (which overwrite the live scope) cannot corrupt it. */
static void evloop_run_until(const int *ids, int n) {
    int top = !g_loop_active;          /* outermost drain converts the thread */
    if (top) scheduler_ensure();
    CtxSnapshot caller;
    memset(&caller, 0, sizeof(caller));  /* ctx_save frees prior contents first */
    ctx_save(&caller);
    g_loop_active = 1;

    for (;;) {
        int all_done = 1;
        for (int k = 0; k < n; k++) {
            if (fiber_valid(ids[k]) && g_fibers[ids[k]].state != FB_DONE) {
                all_done = 0; break;
            }
        }
        if (all_done) break;

        /* 1) Run every ready fiber one step (until its next yield/finish). */
        int ran = 0;
        for (int i = 0; i < TE_FIBER_MAX; i++) {
            if (g_fibers[i].in_use && g_fibers[i].state == FB_READY) {
                fiber_resume(&g_fibers[i]);
                ran = 1;
            }
        }
        if (ran) continue;

        /* 2) Nothing ready: wait for the earliest sleeper and/or an I/O job. */
        long long now = te_now_ms();
        long long earliest = -1;
        int any_sleeping = 0;
        for (int i = 0; i < TE_FIBER_MAX; i++) {
            if (g_fibers[i].in_use && g_fibers[i].state == FB_SLEEPING) {
                any_sleeping = 1;
                if (earliest < 0 || g_fibers[i].wake_at < earliest)
                    earliest = g_fibers[i].wake_at;
            }
        }
        int any_io = io_any_waiting();
        if (!any_sleeping && !any_io) break;  /* deadlock guard: no progress */

        if (any_io) {
            /* Block on I/O completion, bounded by the next timer if any. A
             * worker finishing wakes us early; on return, finished I/O fibers
             * have been promoted to READY. */
            int budget = -1;  /* indefinite */
            if (any_sleeping) {
                long long d = earliest - now;
                budget = d > 0 ? (int)d : 0;
            }
            io_wait_and_collect(budget);
        } else {
            /* Only timers pending: sleep until the earliest one is due. */
            if (earliest > now) te_msleep((int)(earliest - now));
        }
        now = te_now_ms();
        for (int i = 0; i < TE_FIBER_MAX; i++) {
            if (g_fibers[i].in_use && g_fibers[i].state == FB_SLEEPING &&
                g_fibers[i].wake_at <= now) {
                g_fibers[i].state = FB_READY;
            }
        }
    }

    g_loop_active = 0;
    ctx_restore(&caller);
    ctx_free(&caller);   /* caller's strings are now duplicated live; drop ours */
    if (top) scheduler_release();
}

/* ---- .te builtin adapters ------------------------------------------------ */

/* go(lambda) -> int task id (-1 if the pool is full). The lambda is NOT run
 * here; it starts the first time the event loop reaches it (on await). */
static int adapt_go(ASTNode *node, ASTNode *args) {
    (void)node;
    int id = fiber_find_free();
    if (id < 0 || !args) {
        add_or_update_variable("__ret__",
            create_ast_leaf_number("INT", -1, NULL, NULL));
        return 1;
    }
    Fiber *f = &g_fibers[id];
    memset(f, 0, sizeof(*f));
    f->in_use = 1;
    f->state  = FB_READY;
    f->lambda = args;             /* AST outlives the task */
    f->result = NULL;
    f->started = 0;
    /* Seed the fiber's scope with the current (global) scope so the coroutine
     * can read variables defined before it was spawned. */
    ctx_save(&f->ctx);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", id, NULL, NULL));
    return 1;
}

/* sleep_async(ms) -> 0. Inside a fiber: register a timer and yield to the loop
 * so other tasks run while we wait. Outside a fiber: plain blocking sleep. */
static int adapt_sleep_async(ASTNode *node, ASTNode *args) {
    (void)node;
    int ms = args ? (int)evaluate_expression(args) : 0;
    if (ms < 0) ms = 0;
    if (g_current) {
        g_current->wake_at = te_now_ms() + ms;
        g_current->state   = FB_SLEEPING;
        fiber_yield();
    } else {
        te_msleep(ms);
    }
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", 0, NULL, NULL));
    return 1;
}

/* read_file_async(path) -> file contents as a string ("" on error). Inside a
 * fiber the blocking read runs on a worker thread and the fiber yields, so the
 * event loop keeps running other tasks while the disk I/O is in flight (real
 * non-blocking I/O). Outside a fiber it falls back to a synchronous read. */
static int adapt_read_file_async(ASTNode *node, ASTNode *args) {
    (void)node;
    char *path = args ? get_node_string(args) : NULL;

    if (g_current && path) {
        IoJob *job = (IoJob *)calloc(1, sizeof(IoJob));
        if (job) {
            job->kind     = IO_KIND_READ_FILE;
            job->in_str   = path;          /* ownership transferred to job */
            job->fiber_id = (int)(g_current - g_fibers);
            g_current->io_job = job;
            g_current->state  = FB_IO_WAIT;
            io_submit(job);
            fiber_yield();                 /* loop runs others until job->done */
            /* resumed: worker finished, result is published */
            const char *body = (job->ok && job->out_buf) ? job->out_buf : "";
            add_or_update_variable("__ret__",
                create_ast_leaf("STRING", 0, (char *)body, NULL));
            g_current->io_job = NULL;
            free(job->in_str);
            free(job->out_buf);
            free(job);
            return 1;
        }
    }

    /* Fallback: synchronous read (not inside a fiber, or allocation failed). */
    char *body = NULL;
    if (path) {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            if (fseek(fp, 0, SEEK_END) == 0) {
                long sz = ftell(fp);
                if (sz >= 0) {
                    rewind(fp);
                    body = (char *)malloc((size_t)sz + 1);
                    if (body) {
                        size_t got = fread(body, 1, (size_t)sz, fp);
                        body[got] = '\0';
                    }
                }
            }
            fclose(fp);
        }
    }
    add_or_update_variable("__ret__",
        create_ast_leaf("STRING", 0, body ? body : "", NULL));
    free(body);
    free(path);
    return 1;
}

/* await_async(task) -> the task's result value. Drives the loop until done. */
static int adapt_await_async(ASTNode *node, ASTNode *args) {
    (void)node;
    int id = args ? (int)evaluate_expression(args) : -1;
    if (!fiber_valid(id)) {
        add_or_update_variable("__ret__", create_ast_leaf("STRING", 0, "", NULL));
        return 1;
    }
    int ids[1] = { id };
    evloop_run_until(ids, 1);
    add_or_update_variable("__ret__", fiber_take_result(id));
    return 1;
}

/* Collect task ids from a literal/variable LIST or a variadic id list. */
static int evloop_collect_ids(ASTNode *args, int *out, int max) {
    int n = 0;
    if (!args) return 0;
    ASTNode *items = NULL;

    if (args->type && strcmp(args->type, "LIST") == 0) {
        items = args->left;
    } else if (args->type &&
               (strcmp(args->type, "IDENTIFIER") == 0 ||
                strcmp(args->type, "ID") == 0) &&
               args->next == NULL) {
        Variable *v = find_variable(args->id);
        if (v && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *root = (ASTNode *)(intptr_t)v->value.object_value;
            if (root) items = root->left;
        }
    }
    if (items) {
        for (ASTNode *it = items; it && n < max; it = it->next)
            out[n++] = (int)evaluate_expression(it);
        return n;
    }
    for (ASTNode *a = args; a && n < max; a = a->next)
        out[n++] = (int)evaluate_expression(a);
    return n;
}

/* await_async_all(...) -> list of results, run concurrently. */
static int adapt_await_async_all(ASTNode *node, ASTNode *args) {
    (void)node;
    int ids[TE_FIBER_MAX];
    int n = evloop_collect_ids(args, ids, TE_FIBER_MAX);
    evloop_run_until(ids, n);

    ASTNode *head = NULL, *tail = NULL;
    for (int k = 0; k < n; k++) {
        ASTNode *r = fiber_take_result(ids[k]);
        r->next = NULL;
        if (!head) head = tail = r;
        else { tail->next = r; tail = r; }
    }
    add_or_update_variable("__ret__", create_list_node(head));
    return 1;
}

void te_register_evloop_builtins(void) {
    te_builtin_register("go",              adapt_go);
    te_builtin_register("sleep_async",     adapt_sleep_async);
    te_builtin_register("await_async",     adapt_await_async);
    te_builtin_register("await_async_all", adapt_await_async_all);
    te_builtin_register("read_file_async", adapt_read_file_async);
}
