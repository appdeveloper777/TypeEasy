/* te_async.c — TypeEasy cooperative async runtime.
 *
 * See te_async.h for the public surface.
 *
 * Design (intentionally simple and allocation-light):
 *  - A fixed pool of Task slots. A task is a small state machine, NOT an OS
 *    thread: everything runs on the single interpreter thread, cooperatively.
 *  - Two task kinds:
 *      SPAWN: holds a TypeEasy lambda, executed (to completion) the first
 *             time the scheduler reaches it. Useful to defer/orchestrate work.
 *      IO   : created by lang_call_async(). The request line is written to the
 *             child immediately; the reply is collected later via the bridge's
 *             non-blocking te_bridge_poll_line(). This is what gives real
 *             overlap: many children compute while we poll all of them.
 *  - await_task / await_all run the event loop (`run_until`) until the target
 *    task(s) reach DONE, polling IO tasks each pass and sleeping ~1ms only when
 *    no task made progress (so we never spin the CPU at 100%).
 *
 * Task handles are plain ints (the slot index) so they round-trip through
 * TypeEasy variables as ordinary integers — no special value type needed.
 */

#include "te_async.h"
#include "te_bridge.h"
#include "ast.h"
#include "te_builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
  #include <windows.h>
  static void te_async_msleep(int ms) { Sleep((DWORD)ms); }
#else
  #include <unistd.h>
  static void te_async_msleep(int ms) { usleep((useconds_t)ms * 1000); }
#endif

#define TE_TASK_MAX 256

enum { TS_FREE = 0, TS_SPAWN_PENDING, TS_IO_WAIT, TS_RUNNING, TS_DONE };
enum { TK_SPAWN = 1, TK_IO = 2 };

typedef struct {
    int      in_use;
    int      state;
    int      kind;
    ASTNode *lambda;   /* TK_SPAWN: deferred lambda (owned by the AST, not us) */
    int      slot;     /* TK_IO: bridge slot */
    ASTNode *result;   /* DONE: result value (ownership handed to __ret__) */
} TeTask;

static TeTask g_tasks[TE_TASK_MAX];

static int te_task_find_free(void) {
    for (int i = 0; i < TE_TASK_MAX; i++)
        if (!g_tasks[i].in_use) return i;
    return -1;
}

static int te_task_valid(int id) {
    return id >= 0 && id < TE_TASK_MAX && g_tasks[id].in_use;
}

/* ============================================================
 * Scheduler
 * ============================================================ */

/* One pass over every live task. Returns 1 if any task made progress. */
static int te_async_step(void) {
    int progress = 0;
    for (int i = 0; i < TE_TASK_MAX; i++) {
        TeTask *t = &g_tasks[i];
        if (!t->in_use) continue;

        if (t->state == TS_SPAWN_PENDING) {
            /* Guard against re-entrancy: mark RUNNING before calling back into
             * the interpreter (the lambda may itself await other tasks). */
            t->state = TS_RUNNING;
            ASTNode *r = t->lambda ? call_lambda(t->lambda, NULL) : NULL;
            t->result = r; /* may be NULL */
            t->state = TS_DONE;
            progress = 1;
        } else if (t->state == TS_IO_WAIT) {
            char *line = NULL;
            int rr = te_bridge_poll_line(t->slot, &line);
            if (rr == 1) {
                t->result = create_ast_leaf("STRING", 0, line ? line : "", NULL);
                if (line) free(line);
                t->state = TS_DONE;
                progress = 1;
            } else if (rr < 0) {
                /* EOF/error: complete with an empty string rather than hang. */
                t->result = create_ast_leaf("STRING", 0, "", NULL);
                t->state = TS_DONE;
                progress = 1;
            }
            /* rr == 0: no full line yet — keep waiting. */
        }
    }
    return progress;
}

/* Drive the loop until every task id in `ids` is DONE. */
static void te_async_run_until(const int *ids, int n) {
    for (;;) {
        int all_done = 1;
        for (int k = 0; k < n; k++) {
            int id = ids[k];
            if (te_task_valid(id) && g_tasks[id].state != TS_DONE) {
                all_done = 0;
                break;
            }
        }
        if (all_done) return;

        int progressed = te_async_step();
        if (!progressed) te_async_msleep(1); /* yield: avoid busy spin on IO */
    }
}

/* Detach a finished task, returning its result node (ownership transferred to
 * the caller, typically straight into __ret__). */
