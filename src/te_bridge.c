/* te_bridge.c — TypeEasy subprocess language bridge.
 *
 * See te_bridge.h for the public surface and protocol description.
 *
 * Design:
 *  - A fixed pool of subprocess slots (TE_PROC_MAX). lang_spawn() finds a
 *    free slot, launches the command with its stdin/stdout wired to pipes,
 *    and returns the slot index. The .te side uses that int as a handle.
 *  - The framing is line-based: one '\n'-terminated message each way.
 *    Reads are done one byte at a time so we never consume bytes belonging
 *    to a later message (pipes are unbuffered on our side — correctness over
 *    micro-throughput; bridge calls are coarse-grained by nature).
 *  - Cross-platform: POSIX uses pipe()+fork()+execl("/bin/sh","-c",cmd);
 *    Windows uses CreatePipe()+CreateProcessA(cmd). Both inherit our stderr
 *    so the child's diagnostics surface in the host console.
 */

#define _GNU_SOURCE
#include "te_bridge.h"
#include "ast.h"
#include "te_builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <signal.h>
  #include <errno.h>
  #include <poll.h>
  #include <fcntl.h>
#endif

/* ---- Host helpers from ast.c (declared in ast.h) ---- */
/* create_ast_leaf, create_ast_leaf_number, add_or_update_variable,
 * get_node_string, evaluate_expression are all in ast.h. */

#define TE_PROC_MAX 32

typedef struct {
    int in_use;
#if defined(_WIN32)
    HANDLE hProcess;
    HANDLE hStdinWr;   /* we write -> child stdin  */
    HANDLE hStdoutRd;  /* child stdout -> we read  */
#else
    pid_t  pid;
    int    in_fd;      /* we write -> child stdin  */
    int    out_fd;     /* child stdout -> we read  */
#endif
    /* Partial-line accumulator for non-blocking poll (te_bridge_poll_line).
     * Bytes read before a full '\n' arrives are buffered here across calls. */
    char  *rbuf;
    size_t rlen;
    size_t rcap;
    int    eof;
} TeProc;

static TeProc g_procs[TE_PROC_MAX];

static int te_proc_find_free(void) {
    for (int i = 0; i < TE_PROC_MAX; i++) {
        if (!g_procs[i].in_use) return i;
    }
    return -1;
}

static int te_proc_valid(int slot) {
    return slot >= 0 && slot < TE_PROC_MAX && g_procs[slot].in_use;
}

/* ============================================================
 * Spawn
 * Returns the slot index (>=0) on success, -1 on failure.
 * ============================================================ */
static int te_proc_spawn(const char *cmdline) {
    if (!cmdline || !*cmdline) return -1;
    int slot = te_proc_find_free();
    if (slot < 0) return -1;
    TeProc *p = &g_procs[slot];

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE childStdinRd = NULL, childStdinWr = NULL;
    HANDLE childStdoutRd = NULL, childStdoutWr = NULL;

    if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0)) return -1;
    /* Our write end must NOT be inherited by the child. */
    SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0)) {
        CloseHandle(childStdinRd); CloseHandle(childStdinWr);
        return -1;
    }
    /* Our read end must NOT be inherited by the child. */
    SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinRd;
    si.hStdOutput = childStdoutWr;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    /* CreateProcessA requires a mutable command-line buffer. */
    char *cmdbuf = strdup(cmdline);
    if (!cmdbuf) {
        CloseHandle(childStdinRd); CloseHandle(childStdinWr);
        CloseHandle(childStdoutRd); CloseHandle(childStdoutWr);
        return -1;
    }

    BOOL ok = CreateProcessA(NULL, cmdbuf, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdbuf);

    /* Parent no longer needs the child-side handle ends. */
    CloseHandle(childStdinRd);
    CloseHandle(childStdoutWr);

    if (!ok) {
        CloseHandle(childStdinWr);
        CloseHandle(childStdoutRd);
        return -1;
    }
    CloseHandle(pi.hThread);

    p->hProcess  = pi.hProcess;
    p->hStdinWr  = childStdinWr;
    p->hStdoutRd = childStdoutRd;
    p->in_use    = 1;
    return slot;

#else
    int inpipe[2]  = {-1, -1}; /* parent writes inpipe[1]  -> child reads inpipe[0] (stdin)  */
    int outpipe[2] = {-1, -1}; /* child writes outpipe[1]  -> parent reads outpipe[0] (stdout) */
    if (pipe(inpipe) != 0) return -1;
    if (pipe(outpipe) != 0) {
        close(inpipe[0]); close(inpipe[1]);
        return -1;
    }

    /* Mark the parent-retained pipe ends close-on-exec so that workers spawned
     * LATER (each via fork+exec) do not inherit — and thereby keep alive — this
     * worker's stdin/stdout. Without this, closing one worker's stdin would not
     * deliver EOF (a sibling worker still holds a copy of the write end), so
     * lang_close() would block ~2s per worker waiting for an exit that never
     * comes, fully serialising concurrent --api requests. The child of THIS
     * fork still gets correct stdin/stdout because dup2() clears close-on-exec
     * on the duplicated fd 0/1. */
    fcntl(inpipe[1],  F_SETFD, FD_CLOEXEC); /* parent write end (-> p->in_fd)  */
    fcntl(outpipe[0], F_SETFD, FD_CLOEXEC); /* parent read end  (-> p->out_fd) */

    pid_t pid = fork();
    if (pid < 0) {
        close(inpipe[0]); close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child: wire stdin/stdout, keep stderr. */
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]);  close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        execl("/bin/sh", "sh", "-c", cmdline, (char*)NULL);
        _exit(127); /* exec failed */
    }

    /* Parent: close the child-side ends. */
    close(inpipe[0]);
    close(outpipe[1]);

    p->pid    = pid;
    p->in_fd  = inpipe[1];
    p->out_fd = outpipe[0];
    p->in_use = 1;
    return slot;
#endif
}

/* ============================================================
 * Low-level write / read-line
 * ============================================================ */

/* Write `len` bytes followed by '\n'. Returns 0 on success, -1 on error. */
static int te_proc_write_line(int slot, const char *data, size_t len) {
    if (!te_proc_valid(slot)) return -1;
    TeProc *p = &g_procs[slot];
#if defined(_WIN32)
    DWORD wrote = 0;
    if (len > 0 && !WriteFile(p->hStdinWr, data, (DWORD)len, &wrote, NULL)) return -1;
    char nl = '\n';
    if (!WriteFile(p->hStdinWr, &nl, 1, &wrote, NULL)) return -1;
    FlushFileBuffers(p->hStdinWr);
    return 0;
#else
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(p->in_fd, data + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)n;
    }
    for (;;) {
        ssize_t n = write(p->in_fd, "\n", 1);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 1) break;
    }
    return 0;
#endif
}

/* Read one '\n'-terminated line (newline stripped). Returns a malloc'd
 * string the caller must free, or NULL on EOF/error. Empty line -> "". */