static ASTNode *te_task_take_result(int id) {
    ASTNode *r = NULL;
    if (te_task_valid(id)) {
        r = g_tasks[id].result;
        g_tasks[id].in_use = 0;
        g_tasks[id].state  = TS_FREE;
        g_tasks[id].result = NULL;
        g_tasks[id].lambda = NULL;
    }
    if (!r) r = create_ast_leaf("STRING", 0, "", NULL);
    return r;
}

/* ============================================================
 * .te builtin adapters
 * ============================================================ */

/* spawn(lambda) -> int task id (-1 if the pool is full). The lambda is NOT
 * evaluated here; it runs when the scheduler first reaches the task. */
static int adapt_spawn(ASTNode *node, ASTNode *args) {
    (void)node;
    int id = te_task_find_free();
    if (id < 0 || !args) {
        add_or_update_variable("__ret__",
            create_ast_leaf_number("INT", -1, NULL, NULL));
        return 1;
    }
    g_tasks[id].in_use = 1;
    g_tasks[id].state  = TS_SPAWN_PENDING;
    g_tasks[id].kind   = TK_SPAWN;
    g_tasks[id].lambda = args; /* raw lambda node; AST outlives the task */
    g_tasks[id].slot   = -1;
    g_tasks[id].result = NULL;
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", id, NULL, NULL));
    return 1;
}

/* lang_call_async(slot, request) -> int task id. Writes the request line to
 * the child immediately, then defers reading the reply to the event loop. */
static int adapt_lang_call_async(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = args ? (int)evaluate_expression(args) : -1;
    ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
    char *req = a1 ? get_node_string(a1) : NULL;

    int id = te_task_find_free();
    if (id < 0) {
        if (req) free(req);
        add_or_update_variable("__ret__",
            create_ast_leaf_number("INT", -1, NULL, NULL));
        return 1;
    }
    /* Hand the request off now; the reply is harvested non-blockingly later. */
    te_bridge_write_line(slot, req ? req : "", req ? strlen(req) : 0);
    if (req) free(req);

    g_tasks[id].in_use = 1;
    g_tasks[id].state  = TS_IO_WAIT;
    g_tasks[id].kind   = TK_IO;
    g_tasks[id].slot   = slot;
    g_tasks[id].lambda = NULL;
    g_tasks[id].result = NULL;
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", id, NULL, NULL));
    return 1;
}

/* await_task(task) / await(task) -> the task's result value. */
static int adapt_await_task(ASTNode *node, ASTNode *args) {
    (void)node;
    int id = args ? (int)evaluate_expression(args) : -1;
    if (!te_task_valid(id)) {
        add_or_update_variable("__ret__",
            create_ast_leaf("STRING", 0, "", NULL));
        return 1;
    }
    int ids[1] = { id };
    te_async_run_until(ids, 1);
    add_or_update_variable("__ret__", te_task_take_result(id));
    return 1;
}

/* Collect task ids from the argument list. Accepts either a single list
 * (literal `[t1,t2]` or a variable holding a LIST) or a variadic id list
 * `await_all(t1, t2, ...)`. Writes ids into `out` (cap `max`), returns count. */
static int te_async_collect_ids(ASTNode *args, int *out, int max) {
    int n = 0;
    if (!args) return 0;

    ASTNode *items = NULL; /* a chain to walk via ->next if we find a LIST */

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

    /* Variadic form: every argument is an id expression. */
    for (ASTNode *a = args; a && n < max; a = a->next)
        out[n++] = (int)evaluate_expression(a);
    return n;
}

/* await_all(...) -> list of results, one per task, run concurrently. */
static int adapt_await_all(ASTNode *node, ASTNode *args) {
    (void)node;
    int ids[TE_TASK_MAX];
    int n = te_async_collect_ids(args, ids, TE_TASK_MAX);

    te_async_run_until(ids, n);

    ASTNode *head = NULL, *tail = NULL;
    for (int k = 0; k < n; k++) {
        ASTNode *r = te_task_take_result(ids[k]);
        r->next = NULL;
        if (!head) head = tail = r;
        else { tail->next = r; tail = r; }
    }
    add_or_update_variable("__ret__", create_list_node(head));
    return 1;
}

void te_register_async_builtins(void) {
    te_builtin_register("spawn",            adapt_spawn);
    te_builtin_register("lang_call_async",  adapt_lang_call_async);
    te_builtin_register("proc_call_async",  adapt_lang_call_async);
    te_builtin_register("await_task",       adapt_await_task);
    te_builtin_register("await",            adapt_await_task);
    te_builtin_register("await_all",        adapt_await_all);
}