static char *te_proc_read_line(int slot) {
    if (!te_proc_valid(slot)) return NULL;
    TeProc *p = &g_procs[slot];
    size_t cap = 256, len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;

    for (;;) {
        char c;
#if defined(_WIN32)
        DWORD got = 0;
        BOOL ok = ReadFile(p->hStdoutRd, &c, 1, &got, NULL);
        if (!ok || got == 0) { /* EOF / pipe closed */
            if (len == 0) { free(buf); return NULL; }
            break;
        }
#else
        /* Timeout guard: en --api el intérprete corre single-flight; un read()
         * bloqueante indefinido congela el servidor igual que el bug MySQL.
         * poll() con 30 s garantiza que el hilo devuelve el control aunque
         * el proceso hijo se cuelgue sin cerrar el pipe. */
        {
            struct pollfd pfd; pfd.fd = p->out_fd; pfd.events = POLLIN; pfd.revents = 0;
            int pr = poll(&pfd, 1, 30000); /* 30 s */
            if (pr < 0 && errno == EINTR) continue;
            if (pr <= 0) { /* timeout o error de poll */
                if (len == 0) { free(buf); return NULL; }
                break;
            }
        }
        ssize_t got = read(p->out_fd, &c, 1);
        if (got < 0) { if (errno == EINTR) continue; }
        if (got <= 0) { /* EOF / error */
            if (len == 0) { free(buf); return NULL; }
            break;
        }
#endif
        if (c == '\n') break;
        if (c == '\r') continue; /* tolerate CRLF from Windows children */
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return buf;
}

/* ============================================================
 * Close
 * ============================================================ */
static void te_proc_close(int slot) {
    if (!te_proc_valid(slot)) return;
    TeProc *p = &g_procs[slot];
#if defined(_WIN32)
    if (p->hStdinWr)  CloseHandle(p->hStdinWr);   /* EOF to child stdin */
    if (p->hStdoutRd) CloseHandle(p->hStdoutRd);
    if (p->hProcess) {
        /* Give it a moment to exit on its own after stdin EOF, then kill. */
        if (WaitForSingleObject(p->hProcess, 2000) == WAIT_TIMEOUT) {
            TerminateProcess(p->hProcess, 0);
        }
        CloseHandle(p->hProcess);
    }
#else
    if (p->in_fd  >= 0) close(p->in_fd);  /* EOF to child stdin */
    if (p->out_fd >= 0) close(p->out_fd);
    if (p->pid > 0) {
        int status;
        /* Reap without blocking forever: try once, then signal. */
        for (int i = 0; i < 20; i++) {
            pid_t r = waitpid(p->pid, &status, WNOHANG);
            if (r == p->pid || r < 0) { p->pid = -1; break; }
            usleep(100 * 1000); /* 100ms */
        }
        if (p->pid > 0) {
            kill(p->pid, SIGTERM);
            waitpid(p->pid, &status, 0);
        }
    }
#endif
    if (p->rbuf) free(p->rbuf);
    memset(p, 0, sizeof(*p));
}

/* ============================================================
 * Public API for the async event loop (te_async.c)
 *
 * te_bridge_write_line: write a request without reading the reply
 *   (non-blocking handoff). Returns 0 on success, -1 on error.
 * te_bridge_poll_line: non-blocking attempt to extract one full line
 *   from the child's stdout. Returns:
 *     1  -> *out set to a malloc'd line (caller frees), newline stripped
 *     0  -> no complete line yet (try again later)
 *    -1  -> EOF/error and nothing left to deliver
 * It never blocks: it only reads bytes that are already available.
 * ============================================================ */
int te_bridge_write_line(int slot, const char *data, size_t len) {
    return te_proc_write_line(slot, data, len);
}

static void te_rbuf_push(TeProc *p, char c) {
    if (p->rlen + 1 >= p->rcap) {
        size_t nc = p->rcap ? p->rcap * 2 : 256;
        char *nb = (char*)realloc(p->rbuf, nc);
        if (!nb) return; /* drop byte on OOM; extremely unlikely */
        p->rbuf = nb; p->rcap = nc;
    }
    p->rbuf[p->rlen++] = c;
}

/* Extract a completed line (up to first '\n') from p->rbuf into *out. */
static int te_rbuf_take_line(TeProc *p, char **out) {
    for (size_t i = 0; i < p->rlen; i++) {
        if (p->rbuf[i] == '\n') {
            size_t linelen = i;
            /* strip trailing '\r' */
            if (linelen > 0 && p->rbuf[linelen - 1] == '\r') linelen--;
            char *s = (char*)malloc(linelen + 1);
            if (!s) return 0;
            memcpy(s, p->rbuf, linelen);
            s[linelen] = '\0';
            /* shift remainder down */
            size_t rest = p->rlen - (i + 1);
            memmove(p->rbuf, p->rbuf + i + 1, rest);
            p->rlen = rest;
            *out = s;
            return 1;
        }
    }
    return 0;
}

int te_bridge_poll_line(int slot, char **out) {
    if (!out) return -1;
    *out = NULL;
    if (!te_proc_valid(slot)) return -1;
    TeProc *p = &g_procs[slot];

    /* Already have a buffered line? */
    if (te_rbuf_take_line(p, out)) return 1;

    /* Pull whatever bytes are available right now, without blocking. */
#if defined(_WIN32)
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(p->hStdoutRd, NULL, 0, NULL, &avail, NULL)) {
            p->eof = 1; break;
        }
        if (avail == 0) break;
        char c;
        DWORD got = 0;
        if (!ReadFile(p->hStdoutRd, &c, 1, &got, NULL) || got == 0) {
            p->eof = 1; break;
        }
        te_rbuf_push(p, c);
        if (c == '\n') break;
    }
#else
    for (;;) {
        struct pollfd pfd;
        pfd.fd = p->out_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 0); /* 0ms timeout: non-blocking */
        if (pr <= 0) break;        /* nothing readable right now */
        if (pfd.revents & (POLLERR | POLLNVAL)) { p->eof = 1; break; }
        char c;
        ssize_t got = read(p->out_fd, &c, 1);
        if (got < 0) { if (errno == EINTR) continue; break; }
        if (got == 0) { p->eof = 1; break; } /* EOF */
        te_rbuf_push(p, c);
        if (c == '\n') break;
    }
#endif

    if (te_rbuf_take_line(p, out)) return 1;
    if (p->eof) {
        if (p->rlen > 0) {
            /* deliver final unterminated line */
            char *s = (char*)malloc(p->rlen + 1);
            if (s) { memcpy(s, p->rbuf, p->rlen); s[p->rlen] = '\0'; p->rlen = 0; *out = s; return 1; }
        }
        return -1;
    }
    return 0;
}

/* Block until one of the given slots' stdout is readable, or timeout_ms passes.
 * See te_bridge.h. Touches only per-slot fds/buffers (owner-private), so it is
 * safe to run with the global interpreter lock released. */
int te_bridge_wait_readable(const int *slots, int n, int timeout_ms) {
    if (!slots || n <= 0) return 0;

    /* If any slot already holds a complete buffered line, don't block at all
     * (the caller can consume it immediately on the next step). */
    int has_buffered = 0;
    for (int i = 0; i < n; i++) {
        int s = slots[i];
        if (!te_proc_valid(s)) continue;
        TeProc *p = &g_procs[s];
        for (size_t j = 0; j < p->rlen; j++) {
            if (p->rbuf[j] == '\n') { has_buffered = 1; break; }
        }
        if (has_buffered) break;
    }

#if defined(_WIN32)
    /* Windows anonymous pipes have no poll(); keep the caller's existing
     * 1ms-sleep behaviour by reporting "nothing waited" (return 0). The
     * Windows path already overlaps correctly via CRITICAL_SECTION. */
    (void)timeout_ms; (void)has_buffered;
    return 0;
#else
    struct pollfd pfds[TE_PROC_MAX];
    int valid = 0;
    for (int i = 0; i < n && valid < TE_PROC_MAX; i++) {
        int s = slots[i];
        if (!te_proc_valid(s)) continue;
        pfds[valid].fd      = g_procs[s].out_fd;
        pfds[valid].events  = POLLIN;
        pfds[valid].revents = 0;
        valid++;
    }
    if (valid == 0) return 0;
    int t = has_buffered ? 0 : timeout_ms;
    int pr = poll(pfds, (nfds_t)valid, t);
    (void)pr; /* readiness is re-checked by te_bridge_poll_line under the lock */
    return valid;
#endif
}

/* ============================================================
 * .te builtin adapters
 *
 * Follow the established adapter pattern: resolve args with
 * get_node_string()/evaluate_expression() (NOT evaluate_native_args,
 * which permanently mutates AST nodes), then set __ret__ via
 * add_or_update_variable. See the date_* adapters in te_stdlib.c.
 * ============================================================ */

/* lang_spawn(cmdline) -> int slot (>=0) or -1 */
static int adapt_lang_spawn(ASTNode *node, ASTNode *args) {
    (void)node;
    char *cmd = args ? get_node_string(args) : NULL;
    int slot = te_proc_spawn(cmd ? cmd : "");
    if (cmd) free(cmd);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", slot, NULL, NULL));
    return 1;
}

/* lang_call(slot, request) -> string response ("" on error) */
static int adapt_lang_call(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = args ? (int)evaluate_expression(args) : -1;
    ASTNode *a1 = args ? args->next : NULL; /* gotcha #1: 2nd arg via ->next */
    char *req = a1 ? get_node_string(a1) : NULL;
    char *resp = NULL;
    if (te_proc_write_line(slot, req ? req : "", req ? strlen(req) : 0) == 0) {
        resp = te_proc_read_line(slot);
    }
    if (req) free(req);
    add_or_update_variable("__ret__",
        create_ast_leaf("STRING", 0, resp ? resp : "", NULL));
    if (resp) free(resp);
    return 1;
}

/* lang_send(slot, line) -> int 1/0 (write only) */
static int adapt_lang_send(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = args ? (int)evaluate_expression(args) : -1;
    ASTNode *a1 = args ? args->next : NULL;
    char *line = a1 ? get_node_string(a1) : NULL;
    int ok = te_proc_write_line(slot, line ? line : "", line ? strlen(line) : 0) == 0;
    if (line) free(line);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", ok ? 1 : 0, NULL, NULL));
    return 1;
}

/* lang_recv(slot) -> string (read one line, "" on EOF/error) */
static int adapt_lang_recv(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = args ? (int)evaluate_expression(args) : -1;
    char *resp = te_proc_read_line(slot);
    add_or_update_variable("__ret__",
        create_ast_leaf("STRING", 0, resp ? resp : "", NULL));
    if (resp) free(resp);
    return 1;
}

/* lang_close(slot) -> int 0 */
static int adapt_lang_close(ASTNode *node, ASTNode *args) {
    (void)node;
    int slot = args ? (int)evaluate_expression(args) : -1;
    te_proc_close(slot);
    add_or_update_variable("__ret__",
        create_ast_leaf_number("INT", 0, NULL, NULL));
    return 1;
}

void te_register_bridge_builtins(void) {
    te_builtin_register("lang_spawn", adapt_lang_spawn);
    te_builtin_register("lang_call",  adapt_lang_call);
    te_builtin_register("lang_send",  adapt_lang_send);
    te_builtin_register("lang_recv",  adapt_lang_recv);
    te_builtin_register("lang_close", adapt_lang_close);
    /* Generic "process" aliases. */
    te_builtin_register("proc_spawn", adapt_lang_spawn);
    te_builtin_register("proc_call",  adapt_lang_call);
    te_builtin_register("proc_send",  adapt_lang_send);
    te_builtin_register("proc_recv",  adapt_lang_recv);
    te_builtin_register("proc_close", adapt_lang_close);
}
