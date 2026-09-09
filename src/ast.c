
#include "ast.h"
#include "te_vm.h"
/* g_vm -> macro sobre *te_vm_cur (te_vm.h / te_vm.c, Fase 3 paso B) */
#include "ast_internal.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "bytecode.h"
#include "mysql_bridge.h"
#include "postgres_bridge.h"
#include "sqlserver_bridge.h"
#include "debugger.h"
#include "te_builtins.h"
#include "te_buf.h"
#include "te_http.h"
#include "te_json.h"
#include "te_bytecode.h"
#include "te_csv.h"
#include "te_xlsx.h"
#include "te_colcache.h"
#include "te_stdlib.h"
#include "te_linq.h"
#include "te_linq_ops.h"
#include "te_math.h"
#include "te_string.h"
#include "te_list.h"
#include "te_map.h"

#ifdef TE_HAVE_OPENMP
#  include <omp.h>
#endif

/* Crypto / encoding stdlib (linked via -lssl -lcrypto en el Makefile). */
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <time.h>
#include <setjmp.h>
#include <stdarg.h>

/* ============================================================
 * Runtime fatal-error recovery.
 *
 * The interpreter historically calls exit(1) on any runtime error
 * (undefined function, assigning to a const, OOM, etc). That is fine
 * for the one-shot CLI, but in --api server mode it would tear down the
 * whole process on a single bad request.
 *
 * te_runtime_fatal() is the single choke point: if a recovery point is
 * installed (g_runtime_recovery != NULL, set by the API request handler
 * around each invoke) it longjmp's back there so the server can answer
 * HTTP 500 and keep serving. Otherwise it behaves exactly like the old
 * exit(1), so CLI semantics are unchanged.
 * ============================================================ */

/* ------------------------------------------------------------------
 * Interpreter call-depth guard (item #7: limits).
 *
 * g_call_depth tracks how many user function/method/lambda invocations are
 * currently nested on the C stack. Runaway user recursion (a function that
 * never hits a base case) would otherwise grow the native call stack without
 * bound and crash the whole process with a stack overflow (SIGSEGV) — fatal
 * even with prefork, since it kills a worker. te_depth_enter()/te_depth_leave()
 * (defined just below, after te_runtime_fatalf) bracket every invocation; past
 * g_max_call_depth te_depth_enter() raises a normal fatal runtime error: in
 * --api mode te_runtime_fatalf longjmp's back to the request recovery point
 * (HTTP 500, server keeps serving); in CLI mode it exits like any other fatal
 * error. The counter is defined here (above runtime_reset_vars_to_initial_state)
 * so the recovery path can reset it to 0 after a longjmp skips the matching
 * te_depth_leave() calls.
 * ------------------------------------------------------------------ */
/* g_max_call_depth -> g_vm.max_call_depth (lazy desde TYPEEASY_MAX_DEPTH) */

/* ------------------------------------------------------------------
 * Runtime error location capture (item 2.3).
 *
 * g_current_exec_line tracks the source line of the statement currently
 * being interpreted (cheap single int assignment per statement, no debug
 * gate). When a fatal runtime error fires we snapshot that line and the
 * formatted message so the API server can surface file:line in the HTTP
 * 500 body, but ONLY when dev mode is active. In production the server
 * keeps emitting the opaque {"error":"internal_error"}.
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * Source-file table (multi-file programs). The lexer includes imports
 * inline, so `line` alone was a cumulative counter over main.te + every
 * import (useless for the ERP: 120 files). Each file gets an id; the lexer
 * resets yylineno per file and stamps g_lex_file_id on new nodes; the
 * interpreter tracks the file of the statement being executed.
 * ------------------------------------------------------------------ */
#define TE_SRC_FILES_MAX 512
static char *g_src_files[TE_SRC_FILES_MAX];
static int   g_src_file_count = 0;
int  g_lex_file_id = 0;            /* file being lexed (0 = main file) */

int te_src_file_register(const char *path) {
    if (!path) return 0;
    for (int i = 1; i < g_src_file_count; i++)
        if (g_src_files[i] && strcmp(g_src_files[i], path) == 0) return i;
    if (g_src_file_count == 0) { g_src_files[0] = NULL; g_src_file_count = 1; }
    if (g_src_file_count >= TE_SRC_FILES_MAX) return 0;
    g_src_files[g_src_file_count] = strdup(path);
    return g_src_file_count++;
}

const char *te_src_file_name(int id) {
    extern const char *g_debug_source_file;
    if (id > 0 && id < g_src_file_count && g_src_files[id]) return g_src_files[id];
    return (g_debug_source_file && g_debug_source_file[0]) ? g_debug_source_file : "";
}

/* Names of the user fns currently executing (innermost last), for the
 * "in f <- g <- h" trailer of runtime errors. Fixed size; deeper frames
 * are simply not recorded. Reset together with g_call_depth. */
void te_callstack_push(const char *name) { if (g_vm.callstack_n < TE_CALLSTACK_MAX) g_vm.callstack[g_vm.callstack_n] = name; g_vm.callstack_n++; }
void te_callstack_pop(void) { if (g_vm.callstack_n > 0) g_vm.callstack_n--; }
void te_callstack_reset(void) { g_vm.callstack_n = 0; }

/* ------------------------------------------------------------------
 * --profile / TYPEEASY_PROFILE=1: per-named-fn wall time (inclusive and
 * self), call count. Zero cost when disabled (one int test per call).
 * Script mode: report on exit. --api: report per request that exceeds
 * TYPEEASY_PROFILE_MIN_MS (default 0 = every request) to stderr.
 * ------------------------------------------------------------------ */
int g_profile_enabled = -1;   /* -1 = not yet read from env */
typedef struct { const char *name; long long calls, incl_ns, self_ns; } TeProfEntry;
#define TE_PROF_MAX 1024
static TeProfEntry g_prof[TE_PROF_MAX];
static int g_prof_n = 0;
static long long g_prof_child_ns[TE_CALLSTACK_MAX];   /* time spent in callees, per active frame */
static long long g_prof_start_ns[TE_CALLSTACK_MAX];

static long long te_prof_now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
int te_profile_on(void) {
    if (g_profile_enabled < 0) { const char *e = getenv("TYPEEASY_PROF_FN"); g_profile_enabled = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return g_profile_enabled;
}
static TeProfEntry *te_prof_entry(const char *name) {
    for (int i = 0; i < g_prof_n; i++) if (g_prof[i].name == name || strcmp(g_prof[i].name, name) == 0) return &g_prof[i];
    if (g_prof_n >= TE_PROF_MAX) return NULL;
    g_prof[g_prof_n].name = name; g_prof[g_prof_n].calls = 0; g_prof[g_prof_n].incl_ns = 0; g_prof[g_prof_n].self_ns = 0;
    return &g_prof[g_prof_n++];
}
void te_prof_enter(void) {
    int d = g_vm.callstack_n - 1;                     /* frame just pushed */
    if (d < 0 || d >= TE_CALLSTACK_MAX) return;
    g_prof_start_ns[d] = te_prof_now_ns();
    g_prof_child_ns[d] = 0;
}
void te_prof_leave(const char *name) {
    int d = g_vm.callstack_n - 1;                     /* frame about to be popped */
    if (d < 0 || d >= TE_CALLSTACK_MAX) return;
    long long incl = te_prof_now_ns() - g_prof_start_ns[d];
    TeProfEntry *e = te_prof_entry(name);
    if (e) { e->calls++; e->incl_ns += incl; e->self_ns += incl - g_prof_child_ns[d]; }
    if (d > 0) g_prof_child_ns[d - 1] += incl;
}
static int te_prof_cmp(const void *a, const void *b) {
    const TeProfEntry *x = a, *y = b;
    return (y->self_ns > x->self_ns) - (y->self_ns < x->self_ns);
}
void te_profile_report(const char *title) {
    if (!te_profile_on() || g_prof_n == 0) return;
    qsort(g_prof, (size_t)g_prof_n, sizeof(TeProfEntry), te_prof_cmp);
    long long total_self = 0; for (int i = 0; i < g_prof_n; i++) total_self += g_prof[i].self_ns;
    fprintf(stderr, "[profile] %s  (%d fns, %.1f ms in fn bodies)\n", title ? title : "", g_prof_n, total_self / 1e6);
    fprintf(stderr, "[profile] %10s %10s %10s  %s\n", "self_ms", "incl_ms", "calls", "fn");
    int shown = g_prof_n < 25 ? g_prof_n : 25;
    for (int i = 0; i < shown; i++)
        fprintf(stderr, "[profile] %10.2f %10.2f %10lld  %s\n", g_prof[i].self_ns / 1e6, g_prof[i].incl_ns / 1e6, g_prof[i].calls, g_prof[i].name);
    fflush(stderr);
}
void te_profile_reset(void) { g_prof_n = 0; }

/* "    at file.te:12 in fnA <- fnB" (or just "    at file.te:12"). */
void te_runtime_location(char *buf, size_t cap) {
    const char *f = te_src_file_name(g_vm.current_exec_file);
    size_t n = (size_t)snprintf(buf, cap, "    at %s:%d", f[0] ? f : "<main>", g_vm.current_exec_line);
    int top = g_vm.callstack_n < TE_CALLSTACK_MAX ? g_vm.callstack_n : TE_CALLSTACK_MAX;
    for (int i = top - 1; i >= 0 && n + 4 < cap; i--) {
        n += (size_t)snprintf(buf + n, cap - n, "%s%s", i == top - 1 ? " in " : " <- ",
                              g_vm.callstack[i] ? g_vm.callstack[i] : "?");
    }
}

void te_runtime_fatal(void) {
    g_vm.runtime_error_line = g_vm.current_exec_line;
    g_vm.runtime_error_file = g_vm.current_exec_file;
    if (g_vm.runtime_recovery) longjmp(*g_vm.runtime_recovery, 1);
    exit(1);
}

/* Same as te_runtime_fatal() but also formats a human-readable message to
 * stderr (governance: user-facing errors in English on stderr) and stashes
 * it for the dev-mode HTTP 500 body. The location trailer goes to stderr
 * only; the server decides whether to expose it. */
void te_runtime_fatalf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_vm.runtime_error_msg, sizeof(g_vm.runtime_error_msg), fmt, ap);
    va_end(ap);
    char loc[512];
    te_runtime_location(loc, sizeof(loc));
    fprintf(stderr, "%s\n%s\n", g_vm.runtime_error_msg, loc);
    te_runtime_fatal();
}

/* Out-of-memory choke point. Node constructors and other allocations used to
 * `exit(1)` directly on a NULL malloc/calloc; in --api server mode that tore
 * down every in-flight request on a single OOM. Route them here instead so an
 * allocation failure during a request longjmp's back to the recovery point
 * (HTTP 500) while CLI semantics stay identical (no recovery point -> exit(1)).
 * `what` is a short tag for the failed allocation, surfaced in dev-mode 500s. */
void te_oom_fatal(const char *what) {
    te_runtime_fatalf("Fatal: out of memory allocating %s",
                      what ? what : "node");
}

/* item #7: call-depth guard helpers. te_depth_enter() is called at the top of
 * every user function/method/lambda invocation and te_depth_leave() right
 * before it returns; the matched pair keeps g_call_depth equal to the current
 * nesting of invocations on the C stack. The limit (max nested calls before we
 * abort with a clean runtime error instead of a native stack overflow) is read
 * once from TYPEEASY_MAX_DEPTH, defaulting to 250 — calibrated to fire well
 * before the ~1 MiB Windows thread stack overflows, given each interpreter call
 * level consumes several large C frames (call_lambda + evaluate_expression +
 * interpret_ast). Legitimate recursion rarely exceeds a few dozen levels. */
void te_depth_enter(void) {
    if (g_vm.max_call_depth == 0) {
        const char *e = getenv("TYPEEASY_MAX_DEPTH");
        long v = e ? strtol(e, NULL, 10) : 0;
        g_vm.max_call_depth = (v > 0) ? (int)v : 250;
    }
    if (++g_vm.call_depth > g_vm.max_call_depth) {
        --g_vm.call_depth;
        te_runtime_fatalf("Error: maximum call depth %d exceeded "
                          "(possible infinite recursion).", g_vm.max_call_depth);
    }
}

void te_depth_leave(void) {
    if (g_vm.call_depth > 0) --g_vm.call_depth;
}

/* Headers POSIX/sockets disponibles en todas las plataformas (MSYS2 / Linux / macOS).
 * En Windows usamos winsock; en POSIX, sockets BSD. */
#ifdef _WIN32
  #include <io.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifndef STDOUT_FILENO
    #define STDOUT_FILENO _fileno(stdout)
  #endif
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

/* libcurl optional: enabled with -DTE_HAVE_LIBCURL at compile time.
 * Provides HTTPS + custom headers + arbitrary methods for http_*. */
#ifdef TE_HAVE_LIBCURL
  #include <curl/curl.h>
#endif

/* Ola 10: JIT availability — must be defined BEFORE any use further down. */
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <sys/vfs.h>
#include <stddef.h>
#include <pthread.h>

/* Phase F: globals shared with test runner (typeeasy_main.c).
 * Defined here as weak so binaries that don't link the test runner
 * still satisfy references from te_builtin_dispatch. */
int g_test_failed __attribute__((weak)) = 0;
int g_test_assertions __attribute__((weak)) = 0;

/* Phase F.3: error capture for --syntax-check. typeeasy_main.c provides
 * strong defs; weak here so typeeasy_agent (which doesn't link main) builds. */
int g_capture_errors __attribute__((weak)) = 0;
void te_capture_error(int line, const char *msg, const char *near) __attribute__((weak));
void te_capture_error(int line, const char *msg, const char *near) {
    (void)line; (void)msg; (void)near;
}
#define TE_JIT_AVAILABLE 1
#define TE_HAS_MMAP 1
#define TE_HAS_PTHREAD 1
#elif defined(_WIN32)
/* MSYS2/mingw-w64 trae winpthreads y soporta clock_gettime; no hay JIT ni mmap
 * pero si pthread, asi que CSV multi-thread funciona en Windows tambien. */
#include <pthread.h>
#include <time.h>
#define TE_JIT_AVAILABLE 0
#define TE_HAS_MMAP 0
#define TE_HAS_PTHREAD 1
#else
#define TE_JIT_AVAILABLE 0
#define TE_HAS_MMAP 0
#define TE_HAS_PTHREAD 0
/* Phase F/F.3 (portado a este branch): en arm64/android (__linux__ pero NO
 * __x86_64__) el branch de arriba no aplica; sin estas defs weak,
 * te_capture_error queda declarado implicitamente y clang (Android NDK) lo
 * trata como ERROR duro. Mismo contrato weak que el branch x86: typeeasy_main.c
 * provee las defs STRONG; aqui son weak para que binarios sin main (agent) linken. */
int g_test_failed __attribute__((weak)) = 0;
int g_test_assertions __attribute__((weak)) = 0;
int g_capture_errors __attribute__((weak)) = 0;
void te_capture_error(int line, const char *msg, const char *near) __attribute__((weak));
void te_capture_error(int line, const char *msg, const char *near) {
    (void)line; (void)msg; (void)near;
}
#endif

/* Detección portable de número de CPUs (sysconf no existe en MSYS2/mingw). */
#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
static inline long te_nprocs_online(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
/* mingw no trae pread(); emulamos con ReadFile + OVERLAPPED (thread-safe,
 * no toca el file pointer global). fd es un descriptor de CRT, lo
 * convertimos a HANDLE Win32. */
#  include <sys/types.h>
static inline ssize_t te_pread(int fd, void *buf, size_t count, off_t offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
    ov.Offset     = (DWORD)((unsigned long long)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(((unsigned long long)offset >> 32) & 0xFFFFFFFFu);
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)count, &got, &ov)) {
        DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return 0;
        return -1;
    }
    return (ssize_t)got;
}
#  define pread(fd, buf, count, off) te_pread((fd), (buf), (count), (off))
#else
#  include <unistd.h>
static inline long te_nprocs_online(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? n : 1;
}
#endif

#if defined(__AVX2__)
#include <immintrin.h>
#define TE_HAS_AVX2 1
#else
#define TE_HAS_AVX2 0
#endif

/* Forward decls used in helpers below */
char* expand_interp_string(const char *raw);
int is_string_type(struct ASTNode *node);
extern int g_debug_mode;
/* Debugger: lexer line counter (from flex). Used to stamp ASTNode->line at
 * creation time so the runtime debugger can match breakpoints. */
extern int yylineno;

/* Line of the leading keyword (let/var/const/type) of the variable declaration
 * currently being parsed. The scanner (parser.l) stamps it the moment it sees
 * that keyword, so create_var_decl_node() can attribute a multi-line decl to its
 * FIRST line instead of yylineno (which, at bison reduction time, points at the
 * statement's LAST line). */
int g_decl_stmt_line = 0;

/* Phase H: forward declaration so get_node_string / native_json can recursively
 * evaluate nested CALL_FUNC arguments (e.g. json(concat(...)), concat(a, request_param("id"), b)). */
void interpret_call_func(ASTNode *node);
/* Gotcha #2: forward decl para invocar el resultado de una llamada — `make(10)(5)`. */
static void interpret_call_expr(ASTNode *node);

/* item #7: call-depth guard. Public helpers (defined after te_runtime_fatalf)
 * plus the *_impl bodies the guarded wrappers delegate to. */
void te_depth_enter(void);
void te_depth_leave(void);
static void interpret_call_func_impl(ASTNode *node);
static void interpret_call_method_impl(ASTNode *node);
static ASTNode* call_lambda_impl(ASTNode *lambda, ASTNode *argsList);

// Helper: Recursively evaluate arguments for native calls
void evaluate_native_args(ASTNode *arg) {
    /* BUG FIX (mayo 2026): este helper antes mutaba el nodo AST en sitio,
     * cambiando IDENTIFIER → STRING/NUMBER y guardando el valor resuelto en
     * curr->str_value / curr->value. Eso "horneaba" el valor del primer
     * request en el AST compartido, de forma que en --api mode (donde el
     * mismo árbol se re-ejecuta por cada request) un `let b = request_body();
     * concat(..., b, ...)` quedaba congelado al valor del primer POST aunque
     * b se redeclaraba correctamente en cada llamada.
     *
     * Los consumidores (native_concat → get_node_string, native_json, etc.)
     * ya resuelven IDENTIFIER de forma fresca leyendo find_variable() en
     * cada invocación, por lo que la pre-resolución mutante es innecesaria.
     * Dejamos la función como no-op para no romper la ABI y mantener el
     * sitio de llamada en call_native_function. */
    (void)arg;
}

// Global buffer for capturing println output

/* Ruta del script en ejecución (para mensajes de error). La asigna main(). */
const char *g_script_path = NULL;

/* Color ANSI para mensajes de error (rojo). Git Bash / terminales VT lo
 * soportan. Se desactiva automáticamente si stderr no es una terminal para
 * no ensuciar archivos/pipes con códigos de escape. */

int te_stderr_is_tty(void) {
    static int cached = -1;
    if (cached < 0) {
#ifdef _WIN32
        cached = _isatty(_fileno(stderr)) ? 1 : 0;
#else
        cached = isatty(fileno(stderr)) ? 1 : 0;
#endif
    }
    return cached;
}

/* When non-zero, dbg_printf does NOT write to real stdout — only to the
 * capture buffer (g_stdout_buffer) and the debugger sink. Set during
 * --invoke so the body output isn't duplicated alongside __ret__. */

void append_to_stdout(const char *str) {
    if (!str) return;
    /* NOTE: emission to the VS Code Debug Console is handled by dbg_printf
     * (so that we don't double-emit when call sites do both dbg_printf AND
     * append_to_stdout for json() capture). */
    size_t len = strlen(str);
    if (!g_vm.stdout_buffer) {
        g_vm.stdout_size = len + 1024;
        g_vm.stdout_buffer = malloc(g_vm.stdout_size);
        if (g_vm.stdout_buffer) {
            strcpy(g_vm.stdout_buffer, str);
        }
    } else {
        size_t current_len = strlen(g_vm.stdout_buffer);
        if (current_len + len + 1 > g_vm.stdout_size) {
            g_vm.stdout_size = current_len + len + 1024;
            g_vm.stdout_buffer = realloc(g_vm.stdout_buffer, g_vm.stdout_size);
        }
        if (g_vm.stdout_buffer) {
            strcat(g_vm.stdout_buffer, str);
        }
    }
}

void native_json(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_json called\n"); fflush(stderr); }

    /* Phase H: json(call(...)) — invoke nested function, leave __ret__ as the
     * resulting string. interpret_call_func already sets __ret__ via the
     * native dispatcher (concat, request_param, etc.). */
    if (arg && arg->type && strcmp(arg->type, "CALL_FUNC") == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable("__ret__");
        if (r && r->vtype == VAL_STRING) {
            /* already a string — nothing else to do */
            return;
        }
        if (r && r->vtype == VAL_INT) {
            if (r->type && strcmp(r->type, "BOOL") == 0) {
                ASTNode *result_node = create_ast_leaf("STRING", 0, r->value.int_value ? "true" : "false", NULL);
                add_or_update_variable("__ret__", result_node);
                free_ast(result_node);
                return;
            }
            char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)r->value.int_value);
            ASTNode *result_node = create_ast_leaf("STRING", 0, tmp, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            return;
        }
        if (r && r->vtype == VAL_FLOAT) {
            char tmp[64]; te_fmt_double(tmp, sizeof(tmp), r->value.float_value);
            ASTNode *result_node = create_ast_leaf("STRING", 0, tmp, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            return;
        }
        /* Bug fix (v0.0.30): function returns MAP / OBJECT_LITERAL / LIST —
         * serialize via te_json_emit_node instead of emitting an empty string.
         * Fixes `json(fnCall())` → EMPTY when the fn returns an object.
         * Mirrors the same serialization path as the IDENTIFIER branch below. */
        if (r && r->vtype == VAL_OBJECT && r->type
            && (strcmp(r->type, "MAP") == 0
             || strcmp(r->type, "OBJECT_LITERAL") == 0
             || strcmp(r->type, "LIST") == 0)) {
            ASTNode *obj_node = (ASTNode *)(intptr_t)r->value.object_value;
            TeBuf b; tebuf_init(&b);
            te_json_emit_node(&b, obj_node);
            ASTNode *result_node = create_ast_leaf("STRING", 0, b.p ? b.p : "{}", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            if (b.p) free(b.p);
            return;
        }
        ASTNode *result_node = create_ast_leaf("STRING", 0, "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle empty argument (return json())
    if (!arg) {
        if (g_vm.stdout_buffer) {
            ASTNode *result_node = create_ast_leaf("STRING", 0, g_vm.stdout_buffer, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        } else {
            ASTNode *result_node = create_ast_leaf("STRING", 0, "{}", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        }
        return;
    }

    /* Phase H: accept a STRING / STRING_INTERP / ADD literal directly so that
     * `return json("{\"x\":1}")` or `return json(concat(...))` works without
     * the println+buffer dance. */
    if (arg->type && (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, arg->str_value ? arg->str_value : "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(arg->str_value ? arg->str_value : "");
        ASTNode *result_node = create_ast_leaf("STRING", 0, s, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(s);
        return;
    }
    if (arg->type && strcmp(arg->type, "ADD") == 0 && is_string_type(arg)) {
        char *s = get_node_string(arg);
        ASTNode *result_node = create_ast_leaf("STRING", 0, s ? s : "", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        if (s) free(s);
        return;
    }

    /* v1.0.0 gotcha-fix: `return json({...})` / `json([...])` inline literals.
     * Serialize the OBJECT_LITERAL / MAP / LIST node directly via te_json. */
    if (arg->type && (strcmp(arg->type, "OBJECT_LITERAL") == 0 ||
                      strcmp(arg->type, "MAP") == 0 ||
                      strcmp(arg->type, "LIST") == 0)) {
        TeBuf b; tebuf_init(&b);
        te_json_emit_node(&b, arg);
        ASTNode *result_node = create_ast_leaf("STRING", 0, b.p ? b.p : "{}", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        if (b.p) free(b.p);
        return;
    }

    const char *var_id = NULL;
    if (arg->id) {
        var_id = arg->id;
    } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
        var_id = arg->str_value ? arg->str_value : arg->id;
    } else if (arg->left && arg->left->id) {
        var_id = arg->left->id;
    }
    
    if (!var_id) { printf("[DIAG] native_json: var_id is NULL\n"); return; }
    Variable *v = find_variable((char*)var_id);
    if (!v) { printf("[DIAG] native_json: variable '%s' not found\n", var_id); return; }
    
    // Handle STRING
    if (v->vtype == VAL_STRING) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, v->value.string_value, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle LIST
    if (v->type && strcmp(v->type, "LIST") == 0) {
        ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
        if (!listNode) {
             ASTNode *empty = create_ast_leaf("STRING", 0, "[]", NULL);
             add_or_update_variable("__ret__", empty);
             free_ast(empty);
             return;
        }
        
        int estimated_size = 4096; 
        char *json_buffer = malloc(estimated_size);
        if(!json_buffer) return;
        strcpy(json_buffer, "[");
        
        ASTNode *cur = listNode->left;
        int first = 1;
        
        while (cur) {
            if (!first) strcat(json_buffer, ", ");
            first = 0;
            
            if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                ObjectNode *obj = NULL;
                if (cur->extra) obj = (ObjectNode *)cur->extra;
                else obj = (ObjectNode *)(intptr_t)cur->value;
                
                if (obj && obj->class) {
                    if (strlen(json_buffer) + 1024 > estimated_size) {
                        estimated_size *= 2;
                        json_buffer = realloc(json_buffer, estimated_size);
                    }
                    
                    strcat(json_buffer, "{");
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "\"%s\": ", obj->class->attributes[i].id);
                        strcat(json_buffer, temp);
                        
                        Variable *attr = &obj->attributes[i];
                        if (attr->vtype == VAL_STRING) {
                            strcat(json_buffer, "\"");
                            strcat(json_buffer, attr->value.string_value ? attr->value.string_value : "");
                            strcat(json_buffer, "\"");
                        } else if (attr->vtype == VAL_FLOAT) {
                            te_fmt_double(temp, sizeof(temp), attr->value.float_value);
                            strcat(json_buffer, temp);
                        } else {
                            snprintf(temp, sizeof(temp), "%lld", (long long)attr->value.int_value);
                            strcat(json_buffer, temp);
                        }
                        if (i < obj->class->attr_count - 1) strcat(json_buffer, ", ");
                    }
                    strcat(json_buffer, "}");
                }
            }
            cur = cur->next;
        }
        strcat(json_buffer, "]");
        
        ASTNode *result_node = create_ast_leaf("STRING", 0, json_buffer, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(json_buffer);
        return;
    }

    // Handle single OBJECT
    if (v->vtype != VAL_OBJECT) { printf("[DIAG] native_json: variable is not VAL_OBJECT, vtype=%d\n", v->vtype); return; }

    /* v1.0.0 gotcha-fix: variable holding a MAP / OBJECT_LITERAL — serialize
     * via te_json instead of falling through to the class-object path. */
    if (v->type && (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0)) {
        ASTNode *mapNode = (ASTNode *)(intptr_t)v->value.object_value;
        TeBuf b; tebuf_init(&b);
        te_json_emit_node(&b, mapNode);
        ASTNode *result_node = create_ast_leaf("STRING", 0, b.p ? b.p : "{}", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        if (b.p) free(b.p);
        return;
    }

    if (v->type && strcmp(v->type, "OBJECT") != 0) return;

    ObjectNode *obj = v->value.object_value;
    if (!obj || !obj->class) return;

    
    char *json_buffer = malloc(4096);
    strcpy(json_buffer, "{");
    for (int i = 0; i < obj->class->attr_count; i++) {
        char temp[512];
        snprintf(temp, sizeof(temp), "\"%s\": ", obj->class->attributes[i].id);
        strcat(json_buffer, temp);
        if (obj->attributes[i].vtype == VAL_STRING) {
            strcat(json_buffer, "\"");
            strcat(json_buffer, obj->attributes[i].value.string_value ? obj->attributes[i].value.string_value : "");
            strcat(json_buffer, "\"");
        } else if (obj->attributes[i].vtype == VAL_INT) {
            snprintf(temp, sizeof(temp), "%lld", (long long)obj->attributes[i].value.int_value);
            strcat(json_buffer, temp);
        } else if (obj->attributes[i].vtype == VAL_FLOAT) {
            te_fmt_double(temp, sizeof(temp), obj->attributes[i].value.float_value);
            strcat(json_buffer, temp);
        } else {
            strcat(json_buffer, "null");
        }
        if (i < obj->class->attr_count - 1) strcat(json_buffer, ", ");
    }
    strcat(json_buffer, "}");
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, json_buffer, NULL);
    add_or_update_variable("__ret__", result_node);
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_json: __ret__ set to %s\n", json_buffer); fflush(stderr); }
    free_ast(result_node);
    free(json_buffer);
}

// --- Hook para funciones nativas ---

void native_xml(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_xml called\n"); fflush(stderr); }

    /* Bug fix (v0.0.30): xml(fnCall()) — invoke nested function, same CALL_FUNC
     * pattern as native_json. Handles all return types so that
     * `return xml(RepoObj())` works just like `let o = RepoObj(); xml(o)`. */
    if (arg && arg->type && strcmp(arg->type, "CALL_FUNC") == 0) {
        interpret_call_func(arg);
        Variable *r = find_variable("__ret__");
        if (r && r->vtype == VAL_STRING) {
            /* already a string — nothing else to do */
            return;
        }
        if (r && r->vtype == VAL_INT) {
            char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)r->value.int_value);
            ASTNode *result_node = create_ast_leaf("STRING", 0, tmp, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            return;
        }
        if (r && r->vtype == VAL_FLOAT) {
            char tmp[64]; te_fmt_double(tmp, sizeof(tmp), r->value.float_value);
            ASTNode *result_node = create_ast_leaf("STRING", 0, tmp, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            return;
        }
        /* Function returns MAP / OBJECT_LITERAL — serialize as XML using the
         * same <root> wrapper as the inline OBJECT_LITERAL branch below. */
        if (r && r->vtype == VAL_OBJECT && r->type
            && (strcmp(r->type, "MAP") == 0 || strcmp(r->type, "OBJECT_LITERAL") == 0)) {
            ASTNode *map_node = (ASTNode *)(intptr_t)r->value.object_value;
            TeBuf b; tebuf_init(&b);
            tebuf_puts(&b, "<root>");
            for (ASTNode *kv = map_node ? map_node->left : NULL; kv; kv = kv->right) {
                const char *key = kv->id ? kv->id : "item";
                char *val = get_node_string(kv->left);
                tebuf_putc(&b, '<'); tebuf_puts(&b, key); tebuf_putc(&b, '>');
                tebuf_puts(&b, val ? val : "");
                tebuf_puts(&b, "</"); tebuf_puts(&b, key); tebuf_putc(&b, '>');
                if (val) free(val);
            }
            tebuf_puts(&b, "</root>");
            ASTNode *result_node = create_ast_leaf("STRING", 0, b.p ? b.p : "<root></root>", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
            if (b.p) free(b.p);
            return;
        }
        /* Function returns LIST or class object — print_object_as_xml_by_id
         * already handles both cases via the __ret__ variable. */
        if (r && r->vtype == VAL_OBJECT && r->type) {
            print_object_as_xml_by_id("__ret__");
            return;
        }
        /* Fallback: empty XML */
        ASTNode *result_node = create_ast_leaf("STRING", 0, "<root></root>", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle empty argument (return xml())
    if (!arg) {
        if (g_vm.stdout_buffer) {
            ASTNode *result_node = create_ast_leaf("STRING", 0, g_vm.stdout_buffer, NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        } else {
            ASTNode *result_node = create_ast_leaf("STRING", 0, "<root></root>", NULL);
            add_or_update_variable("__ret__", result_node);
            free_ast(result_node);
        }
        return;
    }

    /* v1.0.0 gotcha-fix: `return xml({...})` inline object literal. Emit each
     * key/value pair as <key>value</key> wrapped in <root>. Must run before
     * the var_id resolution below (which would mistake the first KV key for a
     * variable name via arg->left->id). */
    if (arg->type && (strcmp(arg->type, "OBJECT_LITERAL") == 0 || strcmp(arg->type, "MAP") == 0)) {
        TeBuf b; tebuf_init(&b);
        tebuf_puts(&b, "<root>");
        for (ASTNode *kv = arg->left; kv; kv = kv->right) {
            const char *key = kv->id ? kv->id : "item";
            char *val = get_node_string(kv->left);
            tebuf_putc(&b, '<'); tebuf_puts(&b, key); tebuf_putc(&b, '>');
            tebuf_puts(&b, val ? val : "");
            tebuf_puts(&b, "</"); tebuf_puts(&b, key); tebuf_putc(&b, '>');
            if (val) free(val);
        }
        tebuf_puts(&b, "</root>");
        ASTNode *result_node = create_ast_leaf("STRING", 0, b.p ? b.p : "<root></root>", NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        if (b.p) free(b.p);
        return;
    }

    const char *var_id = NULL;
    if (arg->id) {
        var_id = arg->id;
    } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
        var_id = arg->str_value ? arg->str_value : arg->id;
    } else if (arg->left && arg->left->id) {
        var_id = arg->left->id;
    }
    
    if (var_id) {
        print_object_as_xml_by_id(var_id);
        return;
    }
    
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
         ASTNode *result_node = create_ast_leaf("STRING", 0, arg->str_value, NULL);
         add_or_update_variable("__ret__", result_node);
         free_ast(result_node);
         return;
    }
}


/* Forward declarations: usados antes de su definición para soportar
 * resolución de arr[i].attr en get_node_string / is_string_type. */
ASTNode* list_get_item(ASTNode *list, int idx);
int list_length(ASTNode *list);
ASTNode* resolve_to_list(ASTNode *node);
ASTNode* resolve_to_map(ASTNode *node);
int map_length(ASTNode *map);
ASTNode* map_find_pair(ASTNode *map, const char *key);
ASTNode* resolve_access_item(ASTNode *node);
const char* te_map_key_coerce(ASTNode *keyNode, char *buf, size_t cap);
void interpret_call_method(ASTNode *node);

/* Format a double as the shortest decimal string that round-trips back to the
 * same IEEE-754 value (Python/JS style). Integer-valued doubles print without a
 * decimal point ("2.0" -> "2"). Replaces the old "%f" (6-decimal zero padding)
 * so println/concat/interpolation/json all render floats the same, pretty way.
 *   3.1416      -> "3.1416"      (no trailing zeros)
 *   0.1 + 0.2   -> "0.30000000000000004"  (full precision, like Python/JS)
 *   10.0 / 3.0  -> "3.3333333333333335"
 *   2.0         -> "2"
 */
void te_fmt_double(char *buf, size_t cap, double v) {
    if (!buf || cap == 0) return;
    if (isnan(v)) { snprintf(buf, cap, "nan"); return; }
    if (isinf(v)) { snprintf(buf, cap, v < 0 ? "-inf" : "inf"); return; }
    /* Integer-valued within 64-bit range -> render without decimals. */
    if (v == (double)(long long)v && v >= -9.2233720368547758e18 && v <= 9.2233720368547758e18) {
        snprintf(buf, cap, "%lld", (long long)v);
        return;
    }
    /* Shortest round-trip: grow precision until the text parses back exactly. */
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, cap, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) return;
    }
    snprintf(buf, cap, "%.17g", v);
}

/* ERP gotcha #38-bis: `("" + map)` used to yield "" (silently dropping the
 * value). A MAP / OBJECT_LITERAL in string context now renders as JSON,
 * same text json_stringify(map) produces. */
char* te_map_node_to_string(ASTNode *mapNode) {
    TeBuf b; tebuf_init(&b);
    te_json_emit_node(&b, mapNode);
    char *out = strdup(b.p ? b.p : "");
    free(b.p);
    return out;
}
static int te_var_is_map(Variable *v) {
    return v && v->vtype == VAL_OBJECT && v->type && v->value.object_value &&
           (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0);
}

/* Render a LIST node to a malloc'd string like "[1, 2, 3]" for string
 * concatenation (+) and string-context coercion. Mirrors the element
 * formatting of te_print_list_node (STRING raw, FLOAT %f, int %d). */
static char* te_list_node_to_string(ASTNode *listNode) {
    size_t cap = 64, len = 0;
    char *out = (char*)malloc(cap);
    if (!out) return strdup("");
    out[0] = '\0';
#define TE_LS_APPEND(s) do { \
        const char *_s = (s); size_t _l = strlen(_s); \
        if (len + _l + 1 > cap) { while (len + _l + 1 > cap) cap *= 2; out = (char*)realloc(out, cap); } \
        memcpy(out + len, _s, _l); len += _l; out[len] = '\0'; \
    } while (0)
    TE_LS_APPEND("[");
    ASTNode *cur = listNode ? listNode->left : NULL;
    int first = 1;
    char buf[64];
    while (cur) {
        if (!first) TE_LS_APPEND(", ");
        first = 0;
        if (cur->type && strcmp(cur->type, "STRING") == 0) {
            TE_LS_APPEND(cur->str_value ? cur->str_value : "");
        } else if (cur->type && strcmp(cur->type, "FLOAT") == 0) {
            te_fmt_double(buf, sizeof(buf), cur->str_value ? atof(cur->str_value) : 0.0);
            TE_LS_APPEND(buf);
        } else if (cur->type && (strcmp(cur->type, "OBJECT_LITERAL") == 0 ||
                                 strcmp(cur->type, "MAP") == 0 ||
                                 strcmp(cur->type, "LIST") == 0)) {
            /* Elemento anidado (objeto/array de json_parse): serializar a JSON
             * en vez de "%d" de cur->value (=0), que producia "[0, 0]" para un
             * array de objetos y rompia guards tipo ("" + arr) != "". */
            TeBuf jb; tebuf_init(&jb);
            te_json_emit_node(&jb, cur);
            TE_LS_APPEND(jb.p ? jb.p : "");
            if (jb.p) free(jb.p);
        } else if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
            TE_LS_APPEND("object");
        } else {
            snprintf(buf, sizeof(buf), "%lld", (long long)cur->value);
            TE_LS_APPEND(buf);
        }
        cur = cur->next;
    }
    TE_LS_APPEND("]");
#undef TE_LS_APPEND
    return out;
}

// Helper function to get string representation of any node
char* get_node_string(ASTNode* node) {
    if (!node) return strdup("");
    
    char temp[64];

    /* Ternary in string context: evaluate condition, stringify chosen branch. */
    if (node->type && strcmp(node->type, "TERNARY") == 0) {
        int cond = (evaluate_expression(node->left) != 0.0);
        return get_node_string(cond ? node->right : node->extra);
    }

    /* Phase H: nested function call — invoke it and read __ret__. Lets
     * concat(a, request_param("id"), b) work, plus any future nesting. */
    if (node->type && strcmp(node->type, "CALL_FUNC") == 0) {
        interpret_call_func(node);
        Variable *r = find_variable("__ret__");
        if (r) {
            if (r->vtype == VAL_STRING) return strdup(r->value.string_value ? r->value.string_value : "");
            if (r->vtype == VAL_INT)    { snprintf(temp, sizeof(temp), "%lld", r->value.int_value); return strdup(temp); }
            if (r->vtype == VAL_FLOAT)  { te_fmt_double(temp, sizeof(temp), r->value.float_value); return strdup(temp); }
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "LIST") == 0)
                return te_list_node_to_string((ASTNode *)(intptr_t)r->value.object_value);
            if (te_var_is_map(r)) return te_map_node_to_string((ASTNode *)(intptr_t)r->value.object_value);
        }
        return strdup("");
    }

    /* v0.0.11: nested method call — invoke and read __ret__.
     * Enables `"sum=" + nums.sum()` and similar LINQ chains in string contexts. */
    if (node->type && strcmp(node->type, "CALL_METHOD") == 0) {
        interpret_call_method(node);
        Variable *r = find_variable("__ret__");
        if (r) {
            if (r->vtype == VAL_STRING) return strdup(r->value.string_value ? r->value.string_value : "");
            if (r->vtype == VAL_INT)    { snprintf(temp, sizeof(temp), "%lld", r->value.int_value); return strdup(temp); }
            if (r->vtype == VAL_FLOAT)  { te_fmt_double(temp, sizeof(temp), r->value.float_value); return strdup(temp); }
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "NULL") == 0) return strdup("null");
            if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "LIST") == 0)
                return te_list_node_to_string((ASTNode *)(intptr_t)r->value.object_value);
            if (te_var_is_map(r)) return te_map_node_to_string((ASTNode *)(intptr_t)r->value.object_value);
        }
        return strdup("");
    }

    if (node->type && (strcmp(node->type, "STRING") == 0 || strcmp(node->type, "STRING_LITERAL") == 0)) {
        return node->str_value ? strdup(node->str_value) : strdup("");
    }

    if (node->type && strcmp(node->type, "STRING_INTERP") == 0) {
        return expand_interp_string(node->str_value ? node->str_value : "");
    }

    if (node->type && strcmp(node->type, "NULL") == 0) {
        return strdup("null");
    }

    if (node->type && (strcmp(node->type, "MAP") == 0 || strcmp(node->type, "OBJECT_LITERAL") == 0)) {
        return te_map_node_to_string(node);
    }

    if (node->type && strcmp(node->type, "ADD") == 0) {
        /* `+` está sobrecargado: concatena si ALGÚN lado es string; si AMBOS
         * son numéricos, suma. Sin la rama numérica, concat("s=", a+b) con
         * a,b enteros producía "1020" en vez de "30" (gotcha #8), y
         * "x" + (3 + 5) daba "x35" en vez de "x8". */
        if (is_string_type(node)) {
            char *l = get_node_string(node->left);
            char *r = get_node_string(node->right);
            size_t n = strlen(l) + strlen(r) + 1;
            char *out = malloc(n);
            snprintf(out, n, "%s%s", l, r);
            free(l); free(r);
            return out;
        }
        long long i64v;
        if (te_eval_i64(node, &i64v)) { snprintf(temp, sizeof(temp), "%lld", i64v); return strdup(temp); }   /* Fase 1b */
        double d = evaluate_expression(node);
        te_fmt_double(temp, sizeof(temp), d);
        return strdup(temp);
    }
    
    if (node->type && (strcmp(node->type, "NUMBER") == 0 || strcmp(node->type, "INT") == 0)) {
        snprintf(temp, sizeof(temp), "%lld", (long long)node->value);
        return strdup(temp);
    }
    
    if (node->type && strcmp(node->type, "FLOAT") == 0) {
        te_fmt_double(temp, sizeof(temp), node->str_value ? atof(node->str_value) : 0.0);
        return strdup(temp);
    }

    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (v) {
            if (v->vtype == VAL_STRING) return strdup(v->value.string_value ? v->value.string_value : "");
            if (v->vtype == VAL_INT) {
                snprintf(temp, sizeof(temp), "%lld", v->value.int_value);
                return strdup(temp);
            }
            if (v->vtype == VAL_FLOAT) {
                te_fmt_double(temp, sizeof(temp), v->value.float_value);
                return strdup(temp);
            }
            if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0)
                return te_list_node_to_string((ASTNode *)(intptr_t)v->value.object_value);
            if (te_var_is_map(v)) return te_map_node_to_string((ASTNode *)(intptr_t)v->value.object_value);
            if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "NULL") == 0)
                return strdup("null");
        }
        return strdup(""); // Variable not found or unknown type
    }
    
    if (node->type && strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left;
        ASTNode *a = node->right;
        if (!o || !a) return strdup("");
        /* str.length / list.length / map.length — mayo 2026 */
        if (a->id && strcmp(a->id, "length") == 0) {
            ASTNode *list = resolve_to_list(o);
            if (list) { snprintf(temp, sizeof(temp), "%d", list_length(list)); return strdup(temp); }
            ASTNode *map = resolve_to_map(o);
            if (map)  { snprintf(temp, sizeof(temp), "%d", map_length(map));  return strdup(temp); }
            if (o->id) {
                Variable *sv = find_variable(o->id);
                if (sv && sv->vtype == VAL_STRING) {
                    snprintf(temp, sizeof(temp), "%zu", sv->value.string_value ? strlen(sv->value.string_value) : 0);
                    return strdup(temp);
                }
            }
        }
        ObjectNode *obj = NULL;
        /* Caso 1: o es IDENTIFIER → variable OBJECT. */
        if (o->id) {
            Variable *v = find_variable(o->id);
            /* v1.0.0 fix: variable is a MAP / OBJECT_LITERAL (e.g. `r.activo`
             * inside println/concat where r is a `{..}` literal). Its
             * value.object_value is an ASTNode* (the map), NOT an ObjectNode*;
             * the cast + `obj->class` below would segfault. Resolve as
             * `r["activo"]` and stringify the value node. */
            if (v && v->vtype == VAL_OBJECT && v->type &&
                (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0)) {
                ASTNode *map  = (ASTNode*)(intptr_t)v->value.object_value;
                ASTNode *pair = (map && a->id) ? map_find_pair(map, a->id) : NULL;
                ASTNode *val  = pair ? pair->left : NULL;
                if (!val || !val->type) return strdup("");
                if (strcmp(val->type, "BOOL") == 0)
                    return strdup(val->value ? "true" : "false");
                if (strcmp(val->type, "NULL") == 0)
                    return strdup("null");
                if (strcmp(val->type, "STRING") == 0 || strcmp(val->type, "DATETIME") == 0 ||
                    strcmp(val->type, "UUID") == 0)
                    return strdup(val->str_value ? val->str_value : "");
                if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) {
                    snprintf(temp, sizeof(temp), "%lld", (long long)val->value);
                    return strdup(temp);
                }
                if (strcmp(val->type, "FLOAT") == 0) {
                    te_fmt_double(temp, sizeof(temp), val->str_value ? atof(val->str_value) : 0.0);
                    return strdup(temp);
                }
                /* CALL_FUNC / expression → recurse. */
                return get_node_string(val);
            }
            if (v && v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "OBJECT") == 0) obj = v->value.object_value;
        }
        /* Caso 2: arr[i].attr — o es ACCESS_EXPR sobre LIST. */
        if (!obj && o->type && strcmp(o->type, "ACCESS_EXPR") == 0) {
            ASTNode *list = resolve_to_list(o->left);
            if (list && o->right) {
                int idx = (int)evaluate_expression(o->right);
                if (idx >= 0 && idx < list_length(list)) {
                    ASTNode *item = list_get_item(list, idx);
                    if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                        obj = item->extra ? (ObjectNode*)item->extra
                                          : (ObjectNode*)(intptr_t)item->value;
                    }
                }
            }
            /* Caso 3: m["k"].attr — o es ACCESS_EXPR sobre MAP (toMap result). */
            if (!obj) {
                ASTNode *map = resolve_to_map(o->left);
                if (map && o->right && o->right->type) {
                    const char *key = NULL;
                    if (strcmp(o->right->type, "STRING") == 0) key = o->right->str_value;
                    else if (strcmp(o->right->type, "IDENTIFIER") == 0 || strcmp(o->right->type, "ID") == 0) {
                        Variable *kv = find_variable(o->right->id);
                        if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                    }
                    if (key) {
                        ASTNode *pair = map_find_pair(map, key);
                        ASTNode *val  = pair ? pair->left : NULL;
                        if (val && val->type && strcmp(val->type, "OBJECT") == 0) {
                            obj = val->extra ? (ObjectNode*)val->extra
                                             : (ObjectNode*)(intptr_t)val->value;
                        }
                    }
                }
            }
        }
        if (obj && obj->class) {
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    if (!te_attr_access_ok(obj->class, i, o)) return strdup("");
                    Variable *attr = &obj->attributes[i];
                    if (attr->vtype == VAL_STRING) return strdup(attr->value.string_value ? attr->value.string_value : "");
                    if (attr->vtype == VAL_INT) {
                        snprintf(temp, sizeof(temp), "%lld", (long long)attr->value.int_value);
                        return strdup(temp);
                    }
                    if (attr->vtype == VAL_FLOAT) {
                        te_fmt_double(temp, sizeof(temp), attr->value.float_value);
                        return strdup(temp);
                    }
                }
            }
        }
        return strdup(""); // Attribute not found
    }

    /* Bug fix: m["key"] / arr[i] inside a string context (concat, "a" + x,
     * string ==). Without this, ACCESS_EXPR fell through to the empty
     * fallback below and string values from json_parse() were lost. */
    if (node->type && strcmp(node->type, "ACCESS_EXPR") == 0) {
        ASTNode *val = NULL;
        ASTNode *map = resolve_to_map(node->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(node->right, keybuf, sizeof(keybuf));
            if (key) {
                ASTNode *pair = map_find_pair(map, key);
                if (pair) val = pair->left;
            }
        } else {
            ASTNode *list = resolve_to_list(node->left);
            if (list && node->right) {
                int idx = (int)evaluate_expression(node->right);
                if (idx >= 0 && idx < list_length(list)) val = list_get_item(list, idx);
            }
        }
        if (val && val->type) {
            if (strcmp(val->type, "STRING") == 0)
                return strdup(val->str_value ? val->str_value : "");
            if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) {
                snprintf(temp, sizeof(temp), "%lld", (long long)val->value);
                return strdup(temp);
            }
            if (strcmp(val->type, "FLOAT") == 0) {
                te_fmt_double(temp, sizeof(temp), val->str_value ? atof(val->str_value) : 0.0);
                return strdup(temp);
            }
        }
        return strdup("");
    }

    /* Fallback: arithmetic / bitwise sub-expressions (MUL, SUB, DIV, MOD,
     * NEG, BIT_*, SHL, SHR, paren-wrapped, etc.). Let evaluate_expression
     * compute the numeric value and format it. Keeps `"x" + (s*2)` working. */
    if (node->type && (
        strcmp(node->type, "MUL") == 0 ||
        strcmp(node->type, "SUB") == 0 ||
        strcmp(node->type, "DIV") == 0 ||
        strcmp(node->type, "MOD") == 0 ||
        strcmp(node->type, "NEG") == 0 ||
        strcmp(node->type, "BIT_AND") == 0 ||
        strcmp(node->type, "BIT_OR") == 0 ||
        strcmp(node->type, "BIT_XOR") == 0 ||
        strcmp(node->type, "BIT_NOT") == 0 ||
        strcmp(node->type, "SHL") == 0 ||
        strcmp(node->type, "SHR") == 0)) {
        double d = evaluate_expression(node);
        te_fmt_double(temp, sizeof(temp), d);
        return strdup(temp);
    }

    /* Comparison / logical operators in a string context emit "1"/"0" to
     * match how a raw comparison renders elsewhere (println(5 > 3) -> 1) and
     * how boolean-typed variables concatenate. Without this they fell through
     * to the empty fallback. */
    if (node->type && (
        strcmp(node->type, "GT") == 0 ||
        strcmp(node->type, "LT") == 0 ||
        strcmp(node->type, "EQ") == 0 ||
        strcmp(node->type, "GT_EQ") == 0 ||
        strcmp(node->type, "LT_EQ") == 0 ||
        strcmp(node->type, "DIFF") == 0 ||
        strcmp(node->type, "AND") == 0 ||
        strcmp(node->type, "OR") == 0 ||
        strcmp(node->type, "NOT") == 0)) {
        return strdup(evaluate_expression(node) != 0 ? "1" : "0");
    }

    return strdup(""); // Fallback
}

void native_concat(ASTNode *arg) {
    if (g_debug_mode) { fprintf(stderr, "[DEBUG] native_concat called\n"); fflush(stderr); }
    
    // Start with a reasonable buffer size
    size_t buffer_size = 1024;
    size_t current_len = 0;
    char *result = malloc(buffer_size);
    result[0] = '\0';
    
    ASTNode *curr = arg;
    while (curr) {
        char *s_temp = get_node_string(curr);
        if (s_temp) {
            if (g_debug_mode) fprintf(stderr, "[DEBUG] concat appending: '%s'\n", s_temp);
            size_t len = strlen(s_temp);
            if (current_len + len >= buffer_size) {
                buffer_size = (current_len + len) * 2;
                result = realloc(result, buffer_size);
            }
            strcat(result, s_temp);
            current_len += len;
            free(s_temp);
        }
        curr = curr->next; // Next argument (gotcha #1: step via ->next)
    }
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, result, NULL);
    add_or_update_variable("__ret__", result_node);
    free_ast(result_node);
    free(result);
}

/* ============================================================================
 * Phase H: HTTP request/response state (used by API server to pass request data
 * into the interpreted endpoint body, and read back response status/headers).
 *
 * The server populates these globals BEFORE calling typeeasy_embedded_invoke()
 * via the typeeasy_http_* setters declared below; resets them between requests
 * via typeeasy_http_reset(). Inside the interpreted body, builtins like
 * request_query("q") / response_status(404) read/write these.
 * ============================================================================ */
typedef struct TeKV { char *k; char *v; struct TeKV *next; } TeKV;

static char *g_req_method = NULL;
static char *g_req_path   = NULL;
static char *g_req_body   = NULL;
static size_t g_req_body_len = 0;   /* binary-safe length; strlen() not used (body may contain NULs, e.g. xlsx upload) */
static TeKV *g_req_query   = NULL;
static TeKV *g_req_headers = NULL;
static TeKV *g_req_params  = NULL;
static int   g_resp_status = 200;
static TeKV *g_resp_headers = NULL;

/* Binary download channel. When a handler calls xlsx_download()/pdf_download()/
 * response_file(), the bytes of the file live here (binary-safe, explicit
 * length) along with the Content-Type to emit. The embedded API server checks
 * this AFTER invoking the handler and, when set, writes these raw bytes as the
 * body (via mg_write) instead of the textual __ret__ string — so .xlsx (ZIP)
 * and .pdf payloads keep embedded NUL bytes intact. Reset every request. */
static char  *g_resp_body = NULL;
static size_t g_resp_body_len = 0;
static char  *g_resp_content_type = NULL;

/* Response content-type intent. When a handler does `return "text"` (or any
 * bare scalar/string value rather than json()/xml()), the embedded server must
 * answer with Content-Type: text/plain instead of application/json. This flag
 * is set by interpret_return_node and read by the server. It defaults to 0
 * (structured/json) and is reset on every request via typeeasy_http_reset(). */
int g_response_is_raw_text = 0;

/* API server mode. Set once at startup (before any civetweb worker thread is
 * spawned) when the process runs `--api`. While set, the bytecode cache is
 * disabled (see bc_get_or_compile* in te_bytecode.c): the cache lives on the
 * shared AST nodes and stores raw Variable* pointers into vars[], which is
 * unsafe once requests run on parallel threads with thread-local vars[].
 * Read-only after startup, so it needs no synchronization. */
int g_api_mode = 0;

static void te_kv_free_list(TeKV **head) {
    TeKV *c = *head; while (c) { TeKV *n = c->next; free(c->k); free(c->v); free(c); c = n; }
    *head = NULL;
}
static void te_kv_add(TeKV **head, const char *k, const char *v) {
    if (!k) return;
    TeKV *e = (TeKV*)malloc(sizeof(TeKV));
    e->k = strdup(k); e->v = strdup(v ? v : ""); e->next = *head; *head = e;
}
static const char *te_kv_find(TeKV *head, const char *k) {
    if (!k) return NULL;
    while (head) { if (head->k && strcmp(head->k, k) == 0) return head->v; head = head->next; }
    return NULL;
}
/* gotcha #15: los nombres de cabecera HTTP son case-insensitive (RFC 7230 §3.2).
 * Un cliente puede enviar "content-type", "Content-Type" o "CONTENT-TYPE" y todos
 * deben resolver a la misma entrada. te_kv_find_ci hace el match ignorando
 * mayúsculas/minúsculas (ASCII). Se usa para request_header/request_cookie y el
 * accessor interno de cabeceras; queries/params siguen siendo case-sensitive. */
static int te_ascii_casecmp(const char *a, const char *b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static const char *te_kv_find_ci(TeKV *head, const char *k) {
    if (!k) return NULL;
    while (head) { if (head->k && te_ascii_casecmp(head->k, k) == 0) return head->v; head = head->next; }
    return NULL;
}

void typeeasy_http_reset(void) {
    free(g_req_method); g_req_method = NULL;
    free(g_req_path);   g_req_path   = NULL;
    free(g_req_body);   g_req_body   = NULL;  g_req_body_len = 0;
    te_kv_free_list(&g_req_query);
    te_kv_free_list(&g_req_headers);
    te_kv_free_list(&g_req_params);
    te_kv_free_list(&g_resp_headers);
    g_resp_status = 200;
    g_response_is_raw_text = 0;
    free(g_resp_body); g_resp_body = NULL; g_resp_body_len = 0;
    free(g_resp_content_type); g_resp_content_type = NULL;
}
void typeeasy_http_set_method(const char *m) { free(g_req_method); g_req_method = m ? strdup(m) : NULL; }
void typeeasy_http_set_path  (const char *p) { free(g_req_path);   g_req_path   = p ? strdup(p) : NULL; }
/* Binary-safe body setter: keeps explicit length so callers can store payloads
 * with embedded NUL bytes (e.g. .xlsx uploads = ZIP). The buffer is always
 * NUL-terminated past the end so string-style accessors (request_body()) still
 * work for textual bodies. */
void typeeasy_http_set_body_n(const void *buf, size_t len) {
    free(g_req_body); g_req_body = NULL; g_req_body_len = 0;
    if (!buf) return;
    g_req_body = (char*)malloc(len + 1);
    if (!g_req_body) return;
    if (len) memcpy(g_req_body, buf, len);
    g_req_body[len] = '\0';
    g_req_body_len = len;
}
void typeeasy_http_set_body  (const char *b) { typeeasy_http_set_body_n(b, b ? strlen(b) : 0); }
void typeeasy_http_add_query (const char *k, const char *v) { te_kv_add(&g_req_query,  k, v); }
void typeeasy_http_add_header(const char *k, const char *v) { te_kv_add(&g_req_headers, k, v); }
void typeeasy_http_add_param (const char *k, const char *v) { te_kv_add(&g_req_params,  k, v); }
int  typeeasy_http_get_status(void) { return g_resp_status; }
void typeeasy_http_set_status(int s) { g_resp_status = s; }
int  typeeasy_http_iter_response_header(int idx, const char **k, const char **v) {
    TeKV *c = g_resp_headers; int i = 0;
    while (c) { if (i == idx) { if (k) *k = c->k; if (v) *v = c->v; return 1; } c = c->next; i++; }
    return 0;
}

/* Binary download channel (see g_resp_body declaration above). The setter
 * copies the bytes (caller keeps ownership of its buffer). Passing buf=NULL
 * clears any pending binary body. */
void typeeasy_http_set_response_bytes(const void *buf, size_t len, const char *content_type) {
    free(g_resp_body); g_resp_body = NULL; g_resp_body_len = 0;
    free(g_resp_content_type); g_resp_content_type = NULL;
    if (!buf) return;
    g_resp_body = (char*)malloc(len ? len : 1);
    if (!g_resp_body) return;
    if (len) memcpy(g_resp_body, buf, len);
    g_resp_body_len = len;
    g_resp_content_type = content_type ? strdup(content_type) : NULL;
}
const char *typeeasy_http_get_response_bytes(size_t *out_len) {
    if (out_len) *out_len = g_resp_body_len;
    return g_resp_body;
}
const char *typeeasy_http_get_response_content_type(void) { return g_resp_content_type; }

/* Debugger introspection */
const char *typeeasy_http_get_method(void) { return g_req_method; }
const char *typeeasy_http_get_path  (void) { return g_req_path;   }
const char *typeeasy_http_get_body  (void) { return g_req_body;   }
const char *typeeasy_http_get_body_n(size_t *out_len) {
    if (out_len) *out_len = g_req_body_len;
    return g_req_body;
}
static int te_kv_iter(TeKV *head, int idx, const char **k, const char **v) {
    int i = 0; for (TeKV *c = head; c; c = c->next, ++i) {
        if (i == idx) { if (k) *k = c->k; if (v) *v = c->v; return 1; }
    }
    return 0;
}
int typeeasy_http_iter_param (int idx, const char **k, const char **v) { return te_kv_iter(g_req_params,  idx, k, v); }
int typeeasy_http_iter_query (int idx, const char **k, const char **v) { return te_kv_iter(g_req_query,   idx, k, v); }
int typeeasy_http_iter_header(int idx, const char **k, const char **v) { return te_kv_iter(g_req_headers, idx, k, v); }

/* Helpers: extract the first STRING argument from a function-call arg AST.
 * The argument may be a single STRING node, or a chain via ->right, or wrapped
 * in evaluate_native_args (which already resolves identifiers to STRING). */
const char *te_arg_string(ASTNode *arg) {
    if (!arg) return NULL;
    if (arg->type) {
        if (strcmp(arg->type, "STRING") == 0 || strcmp(arg->type, "STRING_LITERAL") == 0)
            return arg->str_value;
        if (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0) {
            Variable *v = find_variable(arg->id);
            if (v && v->vtype == VAL_STRING) return v->value.string_value;
        }
    }
    return NULL;
}
int te_arg_int(ASTNode *arg, int dflt) {
    if (!arg) return dflt;
    if (arg->type) {
        if (strcmp(arg->type, "NUMBER") == 0 || strcmp(arg->type, "INT") == 0) return arg->value;
        if (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0) {
            Variable *v = find_variable(arg->id);
            if (v && v->vtype == VAL_INT) return v->value.int_value;
        }
    }
    return dflt;
}

void te_set_ret_string(const char *s) {
    ASTNode *r = create_ast_leaf("STRING", 0, s ? s : "", NULL);
    add_or_update_variable("__ret__", r);
    free_ast(r);
}
void te_set_ret_int(int n) {
    ASTNode *r = create_ast_leaf("NUMBER", n, NULL, NULL);
    add_or_update_variable("__ret__", r);
    free_ast(r);
}

static void native_request_method(ASTNode *arg) { (void)arg; te_set_ret_string(g_req_method ? g_req_method : ""); }
static void native_request_path  (ASTNode *arg) { (void)arg; te_set_ret_string(g_req_path   ? g_req_path   : ""); }
static void native_request_body  (ASTNode *arg) { (void)arg; te_set_ret_string(g_req_body   ? g_req_body   : ""); }
static void native_request_query (ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find(g_req_query,   k); te_set_ret_string(v ? v : ""); }
static void native_request_header(ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find_ci(g_req_headers, k); te_set_ret_string(v ? v : ""); }
static void native_request_param (ASTNode *arg) { const char *k = te_arg_string(arg); const char *v = te_kv_find(g_req_params,  k); te_set_ret_string(v ? v : ""); }

/* request_cookie(name): parse the "Cookie" request header and return the value
 * of the named cookie ("" if absent). The header looks like
 *   "user=admin; logkey=abc123; PHPSESSID=xyz"
 * Self-contained parser: split on ';', trim spaces, match "<name>=" exactly.
 * Cross-platform (pure C stdlib, no POSIX-only calls). */
static void native_request_cookie(ASTNode *arg) {
    const char *name = te_arg_string(arg);
    const char *hdr  = te_kv_find_ci(g_req_headers, "Cookie");
    if (!name || !*name || !hdr) { te_set_ret_string(""); return; }
    size_t nlen = strlen(name);
    const char *p = hdr;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') p++;   /* skip separators */
        if (!*p) break;
        const char *key = p;
        const char *eq  = key;
        while (*eq && *eq != '=' && *eq != ';') eq++;        /* find '=' */
        size_t klen = (size_t)(eq - key);
        if (*eq == '=' && klen == nlen && strncmp(key, name, nlen) == 0) {
            const char *val = eq + 1;
            const char *end = val;
            while (*end && *end != ';') end++;               /* value until ';' */
            char buf[1024];
            size_t vlen = (size_t)(end - val);
            if (vlen >= sizeof(buf)) vlen = sizeof(buf) - 1;
            memcpy(buf, val, vlen);
            buf[vlen] = '\0';
            te_set_ret_string(buf);
            return;
        }
        /* advance past this pair */
        while (*p && *p != ';') p++;
    }
    te_set_ret_string("");
}

/* v0.0.16: @auth decorator support. g_current_claims holds the validated JWT
 * payload (JSON) for the current request; set by the API dispatch when an
 * @auth endpoint passes verification, exposed to handlers via current_claims(). */
static char *g_current_claims = NULL;
const char *typeeasy_http_get_header(const char *k) { return te_kv_find_ci(g_req_headers, k); }
void typeeasy_set_current_claims(const char *json) {
    if (g_current_claims) { free(g_current_claims); g_current_claims = NULL; }
    if (json) g_current_claims = strdup(json);
}
static void native_current_claims(ASTNode *arg) { (void)arg; te_set_ret_string(g_current_claims ? g_current_claims : ""); }

/* v0.0.13: Debug-print que va a stderr (NO entra al pipeline de captura
 * de stdout que arma el cuerpo HTTP). Pensado para inspeccionar handlers
 * POST/GET en `docker compose logs typeeasy` sin contaminar la respuesta.
 *
 * Usa get_node_string() (no te_arg_string()) para que arguments como
 *   debug_log(concat(...))      -> evalua la CALL_FUNC anidada
 *   debug_log(u.name)           -> resuelve ACCESS_ATTR sobre OBJECT
 *   debug_log("a" + 1)          -> resuelve ADD con coercion a string
 * funcionen en vez de imprimir vacio. */
void native_debug_log(ASTNode *arg) {
    char *s = get_node_string(arg);
    fprintf(stderr, "[debug] %s\n", s ? s : "");
    fflush(stderr);
    if (s) free(s);
    te_set_ret_int(0);
}

/* JSON-encode a TeKV list as { "k": "v", ... } into out (truncates safely). */
static void te_kv_to_json(TeKV *head, char *out, size_t cap) {
    size_t o = 0;
    if (cap < 3) { if (cap) out[0] = 0; return; }
    out[o++] = '{';
    int first = 1;
    for (TeKV *c = head; c && o + 8 < cap; c = c->next) {
        const char *k = c->k ? c->k : "";
        const char *v = c->v ? c->v : "";
        if (!first) { if (o + 1 < cap) out[o++] = ','; }
        first = 0;
        if (o + 1 < cap) out[o++] = '"';
        for (const char *p = k; *p && o + 2 < cap; ++p) {
            if (*p == '"' || *p == '\\') { if (o + 2 < cap) out[o++] = '\\'; }
            out[o++] = *p;
        }
        if (o + 3 < cap) { out[o++] = '"'; out[o++] = ':'; out[o++] = '"'; }
        for (const char *p = v; *p && o + 2 < cap; ++p) {
            if (*p == '"' || *p == '\\') { if (o + 2 < cap) out[o++] = '\\'; }
            else if (*p == '\n') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 'n'; } continue; }
            else if (*p == '\r') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 'r'; } continue; }
            else if (*p == '\t') { if (o + 2 < cap) { out[o++] = '\\'; out[o++] = 't'; } continue; }
            out[o++] = *p;
        }
        if (o + 1 < cap) out[o++] = '"';
    }
    if (o + 1 < cap) out[o++] = '}';
    out[o < cap ? o : cap - 1] = 0;
}

static void native_request_headers(ASTNode *arg) {
    (void)arg;
    char buf[8192]; te_kv_to_json(g_req_headers, buf, sizeof(buf));
    te_set_ret_string(buf);
}
static void native_request_queries(ASTNode *arg) {
    (void)arg;
    char buf[4096]; te_kv_to_json(g_req_query, buf, sizeof(buf));
    te_set_ret_string(buf);
}
static void native_request_params_all(ASTNode *arg) {
    (void)arg;
    char buf[2048]; te_kv_to_json(g_req_params, buf, sizeof(buf));
    te_set_ret_string(buf);
}

static void native_response_status(ASTNode *arg) { g_resp_status = te_arg_int(arg, 200); te_set_ret_int(g_resp_status); }
static void native_response_header(ASTNode *arg) {
    const char *k = te_arg_string(arg);
    ASTNode *vnode = (arg && arg->next) ? arg->next : NULL; /* gotcha #1: 2nd arg via ->next */
    const char *v = te_arg_string(vnode);
    /* gotcha #14: si el valor no es STRING/IDENTIFIER literal (p.ej. un
     * concat(...) inline, una ADD o un ACCESS_ATTR), te_arg_string devuelve
     * NULL y la cabecera salía vacía. get_node_string evalúa esos nodos. */
    char *vheap = NULL;
    if (!v && vnode) { vheap = get_node_string(vnode); v = vheap; }
    if (k) te_kv_add(&g_resp_headers, k, v ? v : "");
    if (vheap) free(vheap);
    te_set_ret_int(0);
}

/* ===== File-download builtins ============================================
 * Permiten que un endpoint devuelva un archivo binario (Excel / PDF). Los
 * bytes se generan en memoria y se entregan al servidor por el canal binario
 * (typeeasy_http_set_response_bytes); también se fija Content-Disposition:
 * attachment para que el navegador descargue con el nombre dado.
 *
 *   xlsx_download(csvString [, "archivo.xlsx"])
 *   pdf_download(textoConSaltosDeLinea [, "archivo.pdf"])
 *   response_file("/ruta/al/archivo" [, "nombre-descarga.ext"])
 *
 * El handler luego simplemente hace `return "";` (o cualquier string); el
 * servidor ignora ese cuerpo textual cuando hay un cuerpo binario pendiente.
 * ========================================================================= */

/* Fija Content-Disposition: attachment; filename="..." si filename no es NULL. */
static void te_set_attachment_header(const char *filename) {
    if (!filename || !*filename) return;
    char hv[512];
    /* filename sin comillas/CR/LF para no romper la cabecera */
    char safe[256]; size_t j = 0;
    for (const char *p = filename; *p && j < sizeof(safe) - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\r' || c == '\n' || c < 0x20) continue;
        safe[j++] = (char)c;
    }
    safe[j] = '\0';
    snprintf(hv, sizeof hv, "attachment; filename=\"%s\"", safe);
    te_kv_add(&g_resp_headers, "Content-Disposition", hv);
}

static void native_xlsx_download(ASTNode *arg) {
    char *csv = arg ? get_node_string(arg) : NULL;
    char *fname = (arg && arg->next) ? get_node_string(arg->next) : NULL;
    size_t len = 0;
    char *xlsx = te_xlsx_from_csv_buf(csv ? csv : "", csv ? strlen(csv) : 0, &len);
    if (xlsx) {
        typeeasy_http_set_response_bytes(xlsx, len,
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
        te_set_attachment_header((fname && *fname) ? fname : "download.xlsx");
        free(xlsx);
    } else {
        g_resp_status = 500;
    }
    if (csv) free(csv);
    if (fname) free(fname);
    te_set_ret_string("");
}

static void native_pdf_download(ASTNode *arg) {
    char *text = arg ? get_node_string(arg) : NULL;
    char *fname = (arg && arg->next) ? get_node_string(arg->next) : NULL;
    size_t len = 0;
    char *pdf = te_pdf_from_text(text ? text : "", NULL, &len);
    if (pdf) {
        typeeasy_http_set_response_bytes(pdf, len, "application/pdf");
        te_set_attachment_header((fname && *fname) ? fname : "download.pdf");
        free(pdf);
    } else {
        g_resp_status = 500;
    }
    if (text) free(text);
    if (fname) free(fname);
    te_set_ret_string("");
}

/* Adivina el MIME por extensión del nombre/ruta. */
static int te_path_iendswith(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return 0;
    const char *p = s + (ls - lf);
    for (size_t i = 0; i < lf; i++) {
        char a = p[i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}
static const char *te_guess_mime(const char *path) {
    if (!path) return "application/octet-stream";
    if (te_path_iendswith(path, ".xlsx") || te_path_iendswith(path, ".xlsm"))
        return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (te_path_iendswith(path, ".pdf"))  return "application/pdf";
    if (te_path_iendswith(path, ".csv"))  return "text/csv; charset=utf-8";
    if (te_path_iendswith(path, ".json")) return "application/json";
    if (te_path_iendswith(path, ".txt"))  return "text/plain; charset=utf-8";
    if (te_path_iendswith(path, ".png"))  return "image/png";
    if (te_path_iendswith(path, ".jpg") || te_path_iendswith(path, ".jpeg")) return "image/jpeg";
    if (te_path_iendswith(path, ".zip"))  return "application/zip";
    return "application/octet-stream";
}

static void native_response_file(ASTNode *arg) {
    char *path  = arg ? get_node_string(arg) : NULL;
    char *fname = (arg && arg->next) ? get_node_string(arg->next) : NULL;
    if (!path || !*path) { g_resp_status = 404; if (path) free(path); if (fname) free(fname); te_set_ret_string(""); return; }
    FILE *f = fopen(path, "rb");
    if (!f) { g_resp_status = 404; free(path); if (fname) free(fname); te_set_ret_string(""); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    if (sz > (64L << 20)) { /* 64 MiB cap */ fclose(f); g_resp_status = 413; free(path); if (fname) free(fname); te_set_ret_string(""); return; }
    char *buf = (char*)malloc((size_t)sz ? (size_t)sz : 1);
    size_t rd = buf ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (!buf) { g_resp_status = 500; free(path); if (fname) free(fname); te_set_ret_string(""); return; }
    typeeasy_http_set_response_bytes(buf, rd, te_guess_mime(path));
    if (fname && *fname) te_set_attachment_header(fname);
    free(buf); free(path); if (fname) free(fname);
    te_set_ret_string("");
}

/* ===== WebSocket builtins (impl in api_server/te_websocket.c) ===== */
extern int te_ws_subscribe_current(const char *channel);
extern int te_ws_send_current(const char *msg);
extern int te_ws_broadcast(const char *channel, const char *msg);
extern int te_ws_current_id_str(char *out, int cap);

static void native_ws_subscribe(ASTNode *arg) {
    const char *ch = te_arg_string(arg);
    int rc = ch ? te_ws_subscribe_current(ch) : 0;
    te_set_ret_int(rc);
}
static void native_ws_send(ASTNode *arg) {
    const char *m = te_arg_string(arg);
    int rc = m ? te_ws_send_current(m) : 0;
    te_set_ret_int(rc);
}
static void native_ws_broadcast(ASTNode *arg) {
    const char *ch = te_arg_string(arg);
    const char *m  = arg && arg->next ? te_arg_string(arg->next) : NULL; /* gotcha #1: 2nd arg via ->next */
    int rc = (ch && m) ? te_ws_broadcast(ch, m) : 0;
    te_set_ret_int(rc);
}
static void native_request_ws_id(ASTNode *arg) {
    (void)arg;
    char buf[32]; te_ws_current_id_str(buf, sizeof(buf));
    te_set_ret_string(buf);
}

/* ===== Generic SQL facade (sql_connect / sql_query / sql_close) ============
 * Estos son builtins NUEVOS que delegan en los conectores existentes segun un
 * argumento extra `engine` al final. NO modifican los conectores especificos
 * (mysql, postgres, sqlserver, sqlite): esos siguen funcionando exactamente
 * igual y se pueden seguir usando directo.
 *
 *   let c = sql_connect(host, user, pass, db, port, { tls, tls_fp }, "mysql");
 *   let r = sql_query(c, "SELECT 1", "mysql");
 *   sql_close(c, "mysql");
 *
 * engine: mysql|mariadb , postgres|postgresql|pg , sqlserver|mssql ,
 * sqlite|sqlite3. El selector `engine` se DESACOPLA de la cadena de argumentos
 * antes de delegar, de modo que el conector subyacente recibe exactamente los
 * mismos argumentos posicionales que ya esperaba (host..opts ; conn,sql ; conn).
 *
 * SQLite es un PLUGIN (se registra en el hash table como sqlite_connect,
 * sqlite_query, sqlite_close), no un simbolo enlazado: se delega via
 * te_builtin_lookup(). Para sqlite, la "base de datos" (arg #4, indice 3) es la
 * ruta del archivo .db; el resto de argumentos (host/user/pass/port/opts) se
 * ignoran. */
typedef enum { TE_SQL_UNKNOWN, TE_SQL_MYSQL, TE_SQL_PG, TE_SQL_MSSQL, TE_SQL_SQLITE } TeSqlEngine;

static TeSqlEngine te_sql_engine_parse(const char *s) {
    if (!s) return TE_SQL_UNKNOWN;
    char b[24]; size_t j = 0;
    for (; s[j] && j < sizeof(b) - 1; j++) { char c = s[j]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); b[j] = c; }
    b[j] = '\0';
    if (!strcmp(b, "mysql") || !strcmp(b, "mariadb")) return TE_SQL_MYSQL;
    if (!strcmp(b, "postgres") || !strcmp(b, "postgresql") || !strcmp(b, "pg")) return TE_SQL_PG;
    if (!strcmp(b, "sqlserver") || !strcmp(b, "mssql") || !strcmp(b, "sql_server")) return TE_SQL_MSSQL;
    if (!strcmp(b, "sqlite") || !strcmp(b, "sqlite3")) return TE_SQL_SQLITE;
    return TE_SQL_UNKNOWN;
}

/* Devuelve el nodo de argumento en la posición `n` (0-based) recorriendo la
 * cadena lineal (sucesor = ->next o, si falta, ->right). */
static ASTNode *te_arg_at(ASTNode *arg, int n) {
    int i = 0;
    for (ASTNode *c = arg; c; c = c->next ? c->next : c->right) { if (i == n) return c; i++; }
    return NULL;
}

/* Invoca un builtin registrado en el hash table (p.ej. los del plugin sqlite).
 * Devuelve 1 si se ejecutó, 0 si no estaba registrado (plugin no cargado). */
static int te_call_registry(const char *name, ASTNode *args) {
    TEBuiltinFn fn = te_builtin_lookup(name);
    if (!fn) { fprintf(stderr, "[sql] '%s' not available (is the sqlite plugin loaded?)\n", name); return 0; }
    ASTNode self = (ASTNode){0}; self.id = (char*)name;
    fn(&self, args);
    return 1;
}

/* Strict-errors hook para el plugin SQLite: el plugin (cargado dinamicamente)
 * no enlaza contra typeeasy_http_set_status, asi que el strict-mode se aplica
 * AQUI, en la fachada generica, INSPECCIONANDO el __ret__ que dejo el plugin.
 * Si contiene un objeto JSON con "error" y estamos en modo --api con el flag
 * encendido, fijamos response_status(500). MySQL/Postgres/SQL Server lo hacen
 * en su bridge nativo; SQLite lo hace aqui despues de la delegacion. */
static void te_sql_strict_check_ret(void) {
    extern int g_api_mode;
    extern int g_db_strict_errors;
    if (!g_db_strict_errors || !g_api_mode) return;
    Variable *r = find_variable("__ret__");
    if (!r || r->vtype != VAL_STRING || !r->value.string_value) return;
    const char *s = r->value.string_value;
    /* Heuristica conservadora: el plugin emite '{"error":"..."}'. Buscamos
     * '"error"' con comillas para no falsear sobre claves tipo "errors". */
    if (strstr(s, "\"error\"")) typeeasy_http_set_status(500);
}

/* Estandar opt-in (sql_set_envelope(true) / env TYPEEASY_SQL_ENVELOPE=1):
 * envuelve el resultado CRUDO de cualquier motor con un campo booleano `success`
 * uniforme, SIN reestructurar el payload (el resultado se queda IGUAL bajo
 * `data`):
 *   OK     -> { success:true,  data:<resultado actual: [...] | {affected_rows} | N> }
 *   fallo  -> { success:false, error:"..." }
 * Asi `r.success` es siempre un bool real y `r.error` siempre accesible, con el
 * mismo formato en SQLite/MySQL/PostgreSQL/SQL Server. OFF por defecto: no rompe
 * el codigo existente. Se ejecuta en la fachada (un solo punto) sobre el __ret__
 * que dejo el bridge/plugin.
 * `force`: -1 = usar el flag global g_db_envelope; 0 = forzar OFF; 1 = forzar ON.
 * El override por-llamada (5to arg bool de sql_query/sql_exec) gana sobre el
 * flag global de la app. */
/* Forward decl: te_invalidate_map_cache se define mas abajo (~L4196) pero se usa
 * aqui en el path de error del envelope; sin este prototipo clang asume retorno
 * int -> "conflicting types" (error duro, no solo warning como en gcc). */
void te_invalidate_map_cache(ASTNode *root);
static void te_sql_envelope_wrap(int force) {
    extern int g_db_envelope;
    int on = (force < 0) ? g_db_envelope : (force ? 1 : 0);
    if (!on) return;
    Variable *r = find_variable("__ret__");
    if (!r) return;

    ASTNode *env = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!env) return;
    env->type = strdup("OBJECT_LITERAL");
    ASTNode *tail = NULL;
    #define ENV_ADD(p) do { ASTNode *_pp=(p); if(!env->left) env->left=_pp; else tail->right=_pp; tail=_pp; } while(0)
    #define ENV_BOOL(b) create_ast_leaf_number("BOOL", (b)?1:0, NULL, NULL)

    if (r->vtype == VAL_INT) {
        /* SQLite exec OK: __ret__ es int = filas afectadas. */
        ENV_ADD(create_kv_pair_node("success", ENV_BOOL(1)));
        ENV_ADD(create_kv_pair_node("data",
                create_ast_leaf_number("INT", r->value.int_value, NULL, NULL)));
    } else if (r->vtype == VAL_STRING && r->value.string_value) {
        const char *s = r->value.string_value;
        const char *t = s;
        while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r') t++;
        if (strncmp(t, "{\"error\"", 8) == 0) {
            /* Fallo: extraemos el MENSAJE de {"error":"..."} para `error`. */
            const char *pp = t;
            ASTNode *inner = te_json_parse_value(&pp);
            ASTNode *ep = inner ? map_find_pair(inner, "error") : NULL;
            char *msg = (ep && ep->left) ? get_node_string(ep->left) : strdup(s);
            ENV_ADD(create_kv_pair_node("success", ENV_BOOL(0)));
            ENV_ADD(create_kv_pair_node("error", create_ast_leaf("STRING", 0, msg ? msg : "", NULL)));
            if (msg) free(msg);
            if (inner) { te_invalidate_map_cache(inner); free_ast(inner); }
        } else {
            /* OK: `data` lleva el STRING crudo tal cual (array de filas
             * '[...]', '{affected_rows,...}', etc.). No lo reestructuramos: el
             * resultado se queda IGUAL, solo envuelto. Para SELECT ya es JSON
             * valido -> `return r.data` lo devuelve directo; si quieres las
             * filas estructuradas usa json_parse(r.data). */
            ENV_ADD(create_kv_pair_node("success", ENV_BOOL(1)));
            ENV_ADD(create_kv_pair_node("data", create_ast_leaf("STRING", 0, s, NULL)));
        }
    } else {
        ENV_ADD(create_kv_pair_node("success", ENV_BOOL(1)));
    }
    #undef ENV_BOOL
    #undef ENV_ADD
    /* __ret__ toma ownership de `env` (te_value_to_variable guarda el ASTNode
     * como object_value para OBJECT_LITERAL); el string viejo se libera dentro
     * de add_or_update_variable. */
    add_or_update_variable("__ret__", env);
}

/* Desacopla el último argumento (selector engine) de la cadena. Devuelve el
 * motor y, vía out-params, lo necesario para re-enlazarlo tras delegar. La
 * cadena de args es lineal (sucesor = ->next o, si falta, ->right), y en modo
 * API los handlers corren bajo un lock global (un solo intérprete a la vez),
 * por lo que mutar/restaurar el enlace dentro de la misma llamada es seguro. */
static TeSqlEngine te_sql_detach_engine(ASTNode *arg, ASTNode **prev_out,
                                        ASTNode **tail_out, int *used_next) {
    *prev_out = NULL; *tail_out = NULL; *used_next = 0;
    if (!arg) return TE_SQL_UNKNOWN;
    ASTNode *prev = NULL, *cur = arg, *next;
    while ((next = cur->next ? cur->next : cur->right)) { prev = cur; cur = next; }
    TeSqlEngine e = te_sql_engine_parse(te_arg_string(cur));
    if (prev) {
        if (prev->next) { *used_next = 1; prev->next = NULL; }
        else            { *used_next = 0; prev->right = NULL; }
    }
    *prev_out = prev; *tail_out = cur;
    return e;
}
static void te_sql_reattach(ASTNode *prev, ASTNode *tail, int used_next) {
    if (!prev || !tail) return;
    if (used_next) prev->next = tail; else prev->right = tail;
}

/* Override opcional del envelope por-llamada: un 5to argumento booleano DESPUES
 * del ENGINE -> sql_query(c, sql, {}, ENGINE, true|false). Gana sobre el flag
 * global. Si el ultimo arg ya es un engine valido, no hay override (compat).
 * Devuelve: -1 = sin override (usar flag global); 0 = forzar OFF; 1 = forzar ON.
 * Cuando hay override, lo DESACOPLA de la cadena (via *prev_out/*tail_out) para
 * que el bridge no lo vea; el caller lo re-engancha con te_sql_reattach. */
static int te_arg_is_truthy(ASTNode *a); /* fwd: definido mas abajo */
static int te_sql_detach_envelope_override(ASTNode *arg, ASTNode **prev_out,
                                           ASTNode **tail_out, int *used_next) {
    *prev_out = NULL; *tail_out = NULL; *used_next = 0;
    if (!arg) return -1;
    ASTNode *prev = NULL, *cur = arg, *next;
    while ((next = cur->next ? cur->next : cur->right)) { prev = cur; cur = next; }
    if (!prev) return -1; /* hace falta al menos engine + override */
    /* Si el ULTIMO arg ya es un engine valido, no hay override (compat). */
    if (te_sql_engine_parse(te_arg_string(cur)) != TE_SQL_UNKNOWN) return -1;
    /* El ultimo no es engine; el PENULTIMO debe serlo para que 'cur' sea el
     * override booleano. */
    if (te_sql_engine_parse(te_arg_string(prev)) == TE_SQL_UNKNOWN) return -1;
    int force = te_arg_is_truthy(cur) ? 1 : 0;
    if (prev->next) { *used_next = 1; prev->next = NULL; }
    else            { *used_next = 0; prev->right = NULL; }
    *prev_out = prev; *tail_out = cur;
    return force;
}

static void native_sql_connect(ASTNode *arg) {
    ASTNode *prev, *tail; int un = 0;
    TeSqlEngine e = te_sql_detach_engine(arg, &prev, &tail, &un);
    switch (e) {
        case TE_SQL_MYSQL: native_mysql_connect(arg);     break;
        case TE_SQL_PG:    native_postgres_connect(arg);  break;
        case TE_SQL_MSSQL: native_sqlserver_connect(arg); break;
        case TE_SQL_SQLITE: /* sqlite_connect(path): la BD es el arg #4 (índice 3). */
            if (!te_call_registry("sqlite_connect", te_arg_at(arg, 3))) te_set_ret_int(-1);
            break;
        default: fprintf(stderr, "[sql_connect] unknown engine (use mysql|postgres|sqlserver|sqlite)\n"); te_set_ret_int(-1); break;
    }
    te_sql_reattach(prev, tail, un);
}
static void native_sql_query(ASTNode *arg) {
    ASTNode *ovp, *ovt; int ovu = 0;
    int force = te_sql_detach_envelope_override(arg, &ovp, &ovt, &ovu);
    ASTNode *prev, *tail; int un = 0;
    TeSqlEngine e = te_sql_detach_engine(arg, &prev, &tail, &un);
    switch (e) {
        case TE_SQL_MYSQL: native_mysql_query(arg);     break;
        case TE_SQL_PG:    native_postgres_query(arg);  break;
        case TE_SQL_MSSQL: native_sqlserver_query(arg); break;
        case TE_SQL_SQLITE: if (!te_call_registry("sqlite_query", arg)) te_set_ret_string(""); else te_sql_strict_check_ret(); break;
        default: fprintf(stderr, "[sql_query] unknown engine\n"); te_set_ret_string(""); break;
    }
    te_sql_reattach(prev, tail, un);
    te_sql_reattach(ovp, ovt, ovu);
    te_sql_envelope_wrap(force);   /* opt-in: { success, data | error }; override por-llamada gana */
}
/* sql_exec: para DML/DDL. En MySQL/PostgreSQL/SQL Server no hay un exec aparte
 * (su *_query ya ejecuta INSERT/UPDATE/DDL), así que delega ahí; en SQLite usa
 * el sqlite_exec del plugin (distinto de sqlite_query, que es solo SELECT). */
static void native_sql_exec(ASTNode *arg) {
    ASTNode *ovp, *ovt; int ovu = 0;
    int force = te_sql_detach_envelope_override(arg, &ovp, &ovt, &ovu);
    ASTNode *prev, *tail; int un = 0;
    TeSqlEngine e = te_sql_detach_engine(arg, &prev, &tail, &un);
    switch (e) {
        case TE_SQL_MYSQL: native_mysql_query(arg);     break;
        case TE_SQL_PG:    native_postgres_query(arg);  break;
        case TE_SQL_MSSQL: native_sqlserver_query(arg); break;
        case TE_SQL_SQLITE: if (!te_call_registry("sqlite_exec", arg)) te_set_ret_int(-1); else te_sql_strict_check_ret(); break;
        default: fprintf(stderr, "[sql_exec] unknown engine\n"); te_set_ret_int(-1); break;
    }
    te_sql_reattach(prev, tail, un);
    te_sql_reattach(ovp, ovt, ovu);
    te_sql_envelope_wrap(force);   /* opt-in: { success, data | error }; override por-llamada gana */
}
static void native_sql_close(ASTNode *arg) {
    ASTNode *prev, *tail; int un = 0;
    TeSqlEngine e = te_sql_detach_engine(arg, &prev, &tail, &un);
    switch (e) {
        case TE_SQL_MYSQL: native_mysql_close(arg);     break;
        case TE_SQL_PG:    native_postgres_close(arg);  break;
        case TE_SQL_MSSQL: native_sqlserver_close(arg); break;
        case TE_SQL_SQLITE: te_call_registry("sqlite_close", arg); break;
        default: te_set_ret_int(0); break;
    }
    te_sql_reattach(prev, tail, un);
}

/* === Flags opt-in del binder/errores ======================================
 * Estos builtins cambian flags globales en db_params.c que ajustan el
 * comportamiento de los conectores SQL existentes SIN romper apps que ya
 * funcionan (los flags arrancan en 0 = comportamiento legacy):
 *
 *   sql_set_empty_as_null(true)   - STRING vacio ""  -> SQL NULL en INSERT/
 *                                    UPDATE (consistente con ACCESS_EXPR).
 *                                    Elimina la ceremonia COALESCE(NULLIF(...)).
 *                                    Aplica SIEMPRE (CLI y --api): solo afecta
 *                                    como se construye el SQL.
 *
 *   sql_set_strict_errors(true)   - SOLO MODO --api: un fallo de query fija
 *                                    response_status(500) automaticamente para
 *                                    evitar el 'falso exito' 200 OK cuando el
 *                                    handler no inspecciona el {"error":...}.
 *                                    En CLI es no-op (no hay respuesta HTTP
 *                                    que cambiar); el script ve el mismo string
 *                                    de error que ya recibia y decide que hacer.
 *
 * Idealmente se llaman una sola vez al arranque del script. Ambos aceptan
 * un valor truthy (NUMBER != 0, STRING "1"/"true"/"yes") y devuelven el
 * estado nuevo del flag (0/1). */
static int te_arg_is_truthy(ASTNode *a) {
    if (!a || !a->type) return 0;
    if (strcmp(a->type, "NUMBER") == 0 || strcmp(a->type, "INT") == 0) return a->value != 0;
    if (strcmp(a->type, "BOOL") == 0) return a->value != 0;
    if (strcmp(a->type, "STRING") == 0) {
        const char *s = a->str_value ? a->str_value : "";
        /* te_ascii_casecmp esta definido arriba en este mismo archivo; usar
         * strcasecmp() de <strings.h> aqui rompe el build en Windows/MSVC
         * (alli es _stricmp). En MinGW funciona pero preferimos el helper
         * propio para no depender de POSIX en ninguna ruta de build. */
        return !strcmp(s, "1") || !te_ascii_casecmp(s, "true") || !te_ascii_casecmp(s, "yes") || !te_ascii_casecmp(s, "on");
    }
    if (strcmp(a->type, "IDENTIFIER") == 0 || strcmp(a->type, "ID") == 0) {
        Variable *v = a->id ? find_variable(a->id) : NULL;
        if (!v) return 0;
        if (v->vtype == VAL_INT) return v->value.int_value != 0;
        if (v->vtype == VAL_STRING) {
            const char *s = v->value.string_value ? v->value.string_value : "";
            return !strcmp(s, "1") || !te_ascii_casecmp(s, "true") || !te_ascii_casecmp(s, "yes") || !te_ascii_casecmp(s, "on");
        }
    }
    return 0;
}
extern int g_db_empty_as_null;
extern int g_db_strict_errors;
static void native_sql_set_empty_as_null(ASTNode *arg) {
    g_db_empty_as_null = te_arg_is_truthy(arg);
    te_set_ret_int(g_db_empty_as_null);
}
static void native_sql_set_strict_errors(ASTNode *arg) {
    g_db_strict_errors = te_arg_is_truthy(arg);
    te_set_ret_int(g_db_strict_errors);
}
static void native_sql_set_envelope(ASTNode *arg) {
    extern int g_db_envelope;
    g_db_envelope = te_arg_is_truthy(arg);
    te_set_ret_int(g_db_envelope);
}

int call_native_function(const char *name, ASTNode *arg) {
    /* Fase 1: registry first. New builtins live in the hash table only;
     * the legacy if-chain below remains as fallback for transparency. */
    {
        ASTNode tmp = (ASTNode){0};
        tmp.id = (char*)name;
        TEBuiltinFn fn = te_builtin_lookup(name);
        if (fn) return fn(&tmp, arg);
    }
    if (strcmp(name, "orm_query") == 0) {
        native_orm_query(arg);
        return 1;
    }
    // Siempre evaluar argumentos antes de llamada nativa
    if (arg) evaluate_native_args(arg);
    if (strcmp(name, "json") == 0) {
        native_json(arg);
        return 1;
    }
    if (strcmp(name, "xml") == 0) {
        native_xml(arg);
        return 1;
    }
    /* Phase H: HTTP request/response builtins */
    if (strcmp(name, "request_method") == 0) { native_request_method(arg); return 1; }
    if (strcmp(name, "request_path")   == 0) { native_request_path(arg);   return 1; }
    if (strcmp(name, "request_body")   == 0) { native_request_body(arg);   return 1; }
    if (strcmp(name, "request_query")  == 0) { native_request_query(arg);  return 1; }
    if (strcmp(name, "request_header") == 0) { native_request_header(arg); return 1; }
    if (strcmp(name, "request_param")  == 0) { native_request_param(arg);  return 1; }
    if (strcmp(name, "request_cookie") == 0) { native_request_cookie(arg); return 1; }
    if (strcmp(name, "current_claims") == 0) { native_current_claims(arg); return 1; }
    if (strcmp(name, "request_headers")== 0) { native_request_headers(arg);return 1; }
    if (strcmp(name, "request_queries")== 0) { native_request_queries(arg);return 1; }
    if (strcmp(name, "request_params") == 0) { native_request_params_all(arg); return 1; }
    if (strcmp(name, "response_status")== 0) { native_response_status(arg);return 1; }
    if (strcmp(name, "response_header")== 0) { native_response_header(arg);return 1; }
    if (strcmp(name, "xlsx_download")  == 0) { native_xlsx_download(arg);  return 1; }
    if (strcmp(name, "pdf_download")   == 0) { native_pdf_download(arg);   return 1; }
    if (strcmp(name, "response_file")  == 0) { native_response_file(arg);  return 1; }
    if (strcmp(name, "debug_log")      == 0) { native_debug_log(arg);      return 1; }
    if (strcmp(name, "ws_subscribe")   == 0) { native_ws_subscribe(arg);   return 1; }
    if (strcmp(name, "ws_send")        == 0) { native_ws_send(arg);        return 1; }
    if (strcmp(name, "ws_broadcast")   == 0) { native_ws_broadcast(arg);   return 1; }
    if (strcmp(name, "request_ws_id")  == 0) { native_request_ws_id(arg);  return 1; }
    if (strcmp(name, "concat") == 0) {
        native_concat(arg);
        return 1;
    }
    if (strcmp(name, "mysql_connect") == 0) {
        native_mysql_connect(arg);
        return 1;
    }
    if (strcmp(name, "mysql_query") == 0) {
        native_mysql_query(arg);
        return 1;
    }
    if (strcmp(name, "mysql_close") == 0) {
        native_mysql_close(arg);
        return 1;
    }
    if (strcmp(name, "postgres_connect") == 0) { native_postgres_connect(arg); return 1; }
    if (strcmp(name, "postgres_query") == 0)   { native_postgres_query(arg);   return 1; }
    if (strcmp(name, "postgres_close") == 0)   { native_postgres_close(arg);   return 1; }
    if (strcmp(name, "sqlserver_connect") == 0) { native_sqlserver_connect(arg); return 1; }
    if (strcmp(name, "sqlserver_query") == 0)   { native_sqlserver_query(arg);   return 1; }
    if (strcmp(name, "sqlserver_close") == 0)   { native_sqlserver_close(arg);   return 1; }
    /* Facade SQL genérico (delega según el engine final; no altera los de arriba) */
    if (strcmp(name, "sql_connect") == 0) { native_sql_connect(arg); return 1; }
    if (strcmp(name, "sql_query") == 0)   { native_sql_query(arg);   return 1; }
    if (strcmp(name, "sql_exec") == 0)    { native_sql_exec(arg);    return 1; }
    if (strcmp(name, "sql_close") == 0)   { native_sql_close(arg);   return 1; }
    if (strcmp(name, "sql_set_empty_as_null") == 0) { native_sql_set_empty_as_null(arg); return 1; }
    if (strcmp(name, "sql_set_strict_errors") == 0) { native_sql_set_strict_errors(arg); return 1; }
    if (strcmp(name, "sql_set_envelope") == 0) { native_sql_set_envelope(arg); return 1; }
    return 0;
}

#include <stdarg.h>

/* Wrapper that does printf to real stdout AND mirrors the formatted text
 * to append_to_stdout (which also forwards to the VS Code Debug Console
 * via debugger_emit_output when the debugger is attached).
 * Used by interpret_print/println/fprint/fprintln so EVERY output path
 * shows up in the Debug Console — not just the few paths that historically
 * called append_to_stdout. Returns the number of chars formatted (or -1). */
int dbg_printf(const char *fmt, ...) {
    char stackbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof(stackbuf)) {
        if (!g_vm.suppress_stdout) fputs(stackbuf, stdout);
        /* NOTE: capture-buffer (g_stdout_buffer) is filled by callers via
         * explicit append_to_stdout(...) calls in interpret_print/println.
         * dbg_printf must NOT append here or every line is duplicated in
         * __ret__ when json() reads the buffer. */
        if (g_debug_enabled) debugger_emit_output("stdout", stackbuf);
        va_end(ap2);
        return n;
    }
    /* Output too big for stack buffer: allocate. */
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { va_end(ap2); return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    if (!g_vm.suppress_stdout) fputs(buf, stdout);
    if (g_debug_enabled) debugger_emit_output("stdout", buf);
    free(buf);
    return n;
}

/* Same as dbg_printf, but writes to stderr (used by fprint/fprintln). */
static int dbg_eprintf(const char *fmt, ...) {
    char stackbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof(stackbuf)) {
        fputs(stackbuf, stderr);
        if (g_debug_enabled) debugger_emit_output("stderr", stackbuf);
        va_end(ap2);
        return n;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { va_end(ap2); return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    fputs(buf, stderr);
    if (g_debug_enabled) debugger_emit_output("stderr", buf);
    free(buf);
    return n;
}

MethodNode* global_methods = NULL;

ASTNode* create_call_node(const char* funcName, ASTNode* args) {
    //printf("[DEBUG] Entering create_call_node: %s\n", funcName); fflush(stdout);
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) { printf("[DEBUG] malloc failed in create_call_node\n"); fflush(stdout); return NULL; }
    //printf("[DEBUG] malloc success\n"); fflush(stdout);
    node->type = strdup("CALL_FUNC");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
   // printf("[DEBUG] create_call_node success\n"); fflush(stdout);
   // if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
   // } else {
   //     printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

/* Gotcha #2: `make(10)(5)` — llamada sobre el resultado de otra llamada.
 * El callee (que evalúa a un LAMBDA) va en node->right; los argumentos en
 * node->left (igual que CALL_FUNC). Se marca BC_NOT_COMPILABLE para que el
 * compilador de bytecode no intente compilarlo. */
ASTNode* create_call_on_expr_node(ASTNode* callee, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;
    node->type = strdup("CALL_EXPR");
    node->id = NULL;
    node->left = args;
    node->right = callee;
    node->str_value = NULL;
    node->value = 0;
    node->bc = BC_NOT_COMPILABLE;
    return node;
}

ASTNode* create_call_node_return_json(const char* funcName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("RETURN_JSON");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    //if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
    //} else {
     //   printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

ASTNode* create_call_node_return_xml(const char* funcName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("RETURN_XML");
    node->id = strdup(funcName);
    node->left = args;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    //if (args) {
    //    printf("[DEBUG] create_call_node: func=%s, args->type=%s, args->id=%s, args->str_value=%s\n", funcName, args->type ? args->type : "NULL", args->id ? args->id : "NULL", args->str_value ? args->str_value : "NULL");
    //} else {
     //   printf("[DEBUG] create_call_node: func=%s, args=NULL\n", funcName);
    //}
    return node;
}

ASTNode* create_method_call_node_alone(ASTNode* objectNode, const char* methodName, ASTNode* args) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (node) { node->line = yylineno; node->file_id = g_lex_file_id; }
    node->type = strdup("METHOD_CALL_ALONE");
    node->left = objectNode;
    node->right = args;
    node->str_value = NULL;
    node->value = 0;
    if (methodName) {
        node->id = strdup(methodName);
    } else {
        node->id = NULL;
    }
    return node;
}

/* Small helper to standardize TypeEasy logs inside ast.c */
static void te_log_ast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char *green = "\x1b[32m";
    const char *reset = "\x1b[0m";
    printf("%sTypeEasy Agent: ", green);
    vprintf(fmt, ap);
    printf("%s\n", reset);
    va_end(ap);
}

/* Allow runtime debug mode controlled by TYPEEASY_DEBUG env var (set in main) */
extern int g_debug_mode;

/* --- ELIMINADOS: g_runtime y los prototipos del servidor --- */

// ====================== CONSTANTES Y ESTRUCTURAS GLOBALES ======================
/* MAX_VARS is defined canonically in ast.h (shared by every module that
 * touches vars[]). Do NOT redefine it here. */
// Variables globales
/* Variable vars[MAX_VARS];  -> ahora en g_vm (te_vm.h, Fase 3) */
/* Registro global de clases. Antes era un array FIJO `ClassNode *classes[50]`:
 * al pasar de 50 clases, add_class descartaba las nuevas en silencio ->
 * find_class devolvia NULL -> el model binding (body : Clase) deserializaba a
 * un objeto VACIO sin error 422. En ERPs con muchas clases importadas, las
 * registradas "tarde" (#51+) perdian el binding. Ahora es un array DINAMICO que
 * crece con realloc en add_class: sin limite practico de clases. */
ClassNode **classes = NULL;
int classes_cap = 0;
Variable __ret_var; // Global variable for return values
int __ret_var_active = 0; // Flag to indicate if __ret_var holds a valid value
/* int var_count = 0;  -> ahora en g_vm (te_vm.h, Fase 3) */
int class_count = 0;

// Estado de retorno
/* int return_flag = 0;  -> ahora en g_vm (te_vm.h, Fase 3) */

/* static ASTNode *return_node = NULL;  -> ahora en g_vm (te_vm.h, Fase 3) */

/* Fase 2: throw/try-catch */
/* int throw_flag = 0;  -> ahora en g_vm (te_vm.h, Fase 3) */
char *throw_message = NULL;

const char *get_throw_message(void) { return throw_message; }

/* Fase 4: break/continue */
/* int break_flag = 0;  -> ahora en g_vm (te_vm.h, Fase 3) */
/* int continue_flag = 0;  -> ahora en g_vm (te_vm.h, Fase 3) */

/* === Bloque D: helper to reset all interpreter control-flow flags ===
 * Called by typeeasy_embedded_load_script() between successive global-scope
 * loads. Without this, an aborted script (uncaught throw, stray return at
 * top level, break/continue leaking) leaves a flag set; the next file's
 * interpret_ast() short-circuits immediately and silently drops its body.
 *
 * Exposed via prototype in ast.h. break_flag/continue_flag are file-static
 * here, so an external "extern int break_flag;" reset would not link — that
 * is why the reset must live in this translation unit. */
void te_runtime_reset_flags(void) {
    g_vm.return_flag = 0;
    g_vm.return_node = NULL;
    g_vm.throw_flag = 0;
    if (throw_message) {
        free(throw_message);
        throw_message = NULL;
    }
    g_vm.break_flag = 0;
    g_vm.continue_flag = 0;
}

/* Fase 3a: forward decl */
char* expand_interp_string(const char *raw);

/* Ola 16: forward decl (defined in symtab block below). */
static void te_sym_reset_to(int initial_count);

// --- INICIO MEJORA: Punteros a los manejadores de bridges ---
static BridgeHandlers g_bridge_handlers = {NULL, NULL, NULL};

/* DB connection lifecycle. 0 mientras se carga el script global; 1 una vez que
 * el servidor empieza a despachar requests. Las conexiones abiertas con este
 * flag en 1 son request-scoped: se cierran/devuelven al pool al final de cada
 * request (red de seguridad si el script olvida *_close()). Las abiertas en el
 * load global persisten durante toda la vida del proceso. Leen este flag los
 * bridges (mysql/postgres/sqlserver) y el plugin sqlite vía host->db_request_phase. */
int g_db_request_phase = 0;

void runtime_save_initial_var_count() {
    g_vm.initial_var_count = g_vm.var_count;
    g_db_request_phase = 1;   /* a partir de aquí, toda conexión es request-scoped */
    if (g_debug_mode) te_log_ast("Initial state saved. %d global variables retained.", g_vm.initial_var_count);
}

void runtime_reset_vars_to_initial_state() {
    /* item #7: a fatal error (e.g. call-depth limit hit) longjmp's straight to
     * the request recovery point, skipping the matching te_depth_leave() calls
     * in the invocation wrappers. Reset the depth counter here so the next
     * request starts from a clean slate. */
    g_vm.call_depth = 0;
    te_frames_reset();   /* same reason: te_frame_pop was skipped by the longjmp */
    te_callstack_reset();

    /* Invalidate all cached bytecode whose Instrs hold raw Variable*
     * pointers into vars[]. After this reset, slots are recycled and
     * those cached pointers become stale. Force recompilation on next
     * access by clearing node->bc / m->bc_body. (Defined later in file
     * after BCInfo is declared.) */
    extern void bc_invalidate_all(void);
    bc_invalidate_all();

    // Libera la memoria de todas las variables CREADAS DURANTE LA ÚLTIMA EJECUCIÓN
    // (es decir, todas las variables DESPUÉS de los bridges)
    for (int i = g_vm.initial_var_count; i < g_vm.var_count; i++) {
        if (g_vm.vars[i].id) free(g_vm.vars[i].id);
        if (g_vm.vars[i].type) free(g_vm.vars[i].type);
        if (g_vm.vars[i].vtype == VAL_STRING && g_vm.vars[i].value.string_value) {
            free(g_vm.vars[i].value.string_value);
        }
        /* Wipe the slot so any AST node that cached `&vars[i]` from a
         * previous request will read garbage-free zeros, and code paths
         * that validate against `id != NULL` can detect staleness. */
        memset(&g_vm.vars[i], 0, sizeof(Variable));

        // ¡Importante! Si la variable es un Objeto (como 'intencion')
        // debemos liberar el objeto en sí (que está en 'extra')
        // PERO 'declare_variable'  y 'add_or_update_variable' 
        // copian el puntero, y los 'free_ast'  ya liberan los nodos.
        // No necesitamos liberar 'extra' aquí, solo el contenedor de la variable.
    }

    // Resetea el contador de variables a su estado "limpio"
    g_vm.var_count = g_vm.initial_var_count;
    /* Ola 16: drop hash entries beyond initial state. */
    te_sym_reset_to(g_vm.initial_var_count);

    // También limpia la variable de retorno global
    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: No desactivar para permitir uso en requests subsiguientes
    }

    // Limpiar buffer de stdout
    if (g_vm.stdout_buffer) {
        free(g_vm.stdout_buffer);
        g_vm.stdout_buffer = NULL;
        g_vm.stdout_size = 0;
    }

    /* Auto-cierre de conexiones DB abiertas en este request que el script no
     * cerró (return temprano, throw, o error antes del *_close()). Evita fugas
     * que acumulan conexiones request-tras-request hasta "too many connections"
     * en el servidor. Con el pool MySQL activo, las devuelve al pool para reuso;
     * sin pool, las cierra. Las conexiones globales no se tocan. Los plugins
     * (sqlite, ...) se limpian vía los hooks registrados con el host API. */
    mysql_close_request_conns();
    postgres_close_request_conns();
    sqlserver_close_request_conns();
    te_db_run_request_cleanup_hooks();
}
void runtime_register_bridge_handlers(BridgeHandlers handlers) {
    g_bridge_handlers.handle_chat_bridge = handlers.handle_chat_bridge;
    g_bridge_handlers.handle_nlu_bridge = handlers.handle_nlu_bridge;
    g_bridge_handlers.handle_api_bridge = handlers.handle_api_bridge;
    g_bridge_handlers.handle_gemini_bridge = handlers.handle_gemini_bridge;
}
// --- FIN MEJORA ---


/* --- ELIMINADAS: Las 5 funciones del Agente (webhook_handler, runtime_start_bridges, etc.) --- */
/* --- (Ahora viven en servidor_agent.c) --- */


// ====================== FUNCIONES AUXILIARES ======================

/**
 * Convierte un entero a string dinámico
 */
static char* int_to_string(int x) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", x);
    return strdup(buf);
}

/**
 * Convierte un double a string dinámico
 */
char* double_to_string(double x) {
    char buf[64];
    te_fmt_double(buf, sizeof(buf), x);
    return strdup(buf);
}

/**
 * Obtiene representación string de cualquier ASTNode
 */
/* --- ELIMINADA LA PALABRA 'static' --- */


// ====================== MANEJO DE CLASES Y OBJETOS ======================
// ====================== ENDPOINT CACHE SUPPORT ======================
#include <time.h>
static CachedResponse *cache_head = NULL;

// Helper: compare args for cache key (simple pointer equality for now)
static int args_equal(ASTNode *a, ASTNode *b) {
    return a == b; // TODO: Deep compare if needed
}

CachedResponse *get_cached_response(MethodNode *method, ASTNode *args) {
    time_t now = time(NULL);
    CachedResponse *cur = cache_head;
    while (cur) {
        if (cur->method == method && args_equal(cur->args, args)) {
            if (cur->ttl > 0 && (now - cur->timestamp) < cur->ttl) {
                return cur;
            }
        }
        cur = cur->next;
    }
    return NULL;
}

void set_cached_response(MethodNode *method, ASTNode *args, ASTNode *result) {
    CachedResponse *entry = get_cached_response(method, args);
    if (entry) {
        entry->result = result;
        entry->timestamp = time(NULL);
        entry->ttl = method->cache_ttl;
        return;
    }
    entry = malloc(sizeof(CachedResponse));
    entry->method = method;
    entry->args = args;
    entry->result = result;
    entry->timestamp = time(NULL);
    entry->ttl = method->cache_ttl;
    entry->next = cache_head;
    cache_head = entry;
}

void invalidate_cache(MethodNode *method) {
    CachedResponse *cur = cache_head, *prev = NULL;
    while (cur) {
        if (cur->method == method) {
            if (prev) prev->next = cur->next;
            else cache_head = cur->next;
            free(cur);
            if (prev) cur = prev->next;
            else cur = cache_head;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

ClassNode *create_class(char *name) {
    ClassNode *class_node = (ClassNode *)malloc(sizeof(ClassNode));
    class_node->name = strdup(name);
    class_node->attributes = NULL;
    class_node->attr_count = 0;
    class_node->attr_defaults = NULL;
    class_node->attr_access = NULL;
    class_node->methods = NULL;
    class_node->parent = NULL;
    class_node->next = NULL;
    return class_node;
}

/* Phase E: copy parent attributes + methods into child class.
 * Called from parser action BEFORE class_body, so child's own members
 * (added later via add_method_to_class which prepends) override parent's. */
void inherit_from(ClassNode *child, char *parent_name) {
    if (!child || !parent_name) return;
    ClassNode *parent = find_class(parent_name);
    if (!parent) {
        fprintf(stderr, "Error: cannot extend unknown class '%s'.\n", parent_name);
        return;
    }
    child->parent = parent;
    /* Copy attributes (child gets a deep-copied array). */
    for (int i = 0; i < parent->attr_count; i++) {
        child->attributes = realloc(child->attributes, (child->attr_count + 1) * sizeof(Variable));
        child->attributes[child->attr_count].id = strdup(parent->attributes[i].id ? parent->attributes[i].id : "");
        child->attributes[child->attr_count].type = strdup(parent->attributes[i].type ? parent->attributes[i].type : "");
        child->attributes[child->attr_count].is_const = parent->attributes[i].is_const;
        child->attributes[child->attr_count].vtype = parent->attributes[i].vtype;
        memset(&child->attributes[child->attr_count].value, 0, sizeof(child->attributes[child->attr_count].value));
        /* Inherit default-value expression (AST is read-only, safe to share). */
        child->attr_defaults = realloc(child->attr_defaults, (child->attr_count + 1) * sizeof(ASTNode *));
        child->attr_defaults[child->attr_count] = parent->attr_defaults ? parent->attr_defaults[i] : NULL;
        /* Inherit access modifier. */
        child->attr_access = realloc(child->attr_access, (child->attr_count + 1) * sizeof(int));
        child->attr_access[child->attr_count] = parent->attr_access ? parent->attr_access[i] : 0;
        child->attr_count++;
    }
    /* Copy methods. Parent's method list is already in some order; we
     * append at the END of the (currently empty) child list, so that
     * subsequent add_method_to_class (which prepends) puts child's own
     * methods FIRST → child overrides win in lookup. */
    MethodNode **tail = &child->methods;
    for (MethodNode *pm = parent->methods; pm; pm = pm->next) {
        MethodNode *cm = (MethodNode*)malloc(sizeof(MethodNode));
        cm->name = strdup(pm->name ? pm->name : "");
        cm->params = pm->params;       /* share params list (read-only) */
        cm->body = pm->body;           /* share body AST (read-only at runtime) */
        cm->route_path = pm->route_path ? strdup(pm->route_path) : NULL;
        cm->http_method = pm->http_method ? strdup(pm->http_method) : NULL;
        cm->cache_ttl = pm->cache_ttl;
        cm->requires_auth = pm->requires_auth;
        cm->guard_name = pm->guard_name ? strdup(pm->guard_name) : NULL;
        cm->return_type = pm->return_type ? strdup(pm->return_type) : NULL;
        cm->bc_body = NULL;            /* don't share bytecode cache */
        cm->next = NULL;
        *tail = cm;
        tail = &cm->next;
    }
}

void add_class(ClassNode *class) {
    if (class_count >= classes_cap) {
        int newcap = classes_cap ? classes_cap * 2 : 64;
        ClassNode **grown = realloc(classes, (size_t)newcap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "[add_class] out of memory while growing the class registry (count=%d)\n", class_count);
            return;
        }
        classes = grown;
        classes_cap = newcap;
    }
    classes[class_count++] = class;
}

ClassNode *find_class(char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i]->name, name) == 0) {
            return classes[i];
        }
    }
    /* Fase 2: silent — `new IDENTIFIER(...)` may be a builtin call, not a
     * class instantiation. The grammar action falls back to create_call_node
     * which surfaces "function not defined" downstream if neither matches. */
    return NULL;
}

void add_attribute_to_class(ClassNode *class, char *attr_name, char *attr_type) {
    if (!class) return;
    
    class->attributes = realloc(class->attributes, (class->attr_count + 1) * sizeof(Variable));
    class->attributes[class->attr_count].id = strdup(attr_name);
    class->attributes[class->attr_count].type = strdup(attr_type);
    /* Keep attr_defaults[] parallel to attributes[]; default is NULL unless
     * the field was declared with an initializer (set_last_attr_default). */
    class->attr_defaults = realloc(class->attr_defaults, (class->attr_count + 1) * sizeof(ASTNode *));
    class->attr_defaults[class->attr_count] = NULL;
    class->attr_access = realloc(class->attr_access, (class->attr_count + 1) * sizeof(int));
    class->attr_access[class->attr_count] = 0; /* public by default */
    class->attr_count++;
}

/* Attach a default-value expression to the most recently added attribute.
 * Called from the parser after add_attribute_to_class for C#-style fields
 * such as `private int _total = 0;`. */
void set_last_attr_default(ClassNode *class, ASTNode *default_expr) {
    if (!class || class->attr_count == 0 || !class->attr_defaults) return;
    class->attr_defaults[class->attr_count - 1] = default_expr;
}

/* Set the access modifier (0=public, 1=private, 2=protected) of the most
 * recently added attribute. Called from the parser for declarations with an
 * explicit access keyword. */
void set_last_attr_access(ClassNode *class, int access) {
    if (!class || class->attr_count == 0 || !class->attr_access) return;
    class->attr_access[class->attr_count - 1] = access;
}

/* Runtime access check for a class attribute.
 * Returns 1 if the access is allowed, 0 (and prints an error) if it is a
 * private-field violation. Private fields may only be touched through `this`
 * (i.e. from inside the class's own methods). `protected` and `public` are
 * always allowed for now. obj_ref is the object expression of the access
 * (its ->id is "this" for internal access). */
int te_attr_access_ok(ClassNode *cls, int idx, ASTNode *obj_ref) {
    if (!cls || idx < 0 || idx >= cls->attr_count) return 1;
    if (!cls->attr_access || cls->attr_access[idx] != 1) return 1; /* not private */
    if (obj_ref && obj_ref->id && strcmp(obj_ref->id, "this") == 0) return 1;
    fprintf(stderr,
            "%sError: attribute '%s' is private and cannot be accessed outside class '%s'.%s\n",
            TE_ERR_RED, cls->attributes[idx].id, cls->name, TE_ERR_RESET);
    return 0;
}

void add_method_to_class(ClassNode *cls, char *method, ParameterNode *params, ASTNode *body, char *return_type) {
    MethodNode *m = malloc(sizeof(MethodNode));
    if (!m) te_oom_fatal("method node");
    m->name = strdup(method);
    m->params = params;
    m->body = body;
    m->route_path = NULL;
    m->http_method = NULL;
    m->cache_ttl = 0;
    m->guard_name = NULL;
    m->return_type = return_type ? strdup(return_type) : NULL;
    m->bc_body = NULL; /* Ola 4: lazy bytecode cache */
    m->next = cls->methods;
    cls->methods = m;
}
ObjectNode* clone_object(ObjectNode *original);

void add_constructor_to_class(ClassNode *class, ParameterNode *params, ASTNode *body) {
    MethodNode *ctor = malloc(sizeof(MethodNode));
    if (!ctor) te_oom_fatal("constructor node");
    ctor->name = strdup("__constructor");
    ctor->params = params;
    ctor->body = body;
    ctor->route_path = NULL;
    ctor->http_method = NULL;
    ctor->cache_ttl = 0;
    ctor->guard_name = NULL;
    ctor->return_type = strdup("void");
    ctor->bc_body = NULL; /* Ola 4: lazy bytecode cache */
    ctor->next = class->methods;
    class->methods = ctor;
}

ObjectNode *create_object(ClassNode *class) {
    ObjectNode *obj = (ObjectNode *)malloc(sizeof(ObjectNode));
    obj->class = class;
    obj->owning_list = NULL;  /* v0.0.13 (perf) */
    obj->attributes = malloc(class->attr_count * sizeof(Variable));
    for (int i = 0; i < class->attr_count; i++) {
        obj->attributes[i].id    = strdup(class->attributes[i].id);
        obj->attributes[i].type  = strdup(class->attributes[i].type);
        /* Sufijo '?' = opcional/nullable: stripear para elegir el vtype por el
         * tipo base. Sin esto `string?`/`int?` no matcheaban ningun strcmp y
         * caian al default int (un `string?` quedaba como INT 0 -> "0"). */
        const char *raw_t = class->attributes[i].type ? class->attributes[i].type : "int";
        size_t tl = strlen(raw_t);
        int optional = (tl > 0 && raw_t[tl - 1] == '?');
        char base_t[32];
        const char *bt = raw_t;
        if (optional) {
            size_t n2 = tl - 1; if (n2 >= sizeof(base_t)) n2 = sizeof(base_t) - 1;
            memcpy(base_t, raw_t, n2); base_t[n2] = '\0'; bt = base_t;
        }
        // Inicialización por defecto (ej. int 0)
        if (strcmp(bt, "string") == 0 ||
            strcmp(bt, "uuid") == 0 ||
            strcmp(bt, "datetime") == 0) {
            /* v1.0.0: uuid/datetime are storage-aliased to STRING. */
            obj->attributes[i].vtype = VAL_STRING;
            obj->attributes[i].value.string_value = strdup("");
        } else if (strcmp(bt, "float") == 0) {
            obj->attributes[i].vtype = VAL_FLOAT;
            obj->attributes[i].value.float_value = 0.0;
        } else {
            obj->attributes[i].vtype = VAL_INT;
            obj->attributes[i].value.int_value = 0;
        }
        /* Campo opcional ('?') SIN default declarado: arranca como NULL de
         * runtime (VAL_OBJECT con puntero NULL), de modo que si el body POST
         * no trae la clave, el binder de @params lo inserta como SQL NULL en
         * vez de 0/'' (que rompe columnas tipadas bajo STRICT). Un default
         * explicito (abajo) lo sobreescribe. */
        if (optional && !(class->attr_defaults && class->attr_defaults[i])) {
            if (obj->attributes[i].vtype == VAL_STRING && obj->attributes[i].value.string_value)
                free(obj->attributes[i].value.string_value);
            obj->attributes[i].vtype = VAL_OBJECT;
            obj->attributes[i].value.object_value = NULL;
        }
        /* Apply declared default value (C#-style field initializer), e.g.
         * `private int _total = 0;` or `public string name = "";`. Runs
         * before the constructor, which may still override it. */
        if (class->attr_defaults && class->attr_defaults[i]) {
            ASTNode *d = class->attr_defaults[i];
            if (obj->attributes[i].vtype == VAL_STRING) {
                if (obj->attributes[i].value.string_value) free(obj->attributes[i].value.string_value);
                obj->attributes[i].value.string_value = strdup(d->str_value ? d->str_value : "");
            } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                obj->attributes[i].value.float_value = evaluate_expression(d);
            } else {
                obj->attributes[i].value.int_value = (long long)evaluate_expression(d);
            }
        }
    }
    
    return obj;
}

// ====================== MANEJO DE VARIABLES ======================

/* Forward decl: defined below in Ola 14 helpers block. */
static uint64_t te_str_hash(const char *s);

/* ============================================================
 * Ola 16 — Hash-indexed symbol table.
 *   Side-index over `vars[]` mapping (FNV-1a hash, key) -> index.
 *   Variable identifier strings are NOT moved; we just index them.
 *   When a string is interned, the hash slot's `key` will point to
 *   the interned copy (immortal), enabling pointer-eq fast paths.
 * ============================================================*/
/* TESymSlot: ahora en te_vm.h */


static inline void te_sym_clear(void) {
    for (int i = 0; i < TE_SYM_CAP; i++) {
        g_vm.sym_slots[i].key = NULL;
        g_vm.sym_slots[i].hash = 0;
        g_vm.sym_slots[i].idx = -1;
    }
    g_vm.sym_init = 1;
}

static inline int te_sym_lookup(const char *id) {
    if (!g_vm.sym_init) return -1;
    if (!id) return -1;
    uint64_t h = te_str_hash(id);
    int mask = TE_SYM_CAP - 1;
    int i = (int)(h & (uint64_t)mask);
    for (;;) {
        const char *sk = g_vm.sym_slots[i].key;
        if (sk == NULL) return -1;
        if (sk == id) return g_vm.sym_slots[i].idx;          /* ptr-eq */
        if (g_vm.sym_slots[i].hash == h && strcmp(sk, id) == 0) return g_vm.sym_slots[i].idx;
        i = (i + 1) & mask;
    }
}

void te_sym_insert(const char *id, int idx) {
    if (!g_vm.sym_init) te_sym_clear();
    if (!id) return;
    uint64_t h = te_str_hash(id);
    int mask = TE_SYM_CAP - 1;
    int i = (int)(h & (uint64_t)mask);
    for (;;) {
        const char *sk = g_vm.sym_slots[i].key;
        if (sk == NULL) {
            g_vm.sym_slots[i].key = id;
            g_vm.sym_slots[i].hash = h;
            g_vm.sym_slots[i].idx = idx;
            return;
        }
        if (sk == id || (g_vm.sym_slots[i].hash == h && strcmp(sk, id) == 0)) {
            g_vm.sym_slots[i].idx = idx;  /* update */
            g_vm.sym_slots[i].key = id;
            return;
        }
        i = (i + 1) & mask;
    }
}

/* Drop all entries whose idx >= initial_count and rebuild from the
 * remaining vars[]. Called from runtime_reset_vars_to_initial_state. */
static void te_sym_reset_to(int initial_count) {
    te_sym_clear();
    for (int i = 0; i < initial_count && i < MAX_VARS; i++) {
        if (g_vm.vars[i].id) te_sym_insert(g_vm.vars[i].id, i);
    }
}

/* Block-scope unwind: free and drop every variable slot at index >= target,
 * restoring var_count and the name->index side-index to the pre-block state.
 * Used by the loop interpreters so a `let` declared inside a loop body reuses
 * the same slot every iteration instead of leaking a fresh slot per iteration.
 * Before this, each iteration's `let` appended a new slot that was never
 * reclaimed; once var_count hit MAX_VARS the interpreter printed "too many
 * declared variables" and then SILENTLY dropped every declaration that followed
 * the loop (the request still returned 200 with a half-built body). Cheap
 * no-op when the iteration declared nothing. */
void te_scope_unwind_to(int target) {
    if (target < 0) target = 0;
    if (target >= g_vm.var_count) return;            /* nothing new this iteration */
    for (int i = target; i < g_vm.var_count; i++) {
        if (g_vm.vars[i].id) free(g_vm.vars[i].id);
        if (g_vm.vars[i].type) free(g_vm.vars[i].type);
        if (g_vm.vars[i].vtype == VAL_STRING && g_vm.vars[i].value.string_value)
            free(g_vm.vars[i].value.string_value);
        memset(&g_vm.vars[i], 0, sizeof(Variable));
    }
    g_vm.var_count = target;
    te_sym_reset_to(target);
}

/* Rebuild the variable name->index side-index from the live vars[0..var_count).
 * Exported for the async event loop (te_evloop.c): when it swaps the active
 * variable set between fibers it rewrites vars[] in place, so the hash keys
 * (which alias vars[idx].id) must be rebuilt to stay correct. */
void te_runtime_rebuild_symtab(void) {
    int n = g_vm.var_count;
    if (n < 0) n = 0;
    if (n > MAX_VARS) n = MAX_VARS;
    te_sym_reset_to(n);
}

/* ============================================================================
 * Cooperative request yield (Option 1a) — transparent multi-user concurrency.
 *
 * Under the API server every handler runs while holding a single global invoke
 * lock, so only ONE interpreter ever runs at a time (this keeps the shared AST
 * value caches and DB connections race-free). When a handler parks in an
 * `await` waiting on I/O it would otherwise keep the lock and block every other
 * request for the whole wait. te_coop_yield_begin() snapshots this request's
 * mutable interpreter state, releases the lock so another request may run, and
 * returns an opaque stash; te_coop_yield_end() re-acquires the lock and
 * restores the state. The interpreter never runs on two threads at once, so
 * single-request behavior is byte-for-byte identical to the serialized model —
 * only the *waiting* now overlaps across requests.
 *
 * Only the per-request variable slice vars[g_initial_var_count..var_count) is
 * saved; module-level globals vars[0..g_initial_var_count) are shared and left
 * in place, matching how handlers already treat them. cached_var/cached_class
 * on shared AST nodes stay valid because they store STABLE slot addresses and
 * are revalidated by id on read; the table is restored into the same slots.
 * ============================================================================ */
typedef struct TeReqState {
    Variable *slice;          /* deep copy of g_vm.vars[g_initial..var_count) */
    int       slice_n;
    Variable  ret;            /* __ret_var (owned) */
    int       ret_active;
    int       return_flag, throw_flag, call_depth;
    void     *frames;         /* g_vm.frame_top of the yielded request */
    jmp_buf  *recovery;
    char     *claims;         /* g_current_claims (owned) */
    /* http context (ownership moved out of the globals) */
    char *req_method, *req_path, *req_body;
    size_t req_body_len;
    TeKV *req_query, *req_headers, *req_params, *resp_headers;
    int   resp_status, raw_text;
    char *resp_body, *resp_content_type;   /* binary download channel (owned) */
    size_t resp_body_len;
} TeReqState;

static void te_var_free_owned(Variable *v) {
    if (v->id)   { free(v->id);   v->id = NULL; }
    if (v->type) { free(v->type); v->type = NULL; }
    if (v->vtype == VAL_STRING && v->value.string_value) {
        free(v->value.string_value); v->value.string_value = NULL;
    }
}

/* Steal this request's interpreter state into a heap stash and reset the live
 * globals to a clean module-globals baseline so the next request starts fresh.
 * Returns an opaque pointer consumed by te_reqstate_restore().
 *
 * MOVE semantics (not deep-copy): the stash takes ownership of the live slots'
 * heap pointers and the live slots are zeroed WITHOUT freeing. This is what
 * keeps the swap transparent — any raw `char*` an interpreter frame borrowed
 * from a variable stays valid across the yield because the string is never
 * freed here, only relocated into the stash and moved back on restore. */
void *te_reqstate_save(void) {
    TeReqState *s = (TeReqState *)calloc(1, sizeof(TeReqState));
    if (!s) return NULL;
    int base = g_vm.initial_var_count;
    if (base < 0) base = 0;
    if (base > MAX_VARS) base = MAX_VARS;
    int top = g_vm.var_count;
    if (top < base) top = base;
    if (top > MAX_VARS) top = MAX_VARS;
    int n = top - base;
    s->slice_n = n;
    if (n > 0) {
        s->slice = (Variable *)malloc((size_t)n * sizeof(Variable));
        memcpy(s->slice, &g_vm.vars[base], (size_t)n * sizeof(Variable)); /* shallow: ownership moves to stash */
    }
    for (int i = base; i < top; i++) memset(&g_vm.vars[i], 0, sizeof(Variable)); /* zero, do NOT free */
    g_vm.var_count = base;
    te_runtime_rebuild_symtab();

    s->ret        = __ret_var;          /* ownership moves */
    s->ret_active = __ret_var_active;
    memset(&__ret_var, 0, sizeof(Variable));
    __ret_var_active = 0;

    s->return_flag = g_vm.return_flag;
    s->throw_flag  = g_vm.throw_flag;
    s->call_depth  = g_vm.call_depth;
    s->frames      = te_frames_save();
    s->recovery    = g_vm.runtime_recovery;
    g_vm.return_flag = 0; g_vm.throw_flag = 0; g_vm.call_depth = 0;

    s->claims = g_current_claims;       /* ownership moves */
    g_current_claims = NULL;

    s->req_method = g_req_method; s->req_path = g_req_path; s->req_body = g_req_body;
    s->req_body_len = g_req_body_len;
    s->req_query  = g_req_query;  s->req_headers = g_req_headers;
    s->req_params = g_req_params; s->resp_headers = g_resp_headers;
    s->resp_status = g_resp_status; s->raw_text = g_response_is_raw_text;
    g_req_method = g_req_path = g_req_body = NULL;
    g_req_body_len = 0;
    g_req_query = g_req_headers = g_req_params = g_resp_headers = NULL;
    g_resp_status = 200; g_response_is_raw_text = 0;
    s->resp_body = g_resp_body; s->resp_body_len = g_resp_body_len;
    s->resp_content_type = g_resp_content_type;
    g_resp_body = NULL; g_resp_body_len = 0; g_resp_content_type = NULL;
    return s;
}

/* Restore a stash produced by te_reqstate_save(), freeing whatever the current
 * (other request's) leftover state occupies the globals. Ownership of the
 * stash's pointers moves back into the live globals; the stash is freed. */
void te_reqstate_restore(void *st) {
    TeReqState *s = (TeReqState *)st;
    if (!s) return;
    int base = g_vm.initial_var_count;
    if (base < 0) base = 0;
    if (base > MAX_VARS) base = MAX_VARS;
    int top = g_vm.var_count;
    if (top < base) top = base;
    if (top > MAX_VARS) top = MAX_VARS;
    for (int i = base; i < top; i++) { te_var_free_owned(&g_vm.vars[i]); memset(&g_vm.vars[i], 0, sizeof(Variable)); }
    int n = s->slice_n;
    if (base + n > MAX_VARS) n = MAX_VARS - base;
    if (n < 0) n = 0;
    if (n > 0 && s->slice) memcpy(&g_vm.vars[base], s->slice, (size_t)n * sizeof(Variable)); /* ownership moves back */
    g_vm.var_count = base + n;
    free(s->slice);
    te_runtime_rebuild_symtab();

    if (__ret_var_active) te_var_free_owned(&__ret_var);
    __ret_var = s->ret;                 /* ownership moves */
    __ret_var_active = s->ret_active;

    g_vm.return_flag = s->return_flag;
    g_vm.throw_flag  = s->throw_flag;
    g_vm.call_depth = s->call_depth;
    te_frames_restore(s->frames);
    g_vm.runtime_recovery = s->recovery;

    if (g_current_claims) free(g_current_claims);
    g_current_claims = s->claims;       /* ownership moves */

    free(g_req_method); free(g_req_path); free(g_req_body);
    te_kv_free_list(&g_req_query); te_kv_free_list(&g_req_headers);
    te_kv_free_list(&g_req_params); te_kv_free_list(&g_resp_headers);
    g_req_method = s->req_method; g_req_path = s->req_path; g_req_body = s->req_body;
    g_req_body_len = s->req_body_len;
    g_req_query  = s->req_query;  g_req_headers = s->req_headers;
    g_req_params = s->req_params; g_resp_headers = s->resp_headers;
    g_resp_status = s->resp_status; g_response_is_raw_text = s->raw_text;
    free(g_resp_body); free(g_resp_content_type);
    g_resp_body = s->resp_body; g_resp_body_len = s->resp_body_len;
    g_resp_content_type = s->resp_content_type;
    free(s);
}

/* ---- coop lock plumbing (the API server registers its invoke lock here) ----
 * Kept as function pointers so engine units (te_async.c / te_evloop.c) can ask
 * to yield the lock without a hard link dependency on the server: in CLI builds
 * nothing registers, g_te_lock_held stays 0, and the yield is a no-op. */
static void (*g_coop_lock_acq)(void) = NULL;
static void (*g_coop_lock_rel)(void) = NULL;
__thread int g_te_lock_held = 0;   /* set by the server handler while it holds the lock */
__thread int g_current_handler_async = 0; /* 1 while running an `async`-declared handler */

void te_coop_register_lock(void (*acq)(void), void (*rel)(void)) {
    g_coop_lock_acq = acq;
    g_coop_lock_rel = rel;
}

/* Begin a cooperative yield: if we currently hold the server invoke lock, save
 * our request state and release it (letting another request run). Returns an
 * opaque stash to pass to te_coop_yield_end(), or NULL when not under the
 * server (CLI) — in which case the caller just waits normally. */
void *te_coop_yield_begin(void) {
    if (!g_te_lock_held || !g_coop_lock_rel) {
        return NULL;
    }
    /* SAFETY GATE (default OFF): releasing the invoke lock mid-request while a
     * second request mutates the SAME process-global per-request state
     * (g_req_ / g_resp_ lists, vars[] slice, __ret_var) was racing under load
     * and corrupting the heap — confirmed on the Arclad VM by two production
     * core dumps (SIGSEGV in te_kv_free_list/typeeasy_http_reset and in
     * te_expr_is_null/interpret_if). te_reqstate_save/restore only isolates ONE
     * parked request cleanly; with several async requests overlapping the
     * save/restore ownership of the shared globals double-frees / dangles.
     * Until that isolation is reworked to a fully per-request heap context, the
     * cooperative overlap is opt-in. Default: serialize (an async handler keeps
     * the lock for the whole await, exactly like a sync handler) — correct and
     * race-free. Set TYPEEASY_ASYNC_OVERLAP=1 to restore the old overlapping
     * behaviour. */
    static int s_overlap = -1;
    if (s_overlap < 0) {
        const char *e = getenv("TYPEEASY_ASYNC_OVERLAP");
        s_overlap = (e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y' ||
                           e[0] == 't' || e[0] == 'T' || e[0] == 'o' || e[0] == 'O')) ? 1 : 0;
    }
    if (!s_overlap) {
        return NULL;
    }
    /* C#/.NET-style async gate: only an `async`-declared handler releases the
     * invoke lock while parked in an await, letting other requests overlap. A
     * plain (sync) handler keeps the lock for the whole wait, so concurrent
     * requests serialise behind it — exactly like a blocking sync method. */
    if (!g_current_handler_async) {
        return NULL;
    }
    void *st = te_reqstate_save();
    g_te_lock_held = 0;
    g_coop_lock_rel();
    return st;
}

/* End a cooperative yield: re-acquire the lock and restore the saved state.
 * No-op when begin returned NULL. */
void te_coop_yield_end(void *st) {
    if (!st) return;
    if (g_coop_lock_acq) g_coop_lock_acq();
    g_te_lock_held = 1;
    te_reqstate_restore(st);
}

Variable *find_variable(char *id) {    
    if (!id) return NULL;  /* Bug fix: caller paths sometimes pass NULL (e.g. arr[i].attr access where 'o' is ACCESS_EXPR with NULL id). */
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    /* Ola 16: hash side-index. */
    int idx = te_sym_lookup(id);
    if (idx >= 0 && idx < g_vm.var_count) {
        return &g_vm.vars[idx];
    }
    /* Fallback: linear scan (also primes the hash if absent). */
    for (int i = 0; i < g_vm.var_count; i++) {
        if (g_vm.vars[i].id) {           
            if (strcmp(g_vm.vars[i].id, id) == 0) {                
                te_sym_insert(g_vm.vars[i].id, i);
                return &g_vm.vars[i];
            }
        }
    }
    return NULL;
}

ASTNode *create_agent_node(char *name, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Fatal error: could not allocate memory for AGENT node.\n");
        te_runtime_fatal();
    }
    node->type = strdup("AGENT");
    node->id = strdup(name);
    node->left = body; // 'body' es la lista de listeners
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_listener_node(ASTNode *event_expr, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Fatal error: could not allocate memory for LISTENER node.\n");
        te_runtime_fatal();
    }
    node->type = strdup("LISTENER");
    node->left = event_expr; // La expresión p.ej. Chat.onMessage(mensaje)
    node->right = body;     // El bloque de código
    node->id = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

Variable *find_variable_for(char *id) {
    if (strcmp(id, "__ret__") == 0 && __ret_var_active) {
        return &__ret_var;
    }

    /* Ola 16: hash side-index. */
    int idx = te_sym_lookup(id);
    if (idx >= 0 && idx < g_vm.var_count) {
        return &g_vm.vars[idx];
    }
    for (int i = 0; i < g_vm.var_count; i++) {
        if (strcmp(g_vm.vars[i].id, id) == 0) {
            te_sym_insert(g_vm.vars[i].id, i);
            return &g_vm.vars[i];
        }
    }
    return NULL;
}

/* #5: punto único de mapeo de tipos. Forward decl porque declare_variable
 * (abajo) lo usa antes de la definición de te_value_to_variable. */
void te_value_to_variable(Variable *dst, ASTNode *value);

/* ─── Function frames (ERP gotcha #38 / 30c-d) ─────────────────────────────
 * vars[] is one flat array: a `let x` inside a fn body used to overwrite the
 * caller's `x` (param or local) for good, e.g. PosRepo_* declaring `id` wiped
 * the `id` of the EcomRepo_* that called it -> UPDATE ... WHERE id=@id hit 0
 * rows while reporting success. Parameters were already shadowed per call;
 * a TeFrame generalises that: every slot that existed BEFORE the call and is
 * (re)declared or bound during it is saved once and restored on exit. Slots
 * created inside the call are left in place (same as before). Frames live on
 * the C stack of call_lambda_impl and chain through g_frame_top. */
typedef struct { Variable *slot; Variable saved; } ParamShadow;
typedef struct TeFrame {
    ParamShadow *sh;
    int n, cap;
    int base;                 /* g_vm.var_count at call entry */
    struct TeFrame *prev;
} TeFrame;

static void te_frame_push(TeFrame *f) {
    f->sh = NULL; f->n = 0; f->cap = 0;
    f->base = g_vm.var_count;
    f->prev = g_vm.frame_top;
    g_vm.frame_top = f;
}

static void te_frame_shadow_slot(TeFrame *f, Variable *ex) {
    if (!f || !ex || ex == &__ret_var) return;
    for (int i = 0; i < f->n; i++)
        if (f->sh[i].slot == ex) return;   /* already saved in this frame */
    if (f->n >= f->cap) {
        int ncap = f->cap ? f->cap * 2 : 16;
        ParamShadow *g = (ParamShadow *)realloc(f->sh, (size_t)ncap * sizeof(ParamShadow));
        if (!g) return;                   /* OOM: fall back to old (leaky) behaviour */
        f->sh = g; f->cap = ncap;
    }
    ParamShadow *ps = &f->sh[f->n++];
    ps->slot  = ex;
    ps->saved = *ex;                       /* shallow copy of the union + tags */
    /* Deep-copy owned strings: the binder frees the slot's `type` and
     * `string_value` while (re)declaring. */
    ps->saved.type = ex->type ? strdup(ex->type) : NULL;
    if (ex->vtype == VAL_STRING)
        ps->saved.value.string_value =
            ex->value.string_value ? strdup(ex->value.string_value) : NULL;
    ex->is_const = 0;                      /* allow the local to overwrite */
}

static void te_frame_pop(TeFrame *f) {
    for (int k = f->n - 1; k >= 0; k--) {
        Variable *slot = f->sh[k].slot;
        free(slot->type);
        if (slot->vtype == VAL_STRING && slot->value.string_value)
            free(slot->value.string_value);
        /* Restore the outer variable (ownership of saved.type / saved string
         * transfers back to the slot). `id` was never touched. */
        slot->is_const = f->sh[k].saved.is_const;
        slot->vtype    = f->sh[k].saved.vtype;
        slot->type     = f->sh[k].saved.type;
        slot->value    = f->sh[k].saved.value;
    }
    free(f->sh);
    g_vm.frame_top = f->prev;
}

/* Fatal errors longjmp past te_frame_pop; the request recovery path calls this. */
void te_frames_reset(void) { g_vm.frame_top = NULL; }
void *te_frames_save(void)  { void *t = g_vm.frame_top; g_vm.frame_top = NULL; return t; }
void  te_frames_restore(void *t) { g_vm.frame_top = (TeFrame *)t; }

/* Bug ERP 2026-08-27 ("stale return" / Bug A): slot ÚNICO por nombre al
 * re-declarar. Antes cada `let/var x` APPENDEABA un slot nuevo aunque ya
 * existiera otro `x` (p.ej. el body de una fn ejecutado en cada llamada).
 * El hash pasaba a apuntar al slot nuevo, pero los cached_var de los nodos
 * AST (condiciones, fast-path de interpret_assign, bytecode) seguían
 * apuntando al slot VIEJO del mismo nombre; la revalidación por id no
 * distingue duplicados -> lecturas/escrituras divididas entre dos slots:
 * `if (pel == 1)` leía el pel de la llamada anterior y la fn devolvía el
 * return "viejo". Este es el punto único de asignación de slots para las
 * declaraciones: reutiliza el slot existente (solo si idx >=
 * g_initial_var_count, para NO clobberear globals de módulo en --api),
 * liberando su payload; id y entrada de symtab quedan intactos. Si no
 * existe, appendea como antes. Devuelve NULL solo si vars[] está lleno. */
Variable *te_decl_slot(const char *id) {
    Variable *ex = find_variable_for((char *)id);
    if (ex && ex != &__ret_var) {
        int idx = (int)(ex - g_vm.vars);
        if (idx >= g_vm.initial_var_count && idx < g_vm.var_count) {
            /* Frames: a local (re)declared inside a fn call shadows the slot the
             * caller owned; save it once so te_frame_pop restores it. Slots
             * created inside this same call (idx >= base) are simply reused. */
            if (g_vm.frame_top && idx < g_vm.frame_top->base) te_frame_shadow_slot(g_vm.frame_top, ex);
            if (ex->vtype == VAL_STRING && ex->value.string_value)
                free(ex->value.string_value);
            if (ex->type) { free(ex->type); ex->type = NULL; }
            memset(&ex->value, 0, sizeof(ex->value));
            ex->vtype = VAL_INT;
            return ex;
        }
    }
    if (g_vm.var_count >= MAX_VARS) return NULL;
    Variable *nv = &g_vm.vars[g_vm.var_count];
    memset(nv, 0, sizeof(*nv));
    nv->id = strdup(id);
    g_vm.var_count++;
    te_sym_insert(nv->id, (int)(nv - g_vm.vars));
    return nv;
}

/* Constructs `new X(...)` items of a LIST literal in place (template keeps the
 * ctor args so the persistent --api can re-run it per request). Extracted from
 * declare_variable; same behaviour, incl. early return on the first non-OBJECT. */
static void te_list_literal_construct_objects(ASTNode *value) {
    ASTNode *cur = value->left;
    int list_count = 0;
    while (cur) {
        list_count++;
        /* debug print removed */
        if (strcmp(cur->type, "OBJECT") != 0) {
            return;
        }

        /* Read the original Object pointer. In the persistent embedded-API
         * scenario this same LIST literal AST node is re-declared on every
         * request, so we must NOT destroy the parse-time template here.
         * The pristine template object lives in `cur->extra` on the first
         * pass; we stash it in `cur->cached_class` (unused for OBJECT item
         * nodes) so subsequent requests clone from the original instead of
         * from the previous request's already-constructed clone, and we
         * keep `cur->left` (the ctor args) intact for re-binding. */
        ObjectNode *obj_template = (ObjectNode *)cur->cached_class;
        if (!obj_template) {
            if (cur->extra) obj_template = (ObjectNode *)(cur->extra);
            else obj_template = (ObjectNode *)(intptr_t)cur->value;
            cur->cached_class = (void *)obj_template;
        } else if (cur->extra && (ObjectNode *)cur->extra != obj_template) {
            /* Free the previous request's clone before replacing it so the
             * persistent API doesn't leak one ObjectNode per item per
             * request. Never frees the pristine template (== cached_class). */
            free_object_node((ObjectNode *)cur->extra);
        }
        ObjectNode *obj_clonado = clone_object(obj_template);
        ASTNode *arg = cur->left;
        cur->value = (int)(intptr_t)obj_clonado;
        cur->extra = (struct ASTNode*)obj_clonado;
        MethodNode *m = obj_clonado->class->methods;
        while (m && strcmp(m->name, "__constructor") != 0) {
            m = m->next;
        }

        if (m) {
            ParameterNode *p = m->params;
            while (p && arg) {
                ASTNode *vn = NULL;
                if (arg->type && strcmp(arg->type, "STRING") == 0) {
                    vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
                } 
                else if (arg->type && strcmp(arg->type, "FLOAT") == 0) {
                    /* Float literal: value lives in str_value (parsed via
                     * atof in te_value_to_variable). Passing it as INT here
                     * truncated 19.99 -> 19. */
                    vn = create_ast_leaf("FLOAT", 0, arg->str_value, NULL);
                }
                else if (arg->type && (strcmp(arg->type, "ID") == 0 || strcmp(arg->type, "IDENTIFIER") == 0)) {
                    Variable *v = find_variable(arg->id);
                    if (!v) {
                        printf("Error: variable '%s' not found.\n", arg->id);
                        return;
                    }
                    if (v->vtype == VAL_STRING) {
                        vn = create_ast_leaf("STRING", 0, strdup(v->value.string_value), NULL);
                    } else if (v->vtype == VAL_FLOAT) {
                        char fbuf[64];
                        te_fmt_double(fbuf, sizeof(fbuf), v->value.float_value);
                        vn = create_ast_leaf("FLOAT", 0, fbuf, NULL);
                    } else {
                        vn = create_ast_leaf_number("INT", v->value.int_value, NULL, NULL);
                    }
                } 
                else {
                    int val = evaluate_expression(arg);
                    vn = create_ast_leaf_number("INT", val, NULL, NULL);
                }
                add_or_update_variable(p->name, vn);
                p = p->next;
                arg = arg->next; /* gotcha #1: step ctor args via ->next */
            }
            call_method(obj_clonado, "__constructor");
            /* Ensure constructor side-effects (like return_flag) don't block later AST execution */
            g_vm.return_flag = 0;
            g_vm.return_node = NULL;
            /* debug print removed */
        }
        /* NOTE: do NOT null out cur->left here. The args must survive so
         * that the persistent embedded API can re-run this constructor on
         * the next request (see template handling above). Nulling it caused
         * numeric attributes to reset to 0 on the second request. */
        cur = cur->next;
    }
/* debug print removed */
}

/* Gotcha 30c: a LIST literal evaluated inside a fn was bound BY REFERENCE to its
 * parse-time AST node, so `var arr = []` accumulated the pushes of every previous
 * call. Each evaluation now yields a fresh head + shallow item copies. Items keep
 * borrowed left/right (borrowed_children=1) so free_ast never touches the
 * template; own strings are duplicated unless interned. Nested LIST items are
 * instanced recursively. Registered as request-owned so --api frees it per
 * request (no-op in script mode, where the process exit reclaims it). */
ASTNode* te_list_literal_instance(ASTNode *lit) {
    if (!lit || !lit->type || strcmp(lit->type, "LIST") != 0) return lit;
    ASTNode *head = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!head) return lit;
    head->type = strdup("LIST");
    head->kind = lit->kind;
    head->line = lit->line; head->file_id = lit->file_id;
    ASTNode *tail = NULL;
    for (ASTNode *src = lit->left; src; src = src->next) {
        ASTNode *copy;
        if (src->type && strcmp(src->type, "LIST") == 0) {
            copy = te_list_literal_instance(src);
            if (copy == src) break;
        } else {
            copy = (ASTNode*)malloc(sizeof(ASTNode));
            if (!copy) break;
            memcpy(copy, src, sizeof(ASTNode));
            copy->from_pool = 0;
            copy->type = src->type ? strdup(src->type) : NULL;
            if (copy->str_value && !copy->str_interned) copy->str_value = strdup(copy->str_value);
            if (copy->id && !copy->id_interned) copy->id = strdup(copy->id);
            copy->bc = NULL;
            copy->col_cache = NULL;
            if (src->type && (strcmp(src->type, "OBJECT_LITERAL") == 0 || strcmp(src->type, "MAP") == 0))
                copy->extra = NULL;   /* side hash belongs to the template */
            copy->borrowed_children = 1;
        }
        copy->next = NULL;
        if (!tail) head->left = copy; else tail->next = copy;
        tail = copy;
    }
    te_req_owned_ast_register(head);
    return head;
}

void declare_variable(char *id, ASTNode *value, int is_const) {
    Variable *slot = te_decl_slot(id);
    if (!slot) {
        te_runtime_fatalf("Error: too many declared variables (limit %d).", MAX_VARS);
        return;
    }

    int my_index = (int)(slot - g_vm.vars);
    slot->is_const = is_const;

    // Aseguramos que el tipo siempre sea una copia dinámica para poder liberarlo después
    if (strcmp(value->type, "NUMBER") == 0) {
        g_vm.vars[my_index].type = strdup("INT");
    } else if (strcmp(value->type, "STRING_LITERAL") == 0) {
        g_vm.vars[my_index].type = strdup("STRING");
    } else if (strcmp(value->type, "FLOAT") == 0) {
        g_vm.vars[my_index].type = strdup("FLOAT");
    } else {
        g_vm.vars[my_index].type = strdup(value->type);
    }

   if (strcmp(value->type, "STRING_INTERP") == 0) {
        /* Fase 3a: expandir interpolación */
        free(g_vm.vars[my_index].type);
        g_vm.vars[my_index].type = strdup("STRING");
        g_vm.vars[my_index].vtype = VAL_STRING;
        g_vm.vars[my_index].value.string_value = expand_interp_string(value->str_value);
    }
    else if (strcmp(value->type, "LIST") == 0) {
        /* debug print removed */
        g_vm.vars[my_index].vtype = VAL_OBJECT;
        g_vm.vars[my_index].value.object_value = (void *)(intptr_t)value;

        /* Fast-path: if listNode->value == 1, every OBJECT child already has
         * its `extra` populated with a fully-initialized ObjectNode (from
         * `from_csv_to_list` Phase 1). Skip the per-item clone + constructor
         * pass entirely — that's what made CSV loads ~65× slower than Python.
         */
        if (value->value == 1) {
            return;
        }

        te_list_literal_construct_objects(value);
        /* Gotcha 30c: the variable must own a fresh instance, not the parse-time
         * literal, or every call of the enclosing fn keeps pushing into it. */
        g_vm.vars[my_index].value.object_value = (void *)(intptr_t)te_list_literal_instance(value);
    }
    else if (strcmp(value->type, "ADD") == 0 || strcmp(value->type, "SUB") == 0 || 
             strcmp(value->type, "MUL") == 0 || strcmp(value->type, "DIV") == 0 ||
             strcmp(value->type, "MOD") == 0 || strcmp(value->type, "NEG") == 0 ||
             strcmp(value->type, "BIT_AND") == 0 || strcmp(value->type, "BIT_OR") == 0 ||
             strcmp(value->type, "BIT_XOR") == 0 || strcmp(value->type, "BIT_NOT") == 0 ||
             strcmp(value->type, "SHL") == 0 || strcmp(value->type, "SHR") == 0 ||
             strcmp(value->type, "IN") == 0) {
        /* Fase 6: string concatenation if ADD with strings */
        if (strcmp(value->type, "ADD") == 0 && is_string_type(value)) {
            char *s = get_node_string(value);
            free(g_vm.vars[my_index].type);
            g_vm.vars[my_index].type = strdup("STRING");
            g_vm.vars[my_index].vtype = VAL_STRING;
            g_vm.vars[my_index].value.string_value = s;
            return;
        }
        long long i64v;
        if (te_eval_i64(value, &i64v)) {   /* Fase 1b: entero exacto de 64 bits */
            g_vm.vars[my_index].vtype = VAL_INT;
            g_vm.vars[my_index].value.int_value = i64v;
            g_vm.vars[my_index].type = strdup("INT");
            return;
        }
        double result = evaluate_expression(value);
        if (result == (long long)result) {
            g_vm.vars[my_index].vtype = VAL_INT;
            g_vm.vars[my_index].value.int_value = (long long)result;
            g_vm.vars[my_index].type = strdup("INT");
        } else {
            g_vm.vars[my_index].vtype = VAL_FLOAT;
            g_vm.vars[my_index].value.float_value = result;
            g_vm.vars[my_index].type = strdup("FLOAT");
        }
    }
    /* Comparison / logical operators yield a boolean (0|1). Storing them as
     * type "BOOL" makes `let/var/bool x = (a > b)` behave exactly like a bool
     * literal variable: println(x) -> true/false, "s=" + x -> "s=1".
     * Without this branch these node types fell through to the final else and
     * were stored as INT 0 (losing the real value). */
    else if (strcmp(value->type, "GT") == 0 || strcmp(value->type, "LT") == 0 ||
             strcmp(value->type, "EQ") == 0 || strcmp(value->type, "GT_EQ") == 0 ||
             strcmp(value->type, "LT_EQ") == 0 || strcmp(value->type, "DIFF") == 0 ||
             strcmp(value->type, "AND") == 0 || strcmp(value->type, "OR") == 0 ||
             strcmp(value->type, "NOT") == 0) {
        free(g_vm.vars[my_index].type);
        g_vm.vars[my_index].type = strdup("BOOL");
        g_vm.vars[my_index].vtype = VAL_INT;
        g_vm.vars[my_index].value.int_value = evaluate_expression(value) != 0 ? 1 : 0;
    }
    else if (strcmp(value->type, "ACCESS_ATTR") == 0) {
        // Esto maneja: let item = intencion.item;
        ASTNode *o = value->left, *a = value->right;

        /* Caso especial: .length / .size sobre LIST o MAP (atributo virtual,
         * no presente en obj->attributes). Delegamos a evaluate_expression
         * que ya implementa toda la lógica (incluyendo TEListIdx). */
        if (a && a->id && (strcmp(a->id, "length") == 0 || strcmp(a->id, "size") == 0)) {
            double r = evaluate_expression(value);
            g_vm.vars[my_index].vtype = VAL_INT;
            g_vm.vars[my_index].value.int_value = (long long)r;
            return;
        }

        Variable *v = find_variable(o->id);

        /* Fase 7b: `?.` safe access (value==1) on a null object → the whole
         * access yields null, stored as the canonical null variable. */
        if (value->value == 1 && (!v || (v->type && strcmp(v->type, "NULL") == 0))) {
            free(g_vm.vars[my_index].type);
            g_vm.vars[my_index].vtype = VAL_OBJECT;
            g_vm.vars[my_index].type = strdup("NULL");
            g_vm.vars[my_index].value.object_value = NULL;
            return;
        }

        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: object '%s' not found for assignment.\n", o->id);
            g_vm.vars[my_index].vtype = VAL_INT;
            g_vm.vars[my_index].value.int_value = 0; // Valor de error
            return;
        }

        /* v1.0.0 fix: the variable is a MAP / OBJECT_LITERAL (e.g. `let x =
         * r.activo` where r is a `{..}` literal). value.object_value is an
         * ASTNode* (the map), NOT an ObjectNode*; the ObjectNode cast +
         * obj->class->attr_count below would segfault. Resolve `r.a` as
         * `r["a"]` and copy the typed value. */
        if (v->type && (strcmp(v->type, "MAP") == 0 ||
                        strcmp(v->type, "OBJECT_LITERAL") == 0)) {
            ASTNode *map  = (ASTNode*)(intptr_t)v->value.object_value;
            ASTNode *pair = (map && a->id) ? map_find_pair(map, a->id) : NULL;
            ASTNode *val  = pair ? pair->left : NULL;
            free(g_vm.vars[my_index].type);
            if (!val || !val->type) {
                g_vm.vars[my_index].vtype = VAL_OBJECT;
                g_vm.vars[my_index].type = strdup("NULL");
                g_vm.vars[my_index].value.object_value = NULL;
                return;
            }
            if (strcmp(val->type, "BOOL") == 0) {
                g_vm.vars[my_index].type = strdup("BOOL");
                g_vm.vars[my_index].vtype = VAL_INT;
                g_vm.vars[my_index].value.int_value = val->value ? 1 : 0;
            } else if (strcmp(val->type, "STRING") == 0 || strcmp(val->type, "DATETIME") == 0 ||
                       strcmp(val->type, "UUID") == 0) {
                g_vm.vars[my_index].type = strdup(val->type);
                g_vm.vars[my_index].vtype = VAL_STRING;
                g_vm.vars[my_index].value.string_value = strdup(val->str_value ? val->str_value : "");
            } else if (strcmp(val->type, "FLOAT") == 0) {
                g_vm.vars[my_index].type = strdup("FLOAT");
                g_vm.vars[my_index].vtype = VAL_FLOAT;
                g_vm.vars[my_index].value.float_value = val->str_value ? atof(val->str_value) : 0.0;
            } else if (strcmp(val->type, "NULL") == 0) {
                g_vm.vars[my_index].type = strdup("NULL");
                g_vm.vars[my_index].vtype = VAL_OBJECT;
                g_vm.vars[my_index].value.object_value = NULL;
            } else if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) {
                g_vm.vars[my_index].type = strdup("INT");
                g_vm.vars[my_index].vtype = VAL_INT;
                g_vm.vars[my_index].value.int_value = val->value;
            } else {
                /* CALL_FUNC / expression → numeric eval fallback. */
                g_vm.vars[my_index].type = strdup("FLOAT");
                g_vm.vars[my_index].vtype = VAL_FLOAT;
                g_vm.vars[my_index].value.float_value = evaluate_expression(val);
            }
            return;
        }

        if (!v->type || strcmp(v->type, "OBJECT") != 0) {
            free(g_vm.vars[my_index].type);
            g_vm.vars[my_index].vtype = VAL_OBJECT;
            g_vm.vars[my_index].type = strdup("NULL");
            g_vm.vars[my_index].value.object_value = NULL;
            return;
        }

        ObjectNode *obj = v->value.object_value;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->attributes[i].id, a->id) == 0) {
                if (!te_attr_access_ok(obj->class, i, o)) {
                    g_vm.vars[my_index].vtype = VAL_INT;
                    g_vm.vars[my_index].value.int_value = 0;
                    return;
                }
                // Encontramos el atributo. Copiamos su valor.
                if (obj->attributes[i].vtype == VAL_OBJECT &&
                    obj->attributes[i].value.object_value == NULL) {
                    /* Nullable attribute holding null → canonical null var. */
                    free(g_vm.vars[my_index].type);
                    g_vm.vars[my_index].vtype = VAL_OBJECT;
                    g_vm.vars[my_index].type = strdup("NULL");
                    g_vm.vars[my_index].value.object_value = NULL;
                } else if (obj->attributes[i].vtype == VAL_STRING) {
                    g_vm.vars[my_index].vtype = VAL_STRING;
                    g_vm.vars[my_index].value.string_value = strdup(obj->attributes[i].value.string_value);
                } else if (obj->attributes[i].vtype == VAL_INT) {
                    g_vm.vars[my_index].vtype = VAL_INT;
                    g_vm.vars[my_index].value.int_value = obj->attributes[i].value.int_value;
                } else if (obj->attributes[i].vtype == VAL_FLOAT) {
                    g_vm.vars[my_index].vtype = VAL_FLOAT;
                    g_vm.vars[my_index].value.float_value = obj->attributes[i].value.float_value;
                }
                return; // ¡Asignación completada!
            }
        }
        
        fprintf(stderr, "Error: attribute '%s' not found in '%s'.\n", a->id, o->id);
        g_vm.vars[my_index].vtype = VAL_INT;
        g_vm.vars[my_index].value.int_value = 0; // Valor de error
        return;
    }
    else if (strcmp(value->type, "IDENTIFIER") == 0 || strcmp(value->type, "ID") == 0) {
        /* `let b = a;` / `var i = start;` — copiar el valor de una variable
         * existente. Sin esta rama el identificador desnudo caía al catch-all
         * de te_value_to_variable y se almacenaba como INT 0 (value->value),
         * perdiendo el valor de origen. Una expresión (a+0) sí funcionaba
         * porque la evaluaba evaluate_expression; el identificador puro no. */
        Variable *src = find_variable(value->id);
        free(g_vm.vars[my_index].type);
        if (!src) {
            fprintf(stderr, "Error: variable '%s' not found.\n", value->id ? value->id : "?");
            g_vm.vars[my_index].vtype = VAL_INT;
            g_vm.vars[my_index].type = strdup("INT");
            g_vm.vars[my_index].value.int_value = 0;
            return;
        }
        g_vm.vars[my_index].vtype = src->vtype;
        g_vm.vars[my_index].type = strdup(src->type ? src->type : "INT");
        if (src->vtype == VAL_STRING) {
            g_vm.vars[my_index].value.string_value = strdup(src->value.string_value ? src->value.string_value : "");
        } else if (src->vtype == VAL_FLOAT) {
            g_vm.vars[my_index].value.float_value = src->value.float_value;
        } else if (src->vtype == VAL_OBJECT) {
            /* LIST/MAP/OBJECT/LAMBDA: copiar la referencia (alias), igual que
             * el resto del intérprete (no se hace deep-copy en asignación). */
            g_vm.vars[my_index].value.object_value = src->value.object_value;
        } else {
            g_vm.vars[my_index].value.int_value = src->value.int_value;
        }
        return;
    }
    else {
        /* #5: ramas terminales de mapeo de tipos delegadas al punto único
         * te_value_to_variable (STRING/STRING_LITERAL, FLOAT, OBJECT_LITERAL→MAP,
         * LAMBDA, NULL, OBJECT, LAZY_ITER y el catch-all→INT). El `type`
         * pre-asignado al inicio se libera antes de delegar, pues el helper
         * hace su propio strdup de dst->type (ver contrato). */
        free(g_vm.vars[my_index].type);
        te_value_to_variable(&g_vm.vars[my_index], value);
    }
}


/* ───────────────────────────────────────────────────────────────────────────
 * te_value_to_variable: ÚNICO punto de verdad para mapear un ASTNode-valor a
 * (vtype, type, payload) sobre un Variable destino.
 *
 * Antes esta lógica estaba TRIPLICADA dentro de add_or_update_variable (handler
 * de __ret__, var existente, var nueva) y ya desincronizada entre sí: el path
 * de var-nueva no manejaba FLOAT ni MAP, y sólo __ret__ normalizaba
 * OBJECT_LITERAL→MAP. Cualquier tipo que cayera al `else` colapsaba en silencio
 * a INT 0. Centralizar elimina esa clase de bug: agregar un tipo de valor de
 * retorno nuevo ahora es editar UN solo switch.
 *
 * Contrato: el caller debe haber liberado dst->type y —si dst->vtype era
 * VAL_STRING— dst->value.string_value ANTES de llamar. No toca dst->id ni
 * dst->is_const. dst->type queda con un strdup propio.
 * ─────────────────────────────────────────────────────────────────────────── */
void te_value_to_variable(Variable *dst, ASTNode *value) {
    const char *t = value->type;
    if (strcmp(t, "STRING") == 0 || strcmp(t, "STRING_LITERAL") == 0) {
        dst->vtype = VAL_STRING;
        dst->type = strdup("STRING");
        dst->value.string_value = strdup(value->str_value ? value->str_value : "");
    } else if (strcmp(t, "OBJECT") == 0) {
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("OBJECT");
        dst->value.object_value = (ObjectNode *)value->extra;
    } else if (strcmp(t, "LIST") == 0) {
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("LIST");
        dst->value.object_value = (ObjectNode *)(intptr_t)value;
    } else if (strcmp(t, "MAP") == 0 || strcmp(t, "OBJECT_LITERAL") == 0) {
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("MAP");
        dst->value.object_value = (ObjectNode *)(intptr_t)value;
    } else if (strcmp(t, "LAMBDA") == 0) {
        /* gotcha closure-return: una función puede devolver un lambda capturado */
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("LAMBDA");
        dst->value.object_value = (ObjectNode *)(intptr_t)value;
    } else if (strcmp(t, "LAZY_ITER") == 0) {
        /* v0.0.12 #8: lazy iterator carries pointer to LAZY_ITER ASTNode.
         * El nodo real está en ->extra cuando viene envuelto (declare_variable
         * desde interpret_var_decl) o es `value` mismo cuando se llama directo. */
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("LAZY_ITER");
        dst->value.object_value = value->extra
            ? (ObjectNode *)value->extra
            : (ObjectNode *)(intptr_t)value;
    } else if (strcmp(t, "NULL") == 0) {
        dst->vtype = VAL_OBJECT;
        dst->type = strdup("NULL");
        dst->value.object_value = NULL;
    } else if (strcmp(t, "FLOAT") == 0) {
        dst->vtype = VAL_FLOAT;
        dst->type = strdup("FLOAT");
        dst->value.float_value = value->str_value ? atof(value->str_value) : 0.0;
    } else if (strcmp(t, "BOOL") == 0) {
        /* #5: un literal/valor BOOL se almacena con payload entero (0|1) pero
         * conserva el tag de tipo "BOOL" para que print/concat lo muestren como
         * true/false. Sin esta rama caería al catch-all y se etiquetaría "INT",
         * perdiendo el formato booleano (regresión bool_basic). */
        dst->vtype = VAL_INT;
        dst->type = strdup("BOOL");
        dst->value.int_value = value->value;
    } else {
        /* Red de seguridad: lo que antes era el `else` silencioso. INT es el
         * destino legítimo para NUMBER/INT/BOOL y nodos de op ya evaluados,
         * pero si llega un tipo desconocido lo avisamos en builds de debug en
         * lugar de colapsar a INT 0 sin dejar rastro. */
#ifdef TE_DEBUG_VALUE_TYPES
        if (strcmp(t, "INT") != 0 && strcmp(t, "NUMBER") != 0 &&
            strcmp(t, "BOOL") != 0) {
            fprintf(stderr, "[te_value_to_variable] WARNING: unhandled type '%s' "
                            "stored as INT (possible silent collapse).\n", t);
        }
#endif
        dst->vtype = VAL_INT;
        dst->type = strdup("INT");
        dst->value.int_value = value->value;
    }
}


void add_or_update_variable(char *id, ASTNode *value) {
    if (!value) return;
    /* Fase 3a: si es STRING_INTERP, expandir a STRING antes de almacenar */
    ASTNode *expanded_holder = NULL;
    if (value->type && strcmp(value->type, "STRING_INTERP") == 0) {
        char *s = expand_interp_string(value->str_value);
        expanded_holder = create_ast_leaf("STRING", 0, s, NULL);
        free(s);
        value = expanded_holder;
    }
    // printf("[DEBUG] add_or_update_variable: id=%s, type=%s, value=%d, str_value=%s\n", id, value->type ? value->type : "NULL", value->value, value->str_value ? value->str_value : "NULL");

    if (strcmp(id, "__ret__") == 0) {
        if (__ret_var_active) {
            if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
            if (__ret_var.id) free(__ret_var.id);
            if (__ret_var.type) free(__ret_var.type);
            memset(&__ret_var, 0, sizeof(Variable));
        }
        __ret_var.id = strdup(id);
        __ret_var.is_const = 0;
        te_value_to_variable(&__ret_var, value);
        __ret_var_active = 1;
        return;
    }

    Variable *var = find_variable_for(id);
    if (var) {        
        if (var->is_const) {
            te_runtime_fatalf("Error: cannot assign to constant variable '%s'.", id);
        }
        if (var->vtype == VAL_STRING && var->value.string_value) free(var->value.string_value);
        free(var->type);
        te_value_to_variable(var, value);
    } else {
        if (g_vm.var_count >= MAX_VARS) {
            te_runtime_fatalf("Error: too many declared variables (limit %d).", MAX_VARS);
            return;
        }
        g_vm.vars[g_vm.var_count].id = strdup(id);
        g_vm.vars[g_vm.var_count].is_const = 0;
        /* Ola 16 */
        te_sym_insert(g_vm.vars[g_vm.var_count].id, g_vm.var_count);
        te_value_to_variable(&g_vm.vars[g_vm.var_count], value);
        g_vm.var_count++;
    }
}

/* v0.0.30 (leak fix): asigna __ret__ desde un nodo ESCALAR recien creado inline
 * (create_ast_leaf/_number con STRING/INT/FLOAT/NULL/BOOL) y libera el temporal.
 * add_or_update_variable COPIA escalares via te_value_to_variable, asi que el
 * nodo queda redundante tras la llamada. USAR SOLO con nodos throwaway recien
 * creados; NUNCA con nodos del AST del programa ni LIST/MAP (aliasados -> UAF). */
void te_ret_scalar(ASTNode *n) {
    add_or_update_variable("__ret__", n);
    free_ast(n);
}

/* v0.0.30 (leak fix): libera un resultado throwaway de call_lambda en un sitio
 * que lo DESCARTA (predicados where/count/any/every/find; proyecciones cuyo
 * valor se consume y no se retiene). Tras el clonado en call_lambda_exec_body,
 * todo resultado ESCALAR es fresco y owned -> liberarlo es seguro. LIST/MAP/
 * OBJECT/OBJECT_LITERAL/LAMBDA pueden aliasar el payload de una variable o el
 * body del programa -> NUNCA se liberan. NO usar en sitios que RETIENEN el
 * resultado (map/select que lo appendean a una lista). */
void te_free_lambda_result(ASTNode *r) {
    if (!r) return;
    if (r->type && (
            strcmp(r->type, "LIST") == 0 ||
            strcmp(r->type, "MAP") == 0 ||
            strcmp(r->type, "OBJECT_LITERAL") == 0 ||
            strcmp(r->type, "OBJECT") == 0 ||
            strcmp(r->type, "LAMBDA") == 0))
        return;
    free_ast(r);
}

// ====================== CREACIÓN DE NODOS AST ======================

/* Fase 1 (perf): lazy NodeKind resolver. Looks up node->kind, computes
 * from node->type if still NK_UNKNOWN, and caches the result on the node.
 * Use this in hot dispatch paths instead of strcmp(node->type, "X"). */
/* nk_of: ahora es static inline en ast_internal.h (Fase 2). */

/* Fase 1 (perf): map type-string -> NodeKind. Called once per node creation.
 * Uses first-character switch to keep dispatch O(1) on the common path. */
NodeKind nk_from_str(const char *t) {
    if (!t) return NK_UNKNOWN;
    switch (t[0]) {
        case 'A':
            if (!strcmp(t, "ADD")) return NK_ADD;
            if (!strcmp(t, "AND")) return NK_AND;
            if (!strcmp(t, "ASSIGN")) return NK_ASSIGN;
            if (!strcmp(t, "ASSIGN_ATTR")) return NK_ASSIGN_ATTR;
            if (!strcmp(t, "ACCESS_ATTR")) return NK_ACCESS_ATTR;
            if (!strcmp(t, "ACCESS_EXPR")) return NK_ACCESS_EXPR;
            if (!strcmp(t, "AGENT")) return NK_AGENT;
            if (!strcmp(t, "AGENT_LIST")) return NK_AGENT_LIST;
            break;
        case 'B':
            if (!strcmp(t, "BREAK")) return NK_BREAK;
            if (!strcmp(t, "BRIDGE_DECL")) return NK_BRIDGE_DECL;
            if (!strcmp(t, "BIT_AND")) return NK_BIT_AND;
            if (!strcmp(t, "BIT_OR"))  return NK_BIT_OR;
            if (!strcmp(t, "BIT_XOR")) return NK_BIT_XOR;
            if (!strcmp(t, "BIT_NOT")) return NK_BIT_NOT;
            /* BOOL literal: store as numeric (value=0|1). Special-cased in
             * print/JSON to emit "true"/"false" instead of "0"/"1". */
            if (!strcmp(t, "BOOL")) return NK_NUMBER;
            break;
        case 'C':
            if (!strcmp(t, "CALL_FUNC")) return NK_CALL_FUNC;
            if (!strcmp(t, "CALL_METHOD")) return NK_CALL_METHOD;
            if (!strcmp(t, "CONTINUE")) return NK_CONTINUE;
            break;
        case 'D':
            if (!strcmp(t, "DIV")) return NK_DIV;
            if (!strcmp(t, "DIFF")) return NK_DIFF;
            if (!strcmp(t, "DATASET")) return NK_DATASET;
            break;
        case 'E':
            if (!strcmp(t, "EQ")) return NK_EQ;
            if (!strcmp(t, "EXPRESSION")) return NK_EXPRESSION;
            break;
        case 'F':
            if (!strcmp(t, "FOR")) return NK_FOR;
            if (!strcmp(t, "FOR_IN")) return NK_FOR_IN;
            if (!strcmp(t, "FOR_C")) return NK_FOR_C;
            if (!strcmp(t, "FLOAT")) return NK_FLOAT;
            if (!strcmp(t, "FILTER_CALL")) return NK_FILTER_CALL;
            if (!strcmp(t, "FPRINT")) return NK_FPRINT;
            if (!strcmp(t, "FPRINTLN")) return NK_FPRINTLN;
            break;
        case 'G':
            if (!strcmp(t, "GT")) return NK_GT;
            if (!strcmp(t, "GT_EQ")) return NK_GT_EQ;
            break;
        case 'I':
            if (!strcmp(t, "IDENTIFIER")) return NK_IDENTIFIER;
            if (!strcmp(t, "ID")) return NK_ID;
            if (!strcmp(t, "INT")) return NK_INT;
            if (!strcmp(t, "IF")) return NK_IF;
            if (!strcmp(t, "INDEX_ASSIGN")) return NK_INDEX_ASSIGN;
            if (!strcmp(t, "IN")) return NK_IN;
            break;
        case 'K':
            if (!strcmp(t, "KV_PAIR")) return NK_KV_PAIR;
            break;
        case 'L':
            if (!strcmp(t, "LT")) return NK_LT;
            if (!strcmp(t, "LT_EQ")) return NK_LT_EQ;
            if (!strcmp(t, "LIST")) return NK_LIST;
            if (!strcmp(t, "LIST_FUNC_CALL")) return NK_LIST_FUNC_CALL;
            if (!strcmp(t, "LISTENER")) return NK_LISTENER;
            break;
        case 'M':
            if (!strcmp(t, "MUL")) return NK_MUL;
            if (!strcmp(t, "MATCH")) return NK_MATCH;
            if (!strcmp(t, "MODEL")) return NK_MODEL;
            if (!strcmp(t, "METHOD_CALL_ALONE")) return NK_METHOD_CALL_ALONE;
            if (!strcmp(t, "MOD")) return NK_MOD;
            break;
        case 'N':
            if (!strcmp(t, "NUMBER")) return NK_NUMBER;
            if (!strcmp(t, "NULL")) return NK_NULL;
            if (!strcmp(t, "NULL_COALESCE")) return NK_NULL_COALESCE;
            if (!strcmp(t, "NOT")) return NK_NOT;
            if (!strcmp(t, "NEG")) return NK_NEG;
            break;
        case 'O':
            if (!strcmp(t, "OR")) return NK_OR;
            if (!strcmp(t, "OBJECT")) return NK_OBJECT;
            if (!strcmp(t, "OBJECT_LITERAL")) return NK_OBJECT_LITERAL;
            break;
        case 'P':
            if (!strcmp(t, "PRINT")) return NK_PRINT;
            if (!strcmp(t, "PRINTLN")) return NK_PRINTLN;
            if (!strcmp(t, "PREDICT")) return NK_PREDICT;
            if (!strcmp(t, "PLOT")) return NK_PLOT;
            break;
        case 'R':
            if (!strcmp(t, "RETURN")) return NK_RETURN;
            if (!strcmp(t, "RETURN_JSON")) return NK_RETURN_JSON;
            if (!strcmp(t, "RETURN_XML")) return NK_RETURN_XML;
            break;
        case 'S':
            if (!strcmp(t, "SUB")) return NK_SUB;
            if (!strcmp(t, "STRING")) return NK_STRING;
            if (!strcmp(t, "STRING_LITERAL")) return NK_STRING_LITERAL;
            if (!strcmp(t, "STRING_INTERP")) return NK_STRING_INTERP;
            if (!strcmp(t, "STATE_DECL")) return NK_STATE_DECL;
            if (!strcmp(t, "STATEMENT_LIST")) return NK_STATEMENT_LIST;
            if (!strcmp(t, "SHL")) return NK_SHL;
            if (!strcmp(t, "SHR")) return NK_SHR;
            break;
        case 'T':
            if (!strcmp(t, "THROW")) return NK_THROW;
            if (!strcmp(t, "TRAIN")) return NK_TRAIN;
            if (!strcmp(t, "TRY_CATCH")) return NK_TRY_CATCH;
            if (!strcmp(t, "TERNARY")) return NK_TERNARY;
            break;
        case 'V':
            if (!strcmp(t, "VAR_DECL")) return NK_VAR_DECL;
            break;
        case 'W':
            if (!strcmp(t, "WHILE")) return NK_WHILE;
            break;
    }
    return NK_UNKNOWN;
}

/* ====================================================================
 * Ola 3 Fase A: STRING INTERNING TABLE
 * --------------------------------------------------------------------
 * Global hash set of immortal const char*. Calling tee_intern("hola")
 * always returns the same pointer for the same content. Strings are
 * never freed (Lua/Python style). Used for STRING literals to enable
 * O(1) pointer-equality in `==` / `!=` comparisons.
 *
 * Switch: TYPEEASY_NO_INTERN=1 disables interning.
 * ==================================================================== */
typedef struct InternEntry {
    char *str;
    size_t len;
    uint32_t hash;
    struct InternEntry *next;
} InternEntry;

#define INTERN_BUCKETS 1024
static InternEntry *g_intern_table[INTERN_BUCKETS];
static int g_intern_init = 0;
static int g_intern_enabled = 1;

/* When >0 we are inside per-request handling on the long-lived API server.
 * Runtime-generated STRING leaves (request_param/header/body, concat, json,
 * jwt outputs, te_set_ret_string, ...) are UNIQUE per request; interning them
 * into the immortal g_intern_table would grow it without bound == a memory
 * leak (~hundreds of bytes/request). While active, create_ast_leaf() strdup()s
 * STRING leaves and marks them non-interned so free_ast() and
 * runtime_reset_vars_to_initial_state() reclaim them. Parse/init-time literals
 * (flag == 0) are still interned (bounded by program size, dedup + fast ==). */
int g_te_request_active = 0;

static inline uint32_t intern_hash(const char *s, size_t len) {
    /* FNV-1a */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

const char *tee_intern(const char *s) {
    if (!s) return NULL;
    if (!g_intern_init) {
        const char *e = getenv("TYPEEASY_NO_INTERN");
        if (e && e[0] && e[0] != '0') g_intern_enabled = 0;
        g_intern_init = 1;
    }
    if (!g_intern_enabled) return s; /* caller must still own the memory */

    size_t len = strlen(s);
    uint32_t h = intern_hash(s, len);
    InternEntry *e = g_intern_table[h % INTERN_BUCKETS];
    while (e) {
        if (e->hash == h && e->len == len && memcmp(e->str, s, len) == 0)
            return e->str;
        e = e->next;
    }
    /* Not found -> insert. We strdup so the caller's memory is independent. */
    InternEntry *ne = (InternEntry*)malloc(sizeof(InternEntry));
    ne->str  = strdup(s);
    ne->len  = len;
    ne->hash = h;
    ne->next = g_intern_table[h % INTERN_BUCKETS];
    g_intern_table[h % INTERN_BUCKETS] = ne;
    return ne->str;
}

ASTNode *create_ast_leaf(char *type, long long value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        te_runtime_fatalf("Fatal error: could not allocate memory for ASTNode.");
    }
    node->line = yylineno; node->file_id = g_lex_file_id;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    /* Ola 3 Fase A: intern STRING literals so equality can be pointer-compared.
     * BUT only at parse/init time: runtime (per-request) strings are unique and
     * interning them leaks (immortal table grows). See g_te_request_active. */
    if (str_value) {
        if (node->kind == NK_STRING) {
            if (g_intern_enabled && !g_te_request_active) {
                node->str_value    = (char*)tee_intern(str_value);
                node->str_interned = 1;
            } else {
                node->str_value    = strdup(str_value);
                node->str_interned = 0;
            }
        } else {
            node->str_value = strdup(str_value);
        }
    } else {
        node->str_value = NULL;
    }
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_leaf_number(char *type, long long value, char *str_value, char *id) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;
    node->line = yylineno; node->file_id = g_lex_file_id;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = NULL;
    node->right = NULL;
    node->value = value;
    node->str_value = str_value ? strdup(str_value) : NULL;
    node->id = id ? strdup(id) : NULL;

    node->next = NULL;
    node->extra = NULL;

    return node;
}

ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->line = yylineno; node->file_id = g_lex_file_id;
    node->type = strdup(type);
    node->kind = nk_from_str(type);
    node->left = left;
    node->right = right;
    node->id = NULL;
    node->str_value = NULL;

   if (strcmp(type, "ADD") == 0) {
        node->value = left->value + right->value;
    } 
    else if (strcmp(type, "SUB") == 0) {
        node->value = left->value - right->value;
    } 
    else if (strcmp(type, "MUL") == 0) {
        node->value = left->value * right->value;
    } 
    else if (strcmp(type, "DIV") == 0) {
        // Parse-time integer constant-fold only. The `value` field holds the
        // INT operand; for FLOAT literals it is 0, so a zero divisor here is
        // NOT a real division by zero. The real division (with float support)
        // happens at runtime in evaluate_expression, which reports genuine
        // errors. Do not emit a spurious "division by zero" while building the
        // AST — that fired for every float division (e.g. `1.0 / 4.0`).
        node->value = (right->value != 0) ? (left->value / right->value) : 0;
    } 
    else {
        node->value = 0;
    }
    // ... (lógica de SUB, MUL, DIV sin cambios) ...
    
    return node;
}

ASTNode *create_int_node(long long value) {
    return create_ast_leaf_number("INT", value, NULL, NULL);
}

ASTNode *create_float_node(int value) {
    return create_ast_leaf_number("FLOAT", value, NULL, NULL);
}

ASTNode *create_string_node(char *value) {
    return create_ast_leaf("STRING", 0, value, NULL);
}

ASTNode *create_identifier_node(const char* name) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("ID");
    node->kind = NK_ID;
    node->id = strdup(name);
    node->left = NULL;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_var_decl_node(char *id, ASTNode *value) {
    //printf("[DEBUG] Entering create_var_decl_node: %s\n", id); fflush(stdout);
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Fatal error: could not allocate memory for VAR_DECL node.\n");
        te_runtime_fatal();
    }
    node->type = strdup("VAR_DECL");
    node->id = strdup(id);
    node->left = value;
    node->right = NULL;
    node->str_value = NULL; // Fix: Initialize to NULL to avoid garbage access
    /* For a MULTI-LINE declaration (e.g. `let xs = [ ...\n... ];`) bison reduces
     * this rule only after scanning the closing `];`, so yylineno already points
     * at the LAST line. The scanner stamps g_decl_stmt_line with the line of the
     * leading keyword (let/var/const/type), so the node is attributed to the
     * statement's FIRST line — which is where a user places a breakpoint. */
    node->line = (g_decl_stmt_line > 0) ? g_decl_stmt_line : yylineno; node->file_id = g_lex_file_id;
    //printf("[DEBUG] create_var_decl_node success\n"); fflush(stdout);
    return node;
}

ASTNode *create_return_node(ASTNode *expr) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (node) { node->line = yylineno; node->file_id = g_lex_file_id; }
    node->type = strdup("RETURN");
    node->id = NULL;
    node->left = expr;
    node->right = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_function_call_node(const char *funcName, ASTNode *args) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (n) { n->line = yylineno; n->file_id = g_lex_file_id; }
    n->type = strdup("CALL_FUNC");
    n->id = strdup(funcName);
    n->left = args;
    n->right = NULL;
    n->str_value = NULL;
    n->value = 0;
    return n;
}

ASTNode *create_method_call_node(ASTNode *objectNode, const char *methodName, ASTNode *args) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (node) { node->line = yylineno; node->file_id = g_lex_file_id; }
    node->type = strdup("CALL_METHOD");
    node->id = strdup(methodName);
    node->left = objectNode;
    node->right = args;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

ASTNode *create_object_with_args(ClassNode *class, ASTNode *args) {
    if (!class) {
        fprintf(stderr, "Error: class not found.\n");
        return NULL;
    }

    ObjectNode *real_obj = create_object(class);
    ASTNode *obj = (ASTNode *)calloc(1, sizeof(ASTNode));
    obj->type = strdup("OBJECT");
    obj->left = args;
    obj->right = NULL;
    obj->id = strdup(class->name);
    // Store the actual ObjectNode pointer in 'extra' (used across the codebase)
    obj->extra = (struct ASTNode*)real_obj;
    obj->value = 0;
   
    return obj;
}

ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (node) { node->line = yylineno; node->file_id = g_lex_file_id; }
    node->type = strdup("FOR");
    node->id = var->id;
    node->left = init;
    node->right = condition;
    
    ASTNode *update_body = (ASTNode *)calloc(1, sizeof(ASTNode));
    update_body->type = strdup("FOR_BODY");
    update_body->left = update;
    update_body->right = body;
    
    node->right->right = update_body;
    return node;
}


ASTNode* create_layer_node(const char* layer_type, int units, const char* activation) {
    ASTNode* node = calloc(1, sizeof(ASTNode));
    node->type = strdup("LAYER");
    node->id = strdup(layer_type);
    node->value = units;
    node->str_value = strdup(activation);
    node->left = NULL;
    node->right = NULL;
    return node;
}

ASTNode* create_model_node(const char* name, ASTNode* layer_list) {
    ModelNode* m = malloc(sizeof(ModelNode));
    int count = 0;
    for (ASTNode* cur = layer_list; cur; cur = cur->right) count++;
    m->layer_count = count;
    m->layers = malloc(sizeof(LayerNode*) * count);

    ASTNode* cur = layer_list;
    for (int i = 0; i < count; i++, cur = cur->right) {
        LayerNode* ln = malloc(sizeof(LayerNode));
        ln->layer_type = strdup(cur->id);
        ln->units = cur->value;
        ln->activation = strdup(cur->str_value);
        m->layers[i] = ln;
    }

    ASTNode* node = calloc(1, sizeof(ASTNode));
    node->type = strdup("OBJECT");
    node->id = strdup(name);
    node->value = (int)(intptr_t)m;
    node->left = NULL;
    node->right = NULL;
    return node;
}

ASTNode* create_dataset_node(const char* name, const char* path) {
    DatasetNode* ds = malloc(sizeof(DatasetNode));
    ds->count = 0;
    ds->inputs = NULL;
    ds->labels = NULL;

    ASTNode* node = calloc(1, sizeof(ASTNode));
    node->type = strdup("DATASET");
    node->id = strdup(name);
    node->str_value = strdup(path);
    node->left = NULL;
    node->right = NULL;
    node->value = (int)(intptr_t)ds;
    return node;
}

ASTNode* create_train_node(const char* model_name,
    const char* data_name,
    ASTNode* options) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = strdup("TRAIN");
    n->id   = NULL;
    n->left = create_identifier_node(model_name);
    ASTNode* dataNode = create_identifier_node(data_name);
    dataNode->right = options;  
    n->right = dataNode;
    return n;
}


ASTNode* create_train_option_node(const char* key, int val) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = strdup("TRAIN_OPTION");
    n->id = strdup(key);
    n->value = val;
    n->left = n->right = NULL;
    return n;
}

ASTNode* create_predict_node(const char* model_name, const char* input_name) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = strdup("PREDICT");
    node->left = create_identifier_node(model_name);
    node->right = create_identifier_node(input_name);
    return node;
}

// ====================== CREACIÓN DE NODOS IF ======================

ASTNode* create_if_node(ASTNode* condition, ASTNode* if_branch, ASTNode* else_branch) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: could not allocate memory for if node\n");
        te_runtime_fatal();
    }
    node->type = strdup("IF");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = if_branch;
    node->next = else_branch;
    return node;
}

ASTNode* create_match_node(ASTNode* condition, ASTNode* case_list) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: could not allocate memory for match node\n");
        te_runtime_fatal();
    }
    node->type = strdup("MATCH");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = case_list;
    node->next = NULL;
    return node;
}

ASTNode* create_case_node(ASTNode* condition, ASTNode* body) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: could not allocate memory for case node\n");
        te_runtime_fatal();    }
    node->type = strdup("CASE");
    node->id = NULL;
    node->value = 0;
    node->str_value = NULL;
    node->left = condition;
    node->right = body;
    node->next = NULL;
    return node;
}

ASTNode* append_case_clause(ASTNode* list, ASTNode* case_clause) {
    if (!list) return case_clause;
    ASTNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = case_clause;
    return list;
}

// ====================== MANEJO DE LISTAS ======================

ASTNode *add_statement(ASTNode *list, ASTNode *stmt) {
    /* v0.0.12 gotcha #1: argument lists (expression_list) are chained on the
     * canonical `->next` field so operator/BINOP arguments — which use `->right`
     * for their own right operand — are never corrupted. ALL arg-stepping
     * consumers have been migrated to walk `->next`. We do NOT touch `->right`
     * here: the list head is still reachable via the parent's left/right slot,
     * and inter-argument links live exclusively on `->next`. */
    if (!list) return stmt;
    ASTNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = stmt;
    return list;
}

ASTNode *add_argument(ASTNode *list, ASTNode *expr) {
    if (!list) return expr;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = expr;
    return list;
}

ASTNode* append_layer_to_list(ASTNode* list, ASTNode* layer_node) {
    if (!list) return layer_node;
    ASTNode* cur = list;
    while (cur->right) cur = cur->right;
    cur->right = layer_node;
    return list;
}

ParameterNode *create_parameter_node(char *name, char *type) {
    ParameterNode *param = (ParameterNode *)malloc(sizeof(ParameterNode));
    param->name = strdup(name);
    param->type = strdup(type);
    param->cached_var = NULL;
    param->next = NULL;
    return param;
}

ParameterNode *add_parameter(ParameterNode *list, char *name, char *type) {
    if (!list) return create_parameter_node(name, type);
    ParameterNode *current = list;
    while (current->next) {
        current = current->next;
    }
    current->next = create_parameter_node(name, type);
    return list;
}

// ====================== INTERPRETACIÓN DEL AST ======================

// Helper to check if node resolves to a string
int is_string_type(ASTNode *node) {
    if (!node) return 0;
    if (node->type && (strcmp(node->type, "STRING") == 0 || strcmp(node->type, "STRING_LITERAL") == 0 || strcmp(node->type, "STRING_INTERP") == 0)) return 1;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (v && v->vtype == VAL_STRING) return 1;
    }
    if (node->type && strcmp(node->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = node->left;
        ASTNode *a = node->right;
        if (!o || !a) return 0;
        ObjectNode *obj = NULL;
        /* Caso 1: o es IDENTIFIER (var de tipo OBJECT). */
        if (o->id) {
            Variable *v = find_variable(o->id);
            /* v1.0.0: skip the ObjectNode cast if var is MAP/OBJECT_LITERAL —
             * value.object_value is actually an ASTNode*, and reading
             * obj->class on it crashes. For maps, check KV value type. */
            if (v && v->type && (strcmp(v->type, "MAP") == 0 ||
                                 strcmp(v->type, "OBJECT_LITERAL") == 0)) {
                ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
                if (map && a->id) {
                    ASTNode *pair = map_find_pair(map, a->id);
                    if (pair && pair->left && pair->left->type) {
                        const char *t = pair->left->type;
                        if (strcmp(t, "STRING") == 0 ||
                            strcmp(t, "DATETIME") == 0 ||
                            strcmp(t, "UUID") == 0) return 1;
                    }
                }
                return 0;
            }
            if (v && v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "OBJECT") == 0) {
                obj = v->value.object_value;
                if (!obj) {
                    ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
                    if (wrapper && wrapper->extra) obj = (ObjectNode *)wrapper->extra;
                }
            }
        }
        /* Caso 2: arr[i].attr — o es ACCESS_EXPR. Resolvemos el item OBJECT. */
        if (!obj && o->type && strcmp(o->type, "ACCESS_EXPR") == 0) {
            ASTNode *list = resolve_to_list(o->left);
            if (list && o->right) {
                int idx = (int)evaluate_expression(o->right);
                if (idx >= 0 && idx < list_length(list)) {
                    ASTNode *item = list_get_item(list, idx);
                    if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                        obj = item->extra ? (ObjectNode*)item->extra
                                          : (ObjectNode*)(intptr_t)item->value;
                    }
                }
            }
        }
        if (obj && obj->class) {
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                    if (obj->attributes[i].vtype == VAL_STRING) return 1;
                }
            }
        }
    }
    if (node->type && strcmp(node->type, "ADD") == 0) {
        if (is_string_type(node->left) || is_string_type(node->right)) return 1;
    }
    /* Bracket access m["key"] / arr[i] cuyo valor es string. ACCESS_ATTR
     * (punto) ya estaba cubierto arriba; sin esto, `return obj["k"]` y los
     * contextos string que dependen de is_string_type no reconocian el corchete
     * y caian a la ruta numerica (que devuelve 0 con error). */
    if (node->type && strcmp(node->type, "ACCESS_EXPR") == 0) {
        ASTNode *map = resolve_to_map(node->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(node->right, keybuf, sizeof(keybuf));
            if (key) {
                ASTNode *pair = map_find_pair(map, key);
                if (pair && pair->left && pair->left->type) {
                    const char *vt = pair->left->type;
                    if (strcmp(vt, "STRING") == 0 || strcmp(vt, "DATETIME") == 0 ||
                        strcmp(vt, "UUID") == 0) return 1;
                }
            }
            return 0;
        }
        ASTNode *list = resolve_to_list(node->left);
        if (list && node->right) {
            int idx = (int)evaluate_expression(node->right);
            if (idx >= 0 && idx < list_length(list)) {
                ASTNode *item = list_get_item(list, idx);
                if (item && item->type && strcmp(item->type, "STRING") == 0) return 1;
            }
        }
    }
    if (node->type && strcmp(node->type, "STRING_INTERP") == 0) return 1;
    return 0;
}

/* --- Helpers para LIST (Fase 1a: Arrays) --- */

/* ============================================================
 * Ola 14 \u2014 Side-cache O(1) para LIST y MAP.
 *   Estructura paralela almacenada en `node->extra` del root
 *   (LIST root o OBJECT_LITERAL root). Construida lazy en la
 *   primera lectura post-mutaci\u00f3n. Invalidada (extra=NULL +
 *   free) al mutar la estructura subyacente.
 * ============================================================*/
typedef struct TEListIdx {
    int len;
    int cap;
    ASTNode **items;
} TEListIdx;

typedef struct TEMapSlot {
    uint64_t hash;
    const char *key;   /* alias to pair->id, do NOT free */
    ASTNode *pair;
} TEMapSlot;

typedef struct TEMapHash {
    int cap;          /* power of 2 */
    int count;
    TEMapSlot *slots;
} TEMapHash;

static uint64_t te_str_hash(const char *s) {
    /* FNV-1a 64 */
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static void te_list_idx_free(TEListIdx *ix) {
    if (!ix) return;
    if (ix->items) free(ix->items);
    free(ix);
}

static void te_map_hash_free(TEMapHash *h) {
    if (!h) return;
    if (h->slots) free(h->slots);
    free(h);
}

/* Invalidate side cache on `root` (LIST or OBJECT_LITERAL). Safe to call
 * even when extra is NULL or root is NULL. Distinguishes by node->type. */
void te_invalidate_list_cache(ASTNode *root) {
    if (!root || !root->extra) return;
    te_list_idx_free((TEListIdx*)root->extra);
    root->extra = NULL;
}

/* v0.0.30 (leak fix): deep-free de un arbol JSON heap producido por json_parse().
 * free_ast() libera los cuerpos de nodo + strings, pero NO los indices laterales
 * por-nodo (TEListIdx para LIST, TEMapHash para OBJECT_LITERAL/MAP) guardados en
 * ->extra. Esto recorre el arbol liberando esos indices primero; luego free_ast()
 * libera los nodos. Los arrays de indice solo guardan punteros prestados a los
 * items/pairs, asi que liberarlos aqui y los nodos via free_ast() nunca hace
 * double-free. Lo usa el registro de AST-owned por-request (typeeasy_api.c). */
static void te_json_strip_side_index(ASTNode *n) {
    while (n) {
        if (n->type) {
            if (strcmp(n->type, "LIST") == 0) {
                te_list_idx_free((TEListIdx*)n->extra); n->extra = NULL;
            } else if (strcmp(n->type, "OBJECT_LITERAL") == 0 || strcmp(n->type, "MAP") == 0) {
                te_map_hash_free((TEMapHash*)n->extra); n->extra = NULL;
            }
        }
        if (n->borrowed_children) { n = n->next; continue; }   /* children belong to the template */
        te_json_strip_side_index(n->left);
        te_json_strip_side_index(n->right);
        n = n->next;
    }
}
void te_req_free_json_tree(ASTNode *root) {
    if (!root) return;
    te_json_strip_side_index(root);
    free_ast(root);
}

/* Forward decl needed because te_list_get_idx is defined further below. */
static TEListIdx* te_list_get_idx(ASTNode *list);

/* Ola 14b: append `item` to a list AND keep the side-cache in sync.
 * Brings push from O(n) (walk to tail + invalidate cache) to O(1) amortized. */
void te_list_append(ASTNode *list, ASTNode *item) {
    if (!list || !item) return;
    item->next = NULL;
    TEListIdx *ix = (TEListIdx*)list->extra;
    if (ix) {
        /* Grow if needed (geometric). */
        if (ix->len >= ix->cap) {
            int newcap = ix->cap * 2;
            if (newcap < 8) newcap = 8;
            ASTNode **nb = (ASTNode**)realloc(ix->items, (size_t)newcap * sizeof(ASTNode*));
            if (!nb) {
                /* OOM: drop cache, fall back to walk. */
                te_invalidate_list_cache(list);
                ASTNode *cur = list->left;
                if (!cur) list->left = item;
                else { while (cur->next) cur = cur->next; cur->next = item; }
                return;
            }
            ix->items = nb;
            ix->cap = newcap;
        }
        if (ix->len == 0) {
            list->left = item;
        } else {
            ASTNode *tail = ix->items[ix->len - 1];
            tail->next = item;
        }
        ix->items[ix->len++] = item;
    } else {
        /* Sin caché: build it from scratch (one O(n) walk amortized over
         * future appends), then append. */
        ASTNode *cur = list->left;
        if (!cur) {
            list->left = item;
        } else {
            while (cur->next) cur = cur->next;
            cur->next = item;
        }
        /* Construir caché perezosamente para que próximos push sean O(1). */
        (void)te_list_get_idx(list);
    }
}
void te_invalidate_map_cache(ASTNode *root) {
    if (!root || !root->extra) return;
    te_map_hash_free((TEMapHash*)root->extra);
    root->extra = NULL;
}

/* Build / get list index. Returns NULL on OOM. Idempotent. */
static TEListIdx* te_list_get_idx(ASTNode *list) {
    if (!list) return NULL;
    if (list->extra) return (TEListIdx*)list->extra;
    /* count */
    int n = 0;
    ASTNode *cur = list->left;
    while (cur) { n++; cur = cur->next; }
    int cap = n < 8 ? 8 : n;
    TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
    if (!ix) return NULL;
    ix->len = n;
    ix->cap = cap;
    ix->items = (ASTNode**)calloc((size_t)cap, sizeof(ASTNode*));
    if (!ix->items) { free(ix); return NULL; }
    cur = list->left;
    for (int i = 0; i < n; i++) { ix->items[i] = cur; cur = cur->next; }
    list->extra = (struct ASTNode*)ix;
    return ix;
}

/* Insert key->pair into hash table; assumes capacity available. */
static void te_map_hash_insert(TEMapHash *h, uint64_t hh, const char *k, ASTNode *p) {
    int mask = h->cap - 1;
    int i = (int)(hh & (uint64_t)mask);
    while (h->slots[i].key != NULL) {
        i = (i + 1) & mask;
    }
    h->slots[i].hash = hh;
    h->slots[i].key  = k;
    h->slots[i].pair = p;
    h->count++;
}

/* Build / get map hash. Returns NULL on OOM. */
static TEMapHash* te_map_get_hash(ASTNode *map) {
    if (!map) return NULL;
    if (map->extra) return (TEMapHash*)map->extra;
    /* count entries */
    int n = 0;
    ASTNode *cur = map->left;
    while (cur) { n++; cur = cur->right; }
    int cap = 16;
    while (cap < n * 2) cap <<= 1;
    TEMapHash *h = (TEMapHash*)calloc(1, sizeof(TEMapHash));
    if (!h) return NULL;
    h->cap = cap;
    h->count = 0;
    h->slots = (TEMapSlot*)calloc((size_t)cap, sizeof(TEMapSlot));
    if (!h->slots) { free(h); return NULL; }
    cur = map->left;
    while (cur) {
        if (cur->id) {
            uint64_t hh = te_str_hash(cur->id);
            te_map_hash_insert(h, hh, cur->id, cur);
        }
        cur = cur->right;
    }
    map->extra = (struct ASTNode*)h;
    return h;
}

ASTNode* list_get_item(ASTNode *list, int idx) {
    if (!list || !list->type || strcmp(list->type, "LIST") != 0) return NULL;
    if (idx < 0) return NULL;
    /* Ola 14: O(1) via side-cache index. */
    TEListIdx *ix = te_list_get_idx(list);
    if (ix) {
        if (idx >= ix->len) return NULL;
        return ix->items[idx];
    }
    /* Fallback (OOM): legacy O(n) walk. */
    ASTNode *cur = list->left;
    int i = 0;
    while (cur && i < idx) { cur = cur->next; i++; }
    return cur;
}

int list_length(ASTNode *list) {
    if (!list || !list->type || strcmp(list->type, "LIST") != 0) return 0;
    /* v0.0.14: columnar puro — col_cache es la fuente de verdad cuando
     * la lista no tiene wrappers (TE_CSV_COLUMNAR=1). */
    if (!list->left && list->col_cache) {
        TeColCache *cc = (TeColCache*)list->col_cache;
        return cc->n_rows;
    }
    /* Ola 14: O(1) via side-cache index. */
    TEListIdx *ix = te_list_get_idx(list);
    if (ix) return ix->len;
    /* Fallback. */
    int n = 0;
    ASTNode *cur = list->left;
    while (cur) { n++; cur = cur->next; }
    return n;
}

/* Resolve an expression node to the underlying LIST ASTNode, or NULL. */
ASTNode* resolve_to_list(ASTNode *node) {
    if (!node) return NULL;
    if (node->type && strcmp(node->type, "LIST") == 0) return node;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (!v || !v->type || strcmp(v->type, "LIST") != 0) return NULL;
        return (ASTNode*)(intptr_t)v->value.object_value;
    }
    /* gotcha #22: chained indexing `parsed[0]["col"]` / `xs[i][j]` inside an
     * expression (e.g. a comparison `chk[0]["n"] > 0`). The inner access is an
     * ACCESS_EXPR; resolve it to the element it points at, then resolve that
     * element to a list. Without this the outer index threw "indexed object
     * is neither a list nor a Map" even though the standalone `let` form worked. */
    if (node->type && strcmp(node->type, "ACCESS_EXPR") == 0) {
        ASTNode *item = resolve_access_item(node);
        if (item == node) return NULL; /* guard against self-loop */
        return resolve_to_list(item);
    }
    /* gotcha inline-index: indexar el resultado de una llamada inline
     * `f(x)[i]` / `xs.split(",")[i]`. Antes resolve_to_list solo conocía
     * literales LIST y variables; una llamada (CALL_FUNC/CALL_METHOD/...) que
     * retorna lista no se resolvía -> "Error: no es lista" o 0. Ejecutamos la
     * llamada y leemos el resultado tipado LIST desde __ret__. */
    if (node->type && (strcmp(node->type, "CALL_FUNC") == 0
                       || strcmp(node->type, "CALL_METHOD") == 0
                       || strcmp(node->type, "FILTER_CALL") == 0
                       || strcmp(node->type, "LIST_FUNC_CALL") == 0
                       || strcmp(node->type, "PREDICT") == 0)) {
        interpret_ast(node);
        Variable *r = find_variable("__ret__");
        if (r && r->type && strcmp(r->type, "LIST") == 0 && r->vtype == VAL_OBJECT) {
            return (ASTNode*)(intptr_t)r->value.object_value;
        }
        return NULL;
    }
    return NULL;
}

/* --- Helpers para MAP (Fase 1c) --- */
ASTNode* resolve_to_map(ASTNode *node) {
    if (!node) return NULL;
    if (node->type && strcmp(node->type, "OBJECT_LITERAL") == 0) return node;
    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)) {
        Variable *v = find_variable(node->id);
        if (!v || !v->type || strcmp(v->type, "MAP") != 0) return NULL;
        return (ASTNode*)(intptr_t)v->value.object_value;
    }
    /* gotcha #22: chained indexing `parsed[0]["col"]` resolving to a Map. */
    if (node->type && strcmp(node->type, "ACCESS_EXPR") == 0) {
        ASTNode *item = resolve_access_item(node);
        if (item == node) return NULL; /* guard against self-loop */
        return resolve_to_map(item);
    }
    return NULL;
}

/* gotcha #22: resolve a single ACCESS_EXPR (map["k"] or list[i]) to the element
 * node it points at, WITHOUT requiring the surrounding statement context. Used
 * by resolve_to_list/resolve_to_map so chained indexing works inside any
 * expression (comparisons, arithmetic, etc.), not just standalone `let`. */
ASTNode* resolve_access_item(ASTNode *node) {
    if (!node || !node->type || strcmp(node->type, "ACCESS_EXPR") != 0) return NULL;
    /* Try Map first (string key). */
    ASTNode *map = resolve_to_map(node->left);
    if (map) {
        const char *key = NULL;
        if (node->right && node->right->type) {
            if (strcmp(node->right->type, "STRING") == 0) key = node->right->str_value;
            else if (strcmp(node->right->type, "IDENTIFIER") == 0 ||
                     strcmp(node->right->type, "ID") == 0) {
                Variable *kv = find_variable(node->right->id);
                if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
            }
        }
        if (!key) return NULL;
        ASTNode *pair = map_find_pair(map, key);
        return pair ? pair->left : NULL;
    }
    /* Else List (integer index). */
    ASTNode *list = resolve_to_list(node->left);
    if (list) {
        int idx = (int)evaluate_expression(node->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) return NULL;
        return list_get_item(list, idx);
    }
    return NULL;
}

int map_length(ASTNode *map) {
    if (!map) return 0;
    /* Ola 14: O(1) via side-cache hash. */
    TEMapHash *h = te_map_get_hash(map);
    if (h) return h->count;
    int n = 0;
    ASTNode *cur = map->left;
    while (cur) { n++; cur = cur->right; }
    return n;
}

/* Find a KV_PAIR by key string. Returns the pair node, or NULL. */
ASTNode* map_find_pair(ASTNode *map, const char *key) {
    if (!map || !key) return NULL;
    /* Ola 14: O(1) via side-cache hash. */
    TEMapHash *h = te_map_get_hash(map);
    if (h && h->cap > 0) {
        uint64_t hh = te_str_hash(key);
        int mask = h->cap - 1;
        int i = (int)(hh & (uint64_t)mask);
        for (;;) {
            const char *sk = h->slots[i].key;
            if (sk == NULL) return NULL;
            /* Ola 15: pointer-eq fast-path (interned keys). */
            if (sk == key) return h->slots[i].pair;
            if (h->slots[i].hash == hh && strcmp(sk, key) == 0) return h->slots[i].pair;
            i = (i + 1) & mask;
        }
    }
    /* Fallback O(n). */
    ASTNode *cur = map->left;
    while (cur) {
        if (cur->id) {
            if (cur->id == key) return cur;  /* Ola 15: ptr-eq */
            if (strcmp(cur->id, key) == 0) return cur;
        }
        cur = cur->right;
    }
    return NULL;
}

/* gotcha #17: coerce any expression used as a Map key into a plain string.
 * Values from json_parse(mysql_query(...,"json")) and chained indexing are NOT
 * stored as "pure" VAL_STRING, so using them directly as a Map key
 * (`m[valor] = x` / `m[valor]`) previously threw "Map key must be a string" and
 * forced the `("" + valor)` workaround. We now coerce: STRING literal ->
 * str_value; IDENTIFIER bound to a VAL_STRING -> its string; anything else
 * (numbers, concat, chained index, calls, json values) -> get_node_string().
 * Simple cases borrow the original pointer; coerced cases are copied into `buf`.
 * Returns the key string (never NULL for a non-NULL node). */
const char* te_map_key_coerce(ASTNode *keyNode, char *buf, size_t cap) {
    if (!keyNode || !keyNode->type) return NULL;
    if (strcmp(keyNode->type, "STRING") == 0)
        return keyNode->str_value ? keyNode->str_value : "";
    if (strcmp(keyNode->type, "IDENTIFIER") == 0 || strcmp(keyNode->type, "ID") == 0) {
        Variable *kv = find_variable(keyNode->id);
        if (kv && kv->vtype == VAL_STRING)
            return kv->value.string_value ? kv->value.string_value : "";
        /* else fall through (numeric var, json-string object, etc.) */
    }
    {
        char *s = get_node_string(keyNode);
        if (!s) return NULL;
        snprintf(buf, cap, "%s", s);
        free(s);
        return buf;
    }
}

/* Fase 3a: Expand string interpolation. Input contains {var} placeholders.
   Special chars: \1 = literal '{', \2 = literal '}'. Returns malloc'd string. */
/* ============================================================
 * Mini-evaluador de expresiones para interpolación ${expr} / {expr}
 * ------------------------------------------------------------
 * Parser recursivo-descendente que construye un AST temporal y lo
 * evalúa reutilizando la semántica del intérprete (variables, acceso
 * a miembros, concatenación, aritmética). Soporta:
 *   números, strings, identificadores, acceso a miembros (a.b.c),
 *   paréntesis, unario '-', y operadores + - * / %.
 * Los nodos transitorios se marcan BC_NOT_COMPILABLE para que el
 * fast-path de bytecode no los registre (bc_register_node) — así es
 * seguro liberarlos con free_ast sin dejar punteros colgantes en el
 * registro global que bc_invalidate_all recorre entre requests.
 * ============================================================ */
#define TEI_SP(c)    ((c)==' '||(c)=='\t')
#define TEI_DIGIT(c) ((c)>='0'&&(c)<='9')
#define TEI_ALPHA(c) ((((c)>='a')&&((c)<='z'))||(((c)>='A')&&((c)<='Z'))||(c)=='_')
#define TEI_ALNUM(c) (TEI_ALPHA(c)||TEI_DIGIT(c))

static ASTNode *tei_leaf(char *type, int value, char *str_value, char *id) {
    ASTNode *n = create_ast_leaf(type, value, str_value, id);
    if (n) n->bc = BC_NOT_COMPILABLE;
    return n;
}
static ASTNode *tei_node(char *type, ASTNode *l, ASTNode *r) {
    ASTNode *n = create_ast_node(type, l, r);
    if (n) n->bc = BC_NOT_COMPILABLE;
    return n;
}

static ASTNode *tei_parse_expr(const char **pp);  /* fwd */

static ASTNode *tei_parse_primary(const char **pp) {
    const char *p = *pp;
    while (TEI_SP(*p)) p++;
    if (*p == '(') {
        p++;
        ASTNode *e = tei_parse_expr(&p);
        while (TEI_SP(*p)) p++;
        if (*p == ')') p++;
        *pp = p;
        return e;
    }
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        const char *start = p;
        while (*p && *p != q) p++;
        size_t len = (size_t)(p - start);
        char *buf = (char*)malloc(len + 1);
        memcpy(buf, start, len); buf[len] = '\0';
        if (*p == q) p++;
        ASTNode *n = tei_leaf("STRING", 0, buf, NULL);
        free(buf);
        *pp = p;
        return n;
    }
    if (TEI_DIGIT(*p) || (*p == '.' && TEI_DIGIT(p[1]))) {
        const char *start = p; int isfloat = 0;
        while (TEI_DIGIT(*p)) p++;
        if (*p == '.') { isfloat = 1; p++; while (TEI_DIGIT(*p)) p++; }
        char numbuf[64]; size_t len = (size_t)(p - start);
        if (len >= sizeof(numbuf)) len = sizeof(numbuf) - 1;
        memcpy(numbuf, start, len); numbuf[len] = '\0';
        *pp = p;
        if (isfloat) return tei_leaf("FLOAT", 0, numbuf, NULL);
        return tei_leaf("NUMBER", atoi(numbuf), NULL, NULL);
    }
    if (TEI_ALPHA(*p)) {
        const char *start = p;
        while (TEI_ALNUM(*p)) p++;
        size_t len = (size_t)(p - start);
        char *name = (char*)malloc(len + 1);
        memcpy(name, start, len); name[len] = '\0';
        ASTNode *node = tei_leaf("IDENTIFIER", 0, NULL, name);
        free(name);
        /* cadena de acceso a miembros: a.b.c → ACCESS_ATTR anidado */
        while (TEI_SP(*p)) p++;
        while (*p == '.') {
            p++;
            while (TEI_SP(*p)) p++;
            const char *as = p;
            while (TEI_ALNUM(*p)) p++;
            size_t al = (size_t)(p - as);
            char *attr = (char*)malloc(al + 1);
            memcpy(attr, as, al); attr[al] = '\0';
            ASTNode *anode = tei_leaf("IDENTIFIER", 0, NULL, attr);
            free(attr);
            node = tei_node("ACCESS_ATTR", node, anode);
            while (TEI_SP(*p)) p++;
        }
        *pp = p;
        return node;
    }
    *pp = p;
    return NULL;
}

static ASTNode *tei_parse_factor(const char **pp) {
    const char *p = *pp;
    while (TEI_SP(*p)) p++;
    if (*p == '-') {
        p++;
        ASTNode *operand = tei_parse_factor(&p);
        *pp = p;
        if (!operand) return NULL;
        return tei_node("NEG", operand, NULL);
    }
    *pp = p;
    return tei_parse_primary(pp);
}

static ASTNode *tei_parse_term(const char **pp) {
    ASTNode *left = tei_parse_factor(pp);
    if (!left) return NULL;
    const char *p = *pp;
    for (;;) {
        while (TEI_SP(*p)) p++;
        char op = *p;
        if (op != '*' && op != '/' && op != '%') break;
        p++;
        *pp = p;
        ASTNode *right = tei_parse_factor(pp);
        p = *pp;
        if (!right) break;
        const char *t = (op == '*') ? "MUL" : (op == '/') ? "DIV" : "MOD";
        left = tei_node((char*)t, left, right);
    }
    *pp = p;
    return left;
}

static ASTNode *tei_parse_expr(const char **pp) {
    ASTNode *left = tei_parse_term(pp);
    if (!left) return NULL;
    const char *p = *pp;
    for (;;) {
        while (TEI_SP(*p)) p++;
        char op = *p;
        if (op != '+' && op != '-') break;
        p++;
        *pp = p;
        ASTNode *right = tei_parse_term(pp);
        p = *pp;
        if (!right) break;
        left = tei_node((char*)(op == '+' ? "ADD" : "SUB"), left, right);
    }
    *pp = p;
    return left;
}

/* Evalúa el contenido de un placeholder (`expr_src`, ya recortado) y
 * devuelve una cadena malloc'd con el resultado. Para expresiones que
 * resuelven a string usa get_node_string (concat/member access); para
 * aritmética numérica usa evaluate_expression. Devuelve "" si no parsea. */
static char *tei_eval_placeholder(const char *expr_src) {
    const char *p = expr_src;
    ASTNode *expr = tei_parse_expr(&p);
    if (!expr) return strdup("");
    char *res;
    if (is_string_type(expr)) {
        res = get_node_string(expr);
        if (!res) res = strdup("");
    } else {
        double d = evaluate_expression(expr);
        char tmp[64];
        te_fmt_double(tmp, sizeof(tmp), d);
        res = strdup(tmp);
    }
    free_ast(expr);
    return res;
}

char* expand_interp_string(const char *raw) {
    if (!raw) return strdup("");
    size_t cap = strlen(raw) + 64;
    char *out = (char*)malloc(cap);
    size_t len = 0;
    const char *p = raw;
    while (*p) {
        if (*p == '\1') {
            if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '{'; p++;
            continue;
        }
        if (*p == '\2') {
            if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '}'; p++;
            continue;
        }
        /* Soporta interpolación estilo JS: ${var} además de {var}.
         * Un '$' seguido de '{' se trata como inicio de placeholder. */
        if (*p == '$' && *(p+1) == '{') {
            p++; /* saltar '$', el '{' lo procesa el bloque siguiente */
        }
        if (*p == '{') {
            const char *end = strchr(p, '}');
            if (!end) {
                if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
                continue;
            }
            char name[128]; size_t nlen = end - p - 1;
            if (nlen >= sizeof(name)) nlen = sizeof(name)-1;
            memcpy(name, p+1, nlen); name[nlen] = '\0';
            char *s = name; while (*s == ' ') s++;
            char *e = s + strlen(s); while (e > s && (e[-1]==' ')) { e--; *e='\0'; }
            /* ¿es un identificador simple (var directa)? Conserva el
             * comportamiento histórico (incl. null) y todos los tests.
             * Si no, trátalo como EXPRESIÓN: ${3*3}, {n*2}, {user.name},
             * {"a"+b}, etc. (v0.0.15) */
            int simple = (s[0] != '\0') && !TEI_DIGIT((unsigned char)s[0]);
            if (simple) {
                for (char *q = s; *q; q++) {
                    if (!TEI_ALNUM((unsigned char)*q)) { simple = 0; break; }
                }
            }
            if (!simple) {
                char *res = tei_eval_placeholder(s);
                size_t rl = res ? strlen(res) : 0;
                while (len + rl + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                if (res) { memcpy(out + len, res, rl); len += rl; free(res); }
                p = end + 1;
                continue;
            }
            char buf[256]; const char *valstr = "";
            Variable *v = find_variable(s);
            if (v) {
                if (v->type && strcmp(v->type, "NULL") == 0) valstr = "null";
                else if (v->vtype == VAL_STRING) valstr = v->value.string_value ? v->value.string_value : "";
                else if (v->vtype == VAL_INT) { snprintf(buf,256,"%lld", (long long)v->value.int_value); valstr = buf; }
                else if (v->vtype == VAL_FLOAT) { te_fmt_double(buf, sizeof(buf), v->value.float_value); valstr = buf; }
                else valstr = "";
            }
            size_t vl = strlen(valstr);
            while (len + vl + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + len, valstr, vl); len += vl;
            p = end + 1;
            continue;
        }
        if (len+1 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = *p++;
    }
    out[len] = '\0';
    return out;
}

/* g_csv_wrapper_obj_type and ast_pool_alloc now declared in te_csv.h. */

/* Pool-allocated wrapper for an OBJECT value. Used by LINQ fast-paths
 * (orderBy/groupBy) that materialize millions of wrappers per call: avoids the
 * per-item calloc + strdup("OBJECT") + strdup(class_name). Returns NULL when
 * the value isn't a plain OBJECT (caller must fall back to build_item_from_value). */
/* Non-static (declared in te_csv.h) so te_linq_ops.c can call directly. */
ASTNode* build_object_wrapper_pooled(ASTNode *value) {
    if (!value || !value->type || strcmp(value->type, "OBJECT") != 0) return NULL;
    if (!g_csv_wrapper_obj_type) g_csv_wrapper_obj_type = strdup("OBJECT");
    ASTNode *w = ast_pool_alloc();  /* slot is calloc'd → zero-initialised */
    w->type = g_csv_wrapper_obj_type;
    if (value->id_interned) {
        w->id = value->id;
        w->id_interned = 1;
    } else if (value->id) {
        /* Fallback: still strdup (rare path; CSV objects are always interned). */
        w->id = strdup(value->id);
        w->id_interned = 0;
    }
    w->extra = value->extra;   /* ObjectNode* */
    w->value = value->value;
    w->from_pool = 1;          /* free_ast: skip free(node) and all field frees */
    return w;
}

/* Build a fresh ASTNode item from a runtime value (used for index assign / push). */
ASTNode* build_item_from_value(ASTNode *value) {
    ASTNode *new_item = (ASTNode*)calloc(1, sizeof(ASTNode));
    memset(new_item, 0, sizeof(ASTNode));
    if (!value) { new_item->type = strdup("NUMBER"); return new_item; }
    /* v0.0.11: preserve OBJECT items (e.g., user-class instances inside lists)
     * so callers of minBy/maxBy/first/last/firstWhere etc. can access attrs.
     * Fast-path (post-Phase 2): when called millions of times by LINQ
     * materializers (orderBy/groupBy/where/...), strdup("OBJECT") and
     * strdup(value->id) dominate. Share the sentinel "OBJECT" string (free_ast
     * already skips g_csv_wrapper_obj_type) and reuse interned class-name id. */
    if (value->type && strcmp(value->type, "OBJECT") == 0) {
        if (!g_csv_wrapper_obj_type) g_csv_wrapper_obj_type = strdup("OBJECT");
        new_item->type = g_csv_wrapper_obj_type;
        if (value->id_interned) {
            new_item->id = value->id;
            new_item->id_interned = 1;
        } else {
            new_item->id = value->id ? strdup(value->id) : NULL;
        }
        new_item->extra = value->extra;  /* ObjectNode* */
        return new_item;
    }
    if (value->type && (strcmp(value->type, "LIST") == 0 ||
                        strcmp(value->type, "MAP") == 0 ||
                        strcmp(value->type, "OBJECT_LITERAL") == 0)) {
        /* Return the original list/map directly — they're shared by design. */
        free(new_item);
        return value;
    }
    if (value->type && strcmp(value->type, "STRING") == 0) {
        new_item->type = strdup("STRING");
        new_item->str_value = strdup(value->str_value ? value->str_value : "");
    } else if (value->type && (strcmp(value->type, "IDENTIFIER") == 0 || strcmp(value->type, "ID") == 0)) {
        Variable *vv = find_variable(value->id);
        if (vv && vv->vtype == VAL_STRING) {
            new_item->type = strdup("STRING");
            new_item->str_value = strdup(vv->value.string_value ? vv->value.string_value : "");
        } else if (vv && vv->vtype == VAL_FLOAT) {
            new_item->type = strdup("FLOAT");
            char buf[64]; te_fmt_double(buf, sizeof(buf), vv->value.float_value);
            new_item->str_value = strdup(buf);
        } else if (vv) {
            new_item->type = strdup("NUMBER");
            new_item->value = vv->value.int_value;
        } else {
            new_item->type = strdup("NUMBER");
        }
    } else {
        double v_ = evaluate_expression(value);
        if (v_ == (int)v_) {
            new_item->type = strdup("NUMBER");
            new_item->value = (int)v_;
        } else {
            new_item->type = strdup("FLOAT");
            char buf[64]; te_fmt_double(buf, sizeof(buf), v_);
            new_item->str_value = strdup(buf);
        }
    }
    return new_item;
}

/* gotcha #18: materialize an OBJECT_LITERAL ({...}) into a fresh map node whose
 * KV values are RESOLVED to concrete leaves (string/number/float/bool/null) at
 * call time. Pushing a literal into a list previously evaluated it as a number
 * (-> 0) producing [0,0,...], and sharing the literal node directly would make
 * every loop iteration observe the variable's final value. A resolved snapshot
 * fixes both: list.push({...}) stores a usable, independent object. */
static ASTNode* te_snapshot_object_literal(ASTNode *lit) {
    if (!lit || !lit->type) return NULL;
    if (strcmp(lit->type, "OBJECT_LITERAL") != 0 && strcmp(lit->type, "MAP") != 0) return NULL;
    ASTNode *newmap = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!newmap) return NULL;
    newmap->type = strdup("OBJECT_LITERAL");
    ASTNode *tail = NULL;
    for (ASTNode *src = lit->left; src; src = src->right) {
        if (!src->id) continue;
        ASTNode *valNode = src->left;
        ASTNode *leaf = NULL;
        if (!valNode || (valNode->type && strcmp(valNode->type, "NULL") == 0)) {
            leaf = create_ast_leaf("NULL", 0, NULL, NULL);
        } else if (valNode->type && strcmp(valNode->type, "BOOL") == 0) {
            leaf = create_ast_leaf_number("BOOL", valNode->value, NULL, NULL);
        } else if (is_string_type(valNode)) {
            char *s = get_node_string(valNode);
            leaf = create_ast_leaf("STRING", 0, s ? s : "", NULL);
            if (s) free(s);
        } else {
            double d = evaluate_expression(valNode);
            if (d == (double)(long long)d && d >= -2147483648.0 && d <= 2147483647.0) {
                leaf = create_ast_leaf_number("NUMBER", (long long)d, NULL, NULL);
            } else {
                char b[64]; te_fmt_double(b, sizeof(b), d);
                leaf = create_ast_leaf("FLOAT", 0, b, NULL);
            }
        }
        ASTNode *pair = create_kv_pair_node(src->id, leaf);
        if (!tail) newmap->left = pair; else tail->right = pair;
        tail = pair;
    }
    return newmap;
}

/* Fase 7b: detect whether an expression node currently evaluates to null.
 * Handles three shapes used by the `??` / `?.` operators:
 *   - the `null` literal node          (type == "NULL")
 *   - a simple variable holding null    (type == "IDENTIFIER")
 *   - an object attribute holding null  (type == "ACCESS_ATTR"), e.g. a
 *     nullable `string?` attribute, or `?.` access on a null object.
 * Returns 1 when the value is null, 0 otherwise. Conservative: any shape it
 * cannot resolve is treated as non-null. */
int te_expr_is_null(ASTNode *l) {
    if (!l || !l->type) return 0;
    if (strcmp(l->type, "NULL") == 0) return 1;
    /* gotcha #12: `env("X") ?? def` y demás `fn() ?? def`. Evaluamos la
     * llamada y miramos si __ret__ quedó null (p.ej. env() de una variable
     * no definida). Solo se usa en contextos `??`, así que la doble
     * evaluación del lado no-null afecta únicamente a builtins idempotentes
     * como env(). */
    if (strcmp(l->type, "CALL_FUNC") == 0 || strcmp(l->type, "CALL_METHOD") == 0) {
        if (strcmp(l->type, "CALL_FUNC") == 0) interpret_call_func(l);
        else interpret_call_method(l);
        Variable *r = find_variable("__ret__");
        if (!r) return 1;
        if (r->type && strcmp(r->type, "NULL") == 0) return 1;
        if (r->vtype == VAL_OBJECT && r->value.object_value == NULL) return 1;
        return 0;
    }
    if (strcmp(l->type, "IDENTIFIER") == 0) {
        Variable *vv = find_variable(l->id);
        if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) return 1;
        return 0;
    }
    if (strcmp(l->type, "ACCESS_ATTR") == 0) {
        ASTNode *obj_ref  = l->left;
        ASTNode *attr_ref = l->right;
        if (!obj_ref || !obj_ref->id || !attr_ref || !attr_ref->id) return 0;
        Variable *obj_var = find_variable(obj_ref->id);
        if (!obj_var) return 0;
        /* `?.` on a null object → the whole access is null. */
        if (obj_var->type && strcmp(obj_var->type, "NULL") == 0) return 1;
        /* v1.0.0: the variable holds a MAP / OBJECT_LITERAL (e.g. a `{..}`
         * literal or a lambda param bound to a map list item). Its
         * value.object_value is an ASTNode*, NOT an ObjectNode*; casting and
         * reading obj->class crashes. Resolve `r.attr` as a map lookup: a
         * missing key or an explicit null value counts as null. */
        if (obj_var->type && (strcmp(obj_var->type, "MAP") == 0 ||
                              strcmp(obj_var->type, "OBJECT_LITERAL") == 0)) {
            ASTNode *map  = (ASTNode*)(intptr_t)obj_var->value.object_value;
            ASTNode *pair = map ? map_find_pair(map, attr_ref->id) : NULL;
            if (!pair) return 1;
            ASTNode *val = pair->left;
            if (!val || !val->type) return 1;
            if (strcmp(val->type, "NULL") == 0) return 1;
            return 0;
        }
        /* ROOT FIX (ASan-confirmed heap-buffer-overflow at ast.c te_expr_is_null,
         * triggered by the mail-cierre handler): a LIST / LAMBDA value is ALSO
         * VAL_OBJECT, but its value.object_value is an ASTNode*, not an
         * ObjectNode*. A condition like `scArr.length == 0` (scArr = a
         * json_parse'd LIST) reaches here as ACCESS_ATTR; casting the list node
         * to ObjectNode and reading obj->class->attr_count over-reads the small
         * json heap buffer -> garbage attr_count -> wild attributes[i] deref ->
         * intermittent SIGSEGV in production. Only a real class instance
         * (type "OBJECT") backs an ObjectNode, so bail for anything else. */
        if (!obj_var->type || strcmp(obj_var->type, "OBJECT") != 0) return 0;
        if (obj_var->vtype != VAL_OBJECT || !obj_var->value.object_value) return 0;
        ObjectNode *obj = obj_var->value.object_value;
        if (!obj->class) return 0;
        /* Hardening (prod SIGSEGV at ast.c te_expr_is_null, confirmed via the
         * crash handler's crash_pc/backtrace): a stale/dangling class-instance
         * object can leave class->attributes[] or attributes[i].id NULL, so the
         * strcmp below would dereference NULL (addr 0x0). Guard the array and
         * each id; a corrupt object resolves as non-null instead of crashing. */
        if (!obj->class->attributes || !obj->attributes) return 0;
        for (int i = 0; i < obj->class->attr_count; i++) {
            const char *aid = obj->class->attributes[i].id;
            if (!aid) continue;
            if (strcmp(aid, attr_ref->id) == 0) {
                Variable *attr = &obj->attributes[i];
                /* Runtime null marker for an attribute: VAL_OBJECT with a
                 * NULL object pointer (set by interpret_assign_attr on
                 * `obj.attr = null` for a nullable `T?` attribute). */
                if (attr->vtype == VAL_OBJECT && attr->value.object_value == NULL) return 1;
                if (attr->type && strcmp(attr->type, "NULL") == 0) return 1;
                return 0;
            }
        }
        return 0;
    }
    if (strcmp(l->type, "ACCESS_EXPR") == 0) {
        /* `xs[i] ?? default` / `m["k"] ?? default`: an out-of-range list index
         * or a missing map key evaluates to null. Also handles a stored null
         * element/value. Conservative: anything resolvable to a concrete value
         * is non-null; anything we cannot resolve is treated as non-null. */
        ASTNode *map = resolve_to_map(l->left);
        if (map) {
            const char *key = NULL;
            if (l->right && l->right->type) {
                if (strcmp(l->right->type, "STRING") == 0) key = l->right->str_value;
                else if (strcmp(l->right->type, "IDENTIFIER") == 0 ||
                         strcmp(l->right->type, "ID") == 0) {
                    Variable *kv = find_variable(l->right->id);
                    if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                }
            }
            if (!key) return 0;
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) return 1;                 /* missing key -> null */
            ASTNode *val = pair->left;
            if (val && val->type && strcmp(val->type, "NULL") == 0) return 1;
            return 0;
        }
        ASTNode *list = resolve_to_list(l->left);
        if (list && l->right) {
            int idx = (int)evaluate_expression(l->right);
            if (idx < 0 || idx >= list_length(list)) return 1;  /* out-of-range -> null */
            ASTNode *item = list_get_item(list, idx);
            if (item && item->type && strcmp(item->type, "NULL") == 0) return 1;
            return 0;
        }
        return 0;
    }
    return 0;
}

/* Bytecode VM (compile+exec), profiler, tracer and x86_64 JIT now live in
 * te_bytecode.c (Fase 2 modularization). Public API in te_bytecode.h. */
/* NK_IDENTIFIER — extraído de evaluate_expression (Fase 2). */
static double te_ev_identifier(ASTNode *node) {
        /* Fase 2 (perf): cache Variable* on first lookup.
         * vars[] is append-only with stable pointers, so this is safe.
         * EXCEPT: runtime_reset_vars_to_initial_state() between API
         * requests truncates var_count and wipes slots, so a cached
         * pointer from a previous request may now point to a recycled
         * (or zeroed) slot. Re-validate by comparing the slot's id. */
        Variable *var = (Variable *)node->cached_var;
        if (var && node->id) {
            if (!var->id || strcmp(var->id, node->id) != 0) {
                var = NULL;
                node->cached_var = NULL;
            }
        }
        if (!var) {
            var = find_variable(node->id);
            node->cached_var = var;
        }
        if (var) {
            if (var->type && strcmp(var->type, "NULL") == 0) {
                return 0;
            }
            if (var->vtype == VAL_INT) {
                return var->value.int_value;
            } else if (var->vtype == VAL_FLOAT) {
                return var->value.float_value;
            } else if (var->vtype == VAL_STRING) {
                printf("Error: variable '%s' is a string, cannot be evaluated as a number.\n", node->id);
                return 0;
            } else {
                printf("Error: variable '%s' is an object, cannot be evaluated as a number.\n", node->id);
                return 0;
            }
        } else {
            printf("Error: variable '%s' is not defined.\n", node->id);
            return 0;
        }
}

/* NK_EQ — extraído de evaluate_expression (Fase 2). */
static double te_ev_eq(ASTNode *node) {
        int left_null = 0, right_null = 0;
        if (node->left  && nk_of(node->left)  == NK_NULL) left_null = 1;
        if (node->right && nk_of(node->right) == NK_NULL) right_null = 1;
        if (node->left  && nk_of(node->left)  == NK_IDENTIFIER) {
            Variable *v = find_variable(node->left->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) left_null = 1;
        }
        if (node->right && nk_of(node->right) == NK_IDENTIFIER) {
            Variable *v = find_variable(node->right->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) right_null = 1;
        }
        /* Indexed / attribute access can also hold null (e.g. m["k"] == null,
         * arr[i] == null, obj.attr == null). NK_EQ historically only knew the
         * `null` literal and a null-typed variable, so `parsed["a"] == null`
         * returned false even when the key held null. te_expr_is_null already
         * resolves these shapes (it powers the `??` operator), so reuse it. */
        if (!left_null  && node->left  &&
            (nk_of(node->left)  == NK_ACCESS_EXPR || nk_of(node->left)  == NK_ACCESS_ATTR) &&
            te_expr_is_null(node->left))  left_null = 1;
        if (!right_null && node->right &&
            (nk_of(node->right) == NK_ACCESS_EXPR || nk_of(node->right) == NK_ACCESS_ATTR) &&
            te_expr_is_null(node->right)) right_null = 1;
        if (left_null || right_null) return (double)(left_null && right_null);
        if (is_string_type(node->left) || is_string_type(node->right)) {
             /* Ola 3 Fase A: zero-alloc string compare.
              * Resolve both sides to const char* without strdup. If at least
              * one side is an interned literal, pointer-equality is enough
              * for the positive case; otherwise fall back to strcmp. */
             const char *s1 = NULL, *s2 = NULL;
             int s1_interned = 0, s2_interned = 0;
             if (node->left) {
                 NodeKind lk = nk_of(node->left);
                 if (lk == NK_STRING) { s1 = node->left->str_value; s1_interned = node->left->str_interned; }
                 else if (lk == NK_IDENTIFIER || lk == NK_ID) {
                     Variable *vv = find_variable(node->left->id);
                     if (vv && vv->vtype == VAL_STRING) s1 = vv->value.string_value;
                 }
             }
             if (node->right) {
                 NodeKind rk = nk_of(node->right);
                 if (rk == NK_STRING) { s2 = node->right->str_value; s2_interned = node->right->str_interned; }
                 else if (rk == NK_IDENTIFIER || rk == NK_ID) {
                     Variable *vv = find_variable(node->right->id);
                     if (vv && vv->vtype == VAL_STRING) s2 = vv->value.string_value;
                 }
             }
             if (s1 && s2) {
                 if (s1 == s2) return 1.0;
                 /* If both are interned and pointers differ, contents differ. */
                 if (s1_interned && s2_interned) return 0.0;
                 return (strcmp(s1, s2) == 0) ? 1.0 : 0.0;
             }
             /* Fallback for the rare cases (e.g. CALL on a side). */
             char *fs1 = get_node_string(node->left);
             char *fs2 = get_node_string(node->right);
             int res = (strcmp(fs1, fs2) == 0);
             free(fs1); free(fs2);
             return (double)res;
        }
        return evaluate_expression(node->left) == evaluate_expression(node->right);
}

/* NK_DIFF — extraído de evaluate_expression (Fase 2). */
static double te_ev_diff(ASTNode *node) {
        int left_null = 0, right_null = 0;
        if (node->left  && nk_of(node->left)  == NK_NULL) left_null = 1;
        if (node->right && nk_of(node->right) == NK_NULL) right_null = 1;
        if (node->left  && nk_of(node->left)  == NK_IDENTIFIER) {
            Variable *v = find_variable(node->left->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) left_null = 1;
        }
        if (node->right && nk_of(node->right) == NK_IDENTIFIER) {
            Variable *v = find_variable(node->right->id);
            if (v && v->type && strcmp(v->type, "NULL") == 0) right_null = 1;
        }
        /* Mirror of NK_EQ: detect null held by indexed / attribute access so
         * `m["k"] != null` / `obj.attr != null` are correct. */
        if (!left_null  && node->left  &&
            (nk_of(node->left)  == NK_ACCESS_EXPR || nk_of(node->left)  == NK_ACCESS_ATTR) &&
            te_expr_is_null(node->left))  left_null = 1;
        if (!right_null && node->right &&
            (nk_of(node->right) == NK_ACCESS_EXPR || nk_of(node->right) == NK_ACCESS_ATTR) &&
            te_expr_is_null(node->right)) right_null = 1;
        if (left_null || right_null) return (double)!(left_null && right_null);
        if (is_string_type(node->left) || is_string_type(node->right)) {
             /* Ola 3 Fase A: zero-alloc string compare (mirror of NK_EQ). */
             const char *s1 = NULL, *s2 = NULL;
             int s1_interned = 0, s2_interned = 0;
             if (node->left) {
                 NodeKind lk = nk_of(node->left);
                 if (lk == NK_STRING) { s1 = node->left->str_value; s1_interned = node->left->str_interned; }
                 else if (lk == NK_IDENTIFIER || lk == NK_ID) {
                     Variable *vv = find_variable(node->left->id);
                     if (vv && vv->vtype == VAL_STRING) s1 = vv->value.string_value;
                 }
             }
             if (node->right) {
                 NodeKind rk = nk_of(node->right);
                 if (rk == NK_STRING) { s2 = node->right->str_value; s2_interned = node->right->str_interned; }
                 else if (rk == NK_IDENTIFIER || rk == NK_ID) {
                     Variable *vv = find_variable(node->right->id);
                     if (vv && vv->vtype == VAL_STRING) s2 = vv->value.string_value;
                 }
             }
             if (s1 && s2) {
                 if (s1 == s2) return 0.0;
                 if (s1_interned && s2_interned) return 1.0;
                 return (strcmp(s1, s2) != 0) ? 1.0 : 0.0;
             }
             char *fs1 = get_node_string(node->left);
             char *fs2 = get_node_string(node->right);
             int res = (strcmp(fs1, fs2) != 0);
             free(fs1); free(fs2);
             return (double)res;
        }
        return evaluate_expression(node->left) != evaluate_expression(node->right);
}

/* NK_IN — extraído de evaluate_expression (Fase 2). */
static double te_ev_in(ASTNode *node) {
        /* `key in container` — container puede ser map (busca clave) o list (busca elemento) */
        ASTNode *container = node->right;
        ASTNode *map = resolve_to_map(container);
        if (map) {
            char *key = get_node_string(node->left);
            int found = (key && map_find_pair(map, key)) ? 1 : 0;
            free(key);
            return (double)found;
        }
        ASTNode *list = resolve_to_list(container);
        if (list) {
            double needle = evaluate_expression(node->left);
            char *needle_s = is_string_type(node->left) ? get_node_string(node->left) : NULL;
            ASTNode *cur = list->left;
            int found = 0;
            while (cur) {
                if (needle_s) {
                    char *cs = get_node_string(cur);
                    if (cs && strcmp(cs, needle_s) == 0) { free(cs); found = 1; break; }
                    if (cs) free(cs);
                } else {
                    if (evaluate_expression(cur) == needle) { found = 1; break; }
                }
                cur = cur->next;
            }
            if (needle_s) free(needle_s);
            return (double)found;
        }
        return 0;
}

/* NK_ACCESS_ATTR — extraído de evaluate_expression (Fase 2). */
static double te_ev_access_attr(ASTNode *node) {
        ASTNode *objRef = node->left;
        ASTNode *attr   = node->right;

        /* Fase 7: null-safe ?. — return 0 (null-as-number) if obj is null */
        if (node->value == 1 && objRef && nk_of(objRef) == NK_IDENTIFIER) {
            Variable *vv = find_variable(objRef->id);
            if (!vv || (vv->type && strcmp(vv->type, "NULL") == 0)) return 0;
        }

        /* Fase 1a: arr.length sobre listas / maps / strings */
        if (attr && attr->id && strcmp(attr->id, "length") == 0) {
            ASTNode *list = resolve_to_list(objRef);
            if (list) return (double)list_length(list);
            ASTNode *map = resolve_to_map(objRef);
            if (map) return (double)map_length(map);
            /* str.length — mayo 2026 */
            if (objRef && objRef->id) {
                Variable *sv = find_variable(objRef->id);
                if (sv && sv->vtype == VAL_STRING)
                    return (double)(sv->value.string_value ? strlen(sv->value.string_value) : 0);
            }
            if (objRef && objRef->type &&
                (strcmp(objRef->type, "STRING") == 0 || strcmp(objRef->type, "STRING_LITERAL") == 0)) {
                return (double)(objRef->str_value ? strlen(objRef->str_value) : 0);
            }
        }

        /* Bug fix: arr[i].attr — objRef es un ACCESS_EXPR (indexing),
         * no un identificador. Resolvemos el item de la lista a su
         * ObjectNode y saltamos directo al lookup de atributo.
         * Esto debe ir ANTES de find_variable porque o->id es NULL
         * en este caso (find_variable hace strcmp con NULL → crash). */
        if (objRef && objRef->type && nk_of(objRef) == NK_ACCESS_EXPR) {
            ASTNode *list = resolve_to_list(objRef->left);
            if (list && objRef->right) {
                int idx = (int)evaluate_expression(objRef->right);
                int len = list_length(list);
                if (idx < 0 || idx >= len) {
                    printf("Error: index %d out of range (length=%d).\n", idx, len);
                    return 0;
                }
                ASTNode *item = list_get_item(list, idx);
                ObjectNode *iobj = NULL;
                if (item && item->type && strcmp(item->type, "OBJECT") == 0) {
                    if (item->extra) iobj = (ObjectNode*)item->extra;
                    else iobj = (ObjectNode*)(intptr_t)item->value;
                }
                if (iobj && iobj->class) {
                    for (int i = 0; i < iobj->class->attr_count; i++) {
                        if (strcmp(iobj->class->attributes[i].id, attr->id) == 0) {
                            if (iobj->attributes[i].vtype == VAL_INT)
                                return iobj->attributes[i].value.int_value;
                            if (iobj->attributes[i].vtype == VAL_FLOAT)
                                return iobj->attributes[i].value.float_value;
                            return 0;
                        }
                    }
                    printf("Error: attribute '%s' not found in indexed object.\n", attr->id);
                    return 0;
                }
            }
            /* m["k"].attr — extend to support MAP-indexed objects (toMap result). */
            ASTNode *map = resolve_to_map(objRef->left);
            if (map && objRef->right) {
                const char *key = NULL;
                if (nk_of(objRef->right) == NK_STRING) key = objRef->right->str_value;
                else if (nk_of(objRef->right) == NK_IDENTIFIER || nk_of(objRef->right) == NK_ID) {
                    Variable *kv = find_variable(objRef->right->id);
                    if (kv && kv->vtype == VAL_STRING) key = kv->value.string_value;
                }
                if (key) {
                    ASTNode *pair = map_find_pair(map, key);
                    ASTNode *val  = pair ? pair->left : NULL;
                    ObjectNode *iobj = NULL;
                    if (val && val->type && strcmp(val->type, "OBJECT") == 0) {
                        if (val->extra) iobj = (ObjectNode*)val->extra;
                        else iobj = (ObjectNode*)(intptr_t)val->value;
                    }
                    if (iobj && iobj->class) {
                        for (int i = 0; i < iobj->class->attr_count; i++) {
                            if (strcmp(iobj->class->attributes[i].id, attr->id) == 0) {
                                if (iobj->attributes[i].vtype == VAL_INT)
                                    return iobj->attributes[i].value.int_value;
                                if (iobj->attributes[i].vtype == VAL_FLOAT)
                                    return iobj->attributes[i].value.float_value;
                                return 0;
                            }
                        }
                        printf("Error: attribute '%s' not found in mapped object.\n", attr->id);
                        return 0;
                    }
                }
            }
            printf("Error: cannot resolve indexed expression for attribute access.\n");
            return 0;
        }
        Variable *v = find_variable(objRef->id);
        if (!v || v->vtype != VAL_OBJECT) {
            printf("Error: '%s' is not a valid object to access an attribute.\n",
                   (objRef && objRef->id) ? objRef->id : "<expr>");
            return 0;
        }

        /* v1.0.0: variable is a MAP (e.g. lambda param bound to an
         * OBJECT_LITERAL list item). Treat `r.activo` as `r["activo"]`.
         * Without this branch the code casts value.object_value (ASTNode*)
         * to ObjectNode* and segfaults. */
        if (v->type && (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0)) {
            ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
            if (map && attr && attr->id) {
                ASTNode *pair = map_find_pair(map, attr->id);
                if (!pair) return 0;
                ASTNode *val = pair->left;
                if (!val || !val->type) return 0;
                if (strcmp(val->type, "BOOL") == 0) return (double)val->value;
                if (strcmp(val->type, "NUMBER") == 0 || strcmp(val->type, "INT") == 0) return (double)val->value;
                if (strcmp(val->type, "FLOAT") == 0) return val->str_value ? atof(val->str_value) : 0;
                if (strcmp(val->type, "NULL") == 0) return 0;
                if (strcmp(val->type, "STRING") == 0 || strcmp(val->type, "DATETIME") == 0 ||
                    strcmp(val->type, "UUID") == 0) {
                    const char *s = val->str_value;
                    if (!s || !*s) return 0;
                    char *endp = NULL; double d = strtod(s, &endp);
                    if (endp && endp != s && *endp == '\0') return d;
                    return 1.0;
                }
                /* CALL_FUNC/CALL_METHOD or expression — evaluate */
                return evaluate_expression(val);
            }
            return 0;
        }

        if (v->type && strcmp(v->type, "OBJECT") != 0) return 0;

        ObjectNode *obj = v->value.object_value;
        if (!obj) {
            ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
            if (wrapper && wrapper->extra) {
                obj = (ObjectNode *)wrapper->extra;
            }
        }

        if (!obj) {
            printf("Error: could not get the object to access the attribute.\n");
            return 0;
        }
        /* Ola 2: inline cache. If the cached class matches, jump directly to
         * the cached attribute index without scanning the attributes array. */
        {
            static int ic_init = 0;
            static int ic_enabled = 1;
            if (!ic_init) {
                const char *e = getenv("TYPEEASY_NO_IC");
                if (e && e[0] && e[0] != '0') ic_enabled = 0;
                ic_init = 1;
            }
            if (ic_enabled && node->cached_class == (void*)obj->class) {
                int idx = node->cached_attr_idx;
                if (idx >= 0 && idx < obj->class->attr_count) {
                    if (!te_attr_access_ok(obj->class, idx, objRef)) return 0;
                    if (obj->attributes[idx].vtype == VAL_INT)
                        return obj->attributes[idx].value.int_value;
                    if (obj->attributes[idx].vtype == VAL_FLOAT)
                        return obj->attributes[idx].value.float_value;
                }
            }
            for (int i = 0; i < obj->class->attr_count; i++) {
                if (strcmp(obj->class->attributes[i].id, attr->id) == 0) {
                    if (!te_attr_access_ok(obj->class, i, objRef)) return 0;
                    /* Cache the resolved (class, idx) for next time. */
                    if (ic_enabled) {
                        node->cached_class    = (void*)obj->class;
                        node->cached_attr_idx = i;
                    }
                    if(obj->attributes[i].vtype == VAL_INT)
                        return obj->attributes[i].value.int_value;
                    if(obj->attributes[i].vtype == VAL_FLOAT)
                        return obj->attributes[i].value.float_value;
                }
            }
        }

        printf("Error: attribute '%s' not found in object '%s'.\n", attr->id, objRef->id);
        return 0;
}

/* NK_ACCESS_EXPR — extraído de evaluate_expression (Fase 2). */
static double te_ev_access_expr(ASTNode *node) {
        /* Map indexing m["key"] */
        ASTNode *map = resolve_to_map(node->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(node->right, keybuf, sizeof(keybuf));
            if (!key) {
                fprintf(stderr, "Error: Map key must be a string.\n");
                return 0;
            }
            ASTNode *pair = map_find_pair(map, key);
            if (!pair) {
                fprintf(stderr, "Error: key '%s' not found in Map.\n", key);
                return 0;
            }
            ASTNode *val = pair->left;
            if (val && nk_of(val) == NK_STRING) {
                fprintf(stderr, "Error: value at Map['%s'] is a string; use print/println.\n", key);
                return 0;
            }
            return evaluate_expression(val);
        }

        ASTNode *list = resolve_to_list(node->left);
        if (!list) {
            fprintf(stderr, "Error: indexed object is neither a list nor a Map.\n");
            return 0;
        }
        int idx = (int)evaluate_expression(node->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            fprintf(stderr, "Error: index %d out of range (length=%d).\n", idx, len);
            return 0;
        }
        ASTNode *item = list_get_item(list, idx);
        if (!item) return 0;
        if (item->type && nk_of(item) == NK_STRING) {
            fprintf(stderr, "Error: item is a string; use print/println to display it.\n");
            return 0;
        }
        return evaluate_expression(item);
}

/* NK_CALL_METHOD — extraído de evaluate_expression (Fase 2). */
static double te_ev_call(ASTNode *node) {
        if (nk_of(node) == NK_CALL_FUNC) interpret_call_func(node);
        else                   interpret_call_method(node);
        Variable *r = find_variable("__ret__");
        if (!r) return 0;
        if (r->vtype == VAL_INT)    return (double)r->value.int_value;
        if (r->vtype == VAL_FLOAT)  return r->value.float_value;
        if (r->vtype == VAL_STRING) {
            const char *s = r->value.string_value;
            if (!s || !*s) return 0;
            /* numeric-looking string -> parse; otherwise non-zero presence */
            char *endp = NULL;
            double d = strtod(s, &endp);
            if (endp && endp != s && *endp == '\0') return d;
            return 1.0; /* truthy non-numeric string */
        }
        if (r->vtype == VAL_OBJECT && r->type && strcmp(r->type, "NULL") == 0) return 0;
        return 0;
}

double evaluate_expression(ASTNode *node) {
    if (!node) return 0;

    /* Gotcha #2: `let x = make(10)(5)` — CALL_EXPR dentro de una expresión.
     * No está mapeado en NodeKind; se intercepta por nombre antes del
     * fast-path de bytecode. interpret_call_expr deja el resultado en __ret__. */
    if (node->type && node->type[0] == 'C' && strcmp(node->type, "CALL_EXPR") == 0) {
        interpret_call_expr(node);
        Variable *r = find_variable("__ret__");
        if (!r) return 0;
        if (r->vtype == VAL_INT)   return (double)r->value.int_value;
        if (r->vtype == VAL_FLOAT) return r->value.float_value;
        if (r->vtype == VAL_STRING && r->value.string_value)
            return atof(r->value.string_value);
        return 0;
    }

    /* Fase 3 (perf): bytecode fast-path. Compile & cache on first hit;
     * on subsequent calls (loop bodies, conditions) skip the AST walker
     * entirely and run the flat opcode stream via computed goto. */
    {
        static int bc_init = 0;
        static int bc_enabled = 1;
        if (!bc_init) {
            const char *e = getenv("TYPEEASY_NO_BC");
            if (e && e[0] && e[0] != '0') bc_enabled = 0;
            bc_init = 1;
        }
        if (bc_enabled && !g_debug_enabled) {
            BCInfo *info = bc_get_or_compile(node);
            if (info) return bc_exec(info->code);
        }
    }

    /* Fase 1 (perf): single dispatch via cached NodeKind enum. */
    NodeKind k = nk_of(node);

    switch (k) {
    case NK_NULL:
        return 0; /* null como número = 0 */

    case NK_IDENTIFIER: return te_ev_identifier(node);

    case NK_NUMBER:
    case NK_INT:
        return (double)node->value;

    case NK_FLOAT:
        return atof(node->str_value);

    case NK_GT:
        return evaluate_expression(node->left) > evaluate_expression(node->right);
    case NK_LT:
        return evaluate_expression(node->left) < evaluate_expression(node->right);
    case NK_GT_EQ:
        return evaluate_expression(node->left) <= evaluate_expression(node->right);
    case NK_LT_EQ:
        return evaluate_expression(node->left) >= evaluate_expression(node->right);

    case NK_EQ: return te_ev_eq(node);

    case NK_DIFF: return te_ev_diff(node);

    case NK_AND:
        if (!evaluate_expression(node->left)) return 0;
        return evaluate_expression(node->right) ? 1 : 0;
    case NK_OR:
        if (evaluate_expression(node->left)) return 1;
        return evaluate_expression(node->right) ? 1 : 0;
    case NK_NOT:
        return evaluate_expression(node->left) ? 0 : 1;

    case NK_NULL_COALESCE: {
        ASTNode *l = node->left;
        if (te_expr_is_null(l)) return evaluate_expression(node->right);
        return evaluate_expression(l);
    }

    case NK_TERNARY:
        /* cond ? then : else  — node->left=cond, node->right=then, node->extra=else */
        return evaluate_expression(node->left)
                   ? evaluate_expression(node->right)
                   : evaluate_expression(node->extra);

    case NK_ADD:
        return evaluate_expression(node->left) + evaluate_expression(node->right);
    case NK_SUB:
        return evaluate_expression(node->left) - evaluate_expression(node->right);
    case NK_MUL:
        return evaluate_expression(node->left) * evaluate_expression(node->right);
    case NK_DIV: {
        double right = evaluate_expression(node->right);
        if (right == 0.0) {
            printf("Error: division by zero.\n");
            return 0;
        }
        return evaluate_expression(node->left) / right;
    }
    case NK_MOD: {
        long long lv = (long long)evaluate_expression(node->left);
        long long rv = (long long)evaluate_expression(node->right);
        if (rv == 0) { printf("Error: modulo by zero.\n"); return 0; }
        return (double)(lv % rv);
    }
    case NK_NEG:
        return -evaluate_expression(node->left);
    case NK_BIT_AND: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a & b);
    }
    case NK_BIT_OR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a | b);
    }
    case NK_BIT_XOR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a ^ b);
    }
    case NK_BIT_NOT: {
        long long a = (long long)evaluate_expression(node->left);
        return (double)(~a);
    }
    case NK_SHL: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a << b);
    }
    case NK_SHR: {
        long long a = (long long)evaluate_expression(node->left);
        long long b = (long long)evaluate_expression(node->right);
        return (double)(a >> b);
    }
    case NK_IN: return te_ev_in(node);

    case NK_ACCESS_ATTR: return te_ev_access_attr(node);

    case NK_ACCESS_EXPR: return te_ev_access_expr(node);

    /* v1.0.0: CALL_FUNC / CALL_METHOD inside an expression context.
     * Previously these fell through to `default` returning 0, which broke
     * `builtin(x) OP y` inside LINQ lambdas and `{"k": builtin()}` in map
     * literals. interpret_call_* sets __ret__; we read it back here. */
    case NK_CALL_FUNC:
    case NK_CALL_METHOD: return te_ev_call(node);

    default:
        return (double)node->value;
    }
}

void call_method(ObjectNode *obj, char *method) {
    /* debug print removed */
    ASTNode *thisNode = (ASTNode *)calloc(1, sizeof(ASTNode));
    thisNode->type = strdup("OBJECT");
    thisNode->id = strdup("this");
    thisNode->left = thisNode->right = NULL;
    thisNode->extra = (struct ASTNode*)obj; // store ObjectNode pointer in 'extra' (consistent with create_object_with_args)
    thisNode->value = 0;
    add_or_update_variable("this", thisNode);

    MethodNode *m = obj->class->methods;
    while (m) {
        if (strcmp(m->name, method) == 0) {
            /* method invocation traces removed */
            debugger_push_frame(m->name, NULL);
            interpret_ast(m->body);
            debugger_pop_frame();
            return;
        }
        m = m->next;
    }
    printf("Error: method '%s' not found in class '%s'.\n",
           method, obj->class->name);
}

void execute_predict(ASTNode* model_node, ASTNode* input_node) {
    if (!model_node || !input_node) {
        printf("Error: predict requires two arguments.\n");
        return;
    }
    Variable* model_var = find_variable(model_node->id);
    if (!model_var || model_var->vtype != VAL_OBJECT) {
        printf("Error: model '%s' not found or invalid.\n", model_node->id);
        return;
    }
    Variable* input_var = find_variable(input_node->id);
    if (!input_var) {
        printf("Error: input '%s' not found.\n", input_node->id);
        return;
    }
    /* predict debug logs removed */
    ASTNode *lit = create_ast_leaf_number("INT", 85, NULL, NULL);
    add_or_update_variable("__ret__", lit);
}

/* Prototipos de funciones auxiliares */
static void interpret_dataset(ASTNode *node);
static void interpret_model_object(ASTNode *node);
static void interpret_train_node(ASTNode *node);
static void interpret_predict_node(ASTNode *node);
void interpret_call_func(ASTNode *node);
static void interpret_return_node(ASTNode *node);
void interpret_call_method(ASTNode *node);
/* te_colcache_invalidate declared in te_colcache.h. */
static void interpret_fprint(ASTNode *node);
static void interpret_fprintln(ASTNode *node);
void interpret_statement_list(ASTNode *node);
/* te_builtin_dispatch now declared in te_stdlib.h. */
double evaluate_number(ASTNode *node);
static void interpret_match(ASTNode *node);
// void interpret_if(&g_vm, ASTNode *node); // Ya en ast.h
// int evaluate_condition(ASTNode* condition); // Ya en ast.h

/* ─── DATASET ─────────────────────────────────────────────────────────── */
static void interpret_dataset(ASTNode *node) {
    add_or_update_variable(node->id, node);
    /* dataset load debug log removed */
}

static void interpret_filter_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *list_expr = node->left;
    ASTNode *lambda = node->right;
    ASTNode *list_node = NULL;
    ASTNode *result_list_items = NULL;
    if (list_expr->type && (strcmp(list_expr->type, "ID") == 0 || strcmp(list_expr->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(list_expr->id);
        if (!v || v->vtype != VAL_OBJECT || strcmp(v->type, "LIST") != 0) {
            printf("Error: '%s' is not a valid list for filter.\n", list_expr->id);
            return;
        }
        list_node = (ASTNode *)(intptr_t)v->value.object_value;
    } else if (list_expr->type && strcmp(list_expr->type, "LIST") == 0) {
        list_node = list_expr;
    } else {
        printf("Error: unsupported expression for filter (type: %s).\n", list_expr->type);
        return;
    }
    if (!list_node || strcmp(list_node->type, "LIST") != 0) {
        printf("Error: the operand for filter is not a list.\n");
        return;
    }
    ASTNode *current_item_node = list_node->left;
    while (current_item_node) {
        add_or_update_variable(lambda->id, current_item_node);
        if (evaluate_expression(lambda->left)) {
            /* Usar ->extra para el puntero ObjectNode completo en 64 bits.
             * ->value es int (32-bit truncation) y falla con heaps >2GB. */
            ObjectNode *original_obj = current_item_node->extra
                                       ? (ObjectNode *)current_item_node->extra
                                       : (ObjectNode *)(intptr_t)current_item_node->value;
            ObjectNode *cloned_obj = clone_object(original_obj);
            result_list_items = append_to_list(result_list_items, create_object_node(cloned_obj));
        }
        current_item_node = current_item_node->next;
    }
    add_or_update_variable("__ret__", result_list_items ? result_list_items : create_list_node(NULL));
}

void interpret_list_func_call(ASTNode *node) {
    if (!node || !node->left || !node->right) return;
    ASTNode *listNode = node->left;
    ASTNode *lambda = node->right;
    const char *func = node->id;
    if (strcmp(func, "filter") == 0) {
        ASTNode *result = NULL;
        ASTNode *item = listNode->left;
        while (item) {
            add_or_update_variable(lambda->id, item);
            int r = evaluate_expression(lambda->left);
            if (r) {
                if (!result) result = item;
                else append_to_list(result, item);
            }
            item = item->right;
        }
        add_or_update_variable("__ret__", create_list_node(result));
    }
}

ASTNode* create_for_in_node(const char *var_name, ASTNode *list_expr, ASTNode *body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type      = strdup("FOR_IN");
    node->id        = strdup(var_name);
    node->left      = list_expr;
    node->right     = body;
    node->next      = NULL;
    node->line      = yylineno;
    return node;
}



/* interpret_for_in: movida a te_interp_flow.c (Fase 2). */

ASTNode *create_object_node(ObjectNode *obj) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("OBJECT");
    node->value = (int)(intptr_t)obj; /* compat legacy: kept for scalar reads */
    node->extra = (struct ASTNode*)obj; /* full 64-bit pointer for 64-bit systems */
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

void interpret_bridge_decl(ASTNode *node) {
    char* bridge_name = node->id;
    ASTNode* call_node = node->left;
    if (call_node == NULL || strcmp(call_node->type, "CALL_METHOD") != 0) {
        fprintf(stderr, "Error: bridge declaration '%s' must be a method call.\n", bridge_name);
        return;
    }
    char* lib_name = call_node->left->id;
    char* func_name = call_node->id;
    if (g_debug_mode) {
        te_log_ast("Registering Bridge (simulated): '%s'", bridge_name);
        te_log_ast(" -> Target: Library '%s', Function '%s'", lib_name, func_name);
    }

    // Create a generic class for all bridges if it doesn't exist
    /* bridge class search debug log removed */
    ClassNode* bridge_class = find_class("Bridge");
    
    // --- INICIO DE LA CORRECCIÓN (V10) ---
    // Si no se encuentra, la creamos "perezosamente"
    if (bridge_class == NULL) {
        /* bridge class not found — creation log removed */
        bridge_class = create_class("Bridge");
        add_class(bridge_class);
        
        // (Ya no salimos con error)
        // fprintf(stderr, "Error fatal de arquitectura: ...\n");
        // exit(1);
    }
    // --- FIN DE LA CORRECCIÓN ---

    // Create an object for this specific bridge instance
    ObjectNode* bridge_obj = create_object(bridge_class);

    // Create a temporary ASTNode to wrap the object for the symbol table
    ASTNode* obj_node = create_ast_leaf("OBJECT", 0, NULL, bridge_name);    
    obj_node->extra = (struct ASTNode*)bridge_obj;
    // Add the bridge object to the variables table  
    add_or_update_variable(bridge_name, obj_node);
    free_ast(obj_node);
   
}

ASTNode *create_bridge_node(char *name, ASTNode *call_expr_node) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Fatal error: could not allocate memory for BRIDGE node.\n");
        te_runtime_fatal();
    }
    node->type = strdup("BRIDGE_DECL");
    node->id = strdup(name);
    node->left = call_expr_node;
    node->right = NULL;
    node->next = NULL;
    node->extra = NULL;
    node->str_value = NULL;
    node->value = 0;
    return node;
}

void interpret_agent(ASTNode *agent_node) {
    if (g_debug_mode) te_log_ast("Agent parsed: %s (ignored in script mode)", agent_node->id);
}

ASTNode *create_access_node(ASTNode *base, ASTNode *index_expr) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Fatal error: could not allocate memory for ACCESS_EXPR node.\n");
        te_runtime_fatal();
    }
    node->type = strdup("ACCESS_EXPR");
    node->left = base;
    node->right = index_expr;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    node->next = NULL;
    node->extra = NULL;
    return node;
}

ASTNode *create_object_literal_node(ASTNode *kv_list) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("OBJECT_LITERAL");
    node->left = kv_list;
    node->right = NULL;
    return node;
}

ASTNode *create_kv_pair_node(char *key, ASTNode *value) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("KV_PAIR");
    /* Ola 15: intern map key for pointer-eq lookup fast-path. */
    if (key && g_intern_enabled) {
        node->id = (char*)tee_intern(key);
        node->id_interned = 1;
    } else {
        node->id = key ? strdup(key) : NULL;
        node->id_interned = 0;
    }
    node->left = value;
    node->right = NULL;
    return node;
}

ASTNode *create_state_decl_node(char *name, ASTNode *value_expr) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("STATE_DECL");
    node->id = strdup(name);
    node->left = value_expr;
    node->right = NULL;
    return node;
}

ASTNode *append_kv_pair(ASTNode *list, ASTNode *pair) {
    if (!list) return pair;
    ASTNode *current = list;
    while (current->right) {
        current = current->right;
    }
    current->right = pair;
    return list;
}


// Forward declarations
static void interpret_return_node(ASTNode *node);
void print_object_as_xml_by_id(const char* id);
void print_object_as_json_by_id(const char* id);

/* NK_METHOD_CALL_ALONE — extraído de interpret_ast (Fase 2). */
static void te_stmt_method_call_alone(ASTNode *node) {
        /* Phase F: top-level bare calls like `assert(0);` are parsed as
         * METHOD_CALL_ALONE; try built-ins before user-defined methods. */
        if (te_builtin_dispatch(node)) return;
        MethodNode *m = global_methods;
        int matched = 0;
        while (m) {
            if (strcmp(m->name, node->id) == 0) {
                debugger_push_frame(m->name, node);
                interpret_ast(m->body);
                debugger_pop_frame();
                matched = 1;
                break;
            }
            m = m->next;
        }
        if (!matched) {
            /* Fallback: bare statement may be a native builtin not handled by
             * te_builtin_dispatch (e.g. ws_subscribe/ws_send/ws_broadcast,
             * request_*, response_*). Without this, such calls used as
             * statements (return value discarded) are silently dropped.
             * For METHOD_CALL_ALONE the arg list lives in node->right; we
             * also accept node->left for legacy paths. */
            ASTNode *a = node->right ? node->right : node->left;
            /* Bugfix: una llamada-statement a una variable de tipo LAMBDA
             * (p.ej. `my_exec(...)` donde my_exec = fn(...) => {...}) debe
             * INVOCAR el lambda. Sin esto cae a call_native_function, que es
             * no-op para un nombre desconocido -> la llamada (y sus efectos
             * secundarios: INSERT/UPDATE via un wrapper db_exec) se descarta
             * en silencio. Solo pasaba cuando el retorno se descartaba; con
             * `let r = my_exec(...)` funcionaba porque va por
             * evaluate_expression -> interpret_call_func (que ya maneja LAMBDA). */
            Variable *fv = node->id ? find_variable(node->id) : NULL;
            if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0) {
                ASTNode *lambda = (ASTNode*)(intptr_t)fv->value.object_value;
                /* Residual C: validar ARIDAD tambien aqui (llamada-statement con
                 * retorno DESCARTADO, p.ej. `f(1);`). Antes call_lambda rellenaba
                 * los params faltantes con null e ignoraba sobrantes EN SILENCIO.
                 * Mismo check/mensaje que interpret_call_func_impl (resultado
                 * consumido). Los callbacks LINQ/async NO pasan por aqui, siguen
                 * con aridad laxa a proposito. */
                int nparams = 0;
                if (lambda->id && lambda->id[0]) {
                    nparams = 1;
                    for (const char *pc = lambda->id; *pc; pc++)
                        if (*pc == '\1') nparams++;
                }
                int nargs = 0;
                for (ASTNode *ar = a; ar; ar = ar->next) nargs++;
                if (nargs != nparams) {
                    te_runtime_fatalf(
                        "TypeError: '%s' expects %d argument%s but %d %s passed.",
                        node->id, nparams, nparams == 1 ? "" : "s",
                        nargs, nargs == 1 ? "was" : "were");
                }
                ASTNode *r = call_lambda(lambda, a);
                if (r) add_or_update_variable("__ret__", r);
            } else {
                call_native_function(node->id, a);
            }
        }
}

/* NK_INDEX_ASSIGN — extraído de interpret_ast (Fase 2). */
static void te_stmt_index_assign(ASTNode *node) {
        /* Fase 1b: arr[i] = x   |   Fase 1c: m["k"] = x */
        ASTNode *access = node->left;
        ASTNode *value  = node->right;
        if (!access || !access->left) return;

        ASTNode *map = resolve_to_map(access->left);
        if (map) {
            char keybuf[1024];
            const char *key = te_map_key_coerce(access->right, keybuf, sizeof(keybuf));
            if (!key) { fprintf(stderr, "Error: Map key must be a string.\n"); return; }
            ASTNode *new_val = build_item_from_value(value);
            ASTNode *pair = map_find_pair(map, key);
            if (pair) {
                pair->left = new_val;  /* same key, value changed: hash entry still valid */
            } else {
                ASTNode *new_pair = create_kv_pair_node((char*)key, new_val);
                if (!map->left) {
                    map->left = new_pair;
                } else {
                    ASTNode *cur = map->left;
                    while (cur->right) cur = cur->right;
                    cur->right = new_pair;
                }
                te_invalidate_map_cache(map);  /* Ola 14: new key */
            }
            return;
        }

        ASTNode *list = resolve_to_list(access->left);
        if (!list) {
            fprintf(stderr, "Error: variable is neither a list nor a Map.\n");
            return;
        }
        int idx = (int)evaluate_expression(access->right);
        int len = list_length(list);
        if (idx < 0 || idx >= len) {
            fprintf(stderr, "Error: index %d out of range (length=%d).\n", idx, len);
            return;
        }
        ASTNode *new_item = build_item_from_value(value);
        ASTNode *cur = list->left;
        ASTNode *prev = NULL;
        for (int k = 0; k < idx && cur; k++) { prev = cur; cur = cur->next; }
        new_item->next = cur ? cur->next : NULL;
        if (prev) prev->next = new_item; else list->left = new_item;
        te_invalidate_list_cache(list);  /* Ola 14: item replaced */
        te_colcache_invalidate(list);    /* v0.0.13 (perf) */
}

/* NK_THROW — extraído de interpret_ast (Fase 2). */
static void te_stmt_throw(ASTNode *node) {
        ASTNode *e = node->left;
        char *msg = NULL;
        if (e && nk_of(e) == NK_STRING) msg = strdup(e->str_value ? e->str_value : "");
        else if (e && nk_of(e) == NK_IDENTIFIER) {
            Variable *v = find_variable(e->id);
            if (v && v->vtype == VAL_STRING) msg = strdup(v->value.string_value ? v->value.string_value : "");
            else if (v && v->vtype == VAL_INT) { char b[32]; snprintf(b,32,"%lld", (long long)v->value.int_value); msg = strdup(b); }
            else msg = strdup("");
        } else if (e) {
            double d = evaluate_expression(e);
            char b[64]; te_fmt_double(b, sizeof(b), d);
            msg = strdup(b);
        } else msg = strdup("");
        if (throw_message) free(throw_message);
        throw_message = msg;
        g_vm.throw_flag = 1;
}

/* NK_TRY_CATCH — extraído de interpret_ast (Fase 2). */
static void te_stmt_try_catch(ASTNode *node) {
        ASTNode *try_body = node->left;
        ASTNode *catch_body = node->right;
        ASTNode *finally_body = node->extra;
        const char *err_var_name = node->id;

        interpret_ast(try_body);
        if (g_vm.throw_flag && catch_body) {
            char *msg = throw_message ? strdup(throw_message) : strdup("");
            g_vm.throw_flag = 0;
            if (throw_message) { free(throw_message); throw_message = NULL; }
            if (err_var_name) {
                ASTNode *lit = create_ast_leaf("STRING", 0, msg, NULL);
                add_or_update_variable((char*)err_var_name, lit);
            }
            free(msg);
            interpret_ast(catch_body);
        }
        if (finally_body) {
            int saved_throw = g_vm.throw_flag;
            char *saved_msg = throw_message; throw_message = NULL; g_vm.throw_flag = 0;
            interpret_ast(finally_body);
            if (!g_vm.throw_flag && saved_throw) { g_vm.throw_flag = 1; throw_message = saved_msg; }
            else if (saved_msg) free(saved_msg);
        }
}

/* NK_WHILE — extraído de interpret_ast (Fase 2). */
static void te_stmt_while(ASTNode *node) {
        /* Fase 4: try compiled-bytecode loop. Only succeeds if the entire
         * body is numeric (assigns/ifs/whiles). Falls back to AST walker
         * for any non-trivial body.
         * Debugger: skip bytecode entirely when attached, otherwise the
         * loop runs in one shot and breakpoints / step inside the body
         * never fire. */
        {
            static int bc4_init = 0;
            static int bc4_enabled = 1;
            if (!bc4_init) {
                const char *e = getenv("TYPEEASY_NO_BC");
                if (e && e[0] && e[0] != '0') bc4_enabled = 0;
                bc4_init = 1;
            }
            if (bc4_enabled && !g_debug_enabled) {
                BCInfo *info = bc_get_or_compile_stmt(node);
                if (info) { bc_exec(info->code); return; }
            }
        }
        /* Block scope: reclaim each iteration's body-local `let`s so they do
         * not accumulate against MAX_VARS across iterations. */
        int te_while_scope_mark = g_vm.var_count;
        while (1) {
            if (g_vm.throw_flag || g_vm.return_flag) break;
            int cond = evaluate_condition(node->left);
            if (!cond) break;
            debugger_on_loop_iteration();
            te_scope_unwind_to(te_while_scope_mark);
            interpret_ast(node->right);
            if (g_vm.break_flag) { g_vm.break_flag = 0; break; }
            if (g_vm.continue_flag) { g_vm.continue_flag = 0; continue; }
            if (g_vm.throw_flag || g_vm.return_flag) break;
        }
}

void interpret_ast(ASTNode *node) {
    if (!node) return;
    if (g_vm.return_flag) return;
    if (g_vm.throw_flag) return;

    /* Gotcha #2: `make(10)(5)` a nivel statement — CALL_EXPR no está mapeado
     * en NodeKind, así que se intercepta por nombre antes del switch. */
    if (node->type && node->type[0] == 'C' && strcmp(node->type, "CALL_EXPR") == 0) {
        interpret_call_expr(node);
        return;
    }

    /* One-time registration of JSON eval hooks (Fase 1: te_json modularizado). */
    static int s_te_json_hooks_set = 0;
    if (!s_te_json_hooks_set) {
        te_json_set_eval_hooks(interpret_call_func, interpret_call_method);
        s_te_json_hooks_set = 1;
    }

    /* Item 2.3: track the source line of the statement currently executing
     * so a fatal runtime error can report file:line in dev mode. Cheap
     * (one branch + store), no debug gate. */
    if (node->line > 0) { g_vm.current_exec_line = node->line; g_vm.current_exec_file = node->file_id; }

    /* Debugger hook: only stop on "stoppable" statement-level nodes.
     * Cheap when g_debug_enabled == 0 (single load+test). */
    if (g_debug_enabled) {
        switch (nk_of(node)) {
            case NK_VAR_DECL:
            case NK_ASSIGN: case NK_ASSIGN_ATTR: case NK_INDEX_ASSIGN:
            case NK_IF: case NK_MATCH:
            case NK_FOR: case NK_FOR_IN: case NK_WHILE: case NK_FOR_C:
            case NK_BREAK: case NK_CONTINUE:
            case NK_RETURN: case NK_THROW: case NK_TRY_CATCH:
            case NK_PRINT: case NK_PRINTLN:
            case NK_FPRINT: case NK_FPRINTLN:
            case NK_CALL_FUNC: case NK_CALL_METHOD: case NK_METHOD_CALL_ALONE:
            case NK_RETURN_JSON: case NK_RETURN_XML:
                debugger_on_statement(node);
                break;
            default:
                break;
        }
    }
    /* Fase 1 (perf): single dispatch via cached NodeKind enum. */
    switch (nk_of(node)) {
    case NK_STATE_DECL:
        printf("[TypeEasy] 'state' '%s' tratado como 'var' en modo script.\n", node->id);
        interpret_var_decl(&g_vm, node);
        break;

    case NK_BRIDGE_DECL:
        interpret_bridge_decl(node);
        break;

    case NK_AGENT:
        interpret_agent(node);
        break;

    case NK_ACCESS_EXPR:
    case NK_OBJECT_LITERAL:
    case NK_KV_PAIR:
        /* expression-only nodes; nothing to do at statement level */
        break;

    case NK_AGENT_LIST:
        interpret_ast(node->left);
        interpret_ast(node->right);
        break;

    case NK_LISTENER:
        /* main puro ignora los listeners */
        break;

    case NK_FOR:        interpret_for(&g_vm, node); break;
    case NK_FOR_C:      interpret_for_c(&g_vm, node); break;
    case NK_IF:         interpret_if(&g_vm, node); break;
    case NK_MATCH:      interpret_match(node); break;
    case NK_FOR_IN:     interpret_for_in(&g_vm, node); break;
    case NK_LIST_FUNC_CALL: interpret_list_func_call(node); break;
    case NK_FILTER_CALL:    interpret_filter_call(node); break;
    case NK_DATASET:    interpret_dataset(node); break;
    case NK_MODEL:
    case NK_OBJECT:
        interpret_model_object(node);
        break;
    case NK_TRAIN:      interpret_train_node(node); break;
    case NK_PREDICT:    interpret_predict_node(node); break;
    case NK_RETURN_JSON: native_json(node->left); break;

    case NK_RETURN_XML:
        if (node->left && node->left->id) {
            print_object_as_xml_by_id(node->left->id);
        } else {
            if (g_vm.stdout_buffer) {
                ASTNode *result_node = create_ast_leaf("STRING", 0, g_vm.stdout_buffer, NULL);
                add_or_update_variable("__ret__", result_node);
                free_ast(result_node);
            } else {
                ASTNode *result_node = create_ast_leaf("STRING", 0, "", NULL);
                add_or_update_variable("__ret__", result_node);
                free_ast(result_node);
            }
        }
        break;

    case NK_CALL_FUNC:    interpret_call_func(node); break;
    case NK_RETURN:       interpret_return_node(node); break;
    case NK_CALL_METHOD:  interpret_call_method(node); break;

    case NK_METHOD_CALL_ALONE: te_stmt_method_call_alone(node); break;

    case NK_VAR_DECL:    interpret_var_decl(&g_vm, node); break;
    case NK_ASSIGN_ATTR: interpret_assign_attr(&g_vm, node); break;
    case NK_ASSIGN:      interpret_assign(&g_vm, node); break;

    case NK_INDEX_ASSIGN: te_stmt_index_assign(node); break;

    case NK_PRINT:    interpret_print(node); break;
    case NK_PRINTLN:  interpret_println(node); break;
    case NK_FPRINT:   interpret_fprint(node); break;
    case NK_FPRINTLN: interpret_fprintln(node); break;
    case NK_STATEMENT_LIST: interpret_statement_list(node); break;

    case NK_THROW: te_stmt_throw(node); break;

    case NK_TRY_CATCH: te_stmt_try_catch(node); break;

    case NK_WHILE: te_stmt_while(node); break;

    case NK_BREAK:    g_vm.break_flag = 1; break;
    case NK_CONTINUE: g_vm.continue_flag = 1; break;

    case NK_PLOT: {
        double values[100];
        int count = 0;
        ASTNode *child = node->left;
        while (child != NULL && count < 100) {
            values[count++] = evaluate_number(child);
            child = child->next;
        }
        generate_plot(values, count);
        break;
    }

    default:
        /* unknown / unhandled node type: silent no-op */
        break;
    }
}


double evaluate_number(ASTNode *node) {
    if (node == NULL) return 0;
    if (strcmp(node->type, "NUMBER") == 0) return node->value;
    if (strcmp(node->type, "FLOAT") == 0) {
        return atof(node->str_value);
    }
    if (strcmp(node->type, "EXPRESSION") == 0) {
        return evaluate_number(node->left); // simplificado
    }
    fprintf(stderr, "Error: unsupported type in evaluate_number: %s\n", node->type);
    return 0;
}

/* interpret_if: movida a te_interp_flow.c (Fase 2). */

void interpret_match(ASTNode *node) {
    /* interpret_match debug logs removed */
    if (!node || !node->left || !node->right) return;
    char* match_value = get_node_string(node->left);

    ASTNode* case_node = node->right;
    while (case_node) {
        if (strcmp(case_node->type, "CASE") == 0) {
            char* case_value = get_node_string(case_node->left);
            if (strcmp(match_value, case_value) == 0) {
                free(case_value);
                interpret_ast(case_node->right);
                break;
            }
            free(case_value);
        }
        case_node = case_node->next;
    }
    free(match_value);
}



int evaluate_condition(ASTNode* condition) {
    if (!condition) return 0;
    // Delegate to evaluate_expression which now supports string comparisons
    return (int)evaluate_expression(condition);
}

void generate_plot(double *values, int count) {
    FILE *fp = fopen("plot_data.txt", "w");
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %f\n", i, values[i]);
    }
    fclose(fp);
    FILE *gnuplot = popen("gnuplot -persistent", "w");
    fprintf(gnuplot, "set title 'Gráfico generado por TypeEasy'\n");
    fprintf(gnuplot, "plot 'plot_data.txt' with linespoints\n");
    pclose(gnuplot);
}


/* interpret_for: movida a te_interp_flow.c (Fase 2). */

static void interpret_model_object(ASTNode *node) {
    add_or_update_variable(node->id, node);
}

/* Java/C-style `for (INIT; COND; UPDATE) { BODY }`:
 *   node->left  = INIT statement (VAR_DECL / ASSIGN) or NULL
 *   node->right = COND expression or NULL (= true)
 *   node->extra = FOR_BODY { left = UPDATE statement or NULL, right = BODY }
 * Desugars to init; while (cond) { body; update; }. `break`/`continue` behave as
 * in the while loop (continue still runs UPDATE, like Java). INIT's variable is
 * scoped to the loop: the slots created from here on are unwound at exit. */
ASTNode *create_for_c_node(ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) te_oom_fatal("FOR_C");
    node->type = strdup("FOR_C");
    node->kind = NK_FOR_C;
    node->line = yylineno; node->file_id = g_lex_file_id;
    node->left = init;
    node->right = cond;
    ASTNode *fb = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!fb) te_oom_fatal("FOR_BODY");
    fb->type = strdup("FOR_BODY");
    fb->left = update;
    fb->right = body;
    node->extra = fb;
    return node;
}

/* interpret_for_c: movida a te_interp_flow.c (Fase 2). */

static void interpret_train_node(ASTNode *node) {
    int epochs = 1;
    ASTNode *opt = node->right->right;
    if (opt && strcmp(opt->id, "epochs") == 0) {
        epochs = opt->value;
    }   
    (void)epochs; // Suprime el warning de 'unused variable'
}

static void interpret_predict_node(ASTNode *node) {
    execute_predict(node->left, node->right);
}

/* ==========================================================================
 * Ola 13 — Stdlib aditiva: builtins free-standing.
 *   Devuelven valor v\u00eda __ret__ (igual que los m\u00e9todos existentes).
 *   Hooked al inicio de interpret_call_func.  No modifican Variable.value
 *   ni el AST de listas/maps existentes; son puramente aditivos.
 * ========================================================================*/
/* =================== Phase D: stdlib helpers =================== */
/* te_resolve_arg + te_builtin_dispatch + adapt_* + te_register_ast_builtins + te_fill_host_api
 * now live in te_stdlib.{c,h} (Fase 2 paso 4). */

/* TeBuf + tebuf_* now live in te_buf.h (Fase 1 modularization). */

/* JSON parser+emitter now live in te_json.c (Fase 1 modularization). */
/* ---------------------------------------------------------------
 * Automatic typed-body validation (FastAPI-style).
 * Validates `json` against the attribute types declared in `cls`
 * and returns a malloc'd JSON error string describing every problem
 * (missing required field, wrong type), or NULL when the body is
 * valid. The caller owns the returned string; the API server replies
 * HTTP 422 with it as the body.
 * --------------------------------------------------------------- */
static int te_str_is_numeric(const char *s, int allow_dot) {
    if (!s || !*s) return 0;
    int i = 0, digits = 0, dot = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    for (; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') { digits++; continue; }
        if (allow_dot && s[i] == '.' && !dot) { dot = 1; continue; }
        return 0;
    }
    return digits > 0;
}

char *te_validate_body_against_class(ClassNode *cls, const char *json) {
    if (!cls) return NULL;
    char errbuf[2048];
    int n = 0;
    n += snprintf(errbuf + n, sizeof(errbuf) - n,
                  "{\"error\":\"validation_failed\",\"detail\":[");
    int first = 1;
    #define TE_ADD_ERR(field, issue) do { \
        if (n < (int)sizeof(errbuf) - 128) { \
            n += snprintf(errbuf + n, sizeof(errbuf) - n, \
                "%s{\"field\":\"%s\",\"issue\":\"%s\"}", first ? "" : ",", (field), (issue)); \
            first = 0; \
        } \
    } while (0)

    /* The body must be a JSON object. */
    const char *p = json ? json : "";
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '{') {
        TE_ADD_ERR("body", "request body must be a JSON object");
        n += snprintf(errbuf + n, sizeof(errbuf) - n, "]}");
        return strdup(errbuf);
    }
    const char *pp = json;
    ASTNode *root = te_json_parse_value(&pp);
    if (!root || !root->type || strcmp(root->type, "OBJECT_LITERAL") != 0) {
        if (root) free_ast(root);
        TE_ADD_ERR("body", "request body must be a JSON object");
        n += snprintf(errbuf + n, sizeof(errbuf) - n, "]}");
        return strdup(errbuf);
    }

    for (int a = 0; a < cls->attr_count; a++) {
        const char *aname = cls->attributes[a].id;
        const char *atype_raw = cls->attributes[a].type ? cls->attributes[a].type : "int";
        if (!aname) continue;

        /* Sufijo '?' = campo OPCIONAL (nullable), estilo TS/Kotlin:
         *   class Producto { nombre: string?; stock: int?; }
         * Un campo opcional nunca es "required field missing" y un "" no es
         * error de tipo: se trata como ausencia de valor (NULL/default). El
         * tipo base (sin '?') se usa para validar el tipo cuando SÍ viene un
         * valor no vacío. */
        size_t atlen = strlen(atype_raw);
        int field_optional = (atlen > 0 && atype_raw[atlen - 1] == '?');
        char atype_buf[32];
        const char *atype = atype_raw;
        if (field_optional) {
            size_t n2 = atlen - 1; if (n2 >= sizeof(atype_buf)) n2 = sizeof(atype_buf) - 1;
            memcpy(atype_buf, atype_raw, n2); atype_buf[n2] = '\0';
            atype = atype_buf;
        }

        ASTNode *val = NULL;
        for (ASTNode *pair = root->left; pair; pair = pair->right) {
            if (pair->id && strcmp(pair->id, aname) == 0) { val = pair->left; break; }
        }
        /* Tolerancia: un campo es opcional si lo marca con '?' O si el flag
         * global "forms tolerantes" (sql_set_empty_as_null / env
         * TYPEEASY_SQL_EMPTY_AS_NULL) esta activo. En ese caso, ausencia y "" no
         * son error (el binder guarda NULL/default). Sin '?' ni flag, se
         * mantiene el comportamiento estricto FastAPI: declarar tipo => requerido. */
        extern int g_db_empty_as_null;
        int lenient = field_optional || g_db_empty_as_null;
        if (!val) {
            if (lenient) continue;                   /* opcional: no requerido */
            TE_ADD_ERR(aname, "required field missing");
            continue;
        }
        const char *vt = val->type ? val->type : "";
        /* JSON null nunca es un "tipo equivocado": es ausencia de valor. Lo
         * aceptamos para cualquier campo (el binder lo guardará como SQL NULL).
         * Sin este skip, ahora que `null` parsea a un nodo NULL (antes INT 0),
         * un `bool` recibido como null dispararía un 422 espurio "expected bool". */
        if (strcmp(vt, "NULL") == 0) continue;
        /* Un STRING vacio "" en un campo opcional (o bajo el flag) tampoco es
         * error de tipo: es ausencia de valor (se guardara NULL). */
        if (lenient && strcmp(vt, "STRING") == 0 &&
            (!val->str_value || !*val->str_value)) continue;
        int is_str = strcmp(vt, "STRING") == 0;
        int is_flt = strcmp(vt, "FLOAT") == 0;
        int is_int = strcmp(vt, "INT") == 0;
        int is_obj = strcmp(vt, "OBJECT_LITERAL") == 0;
        int is_lst = strcmp(vt, "LIST") == 0;
        if (strcmp(atype, "string") == 0) {
            if (is_obj || is_lst) TE_ADD_ERR(aname, "expected string");
        } else if (strcmp(atype, "float") == 0) {
            if (is_obj || is_lst) TE_ADD_ERR(aname, "expected float");
            else if (is_str && !te_str_is_numeric(val->str_value, 1)) TE_ADD_ERR(aname, "expected float");
        } else if (strcmp(atype, "bool") == 0) {
            if (!is_int) TE_ADD_ERR(aname, "expected bool");
        } else { /* int and integer-like types */
            if (is_obj || is_lst) TE_ADD_ERR(aname, "expected int");
            else if (is_str && !te_str_is_numeric(val->str_value, 0)) TE_ADD_ERR(aname, "expected int");
        }
        (void)is_flt;
    }
    #undef TE_ADD_ERR

    /* item #6 (leak audit): el árbol JSON parseado para validación es
     * per-request y debe liberarse en TODAS las rutas de salida; antes
     * leakeaba ~1KB por request con body tipado (model binding /api/login),
     * el grueso de los +265 B/req sostenidos de la regresión total. */
    free_ast(root);

    if (first) return NULL; /* all attributes valid */
    n += snprintf(errbuf + n, sizeof(errbuf) - n, "]}");
    return strdup(errbuf);
}

/* ---------------------------------------------------------------
 * v0.0.13: POST/PUT body -> typed-class model binding helper.
 * Parses `json` as an object and copies matching attribute values
 * into a freshly-created ObjectNode of `cls`. Missing keys keep
 * the class default; extra keys are ignored. Returns NULL on bad
 * class; on bad JSON returns an object with defaults.
 * --------------------------------------------------------------- */
ObjectNode *te_object_from_json(ClassNode *cls, const char *json) {
    if (!cls) return NULL;
    ObjectNode *obj = create_object(cls);
    if (!json || !*json) return obj;
    const char *p = json;
    /* Skip leading whitespace (te_json_parse_value also does this). */
    ASTNode *root = te_json_parse_value(&p);
    if (!root || !root->type || strcmp(root->type, "OBJECT_LITERAL") != 0) {
        return obj; /* Not a JSON object; keep defaults. */
    }
    for (ASTNode *pair = root->left; pair; pair = pair->right) {
        const char *key = pair->id;
        ASTNode *val = pair->left;
        if (!key || !val || !val->type) continue;
        for (int a = 0; a < cls->attr_count; a++) {
            if (!cls->attributes[a].id || strcmp(cls->attributes[a].id, key) != 0) continue;
            Variable *dst = &obj->attributes[a];
            const char *atype_raw = cls->attributes[a].type ? cls->attributes[a].type : "int";
            /* Sufijo '?' = opcional/nullable: stripear para coercer por el tipo
             * base. Sin esto, un campo `string?`/`int?` no matcheaba ningun
             * strcmp y caia al default int (corrompiendo strings). */
            size_t atlen = strlen(atype_raw);
            int field_optional = (atlen > 0 && atype_raw[atlen - 1] == '?');
            char atype_buf[32];
            const char *atype = atype_raw;
            if (field_optional) {
                size_t n2 = atlen - 1; if (n2 >= sizeof(atype_buf)) n2 = sizeof(atype_buf) - 1;
                memcpy(atype_buf, atype_raw, n2); atype_buf[n2] = '\0';
                atype = atype_buf;
            }
            /* JSON null → SQL NULL. Guardamos un marcador null de runtime
             * (VAL_OBJECT con puntero NULL, la misma representación que usa
             * `obj.attr = null`) sin importar el tipo declarado. Así el binder
             * de @params lo interpola como SQL NULL en vez de coercerlo a "0"
             * (string-attr) o 0 (int/float), que rompía columnas DATE/DATETIME/
             * ENUM bajo STRICT_TRANS_TABLES con ERROR 1292. */
            if (val->type && strcmp(val->type, "NULL") == 0) {
                if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
                dst->vtype = VAL_OBJECT;
                dst->value.object_value = NULL;
                break;
            }
            /* "" en un campo opcional (o bajo el flag global) -> NULL, igual que
             * el null JSON: evita coercer "" a 0 / '' en columnas numericas. */
            extern int g_db_empty_as_null;
            if ((field_optional || g_db_empty_as_null) && val->type &&
                strcmp(val->type, "STRING") == 0 && (!val->str_value || !*val->str_value)) {
                if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
                dst->vtype = VAL_OBJECT;
                dst->value.object_value = NULL;
                break;
            }
            if (strcmp(atype, "string") == 0) {
                if (dst->vtype == VAL_STRING && dst->value.string_value) free(dst->value.string_value);
                if (strcmp(val->type, "STRING") == 0) {
                    dst->value.string_value = strdup(val->str_value ? val->str_value : "");
                } else if (strcmp(val->type, "INT") == 0) {
                    char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)val->value);
                    dst->value.string_value = strdup(buf);
                } else if (strcmp(val->type, "FLOAT") == 0) {
                    dst->value.string_value = strdup(val->str_value ? val->str_value : "0");
                } else {
                    dst->value.string_value = strdup("");
                }
                dst->vtype = VAL_STRING;
            } else if (strcmp(atype, "float") == 0) {
                double d = 0;
                if (strcmp(val->type, "FLOAT") == 0)      d = atof(val->str_value ? val->str_value : "0");
                else if (strcmp(val->type, "INT") == 0)   d = (double)val->value;
                else if (strcmp(val->type, "STRING") == 0) d = atof(val->str_value ? val->str_value : "0");
                dst->value.float_value = d;
                dst->vtype = VAL_FLOAT;
            } else {
                /* int / default */
                int iv = 0;
                if (strcmp(val->type, "INT") == 0)         iv = val->value;
                else if (strcmp(val->type, "FLOAT") == 0)  iv = (int)atof(val->str_value ? val->str_value : "0");
                else if (strcmp(val->type, "STRING") == 0) iv = atoi(val->str_value ? val->str_value : "0");
                dst->value.int_value = iv;
                dst->vtype = VAL_INT;
            }
            break;
        }
    }
    /* item #6 (leak audit): los valores ya se copiaron a `obj` con strdup,
     * así que el AST JSON temporal puede liberarse aquí en vez de leakear
     * en cada request con model binding. v0.0.30: deep-free para liberar
     * tambien los indices laterales (TEListIdx/TEMapHash) de listas/maps
     * anidados que free_ast() no toca. */
    if (root) te_req_free_json_tree(root);
    return obj;
}

/* item #6 (leak audit): libera un ObjectNode creado por create_object/
 * te_object_from_json. Solo libera los slots primitivos (id, type, string).
 * NO hace deep-free de atributos VAL_OBJECT anidados para evitar double-free
 * por aliasing — coincide con la política de runtime_reset_vars_to_initial_state. */
void free_object_node(ObjectNode *obj) {
    if (!obj) return;
    if (obj->attributes && obj->class) {
        for (int i = 0; i < obj->class->attr_count; i++) {
            Variable *a = &obj->attributes[i];
            if (a->id) free(a->id);
            if (a->type) free(a->type);
            if (a->vtype == VAL_STRING && a->value.string_value) free(a->value.string_value);
        }
    }
    if (obj->attributes) free(obj->attributes);
    free(obj);
}

/* Lookup a path-param value by name (returns NULL if not bound). */
const char *typeeasy_http_find_param(const char *k) {
    return te_kv_find(g_req_params, k);
}
const char *typeeasy_http_find_query(const char *k) {
    return te_kv_find(g_req_query, k);
}

/* ─── HTTP client moved to te_http.c (Fase 1 modularization).
 *     Exposed via te_http.h → te_http_do(method, url, body, headers_str).
 *     See docs/REFACTOR_AST_C.md for the full plan.
 */

/* stdlib dispatcher + plugin host API moved to te_stdlib.c (Fase 2 paso 4). */

void interpret_call_func(ASTNode *node) {
    te_depth_enter();
    interpret_call_func_impl(node);
    te_depth_leave();
}

static void interpret_call_func_impl(ASTNode *node) {
    if (te_builtin_dispatch(node)) return;

    /* Fase B: si node->id es una variable de tipo LAMBDA, invocar el lambda. */
    if (node->id) {
        Variable *fv = find_variable(node->id);
        if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0) {
            ASTNode *lambda = (ASTNode*)(intptr_t)fv->value.object_value;
            /* v0.0.30: validar ARIDAD en la llamada DIRECTA a una fn nombrada.
             * Las fn de TypeEasy son de aridad fija (sin defaults ni ...rest),
             * pero el binder posicional rellenaba con null los params faltantes
             * e ignoraba los args sobrantes -> resultados silenciosamente
             * incorrectos (p.ej. el keygen de licencias con 3 vs 4 args). Contar
             * params ('\1'-separados en lambda->id) y args (encadenados por
             * ->next; los binops usan ->right para su operando, ver
             * add_statement) y lanzar un error claro si difieren. Solo aplica
             * a la llamada directa: los callbacks internos de LINQ
             * (map/reduce/forEach) y async siguen usando call_lambda con aridad
             * laxa a proposito (pasan item/acc/index). */
            int nparams = 0;
            if (lambda->id && lambda->id[0]) {
                nparams = 1;
                for (const char *pc = lambda->id; *pc; pc++)
                    if (*pc == '\1') nparams++;
            }
            int nargs = 0;
            for (ASTNode *a = node->left; a; a = a->next)
                nargs++;
            if (nargs != nparams) {
                te_runtime_fatalf(
                    "TypeError: '%s' expects %d argument%s but %d %s passed.",
                    node->id, nparams, nparams == 1 ? "" : "s",
                    nargs, nargs == 1 ? "was" : "were");
            }
            te_callstack_push(node->id);
            if (g_profile_enabled) te_prof_enter();
            ASTNode *r = call_lambda(lambda, node->left);
            if (g_profile_enabled) te_prof_leave(node->id);
            te_callstack_pop();
            if (r) add_or_update_variable("__ret__", r);
            return;
        }
    }

    if (strcmp(node->id, "json") == 0) {
        native_json(node->left);
        return;
    }

    // Try native functions (orm_query, mysql_*, etc.)
    if (call_native_function(node->id, node->left)) {
        return;
    }

    // If not native, check for global user functions
    MethodNode *m = global_methods;
    while (m) {
        if (strcmp(m->name, node->id) == 0) {
             // Setup arguments and execute body
             // Note: This is a simplified handling. Ideally we should share logic with interpret_call_method
             // For now, we assume global functions are mostly native or handled via METHOD_CALL_ALONE
             // But if we are here, it means it was parsed as CALL_FUNC (e.g. inside an expression)
             
             // Create a new scope/context if needed?
             // For embedded, we just execute the body.
             // But we need to map arguments.
             // Since we don't have argument mapping logic ready here, and orm_query is native,
             // we just print an error if it's not found.
             // If the user defines a global function and calls it inside an expression, it might fail here.
             // But our current goal is to fix orm_query.
             break;
        }
        m = m->next;
    }
    
    if (m) {
        // Found user function, but execution logic is missing here.
        // We rely on the fact that most user functions are called as statements (METHOD_CALL_ALONE).
        // If called as expression, we might need to implement this.
        // For now, let's assume it's not needed for orm_query.
         fprintf(stderr, "Warning: Calling user function '%s' as expression is not fully supported yet.\n", node->id);
    } else {
        te_runtime_fatalf("Error: function '%s' not defined.", node->id);
    }
}

/* Gotcha #2: invoca el resultado de otra llamada — `make(10)(5)`.
 * node->right = callee (debe evaluar a un LAMBDA), node->left = argumentos.
 * Evalúa el callee, lee el LAMBDA de __ret__, lo invoca y deja el resultado
 * de nuevo en __ret__. */
static void interpret_call_expr(ASTNode *node) {
    if (!node || !node->right) return;
    interpret_ast(node->right);
    Variable *r = find_variable("__ret__");
    if (!r || r->vtype != VAL_OBJECT || !r->type || strcmp(r->type, "LAMBDA") != 0) {
        te_runtime_fatalf("Error: expression is not callable (a function was expected).");
        return;
    }
    /* Capturar el puntero del lambda ANTES de invocar: call_lambda puede
     * sobrescribir __ret__ (y el global FASTRET que lo sombrea). */
    ASTNode *lambda = (ASTNode*)(intptr_t)r->value.object_value;
    ASTNode *res = call_lambda(lambda, node->left);
    if (res) add_or_update_variable("__ret__", res);
}

/* v0.0.24 — user-defined `@<name>` decorator guard.
 * Resolves `name` to a global LAMBDA variable (e.g. `let login = fn() => {...}`),
 * invokes it with no arguments inside the active request context, and reports
 * whether the result is "truthy":
 *   - non-empty string            -> pass (1)
 *   - non-zero int / true bool     -> pass (1)
 *   - non-zero float               -> pass (1)
 *   - non-null object              -> pass (1)
 *   - empty string "", 0, false    -> deny (0)
 * Returns -1 when `name` does not resolve to a callable lambda (misconfigured
 * guard). The caller fails closed (responds 401) on both 0 and -1. */
int te_invoke_decorator_guard(const char *name) {
    if (!name || !*name) return -1;

    Variable *fv = find_variable(name);
    if (!fv || fv->vtype != VAL_OBJECT || !fv->type ||
        strcmp(fv->type, "LAMBDA") != 0) {
        /* Not a lambda — guard not defined or not callable. */
        return -1;
    }

    ASTNode *lambda = (ASTNode*)(intptr_t)fv->value.object_value;
    ASTNode *res = call_lambda(lambda, NULL);
    if (res) add_or_update_variable("__ret__", res);

    Variable *ret = find_variable("__ret__");
    if (!ret) return 0;

    switch (ret->vtype) {
        case VAL_STRING:
            return (ret->value.string_value && ret->value.string_value[0]) ? 1 : 0;
        case VAL_INT:   /* covers BOOL (stored as 0/1) */
            return ret->value.int_value != 0 ? 1 : 0;
        case VAL_FLOAT:
            return ret->value.float_value != 0.0 ? 1 : 0;
        case VAL_OBJECT:
            return ret->value.object_value != NULL ? 1 : 0;
        default:
            return 0;
    }
}

/* CSV/LINQ columnar cache + lambda specializer now live in te_colcache.{c,h}
 * (Fase 2 paso 3). Public surface declared in te_colcache.h. */


/* ---- orderBy / groupBy support: moved to te_linq_ops.c (Nivel B paso 2.f). ---- */

/* lazy_* LINQ helpers moved to te_linq.{c,h} (Nivel B paso 1). */
/* DataFrame, te_list_df, te_df_dispatch_method now declared in te_csv.h. */

void interpret_call_method(ASTNode *node) {
    te_depth_enter();
    interpret_call_method_impl(node);
    te_depth_leave();
}

/* Extraído de interpret_call_method_impl (Fase 2): devuelve 1 si manejó la llamada. */
static int te_cm_fusion_where_chain(ASTNode *node, ASTNode *objNode) {
    if (objNode && objNode->type && strcmp(objNode->type, "CALL_METHOD") == 0 &&
        objNode->id && node->id) {
        const char *inner_m = objNode->id;
        const char *outer_m = node->id;
        int inner_is_where = (strcmp(inner_m, "where") == 0 || strcmp(inner_m, "filter") == 0);
        int outer_kind = 0; /* 1=map,2=first,3=sum */
        if      (strcmp(outer_m, "select") == 0 || strcmp(outer_m, "map") == 0) outer_kind = 1;
        else if (strcmp(outer_m, "first")  == 0 || strcmp(outer_m, "find") == 0 ||
                 strcmp(outer_m, "firstWhere") == 0) outer_kind = 2;
        else if (strcmp(outer_m, "sum")    == 0) outer_kind = 3;
        ASTNode *inner_lhs = objNode->left;
        if (inner_is_where && outer_kind &&
            inner_lhs && inner_lhs->type && inner_lhs->id &&
            (strcmp(inner_lhs->type, "ID") == 0 || strcmp(inner_lhs->type, "IDENTIFIER") == 0)) {
            Variable *lv = find_variable(inner_lhs->id);
            if (lv && lv->type && strcmp(lv->type, "LIST") == 0) {
                ASTNode *list = (ASTNode*)(intptr_t)lv->value.object_value;
                /* Resolve predicate (inner arg) */
                ASTNode *pred_arg = objNode->right;
                ASTNode *pred = NULL;
                if (pred_arg && pred_arg->type && strcmp(pred_arg->type, "LAMBDA") == 0) pred = pred_arg;
                else if (pred_arg && pred_arg->type &&
                         (strcmp(pred_arg->type, "ID") == 0 || strcmp(pred_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(pred_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        pred = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                /* Resolve outer action lambda (if any: select/firstWhere/find take fn; first/sum/firstWhere optional) */
                ASTNode *act_arg = node->right;
                ASTNode *act = NULL;
                int outer_needs_lambda = (outer_kind == 1 ||
                                          strcmp(outer_m, "find") == 0 ||
                                          strcmp(outer_m, "firstWhere") == 0);
                if (act_arg && act_arg->type && strcmp(act_arg->type, "LAMBDA") == 0) act = act_arg;
                else if (act_arg && act_arg->type &&
                         (strcmp(act_arg->type, "ID") == 0 || strcmp(act_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(act_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        act = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                /* For find/firstWhere, treat outer fn as ADDITIONAL predicate (and-combined).
                 * For plain first(), no act needed. For sum, no act needed. */
                if (pred && list && (!outer_needs_lambda || act)) {
                    /* For first(): outer_kind=2, act may be NULL (plain .first()) or a predicate (find/firstWhere). */
                    int is_find_variant = (outer_kind == 2 && act != NULL);
                    ASTNode *result = NULL;
                    double sum_acc = 0.0; int sum_is_int = 1;
                    if (outer_kind == 1) result = create_list_node(NULL);
                    ASTNode *item = list->left;
                    while (item) {
                        ASTNode *pr = call_lambda(pred, item);
                        int truthy = 0;
                        if (pr) {
                            if (pr->type && strcmp(pr->type, "STRING") == 0) {
                                truthy = (pr->str_value && pr->str_value[0]) ? 1 : 0;
                            } else if (pr->type && strcmp(pr->type, "NULL") == 0) {
                                truthy = 0;
                            } else {
                                truthy = (evaluate_expression(pr) != 0) ? 1 : 0;
                            }
                        }
                        if (truthy) {
                            if (outer_kind == 1) {
                                ASTNode *mapped = call_lambda(act, item);
                                if (mapped) te_list_append(result, mapped);
                            } else if (outer_kind == 2) {
                                if (is_find_variant) {
                                    ASTNode *ar = call_lambda(act, item);
                                    int at = 0;
                                    if (ar) {
                                        if (ar->type && strcmp(ar->type, "STRING") == 0)
                                            at = (ar->str_value && ar->str_value[0]) ? 1 : 0;
                                        else if (ar->type && strcmp(ar->type, "NULL") == 0)
                                            at = 0;
                                        else
                                            at = (evaluate_expression(ar) != 0) ? 1 : 0;
                                    }
                                    if (at) {
                                        add_or_update_variable("__ret__", build_item_from_value(item));
                                        return 1;
                                    }
                                } else {
                                    add_or_update_variable("__ret__", build_item_from_value(item));
                                    return 1;
                                }
                            } else if (outer_kind == 3) {
                                double v = item->str_value ? atof(item->str_value) : (double)item->value;
                                if (v != (double)(long long)v) sum_is_int = 0;
                                sum_acc += v;
                            }
                        }
                        item = item->next;
                    }
                    if (outer_kind == 1) {
                        add_or_update_variable("__ret__", result);
                        return 1;
                    }
                    if (outer_kind == 2) {
                        add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL));
                        return 1;
                    }
                    if (outer_kind == 3) {
                        if (sum_is_int && sum_acc == (double)(long long)sum_acc) {
                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)sum_acc, NULL, NULL));
                        } else {
                            char buf[64]; te_fmt_double(buf, sizeof(buf), sum_acc);
                            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                        }
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2): devuelve 1 si manejó la llamada. */
static int te_cm_fusion_where_aggregate(ASTNode *node, ASTNode *objNode) {
    if (objNode && objNode->type && strcmp(objNode->type, "CALL_METHOD") == 0 &&
        objNode->id && node->id) {
        const char *inner_m = objNode->id;
        const char *outer_m = node->id;
        int inner_is_where = (strcmp(inner_m, "where") == 0 || strcmp(inner_m, "filter") == 0);
        int outer_is_sumBy = (strcmp(outer_m, "sumBy") == 0);
        int outer_is_countWhere = (strcmp(outer_m, "countWhere") == 0);
        ASTNode *inner_lhs = objNode->left;
        if (inner_is_where && (outer_is_sumBy || outer_is_countWhere) &&
            inner_lhs && inner_lhs->type && inner_lhs->id &&
            (strcmp(inner_lhs->type, "ID") == 0 || strcmp(inner_lhs->type, "IDENTIFIER") == 0)) {
            Variable *lv = find_variable(inner_lhs->id);
            if (lv && lv->type && strcmp(lv->type, "LIST") == 0) {
                ASTNode *list = (ASTNode*)(intptr_t)lv->value.object_value;
                ASTNode *pred_arg = objNode->right;
                ASTNode *pred = NULL;
                if (pred_arg && pred_arg->type && strcmp(pred_arg->type, "LAMBDA") == 0) pred = pred_arg;
                else if (pred_arg && pred_arg->type &&
                         (strcmp(pred_arg->type, "ID") == 0 || strcmp(pred_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(pred_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        pred = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                ASTNode *proj_arg = node->right;
                ASTNode *proj = NULL;
                if (proj_arg && proj_arg->type && strcmp(proj_arg->type, "LAMBDA") == 0) proj = proj_arg;
                else if (proj_arg && proj_arg->type &&
                         (strcmp(proj_arg->type, "ID") == 0 || strcmp(proj_arg->type, "IDENTIFIER") == 0)) {
                    Variable *fv = find_variable(proj_arg->id);
                    if (fv && fv->vtype == VAL_OBJECT && fv->type && strcmp(fv->type, "LAMBDA") == 0)
                        proj = (ASTNode*)(intptr_t)fv->value.object_value;
                }
                if (pred && proj && list) {
                    FastLambda flp; fast_lambda_analyze(pred, &flp);
                    FastLambda flj; fast_lambda_analyze(proj, &flj);
                    /* v0.0.13 (perf) COLUMNAR FUSION: where(pred).sumBy(proj) or
                     * .countWhere(p2) where pred maps to typed column AND proj
                     * (if sumBy) is SPEC_ATTR over a numeric column. */
                    if (flp.spec != SPEC_NONE && flp.attr_name && list->col_cache) {
                        TeColCache *cc = (TeColCache*)list->col_cache;
                        int pidx = te_class_attr_idx(cc->cls, flp.attr_name);
                        int jidx = (outer_is_countWhere || flj.spec != SPEC_ATTR || !flj.attr_name)
                                    ? -1 : te_class_attr_idx(cc->cls, flj.attr_name);
                        if (pidx >= 0 && (outer_is_countWhere || jidx >= 0)) {
                            int nrow = cc->n_rows;
                            char *mask = (char*)malloc((size_t)nrow);
                            if (te_colcache_eval_pred(cc, pidx, &flp, mask)) {
                                if (outer_is_countWhere) {
                                    long long total = te_colcache_count(cc, mask);
                                    free(mask);
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)total, NULL, NULL));
                                    return 1;
                                }
                                long long itotal = 0; double dtotal = 0.0; int is_int = 1;
                                if (te_colcache_sum(cc, jidx, mask, &itotal, &dtotal, &is_int)) {
                                    free(mask);
                                    if (is_int) {
                                        if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                            add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)itotal, NULL, NULL));
                                        } else {
                                            char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                            add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                        }
                                    } else {
                                        char buf[64]; te_fmt_double(buf, sizeof(buf), dtotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                    return 1;
                                }
                            }
                            free(mask);
                        }
                    }
                    if (flp.spec != SPEC_NONE && flj.spec != SPEC_NONE) {
                        int n = 0;
                        for (ASTNode *it = list->left; it; it = it->next) n++;
#ifdef TE_HAVE_OPENMP
                        if (te_openmp_enabled() && n >= TE_OMP_MIN_N) {
                            ASTNode **arr = (ASTNode**)malloc((size_t)n * sizeof(ASTNode*));
                            int i = 0;
                            for (ASTNode *it = list->left; it; it = it->next) arr[i++] = it;
                            long long itotal = 0; double dtotal = 0.0; long long cnt = 0;
                            int fail = 0, any_float = 0;
                            #pragma omp parallel reduction(+:itotal,dtotal,cnt)
                            {
                                FastLambda flp_local = flp; flp_local.cached_class = NULL; flp_local.cached_idx = -1;
                                FastLambda flj_local = flj; flj_local.cached_class = NULL; flj_local.cached_idx = -1;
                                #pragma omp for schedule(static)
                                for (int k = 0; k < n; k++) {
                                    double vp; int vpi;
                                    if (!fast_eval(&flp_local, arr[k], &vp, &vpi)) { fail = 1; continue; }
                                    if (vp == 0.0) continue;
                                    if (outer_is_countWhere) { cnt++; continue; }
                                    double vj; int vji;
                                    if (!fast_eval(&flj_local, arr[k], &vj, &vji)) { fail = 1; continue; }
                                    if (vji) itotal += (long long)vj;
                                    else { dtotal += vj; any_float = 1; }
                                }
                            }
                            free(arr);
                            if (!fail) {
                                if (outer_is_countWhere) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)cnt, NULL, NULL));
                                } else if (!any_float) {
                                    if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                        add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)itotal, NULL, NULL));
                                    } else {
                                        char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                        add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                    }
                                } else {
                                    char buf[64]; te_fmt_double(buf, sizeof(buf), dtotal + (double)itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                                return 1;
                            }
                        }
#endif
                        /* Sequential fused fallback. */
                        long long itotal = 0; double dtotal = 0.0; long long cnt = 0;
                        int fail = 0, any_float = 0;
                        for (ASTNode *it = list->left; it; it = it->next) {
                            double vp; int vpi;
                            if (!fast_eval(&flp, it, &vp, &vpi)) { fail = 1; break; }
                            if (vp == 0.0) continue;
                            if (outer_is_countWhere) { cnt++; continue; }
                            double vj; int vji;
                            if (!fast_eval(&flj, it, &vj, &vji)) { fail = 1; break; }
                            if (vji) itotal += (long long)vj;
                            else { dtotal += vj; any_float = 1; }
                        }
                        if (!fail) {
                            if (outer_is_countWhere) {
                                add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)cnt, NULL, NULL));
                            } else if (!any_float) {
                                if (itotal >= INT_MIN && itotal <= INT_MAX) {
                                    add_or_update_variable("__ret__", create_ast_leaf_number("INT", (long long)itotal, NULL, NULL));
                                } else {
                                    char buf[32]; snprintf(buf, sizeof(buf), "%lld", itotal);
                                    add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                                }
                            } else {
                                char buf[64]; te_fmt_double(buf, sizeof(buf), dtotal + (double)itotal);
                                add_or_update_variable("__ret__", create_ast_leaf("FLOAT", 0, buf, NULL));
                            }
                            return 1;
                        }
                        /* fall through to standard chained handling */
                    }
                }
            }
        }
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2): devuelve 1 si manejó la llamada. */
static int te_cm_list_builtin(ASTNode *node, ASTNode *objNode, Variable *v) {
    if (v && v->type && strcmp(v->type, "LIST") == 0) {
        ASTNode *list = (ASTNode*)(intptr_t)v->value.object_value;

        /* ===== Nivel B paso 2.f: LINQ-with-lambda dispatcher (~1040 LOC).
         * Implemented in te_linq_ops.c. Methods: map/filter/reduce/forEach/
         * find/any/every/none/where/select/all/firstWhere/lastWhere/countWhere/
         * sumBy/avgBy/minBy/maxBy/takeWhile/skipWhile/flatMap/selectMany/groupBy/
         * orderBy/orderByDescending/distinctBy/aggregate/fold/toMap/toDictionary,
         * incl. COLUMNAR + OpenMP fast-paths. ===== */
        if (te_linq_ops_method_dispatch(node, list)) return 1;


        /* ===== v0.0.12 #8 Lazy iterator promotion + v0.0.11 numeric/no-arg LINQ.
         * Dispatched in te_linq.c (Nivel B paso 2.e). ===== */
        if (te_linq_list_method_dispatch(node, list)) return 1;

        if (list && node->id && strcmp(node->id, "push") == 0) {
            ASTNode *arg = node->right;
            if (!arg) return 1;
            ASTNode *new_item = (ASTNode*)calloc(1, sizeof(ASTNode));
            memset(new_item, 0, sizeof(ASTNode));
            if (arg->type && (strcmp(arg->type, "OBJECT_LITERAL") == 0 ||
                              strcmp(arg->type, "MAP") == 0)) {
                /* gotcha #18: push of a `{...}` object literal. Previously fell
                 * through to evaluate_expression (-> 0), storing [0,0,...].
                 * Snapshot the literal so each push is an independent, readable
                 * map item (works with list[i]["k"], list.length, etc.). */
                ASTNode *snap = te_snapshot_object_literal(arg);
                free(new_item);
                if (snap) {
                    snap->next = NULL;
                    te_list_append(list, snap);
                    te_colcache_invalidate(list);
                }
                return 1;
            }
            if (arg->type && strcmp(arg->type, "OBJECT") == 0) {
                /* Fix: empujar objetos creados con `new ClaseX(args)`.
                 * El parser eager-construye un OBJECT ASTNode con extra=ObjectNode*
                 * y left=args, pero no corre el constructor. Para que cada push
                 * en un loop produzca instancias independientes, clonamos el
                 * objeto y corremos el constructor con los args reevaluados. */
                ObjectNode *obj_orig = NULL;
                if (arg->extra) obj_orig = (ObjectNode*)arg->extra;
                else obj_orig = (ObjectNode*)(intptr_t)arg->value;
                if (obj_orig && obj_orig->class) {
                    ObjectNode *obj_clone = clone_object(obj_orig);
                    MethodNode *m = obj_clone->class->methods;
                    while (m && strcmp(m->name, "__constructor") != 0) m = m->next;
                    if (m) {
                        ParameterNode *p = m->params;
                        ASTNode *carg = arg->left;
                        while (p && carg) {
                            ASTNode *vn = NULL;
                            if (carg->type && strcmp(carg->type, "STRING") == 0) {
                                vn = create_ast_leaf("STRING", 0, carg->str_value, NULL);
                            } else if (carg->type && strcmp(carg->type, "FLOAT") == 0) {
                                vn = create_ast_leaf("FLOAT", 0, carg->str_value, NULL);
                            } else if (carg->type && (strcmp(carg->type, "ID") == 0 || strcmp(carg->type, "IDENTIFIER") == 0)) {
                                Variable *vv = find_variable(carg->id);
                                if (vv && vv->vtype == VAL_STRING) {
                                    vn = create_ast_leaf("STRING", 0, strdup(vv->value.string_value), NULL);
                                } else if (vv && vv->vtype == VAL_FLOAT) {
                                    char fbuf[64];
                                    te_fmt_double(fbuf, sizeof(fbuf), vv->value.float_value);
                                    vn = create_ast_leaf("FLOAT", 0, fbuf, NULL);
                                } else if (vv) {
                                    vn = create_ast_leaf_number("INT", vv->value.int_value, NULL, NULL);
                                } else {
                                    vn = create_ast_leaf_number("INT", 0, NULL, NULL);
                                }
                            } else {
                                long long val = (long long)evaluate_expression(carg);
                                vn = create_ast_leaf_number("INT", val, NULL, NULL);
                            }
                            add_or_update_variable(p->name, vn);
                            p = p->next;
                            carg = carg->next; /* gotcha #1: step ctor args via ->next */
                        }
                        call_method(obj_clone, "__constructor");
                        g_vm.return_flag = 0;
                        g_vm.return_node = NULL;
                    }
                    new_item->type = strdup("OBJECT");
                    new_item->extra = (struct ASTNode*)obj_clone;
                    new_item->value = (int)(intptr_t)obj_clone;
                } else {
                    /* Sin clase resoluble: degradar a NUMBER 0 para no segfaultear */
                    new_item->type = strdup("NUMBER");
                    new_item->value = 0;
                }
            } else if (arg->type && strcmp(arg->type, "STRING") == 0) {
                new_item->type = strdup("STRING");
                new_item->str_value = strdup(arg->str_value);
            } else if (arg->type && (strcmp(arg->type, "IDENTIFIER") == 0 || strcmp(arg->type, "ID") == 0)) {
                Variable *av = find_variable(arg->id);
                if (av && av->vtype == VAL_OBJECT && av->value.object_value) {
                    /* Root fix (prod UAF SIGSEGV in te_expr_is_null/is_string_type):
                     * a CLASS instance pushed via a variable used to SHARE the
                     * ObjectNode pointer. When the source variable's object was
                     * later freed (free_object_node on scope/lambda teardown) the
                     * list element dangled -> use-after-free reading
                     * obj->class->attributes[i].id (== NULL after the chunk was
                     * reused). Clone the instance so the list OWNS its own copy,
                     * exactly like the push(new X()) and push({...}) paths already
                     * do. Only class instances (type "OBJECT") are cloned;
                     * MAP/LIST/LAMBDA (also VAL_OBJECT) keep the shared pointer. */
                    if (av->type && strcmp(av->type, "OBJECT") == 0) {
                        ObjectNode *cl = clone_object((ObjectNode*)av->value.object_value);
                        new_item->type = strdup("OBJECT");
                        new_item->extra = (struct ASTNode*)cl;
                        new_item->value = (int)(intptr_t)cl;
                    } else {
                        new_item->type = strdup("OBJECT");
                        new_item->extra = (struct ASTNode*)av->value.object_value;
                        new_item->value = (int)(intptr_t)av->value.object_value;
                    }
                } else if (av && av->vtype == VAL_STRING) {
                    new_item->type = strdup("STRING");
                    new_item->str_value = strdup(av->value.string_value);
                } else if (av && av->vtype == VAL_FLOAT) {
                    new_item->type = strdup("FLOAT");
                    char buf[64]; te_fmt_double(buf, sizeof(buf), av->value.float_value);
                    new_item->str_value = strdup(buf);
                } else if (av) {
                    new_item->type = strdup("NUMBER");
                    new_item->value = av->value.int_value;
                }
            } else {
                double vv = evaluate_expression(arg);
                if (vv == (int)vv) { new_item->type = strdup("NUMBER"); new_item->value = (int)vv; }
                else { new_item->type = strdup("FLOAT"); char buf[64]; te_fmt_double(buf, sizeof(buf), vv); new_item->str_value = strdup(buf); }
            }
            new_item->next = NULL;
            te_list_append(list, new_item);   /* Ola 14b: O(1) amortizado */
            te_colcache_invalidate(list);     /* v0.0.13 (perf) */
            return 1;
        }
        if (list && node->id && strcmp(node->id, "pop") == 0) {
            ASTNode *cur = list->left;
            if (!cur) { add_or_update_variable("__ret__", create_ast_leaf("NULL", 0, NULL, NULL)); return 1; }
            te_colcache_invalidate(list);     /* v0.0.13 (perf) */
            if (!cur->next) {
                /* single element: capture it, then empty the list */
                add_or_update_variable("__ret__", build_item_from_value(cur));
                list->left = NULL; te_invalidate_list_cache(list); return 1;
            }
            while (cur->next && cur->next->next) cur = cur->next;
            /* cur->next is the last element — capture its value into __ret__ */
            add_or_update_variable("__ret__", build_item_from_value(cur->next));
            cur->next = NULL;
            te_invalidate_list_cache(list);  /* Ola 14 */
            return 1;
        }
        /* Ola 13: list extras (size/length/contains/reverse/sort/get) + join.
         * Dispatched in te_list.c (Nivel B paso 2.c). */
        if (te_list_method_dispatch(node, list)) return 1;
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2): devuelve 1 si manejó la llamada. */
static int te_cm_map_builtin(ASTNode *node, ASTNode *objNode, Variable *v) {
    if (v && v->type &&
        (strcmp(v->type, "MAP") == 0 || strcmp(v->type, "OBJECT_LITERAL") == 0)) {
        ASTNode *map = (ASTNode*)(intptr_t)v->value.object_value;
        if (te_map_method_dispatch(node, map)) return 1;
        fprintf(stderr, "[method] unknown method '%s' on %s value\n",
                node->id ? node->id : "?", v->type);
        ASTNode *nullret = create_ast_leaf("NULL", 0, NULL, NULL);
        add_or_update_variable("__ret__", nullret);
        free_ast(nullret);
        return 1;
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_cm_bridge(ASTNode *node, ObjectNode *obj, Variable *v) {
    if (obj && strcmp(obj->class->name, "Bridge") == 0) {
        if (g_debug_mode) te_log_ast("Calling native bridge: %s.%s", v->id, node->id);
        // Aquí es donde la magia ocurre. Delegamos al manejador específico del bridge.
        if (strcmp(v->id, "Chat") == 0) {
            if (g_bridge_handlers.handle_chat_bridge) g_bridge_handlers.handle_chat_bridge(node->id, node->right);
        } else if (strcmp(v->id, "NLU") == 0) {
            if (g_debug_mode) te_log_ast("Calling NLU bridge");
            if (g_bridge_handlers.handle_nlu_bridge) {
                g_bridge_handlers.handle_nlu_bridge(node->id, node->right);
                // Diagnostic: did the bridge set __ret__? (only print when debug mode enabled)
                if (g_debug_mode) {
                    if (__ret_var_active) {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=1 type='%s'", __ret_var.type ? __ret_var.type : "(null)");
                    } else {
                        te_log_ast("[DEBUG] After NLU bridge: __ret__ active=0");
                    }
                }
            }
        } else if (strcmp(v->id, "API") == 0) {
            if (g_bridge_handlers.handle_api_bridge) g_bridge_handlers.handle_api_bridge(node->id, node->right);
        } else if (strcmp(v->id, "Gemini") == 0) {
            if (g_bridge_handlers.handle_gemini_bridge) g_bridge_handlers.handle_gemini_bridge(node->id, node->right);
        } else {
            printf("Warning: Bridge '%s' unknown or not implemented in this executable.\n", v->id);
        }
        // Los bridges pueden o no devolver un valor. Si lo hacen, lo ponen en __ret_var.
        // Por ahora, no necesitamos un valor de retorno falso.
        // ASTNode* dummy_return = create_ast_leaf("STRING", 0, "dummy_return_from_bridge", NULL);
        // add_or_update_variable("__ret__", dummy_return);
        // free_ast(dummy_return);
        return 1; // Skip normal method dispatch
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_cm_bind_this(ASTNode *node, ObjectNode *obj, Variable *v) {
    /* ====================================================================
     * Ola 3 Fase D (perf): FAST `this` setup.
     * Cached: persistent Variable* "this" + single reusable wrapper.
     * Hot path patches both pointers (no calloc/strdup/add_or_update).
     * Switch: TYPEEASY_NO_FASTTHIS=1.
     * ==================================================================== */
    static int      ft_init        = 0;
    static int      ft_enabled     = 1;
    static Variable *g_this_var    = NULL;
    static ASTNode  *g_this_wrap   = NULL;
    if (!ft_init) {
        const char *e = getenv("TYPEEASY_NO_FASTTHIS");
        if (e && e[0] && e[0] != '0') ft_enabled = 0;
        ft_init = 1;
    }
    if (ft_enabled && g_this_var && g_this_wrap) {
        /* Hot path: just patch the cached objects. */
        g_this_var->vtype              = VAL_OBJECT;
        g_this_var->value.object_value = obj;
        g_this_wrap->extra             = (struct ASTNode*)obj;
    } else {
        /* Cold path: original setup, plus capture the cache. */
        ASTNode *thisNode = calloc(1, sizeof(ASTNode));
        thisNode->type  = strdup("OBJECT");
        thisNode->id    = strdup("this");
        thisNode->left  = thisNode->right = NULL;
        thisNode->extra = (struct ASTNode*)obj;
        thisNode->value = 0;
        add_or_update_variable("this", thisNode);
        if (ft_enabled) {
            g_this_var  = find_variable_for("this");
            g_this_wrap = thisNode;
        }
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_cm_bind_args(ASTNode *node, MethodNode *m, ObjectNode *obj) {
    ParameterNode *p = m->params;
    ASTNode      *arg = node->right; // CORRECCIÓN
    /* ====================================================================
     * Ola 3 Fase B: FAST CALL PATH
     * --------------------------------------------------------------------
     * If every declared parameter is numeric (int/float) we bypass
     * create_ast_leaf_number + add_or_update_variable per arg and write
     * directly into the param's cached Variable*. Switch TYPEEASY_NO_FASTCALL=1
     * to disable.
     * ==================================================================== */
    {
        static int fc_init = 0;
        static int fc_enabled = 1;
        if (!fc_init) {
            const char *e = getenv("TYPEEASY_NO_FASTCALL");
            if (e && e[0] && e[0] != '0') fc_enabled = 0;
            fc_init = 1;
        }
        if (fc_enabled && p) {
            int all_numeric = 1;
            ParameterNode *pp = p;
            while (pp) {
                if (!pp->type
                    || (strcmp(pp->type, "int")    != 0
                     && strcmp(pp->type, "float")  != 0
                     && strcmp(pp->type, "INT")    != 0
                     && strcmp(pp->type, "FLOAT")  != 0)) {
                    all_numeric = 0; break;
                }
                pp = pp->next;
            }
            if (all_numeric) {
                ParameterNode *cur_p = p;
                ASTNode       *cur_a = arg;
                while (cur_p && cur_a) {
                    Variable *fv = (Variable*)cur_p->cached_var;
                    if (!fv) {
                        fv = find_variable_for(cur_p->name);
                        if (!fv) {
                            /* Create a fresh slot only once. */
                            if (g_vm.var_count < MAX_VARS) {
                                g_vm.vars[g_vm.var_count].id       = strdup(cur_p->name);
                                g_vm.vars[g_vm.var_count].type     = strdup(cur_p->type);
                                g_vm.vars[g_vm.var_count].is_const = 0;
                                g_vm.vars[g_vm.var_count].vtype    = (strcmp(cur_p->type, "float")==0
                                                           || strcmp(cur_p->type, "FLOAT")==0)
                                                           ? VAL_FLOAT : VAL_INT;
                                if (g_vm.vars[g_vm.var_count].vtype == VAL_FLOAT)
                                    g_vm.vars[g_vm.var_count].value.float_value = 0.0;
                                else
                                    g_vm.vars[g_vm.var_count].value.int_value = 0;
                                fv = &g_vm.vars[g_vm.var_count];
                                g_vm.var_count++;
                            }
                        }
                        cur_p->cached_var = fv;
                    }
                    if (fv) {
                        double d = evaluate_expression(cur_a);
                        if (fv->vtype == VAL_FLOAT) {
                            fv->value.float_value = d;
                        } else {
                            fv->vtype = VAL_INT;
                            fv->value.int_value  = (int)d;
                        }
                    }
                    cur_p = cur_p->next;
                    cur_a = cur_a->next; /* gotcha #1: step args via ->next */
                }
                /* args bound directly; skip slow path */
                goto fastcall_args_done;
            }
        }
    }
    while (p && arg) {
        ASTNode *vn = NULL;
        if (arg->type && strcmp(arg->type, "STRING") == 0) {
            vn = create_ast_leaf("STRING", 0, arg->str_value, NULL);
        } else if (arg->type && (strcmp(arg->type,"ID")==0 || strcmp(arg->type,"IDENTIFIER")==0)) {
            Variable *v_arg = find_variable(arg->id);
            if (!v_arg) {
                printf("Error: variable '%s' not found.\n", arg->id);
                return 1;
            }
            if (v_arg->vtype == VAL_STRING) {
                vn = create_ast_leaf("STRING", 0, strdup(v_arg->value.string_value), NULL);
            } else {
                vn = create_ast_leaf_number("INT", v_arg->value.int_value, NULL, NULL);
            }
        } else {
            int val = evaluate_expression(arg);
            vn = create_ast_leaf_number("INT", val, NULL, NULL);
        }
        add_or_update_variable(p->name, vn);
        p   = p->next;
        arg = arg->next; /* gotcha #1: step args via ->next */
    }
fastcall_args_done:
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_cm_body_bytecode(ASTNode *node, MethodNode *m, ObjectNode *obj) {
    /* ====================================================================
     * Ola 4 (perf): METHOD-BODY BYTECODE.
     * If the body is a single `return <numeric expr>;` and the declared
     * return type is int/float, run a precompiled bytecode program that
     * reads parameters and `this.attr` directly from cached pointers.
     * Skips both interpret_ast(m->body) and the FAST RETURN evaluator.
     * Switch: TYPEEASY_NO_BCMETHOD=1 disables.
     * ==================================================================== */
    {
        static int bcm_init = 0;
        static int bcm_enabled = 1;
        if (!bcm_init) {
            const char *e = getenv("TYPEEASY_NO_BCMETHOD");
            if (e && e[0] && e[0] != '0') bcm_enabled = 0;
            bcm_init = 1;
        }
        if (bcm_enabled
            && m->return_type
            && (strcmp(m->return_type, "int")   == 0
             || strcmp(m->return_type, "float") == 0)
            && obj && obj->class) {
            BCInfo *bi = bc_get_or_compile_method(m, obj->class);
            if (bi) {
                ObjectNode *saved_this = g_bc_this;
                g_bc_this = obj;
                double rv = bc_exec(bi->code);
                g_bc_this = saved_this;
                /* Write __ret_var directly (mirrors FAST RETURN path). */
                if (__ret_var_active) {
                    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                        free(__ret_var.value.string_value);
                        __ret_var.value.string_value = NULL;
                    }
                    if (__ret_var.id)   { free(__ret_var.id);   __ret_var.id   = NULL; }
                    if (__ret_var.type) { free(__ret_var.type); __ret_var.type = NULL; }
                }
                __ret_var.id = strdup("__ret__");
                if (strcmp(m->return_type, "float") == 0) {
                    __ret_var.type  = strdup("FLOAT");
                    __ret_var.vtype = VAL_FLOAT;
                    __ret_var.value.float_value = rv;
                } else {
                    __ret_var.type  = strdup("INT");
                    __ret_var.vtype = VAL_INT;
                    __ret_var.value.int_value = (long long)rv;
                }
                __ret_var_active = 1;
                g_vm.return_flag = 0;
                g_vm.return_node = NULL;
                return 1;
            }
        }
    }
    return 0;
}

/* Extraído de interpret_call_method_impl (Fase 2). Devuelve 1 si manejó la llamada. */
static int te_cm_materialize_return(ASTNode *node, MethodNode *m, ObjectNode *obj) {
        if (g_vm.return_flag && g_vm.return_node) {
            /* ============================================================
             * Ola 3 Fase B: FAST RETURN path.
             * If the declared return type is numeric and the return
             * expression is NOT a special form (STRING/CALL/ACCESS_ATTR/
             * pure identifier), write the result straight into __ret_var
             * without allocating an intermediate ASTNode literal.
             * Switch: TYPEEASY_NO_FASTRET=1
             * ============================================================ */
            {
                static int fr_init = 0;
                static int fr_enabled = 1;
                if (!fr_init) {
                    const char *e = getenv("TYPEEASY_NO_FASTRET");
                    if (e && e[0] && e[0] != '0') fr_enabled = 0;
                    fr_init = 1;
                }
                if (fr_enabled
                    && m->return_type
                    && (strcmp(m->return_type, "int")   == 0
                     || strcmp(m->return_type, "float") == 0)
                    && g_vm.return_node->type
                    && strcmp(g_vm.return_node->type, "STRING")      != 0
                    && strcmp(g_vm.return_node->type, "CALL_FUNC")   != 0
                    && strcmp(g_vm.return_node->type, "CALL_METHOD") != 0
                    && strcmp(g_vm.return_node->type, "RETURN_JSON") != 0
                    && strcmp(g_vm.return_node->type, "RETURN_XML")  != 0) {
                    long long rv_i64; double rv; int rv_is_i64 = te_eval_num(g_vm.return_node, &rv_i64, &rv);   /* Fase 1b */
                    /* Reset and write __ret_var directly. */
                    if (__ret_var_active) {
                        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                            free(__ret_var.value.string_value);
                            __ret_var.value.string_value = NULL;
                        }
                        if (__ret_var.id) { free(__ret_var.id); __ret_var.id = NULL; }
                        if (__ret_var.type) { free(__ret_var.type); __ret_var.type = NULL; }
                    }
                    __ret_var.id   = strdup("__ret__");
                    if (strcmp(m->return_type, "float") == 0) {
                        __ret_var.type  = strdup("FLOAT");
                        __ret_var.vtype = VAL_FLOAT;
                        __ret_var.value.float_value = rv;
                    } else {
                        __ret_var.type  = strdup("INT");
                        __ret_var.vtype = VAL_INT;
                        __ret_var.value.int_value = rv_is_i64 ? rv_i64 : (long long)rv;
                    }
                    __ret_var_active = 1;
                    g_vm.return_flag = 0;
                    g_vm.return_node = NULL;
                    return 1;
                }
            }
            /* `return <call>` (e.g. `return Math.sqrt(x)` or `return helper()`):
             * the nested call already placed its result in __ret__ while the
             * return expression was evaluated. Skip the literal re-extraction
             * below (which would treat return_node->id as a variable name and
             * fail). The json builtin keeps its dedicated path below. */
            if (g_vm.return_node->type
                && (strcmp(g_vm.return_node->type, "CALL_METHOD") == 0
                 || (strcmp(g_vm.return_node->type, "CALL_FUNC") == 0
                     && !(g_vm.return_node->id && strcmp(g_vm.return_node->id, "json") == 0)))) {
                g_vm.return_flag = 0;
                g_vm.return_node = NULL;
                return 1;
            }
            // If the return is a call to json, print the JSON output
            if (g_vm.return_node && g_vm.return_node->type && strcmp(g_vm.return_node->type, "CALL_FUNC") == 0 && g_vm.return_node->id && strcmp(g_vm.return_node->id, "json") == 0) {
            //    printf("[DIAG] Entrando a native_json desde interpret_call_method\n");
                native_json(g_vm.return_node->left);
            } else {
                ASTNode *lit = NULL;
                if (g_vm.return_node->type && strcmp(g_vm.return_node->type, "STRING") == 0) {
                    lit = create_ast_leaf("STRING", 0, g_vm.return_node->str_value, NULL);
                } else if (g_vm.return_node->id) {
                    Variable *rv = find_variable(g_vm.return_node->id);
                    if (rv) {
                        if (rv->vtype == VAL_STRING) {
                            lit = create_ast_leaf("STRING", 0, strdup(rv->value.string_value), NULL);
                        } else if (rv->vtype == VAL_FLOAT) {
                            lit = create_ast_leaf("FLOAT", 0, double_to_string(rv->value.float_value), NULL);
                        } else if (rv->vtype == VAL_INT) {
                            lit = create_ast_leaf_number("INT", rv->value.int_value, NULL, NULL);
                        }
                    } else {
                        printf("Error: variable '%s' not found in return statement.\n", g_vm.return_node->id);
                        return 1;
                    }
                } else if (g_vm.return_node->type && strcmp(g_vm.return_node->type, "ACCESS_ATTR") == 0) {
                    ASTNode *objN = g_vm.return_node->left;
                    ASTNode *attrN = g_vm.return_node->right;
                    Variable *ov = find_variable(objN->id);
                    if (ov && ov->vtype == VAL_OBJECT && ov->type && strcmp(ov->type, "OBJECT") == 0) {
                        ObjectNode *oobj = ov->value.object_value; int i = -1;
                        int idx = -1;
                        for (i=0; i<oobj->class->attr_count; i++) { // Inicializa 'i'
                            if (strcmp(oobj->class->attributes[i].id, attrN->id)==0) { idx=i; break; }
                        }
                        if (idx>=0) {
                            if (strcmp(oobj->class->attributes[idx].type,"string")==0)
                                lit = create_ast_leaf("STRING",0,strdup(oobj->attributes[idx].value.string_value),NULL);
                            else if (strcmp(oobj->attributes[idx].type,"float")==0)
                                lit = create_ast_leaf("FLOAT",0,double_to_string(oobj->attributes[idx].value.float_value),NULL); // Usar idx
                            else
                                lit = create_ast_leaf_number("INT",oobj->attributes[idx].value.int_value,NULL,NULL);
                        }
                    }
                } else if (g_vm.return_node->type
                           && strcmp(g_vm.return_node->type, "ADD") == 0
                           && is_string_type(g_vm.return_node)) {
                    /* String concatenation, e.g. `return "Hi " + this.name;`.
                     * Resolve to a STRING value instead of a numeric ADD. */
                    char *s = get_node_string(g_vm.return_node);
                    lit = create_ast_leaf("STRING", 0, s ? s : "", NULL);
                    if (s) free(s);
                } else if (g_vm.return_node->type
                           && (strcmp(g_vm.return_node->type, "GT") == 0
                            || strcmp(g_vm.return_node->type, "LT") == 0
                            || strcmp(g_vm.return_node->type, "EQ") == 0
                            || strcmp(g_vm.return_node->type, "GT_EQ") == 0
                            || strcmp(g_vm.return_node->type, "LT_EQ") == 0
                            || strcmp(g_vm.return_node->type, "DIFF") == 0
                            || strcmp(g_vm.return_node->type, "AND") == 0
                            || strcmp(g_vm.return_node->type, "OR") == 0
                            || strcmp(g_vm.return_node->type, "NOT") == 0)) {
                    /* Comparison / logical operator yields a boolean (0|1).
                     * Tag it as BOOL so the type check accepts `: bool` and
                     * print emits true/false. */
                    int b = evaluate_expression(g_vm.return_node) != 0 ? 1 : 0;
                    lit = create_ast_leaf_number("BOOL", b, NULL, NULL);
                } else {
                    double rv_double = evaluate_expression(g_vm.return_node);
                    if (rv_double == (int)rv_double) {
                        lit = create_ast_leaf_number("INT", (long long)rv_double, NULL, NULL);
                    } else {
                        lit = create_ast_leaf("FLOAT", 0, double_to_string(rv_double), NULL);
                    }
                }
                if (lit) {
                    // --- TYPE CHECK: validate lit->type against declared return_type ---
                    if (m->return_type
                        && strcmp(m->return_type, "dynamic") != 0
                        && strcmp(m->return_type, "void") != 0
                        && lit->type) {
                        const char *expected = m->return_type;
                        const char *actual = lit->type;
                        /* T? : strip trailing '?' for the comparison; null is always OK for optional */
                        char base_expected[64];
                        size_t el = strlen(expected);
                        int optional = (el > 0 && expected[el-1] == '?');
                        if (optional) { strncpy(base_expected, expected, el-1); base_expected[el-1] = '\0'; }
                        else { strncpy(base_expected, expected, sizeof(base_expected)-1); base_expected[sizeof(base_expected)-1]='\0'; }
                        if (optional && strcmp(actual, "NULL") == 0) {
                            /* OK: optional type returning null */
                        } else {
                            const char *expected_cmp = base_expected;
                            int ok = 0;
                            if (strcmp(expected_cmp, "int") == 0    && strcmp(actual, "INT") == 0)    ok = 1;
                            if (strcmp(expected_cmp, "string") == 0 && strcmp(actual, "STRING") == 0) ok = 1;
                            if (strcmp(expected_cmp, "float") == 0  && (strcmp(actual, "FLOAT") == 0 || strcmp(actual, "INT") == 0)) ok = 1;
                            if (strcmp(expected_cmp, "bool") == 0   && (strcmp(actual, "BOOL") == 0 || strcmp(actual, "INT") == 0)) ok = 1;
                            if (!ok) {
                                const char *actual_lower = "?";
                                if (strcmp(actual, "INT") == 0)    actual_lower = "int";
                                else if (strcmp(actual, "STRING") == 0) actual_lower = "string";
                                else if (strcmp(actual, "FLOAT") == 0)  actual_lower = "float";
                                else if (strcmp(actual, "BOOL") == 0)   actual_lower = "bool";
                                else if (strcmp(actual, "NULL") == 0)   actual_lower = "null";
                                char buf[256];
                                snprintf(buf, sizeof(buf),
                                    "TypeError: method '%s' is declared as '%s' but returns a value of type '%s'.",
                                    m->name, expected, actual_lower);
                                if (throw_message) free(throw_message);
                                throw_message = strdup(buf);
                                g_vm.throw_flag = 1;
                                g_vm.return_flag = 0; g_vm.return_node = NULL;
                                return 1;
                            }
                        }
                    }
                    add_or_update_variable("__ret__", lit);
                }
            }
            g_vm.return_flag = 0;
            g_vm.return_node = NULL;
        }
    return 0;
}

static void interpret_call_method_impl(ASTNode *node) {
    ASTNode *objNode = node->left;

    /* ===== Gotcha #2: método sobre literal — `[1,2,3].map(...)`, `{...}.keys()`.
     * Si el receptor es un literal LIST/OBJECT_LITERAL no tiene `id`, así que la
     * resolución `find_variable(objNode->id)` da NULL y ningún dispatcher LIST/MAP
     * corre (silenciosamente no hace nada / null-deref). Materializamos el literal
     * en una variable temporal y reescribimos node->left a un ID, igual que el
     * manejador de llamadas encadenadas más abajo. ===== */
    if (objNode && objNode->type && !objNode->id &&
        (strcmp(objNode->type, "LIST") == 0 || strcmp(objNode->type, "OBJECT_LITERAL") == 0)) {
        static int _lit_seq = 0;
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "__lit_%d__", _lit_seq++);
        add_or_update_variable(tmp, objNode);
        /* add_or_update_variable guarda OBJECT_LITERAL con type "OBJECT_LITERAL",
         * pero los dispatchers de MAP exigen type "MAP" (igual que declare_variable). */
        if (strcmp(objNode->type, "OBJECT_LITERAL") == 0) {
            Variable *tv = find_variable(tmp);
            if (tv && tv->type) { free(tv->type); tv->type = strdup("MAP"); }
        }
        ASTNode *id = (ASTNode*)calloc(1, sizeof(ASTNode));
        id->type = strdup("ID");
        id->id = strdup(tmp);
        node->left = id;
        objNode = id;
    }

    /* ===== v0.0.11-pre: DataFrame analytics fast-path =====
     * Si el receptor es un LIST con DataFrame columnar adjunto, intentamos
     * despachar a sum/min/max/count/group_sum/print directamente sobre las
     * columnas (SIMD AVX2 + pthread parallel). Si el método no aplica,
     * caemos al dispatch estándar. */
    {
        ASTNode *recv_list = NULL;
        if (objNode && objNode->type) {
            if (strcmp(objNode->type, "LIST") == 0) {
                recv_list = objNode;
            } else if (strcmp(objNode->type, "ID") == 0 || strcmp(objNode->type, "IDENTIFIER") == 0) {
                Variable *lv = find_variable(objNode->id);
                if (lv && lv->type && strcmp(lv->type, "LIST") == 0)
                    recv_list = (ASTNode*)(intptr_t)lv->value.object_value;
            }
        }
        DataFrame *df_recv = te_list_df(recv_list);
        if (df_recv) {
            if (te_df_dispatch_method(df_recv, node)) return;
            /* fallthrough si el método no es analítico */
        }
    }

    /* ===== v0.0.12 #5 Fusion peephole: where(p).{select|map|first|find|firstWhere|sum} =====
     * Detect `xs.where(p).OUTER(...)` where xs is an ID resolving to a LIST.
     * Avoids materializing the intermediate filtered list. Falls through to
     * standard chained-call handling if pattern doesn't match. */
    if (te_cm_fusion_where_chain(node, objNode)) return;

    /* ===== v0.0.12 #N Fusion-parallel: where(pred).sumBy(proj) / .countWhere(p2) =====
     * Detect `xs.where(pred).sumBy(proj)` and `xs.where(pred).countWhere(p2)`
     * where BOTH lambdas are fast-pathable (SPEC_*). Runs as a single parallel
     * pass with OpenMP, never materializing the intermediate filtered list.
     * Falls through if any lambda is not fast-pathable. */
    if (te_cm_fusion_where_aggregate(node, objNode)) return;

    /* v0.0.11: chained method/function call — `a.foo().bar()` or `foo().bar()`.
     * Evaluate the inner call first, bind result to a unique temp variable,
     * and rewrite node->left to an ID node so the rest of this function
     * (which assumes objNode is an identifier) works unchanged. */
    if (objNode && objNode->type &&
        (strcmp(objNode->type, "CALL_METHOD") == 0 || strcmp(objNode->type, "CALL_FUNC") == 0)) {
        if (strcmp(objNode->type, "CALL_METHOD") == 0) interpret_call_method(objNode);
        else interpret_call_func(objNode);
        Variable *rv = find_variable("__ret__");
        if (rv && rv->vtype == VAL_OBJECT && rv->value.object_value) {
            static int _chain_seq = 0;
            char tmp[40];
            snprintf(tmp, sizeof(tmp), "__chain_%d__", _chain_seq++);
            ASTNode *holder = (ASTNode*)calloc(1, sizeof(ASTNode));
            holder->type = strdup(rv->type ? rv->type : "LIST");
            /* For LIST/MAP, add_or_update_variable stores value as ASTNode*
             * via object_value, so synthesize a wrapper that points to it. */
            ASTNode *inner = (ASTNode*)(intptr_t)rv->value.object_value;
            holder->left = inner ? inner->left : NULL;
            holder->right = inner ? inner->right : NULL;  /* v0.0.12 #8: preserve LAZY_ITER op chain */
            holder->str_value = inner ? inner->str_value : NULL;
            holder->value = inner ? inner->value : 0;
            add_or_update_variable(tmp, holder);
            ASTNode *id = (ASTNode*)calloc(1, sizeof(ASTNode));
            id->type = strdup("ID");
            id->id = strdup(tmp);
            node->left = id;
            objNode = id;
        } else if (rv && rv->vtype == VAL_STRING) {
            /* v0.0.30: el call interno devolvió un STRING; materialízalo en una
             * var temporal para que los métodos de string (.trim/.upper/.lower/
             * .contains/.split/...) puedan despacharse sobre el resultado de la
             * cadena. Sin esto, `x.trim().upper()`, `f().trim()` o
             * `request_query("q").trim()` fallan con
             * "'<interno>' is not a valid object" (gotcha .trim en --api). */
            static int _chain_str_seq = 0;
            char tmp[40];
            snprintf(tmp, sizeof(tmp), "__chain_s_%d__", _chain_str_seq++);
            ASTNode *sid = create_ast_leaf("STRING", 0,
                rv->value.string_value ? rv->value.string_value : "", NULL);
            add_or_update_variable(tmp, sid);
            ASTNode *id = (ASTNode*)calloc(1, sizeof(ASTNode));
            id->type = strdup("ID");
            id->id = strdup(tmp);
            node->left = id;
            objNode = id;
        }
    }

    Variable *v = (objNode && objNode->id) ? find_variable(objNode->id) : NULL;
    g_vm.return_flag = 0;
    g_vm.return_node = NULL;

    /* Bug ERP 2026-08-27 (Bug B): método sobre una expresión PARENTIZADA de
     * string — `("" + x).lower()` dentro de una fn. El receptor es un nodo
     * ADD/STRING_INTERP sin id: ningún dispatcher corría y el caller abortaba
     * con "no return value captured from expression 'CALL_METHOD'".
     * Materializamos el string en una Variable de STACK y despachamos directo
     * (sin reescribir node->left: el AST queda intacto y la expresión se
     * re-evalúa en cada llamada). Si no era un método de string, seguimos por
     * el flujo normal. */
    if (objNode && objNode->type && !objNode->id &&
        (strcmp(objNode->type, "ADD") == 0 ||
         strcmp(objNode->type, "STRING_INTERP") == 0) &&
        is_string_type(objNode)) {
        char *s = get_node_string(objNode);
        Variable sv;
        memset(&sv, 0, sizeof(sv));
        sv.vtype = VAL_STRING;
        sv.value.string_value = s ? s : "";
        int handled = te_string_method_dispatch(node, NULL, &sv);
        if (s) free(s);
        if (handled) return;
    }

    /* Fase 7: null-safe ?. — if obj is null, set __ret__ = null and return */
    if (node->value == 1) {
        if (!v || (v->type && strcmp(v->type, "NULL") == 0)) {
            ASTNode *r = create_ast_leaf("NULL", 0, NULL, NULL);
            add_or_update_variable("__ret__", r);
            return;
        }
    }

    /* Fase 8: Math.* — pseudo-static methods on identifier "Math".
     * Implementation moved to te_math.{c,h} (Nivel B paso 2.a). */
    if (te_math_method_dispatch(node, objNode)) return;

    /* Fase 8: string methods (.upper/.lower/.trim/.contains/.split/.length
     * + Ola 13 extras). Dispatched in te_string.c (Nivel B paso 2.b). */
    if (te_string_method_dispatch(node, objNode, v)) return;

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
            free(__ret_var.value.string_value);
        }
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }

    /* ===== v0.0.12 #8 Lazy iterators dispatch =====
     * If LHS resolves to a LAZY_ITER, dispatch intermediate/terminal methods
     * here BEFORE the LIST handlers. Implementation in te_linq.c (paso 2.e). */
    if (te_linq_lazy_method_dispatch(node, v)) return;

    /* Fase 1b: métodos built-in en LIST: push, pop */
    if (te_cm_list_builtin(node, objNode, v)) return;

    /* Fase 1c + Ola 13: métodos built-in en MAP (keys/values/has/remove/size/length/clear).
     * Dispatched in te_map.c (Nivel B paso 2.d).
     *
     * MAP and OBJECT_LITERAL store an ASTNode in value.object_value (NOT an
     * ObjectNode). They must be fully handled here and must NEVER fall through
     * to the class-instance dispatch below: that path casts value.object_value
     * to ObjectNode and dereferences obj->class->name, which on a plain map is
     * garbage -> SIGSEGV that takes down the whole single-flight API server
     * (e.g. calling a string method like `.contains()` on a `{...}` value or on
     * the sql_exec envelope object). For an unknown method we return null
     * instead of crashing. */
    if (te_cm_map_builtin(node, objNode, v)) return;
    
    if (!v || v->vtype != VAL_OBJECT) {
        printf("Error: '%s' is not a valid object.\n", objNode->id);
        return;
    }
    if (v->type && strcmp(v->type, "OBJECT") != 0) {
        fprintf(stderr, "[method] unknown method '%s' on %s value\n",
                node->id ? node->id : "?", v->type);
        ASTNode *nullret = create_ast_leaf("NULL", 0, NULL, NULL);
        add_or_update_variable("__ret__", nullret);
        free_ast(nullret);
        return;
    }
    // Try to get ObjectNode from value.object_value first, then from extra
    // This supports both storage patterns: direct storage and wrapper pattern (used by for-in)
    ObjectNode *obj = v->value.object_value;
    //printf("[DEBUG interpret_call_method] objNode->id='%s', v->type='%s', v->vtype=%d, obj=%p\n", 
      //     objNode->id, v->type, v->vtype, (void*)obj);
    if (!obj) {
        // Fallback: The ObjectNode might be stored in an ASTNode wrapper's extra field
        // This happens when objects are created by interpret_for_in(&g_vm, )
        ASTNode *wrapper = (ASTNode*)(intptr_t)v->value.object_value;
       // printf("[DEBUG interpret_call_method] Trying fallback: wrapper=%p\n", (void*)wrapper);
        if (wrapper && wrapper->extra) {
            obj = (ObjectNode *)wrapper->extra;
            //printf("[DEBUG interpret_call_method] Got obj from wrapper->extra: obj=%p\n", (void*)obj);
        }
    }
    
    if (!obj) {
        printf("[DEBUG interpret_call_method] ERROR: obj is NULL after fallback!\n");
        return;
    }
    
   // printf("[DEBUG interpret_call_method] obj=%p, obj->class=%p\n", (void*)obj, (void*)obj->class);
//if (obj->class) {
    //    printf("[DEBUG interpret_call_method] obj->class->name='%s'\n", obj->class->name);
   // }

    // === FIX START: Handle Bridge method calls ===
    if (te_cm_bridge(node, obj, v)) return;
    // === FIX END ===

    MethodNode *m = obj->class->methods;
    while (m && strcmp(m->name, node->id) != 0) m = m->next;
    if (!m) {
        printf("Error: method '%s' not found in class '%s'.\n", node->id, obj->class->name);
        return;
    }

    // --- CACHE LOGIC ---
    if (m->cache_ttl > 0) {
        CachedResponse *cached = get_cached_response(m, node->right);
        if (cached && cached->result) {
            add_or_update_variable("__ret__", cached->result);
            return;
        }
    }

    if (te_cm_bind_this(node, obj, v)) return;
    if (te_cm_bind_args(node, m, obj)) return;

    if (te_cm_body_bytecode(node, m, obj)) return;

    debugger_push_frame(m->name, node);
    interpret_ast(m->body);
    debugger_pop_frame();

    // --- TYPE CHECK: void method must NOT return a value ---
    if (m->return_type && strcmp(m->return_type, "void") == 0 && g_vm.return_flag && g_vm.return_node) {
        char buf[256];
        snprintf(buf, sizeof(buf), "TypeError: method '%s' is declared as 'void' and cannot return a value.", m->name);
        if (throw_message) free(throw_message);
        throw_message = strdup(buf);
        g_vm.throw_flag = 1;
        g_vm.return_flag = 0; g_vm.return_node = NULL;
        return;
    }
    // --- TYPE CHECK: non-void method should produce a return value ---
    // Optional types (T?) and dynamic allow no return (treated as null).
    if (m->return_type
        && strcmp(m->return_type, "void") != 0
        && strcmp(m->return_type, "dynamic") != 0
        && m->return_type[strlen(m->return_type)-1] != '?'
        && (!g_vm.return_flag || !g_vm.return_node)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "TypeError: method '%s' is declared as '%s' but does not return a value.", m->name, m->return_type);
        if (throw_message) free(throw_message);
        throw_message = strdup(buf);
        g_vm.throw_flag = 1;
        return;
    }

    // --- STORE RESULT IN CACHE IF NEEDED ---
    if (m->cache_ttl > 0 && __ret_var_active) {
        Variable *ret = find_variable("__ret__");
        if (ret && ret->type) {
            ASTNode *ret_node = create_ast_leaf(ret->type, ret->value.int_value, ret->value.string_value, ret->id);
            set_cached_response(m, node->right, ret_node);
        }
    }
   // printf("[DIAG] interpret_call_method: después de interpretar cuerpo de método, return_flag=%d\n", return_flag);
    if (te_cm_materialize_return(node, m, obj)) return;
}

/* interpret_call_method_alone: eliminada (codigo muerto, 400 lineas sin llamadores; Fase 2). */
ObjectNode* clone_object(ObjectNode *original) {
    if (!original || !original->class) {
        /* clone_object debug log removed */
        return NULL;
    }
    ObjectNode *clone = malloc(sizeof(ObjectNode));
    clone->class = original->class;
    clone->owning_list = NULL;  /* v0.0.13 (perf): clone is not in any list yet */
    clone->attributes = malloc(original->class->attr_count * sizeof(Variable));
    for (int i = 0; i < original->class->attr_count; i++) {
        clone->attributes[i].id = strdup(original->attributes[i].id);
        clone->attributes[i].type = strdup(original->attributes[i].type);
        clone->attributes[i].vtype = original->attributes[i].vtype;
        if (clone->attributes[i].vtype == VAL_STRING) {
            const char *src = original->attributes[i].value.string_value;
            clone->attributes[i].value.string_value = src ? strdup(src) : strdup("");
        } else if (clone->attributes[i].vtype == VAL_INT) {
            clone->attributes[i].value.int_value = original->attributes[i].value.int_value;
        } else if (clone->attributes[i].vtype == VAL_FLOAT) {
            clone->attributes[i].value.float_value = original->attributes[i].value.float_value;
        } else {
            // (Manejar otros tipos como float si es necesario)
        }
    }
    return clone;
}

/* CSV reader, ORM arena, AST node pool and DataFrame analytics now live
 * in te_csv.c (Fase 2 paso 2). Public surface declared in te_csv.h. */




/* interpret_var_decl: movida a te_interp_decl.c (Fase 2). */

ASTNode *create_list_node(ASTNode *items) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = strdup("LIST");
    node->left = items; // 'left' apunta al primer item
    node->right = NULL;
    node->next = NULL;
    node->id = NULL;
    node->str_value = NULL;
    node->value = 0;
    
    // v0.0.11: items now come pre-chained via ->next from append_to_list_parser.
    // No right→next conversion needed (and doing it would clobber BINOP right operands).

    /* Ola 14c: pre-build TEListIdx así el primer push es O(1) puro
     * sin pasar por la rama lazy-build de te_list_append. */
    {
        TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
        if (ix) {
            ix->cap = 8;
            ix->len = 0;
            ix->items = (ASTNode**)calloc((size_t)ix->cap, sizeof(ASTNode*));
            if (!ix->items) { free(ix); ix = NULL; }
            if (ix && items) {
                /* poblar con items existentes */
                ASTNode *cur = items;
                while (cur) {
                    if (ix->len >= ix->cap) {
                        int nc = ix->cap * 2;
                        ASTNode **nb = (ASTNode**)realloc(ix->items, (size_t)nc * sizeof(ASTNode*));
                        if (!nb) { free(ix->items); free(ix); ix = NULL; break; }
                        ix->items = nb; ix->cap = nc;
                    }
                    ix->items[ix->len++] = cur;
                    cur = cur->next;
                }
            }
        }
        node->extra = ix;
    }
    
    return node;
}

ASTNode *append_to_list_parser(ASTNode *list, ASTNode *item) {
    if (!list) { if (item) item->next = NULL; return item; }
    /* v0.0.11 fix: chain via ->next ONLY so BINOP/method-call items (which use
     * ->right for their own right operand) are not clobbered. Constructor arg
     * walkers that previously walked ->right have been migrated to use
     * expression_list (still ->right) for NEW; this list-literal builder is
     * for `[...]` only. */
    ASTNode *cur = list;
    while (cur->next) cur = cur->next;
    cur->next = item;
    if (item) item->next = NULL;
    return list;
}

ASTNode *append_argument_raw(ASTNode *list, ASTNode *arg) {
    if (!list) return arg;
    ASTNode *cur = list;
    while (cur->right) cur = cur->right;
    cur->right = arg;
    return list;
}


ASTNode* append_to_list(ASTNode* list, ASTNode* item) {
    if (!item) return list;
    if (strcmp(item->type, "LIST") == 0) {
        fprintf(stderr, "Error: cannot insert a LIST node inside a list.\n");
        return list;
    }
    if (list == item) {
        printf("Error: attempt to insert the same list into itself.\n");
        return list;
    }
    if (!list) {
        if (strcmp(item->type, "LIST") == 0) {
            fprintf(stderr, "Error: cannot create a list with a LIST node as item.\n");
            return NULL;
        }
        item->next = NULL;
        ASTNode *listNode = calloc(1, sizeof(ASTNode));
        listNode->type = strdup("LIST");
        listNode->left = item;
        listNode->right = NULL;
        listNode->next = NULL;
        listNode->id = NULL;
        listNode->str_value = NULL;
        listNode->value = 0;
        return listNode;
    }
    if (strcmp(list->type, "LIST") != 0) {
        return NULL;
    }
    ASTNode* current = list->left;
    if (!current) {
        list->left = item;
    } else {
        int safety = 0;
        while (current->next) {
            if (++safety > 10000000) {
                printf("Error: infinite loop detected in append_to_list()\n");
                break;
            }
            current = current->next;
        }     
        current->next = item;
    }
    item->next = NULL;
    return list;
}


ASTNode* create_list_function_call_node(ASTNode* list, const char* funcName, ASTNode* lambda) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("FILTER_CALL");
    node->id = strdup(funcName);
    node->left = list;
    node->right = lambda;
    node->next = NULL;
    return node;
}


ASTNode* create_lambda_node(const char* argName, ASTNode* body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(argName);
    node->left = body;
    node->right = NULL;
    node->next = NULL;
    return node;
}

/* Fase B: lambda multi-param. paramsCsv = nombres separados por '\1'.
 * Usamos el mismo NodeKind LAMBDA. body puede ser expr o STATEMENT_LIST. */
ASTNode* create_lambda_multi_node(const char *paramsCsv, ASTNode *body) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = strdup("LAMBDA");
    node->id = strdup(paramsCsv ? paramsCsv : "");
    node->left = body;
    node->right = NULL;
    node->next = NULL;
    node->line = yylineno; node->file_id = g_lex_file_id;
    return node;
}

/* Fase B: invoca un lambda con argsList (lista enlazada por ->right o ->next).
 * Setea cada parámetro en el scope global, evalúa el body y devuelve el ASTNode*
 * con el resultado. Si body es expresión, retorna su valor evaluado.
 * Si body es STATEMENT_LIST, ejecuta y lee return_node / __ret__. */

/* ============================================================
 * gotcha closure-return: captura de variables libres al RETORNAR un lambda
 * desde otro lambda/función (p.ej. `fn(n) => fn(x) => x + n`).
 * Cuando `make(10)` retorna el lambda interno, `n` ya no estará en scope al
 * invocar `add10(5)`. Capturamos por VALOR: clonamos el lambda y sustituimos
 * cada identificador libre (no sombreado por un parámetro de lambda) por un
 * leaf con su valor concreto actual. Es semántica de closure-by-value.
 * ============================================================ */

/* ¿`name` aparece en el set '\1'-separado `shadow`? */
static int te_name_in_shadow(const char *shadow, const char *name) {
    if (!shadow || !name) return 0;
    size_t nl = strlen(name);
    const char *p = shadow;
    while (*p) {
        const char *e = p; while (*e && *e != '\1') e++;
        if ((size_t)(e - p) == nl && strncmp(p, name, nl) == 0) return 1;
        if (*e == '\1') p = e + 1; else p = e;
    }
    return 0;
}

/* Clona `n` recursivamente; sustituye identificadores libres por su valor.
 * `shadow` lista (separada por '\1') los nombres ligados por parámetros de
 * lambdas anidados, que NO deben sustituirse. */
static ASTNode *te_clone_capture(ASTNode *n, const char *shadow) {
    if (!n) return NULL;
    /* Identificador libre -> sustituir por su valor concreto actual. */
    if (n->type && (strcmp(n->type, "IDENTIFIER") == 0 || strcmp(n->type, "ID") == 0)
        && n->id && !te_name_in_shadow(shadow, n->id)) {
        Variable *v = find_variable(n->id);
        if (v) {
            if (v->vtype == VAL_INT) {
                const char *t = (v->type && strcmp(v->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
                ASTNode *r = create_ast_leaf_number((char*)t, v->value.int_value, NULL, NULL);
                if (r) r->bc = BC_NOT_COMPILABLE;
                return r;
            }
            if (v->vtype == VAL_FLOAT) {
                char buf[64]; te_fmt_double(buf, sizeof(buf), v->value.float_value);
                ASTNode *r = create_ast_leaf("FLOAT", 0, buf, NULL);
                if (r) r->bc = BC_NOT_COMPILABLE;
                return r;
            }
            if (v->vtype == VAL_STRING) {
                ASTNode *r = create_ast_leaf("STRING", 0,
                    v->value.string_value ? v->value.string_value : "", NULL);
                if (r) r->bc = BC_NOT_COMPILABLE;
                return r;
            }
            /* OBJECT/LIST/MAP/etc.: no inlineable como leaf simple; se deja el
             * identificador (puede ser global/persistente). */
        }
        /* No resuelto: copiar el identificador tal cual. */
    }
    /* Copia genérica del nodo. */
    ASTNode *c = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!c) return NULL;
    c->type = n->type ? strdup(n->type) : NULL;
    c->kind = n->kind;
    c->id = n->id ? strdup(n->id) : NULL;
    c->value = n->value;
    c->str_value = n->str_value ? strdup(n->str_value) : NULL;
    c->str_interned = 0;
    c->line = n->line;
    c->bc = BC_NOT_COMPILABLE;
    /* Al descender en un LAMBDA anidado, sus parámetros sombrean. */
    if (n->type && strcmp(n->type, "LAMBDA") == 0 && n->id && n->id[0]) {
        size_t sl = shadow ? strlen(shadow) : 0;
        size_t pl = strlen(n->id);
        char *ns = (char*)malloc(sl + 1 + pl + 1);
        if (ns) {
            if (sl) { memcpy(ns, shadow, sl); ns[sl] = '\1'; memcpy(ns + sl + 1, n->id, pl + 1); }
            else    { memcpy(ns, n->id, pl + 1); }
            c->left  = te_clone_capture(n->left,  ns);
            c->right = te_clone_capture(n->right, ns);
            c->next  = te_clone_capture(n->next,  shadow);
            c->extra = te_clone_capture(n->extra, ns);
            free(ns);
            return c;
        }
    }
    c->left  = te_clone_capture(n->left,  shadow);
    c->right = te_clone_capture(n->right, shadow);
    c->next  = te_clone_capture(n->next,  shadow);
    c->extra = te_clone_capture(n->extra, shadow);
    return c;
}

/* Captura un lambda que será retornado: clona el body sustituyendo las
 * variables libres por su valor actual. Los parámetros del propio lambda
 * (lam->id) se mantienen como variables. */
ASTNode *te_capture_lambda(ASTNode *lam) {
    if (!lam) return NULL;
    ASTNode *c = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!c) return lam;
    c->type = strdup("LAMBDA");
    c->kind = lam->kind;
    c->id = lam->id ? strdup(lam->id) : strdup("");
    c->line = lam->line;
    c->bc = BC_NOT_COMPILABLE;
    /* El body se clona con los parámetros propios sombreados. */
    c->left = te_clone_capture(lam->left, c->id);
    return c;
}

ASTNode* call_lambda(ASTNode *lambda, ASTNode *argsList) {
    te_depth_enter();
    ASTNode *r = call_lambda_impl(lambda, argsList);
    te_depth_leave();
    return r;
}

/* ─── Parameter scoping for lambda calls ──────────────────────────────────
 * TypeEasy stores every variable in the single global `vars[]` array; there
 * is no per-call scope. Binding a lambda parameter therefore writes straight
 * into that global array via add_or_update_variable(). When a parameter name
 * coincides with an *outer* variable that happens to be const (e.g. a `let
 * conn = mysql_connect(...)` reused as the param `conn`, or a map key whose
 * name matches a const in scope), the binding used to abort with
 *   "Error: cannot assign to constant variable '<name>'."
 * (reported bugs #1 and #2 in 0.0.23). Parameters are local to the call and
 * must *shadow* any outer variable — const or not. We save the outer slot's
 * contents before binding and restore them once the body has run, so the
 * outer const is untouched after the call returns. Save/restore lives on the
 * C stack, making it re-entrant for recursive lambdas. */
#define LAMBDA_MAX_SHADOW 64

static void te_lambda_save_shadow(const char *name) {
    Variable *ex = find_variable_for((char *)name);
    if (!ex) return;                       /* fresh param: nothing to shadow */
    te_frame_shadow_slot(g_vm.frame_top, ex);
}

static ASTNode* call_lambda_exec_body(ASTNode *lambda);

static ASTNode* call_lambda_impl(ASTNode *lambda, ASTNode *argsList) {
    if (!lambda || !lambda->id) {
        return create_ast_leaf("NULL", 0, NULL, NULL);
    }
    /* Parsear paramsCsv ('\1'-separated). Casos: "" (0 params), "x", "x\1y", etc. */
    const char *params = lambda->id;
    /* Bind args a params, en orden. argsList puede ser NULL o lista por ->right. */
    ASTNode *cur_arg = argsList;
    TeFrame _frame;
    te_frame_push(&_frame);
    if (params[0]) {
        const char *p = params;
        while (*p) {
            const char *e = p;
            while (*e && *e != '\1') e++;
            size_t n = (size_t)(e - p);
            char name[128];
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, p, n); name[n] = '\0';
            /* Construir un ASTNode "valor concreto" para este param */
            ASTNode *valNode = NULL;
            /* Bugfix v0.0.29: pasar un objeto/map/list por VARIABLE (no literal)
             * a un lambda. Cuando cur_arg es un IDENTIFIER que resuelve a una
             * variable VAL_OBJECT, hay que reconstruir el ASTNode equivalente
             * y enlazarlo (alias, como literales OBJECT/LIST). Sin esto el arg
             * caia al `else` final -> evaluate_expression -> se coercia a numero
             * y el objeto llegaba vacio dentro de la funcion (rompia el patron
             * "repository": Repo(body) / Repo(p)). Los escalares no se ven
             * afectados: solo capturamos identificadores de tipo VAL_OBJECT. */
            Variable *obj_var = NULL;
            if (cur_arg && cur_arg->type && cur_arg->id &&
                (strcmp(cur_arg->type, "IDENTIFIER") == 0 || strcmp(cur_arg->type, "ID") == 0)) {
                Variable *v = find_variable(cur_arg->id);
                if (v && v->vtype == VAL_OBJECT) obj_var = v;
            }
            if (cur_arg) {
                /* v1.0.0: list items can be CALL_FUNC/CALL_METHOD (e.g.
                 * `[uuid_v4(), uuid_v4(), ...]` was materialized without
                 * eager evaluation). Dispatch the call and bind from
                 * __ret__ so lambdas see the actual produced value. */
                if (cur_arg->type && (strcmp(cur_arg->type, "CALL_FUNC") == 0 ||
                                      strcmp(cur_arg->type, "CALL_METHOD") == 0)) {
                    if (strcmp(cur_arg->type, "CALL_FUNC") == 0) interpret_call_func(cur_arg);
                    else                                          interpret_call_method(cur_arg);
                    Variable *rr = find_variable("__ret__");
                    if (rr && rr->vtype == VAL_STRING) {
                        const char *tt = (rr->type && (strcmp(rr->type, "DATETIME") == 0 ||
                                                       strcmp(rr->type, "UUID") == 0))
                                         ? rr->type : "STRING";
                        valNode = create_ast_leaf((char*)tt, 0,
                            rr->value.string_value ? rr->value.string_value : "", NULL);
                    } else if (rr && rr->vtype == VAL_INT) {
                        const char *tt = (rr->type && strcmp(rr->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
                        valNode = create_ast_leaf_number((char*)tt, rr->value.int_value, NULL, NULL);
                    } else if (rr && rr->vtype == VAL_FLOAT) {
                        char buf[64]; te_fmt_double(buf, sizeof(buf), rr->value.float_value);
                        valNode = create_ast_leaf("FLOAT", 0, buf, NULL);
                    } else if (rr && rr->vtype == VAL_OBJECT) {
                        /* Bugfix: una llamada inline que devuelve un OBJETO/MAP/
                         * LIST (p.ej. json_parse(request_body())) caia aqui al
                         * `else` -> NULL, y el objeto llegaba VACIO al parametro
                         * (rompia el patron Repo(json_parse(body())) inline: los
                         * campos p["x"] salian ""). Reconstruir el nodo
                         * equivalente y aliasarlo, igual que la rama
                         * IDENTIFIER->obj_var de abajo. "OBJECT" guarda un
                         * ObjectNode* (envolver con create_object_node);
                         * MAP/LIST/etc. guardan el propio ASTNode (aliasar; su
                         * ciclo de vida ya lo maneja te_req_owned_ast_register
                         * / la variable de origen). */
                        if (rr->type && strcmp(rr->type, "OBJECT") == 0) {
                            valNode = create_object_node(rr->value.object_value);
                        } else if (rr->value.object_value) {
                            valNode = (ASTNode *)(intptr_t)rr->value.object_value;
                        } else {
                            valNode = create_ast_leaf("NULL", 0, NULL, NULL);
                        }
                    } else {
                        valNode = create_ast_leaf("NULL", 0, NULL, NULL);
                    }
                } else if (obj_var) {
                    /* IDENTIFIER -> variable VAL_OBJECT: reconstruir el nodo
                     * equivalente para que te_value_to_variable reponga el mismo
                     * (type, object_value) en el parametro. "OBJECT" guarda un
                     * ObjectNode* (envolver con create_object_node); MAP/LIST/
                     * LAMBDA/LAZY_ITER guardan el propio ASTNode (aliasar). */
                    if (obj_var->type && strcmp(obj_var->type, "OBJECT") == 0) {
                        valNode = create_object_node(obj_var->value.object_value);
                    } else if (obj_var->value.object_value) {
                        valNode = (ASTNode *)(intptr_t)obj_var->value.object_value;
                    } else {
                        valNode = create_ast_leaf("NULL", 0, NULL, NULL);
                    }
                } else if (cur_arg->type && (strcmp(cur_arg->type, "OBJECT") == 0 ||
                                      strcmp(cur_arg->type, "STRING") == 0 ||
                                      strcmp(cur_arg->type, "NUMBER") == 0 ||
                                      strcmp(cur_arg->type, "INT") == 0 ||
                                      strcmp(cur_arg->type, "FLOAT") == 0 ||
                                      strcmp(cur_arg->type, "NULL") == 0 ||
                                      strcmp(cur_arg->type, "LIST") == 0 ||
                                      strcmp(cur_arg->type, "OBJECT_LITERAL") == 0)) {
                    valNode = cur_arg;
                } else if (is_string_type(cur_arg)) {
                    char *s = get_node_string(cur_arg);
                    valNode = create_ast_leaf("STRING", 0, s, NULL);
                    free(s);
                } else {
                    long long i64v; double r;
                    if (te_eval_num(cur_arg, &i64v, &r)) {   /* Fase 1b */
                        valNode = create_ast_leaf_number("NUMBER", i64v, NULL, NULL);
                    } else {
                        char buf[64]; te_fmt_double(buf, sizeof(buf), r);
                        valNode = create_ast_leaf("FLOAT", 0, buf, NULL);
                    }
                }
                cur_arg = cur_arg->next ? cur_arg->next : cur_arg->right; /* gotcha #1: prefer ->next (binop args); ->right is reduce's manual acc->right=item link */
            } else {
                valNode = create_ast_leaf("NULL", 0, NULL, NULL);
            }
            /* Param scoping: shadow any outer var (const-safe), then bind. */
            te_lambda_save_shadow(name);
            add_or_update_variable(name, valNode);
            if (*e == '\1') p = e + 1; else p = e;
        }
    }
    {
        ASTNode *_lam_res = call_lambda_exec_body(lambda);
        te_frame_pop(&_frame);
        return _lam_res;
    }
}

static ASTNode* call_lambda_exec_body(ASTNode *lambda) {
    /* Ejecutar el body */
    ASTNode *body = lambda->left;
    if (!body) return create_ast_leaf("NULL", 0, NULL, NULL);
    if (body->type && strcmp(body->type, "STATEMENT_LIST") == 0) {
        int saved_return_flag = g_vm.return_flag;
        ASTNode *saved_return_node = g_vm.return_node;
        g_vm.return_flag = 0;
        g_vm.return_node = NULL;
        interpret_ast(body);
        ASTNode *ret = g_vm.return_node;
        g_vm.return_flag = saved_return_flag;
        g_vm.return_node = saved_return_node;
        if (!ret) return create_ast_leaf("NULL", 0, NULL, NULL);
        if (ret->type && strcmp(ret->type, "RETURN") == 0 && ret->left) ret = ret->left;
        /* Normalizar a valor concreto (evaluar la expresión) */
        /* v0.0.30 (leak fix + soundness): `ret` es el return_node, un nodo
         * PRESTADO del AST del programa (statement-body `return <literal>`).
         * Para ESCALARES devolvemos un CLON fresco e independiente, de modo que
         * todo resultado escalar de call_lambda sea owned: los consumidores LINQ
         * pueden liberarlo (te_free_lambda_result) sin corromper el programa, y
         * se evita el cross-link del AST que advierte te_evloop. LIST/OBJECT/MAP
         * se devuelven tal cual (aliasan el payload de una variable; el
         * consumidor NUNCA los libera). */
        if (ret->type && (
                strcmp(ret->type, "STRING") == 0 ||
                strcmp(ret->type, "NUMBER") == 0 ||
                strcmp(ret->type, "INT") == 0 ||
                strcmp(ret->type, "FLOAT") == 0 ||
                strcmp(ret->type, "NULL") == 0)) {
            if (strcmp(ret->type, "STRING") == 0)
                return create_ast_leaf("STRING", 0, ret->str_value ? ret->str_value : "", NULL);
            if (strcmp(ret->type, "FLOAT") == 0)
                return create_ast_leaf("FLOAT", 0, ret->str_value ? ret->str_value : "0", NULL);
            if (strcmp(ret->type, "NULL") == 0)
                return create_ast_leaf("NULL", 0, NULL, NULL);
            return create_ast_leaf_number(ret->type, ret->value, NULL, NULL); /* NUMBER / INT */
        }
        if (ret->type && (
                strcmp(ret->type, "LIST") == 0 ||
                strcmp(ret->type, "OBJECT") == 0 ||
                strcmp(ret->type, "OBJECT_LITERAL") == 0 ||
                strcmp(ret->type, "MAP") == 0)) {
            return ret;
        }
        if (is_string_type(ret)) {
            char *s = get_node_string(ret);
            ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
            free(s);
            return r;
        }
        /* A `return f(...)` / `return obj.m(...)` already executed during
         * interpret_ast(body) and left its value in __ret__. Re-evaluating it
         * here would run the call a SECOND time (wrong for side-effecting or
         * yielding builtins such as read_file_async/await) and coerce the
         * result to a number — dropping string/list/map results. Read __ret__
         * instead, mirroring the argument-binding path above. */
        if (ret->type && (strcmp(ret->type, "CALL_FUNC") == 0 ||
                          strcmp(ret->type, "CALL_METHOD") == 0)) {
            Variable *rr = find_variable("__ret__");
            if (rr && rr->vtype == VAL_STRING) {
                const char *tt = (rr->type && (strcmp(rr->type, "DATETIME") == 0 ||
                                               strcmp(rr->type, "UUID") == 0))
                                 ? rr->type : "STRING";
                return create_ast_leaf((char*)tt, 0,
                    rr->value.string_value ? rr->value.string_value : "", NULL);
            } else if (rr && rr->vtype == VAL_FLOAT) {
                char buf[64]; te_fmt_double(buf, sizeof(buf), rr->value.float_value);
                return create_ast_leaf("FLOAT", 0, buf, NULL);
            } else if (rr && rr->vtype == VAL_OBJECT && rr->type &&
                       (strcmp(rr->type, "LIST") == 0 || strcmp(rr->type, "MAP") == 0)) {
                return (ASTNode*)(intptr_t)rr->value.object_value;
            } else if (rr && rr->vtype == VAL_INT) {
                const char *tt = (rr->type && strcmp(rr->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
                return create_ast_leaf_number((char*)tt, rr->value.int_value, NULL, NULL);
            }
            return create_ast_leaf("NULL", 0, NULL, NULL);
        }
        if (ret->type && (strcmp(ret->type, "ID") == 0 || strcmp(ret->type, "IDENTIFIER") == 0)) {
            Variable *v = find_variable(ret->id);
            if (v && v->vtype == VAL_OBJECT && v->type) {
                if (strcmp(v->type, "LIST") == 0 || strcmp(v->type, "MAP") == 0) {
                    return (ASTNode*)(intptr_t)v->value.object_value;
                }
            }
        }
        long long i64r;
        if (te_eval_i64(ret, &i64r)) return create_ast_leaf_number("NUMBER", i64r, NULL, NULL);   /* Fase 1b */
        double dr = evaluate_expression(ret);
        if (dr == (double)(long long)dr) return create_ast_leaf_number("NUMBER", (long long)dr, NULL, NULL);
        char buf[64]; te_fmt_double(buf, sizeof(buf), dr);
        return create_ast_leaf("FLOAT", 0, buf, NULL);
    }
    /* body es expresión. Si produce string, devolver STRING; si numérica, NUMBER/FLOAT. */
    /* gotcha closure-return: el body es OTRO lambda (currying:
     * `fn(n) => fn(x) => x + n`). Capturamos por valor las variables libres
     * (p.ej. n) y devolvemos el lambda resultante como first-class value. */
    if (body->type && strcmp(body->type, "LAMBDA") == 0) {
        return te_capture_lambda(body);
    }
    if (is_string_type(body)) {
        char *s = get_node_string(body);
        ASTNode *r = create_ast_leaf("STRING", 0, s, NULL);
        free(s);
        return r;
    }
    /* v0.0.11: lambda expression-body returns a LIST/MAP literal directly.
     * For LIST, build a fresh list with each item evaluated (items may be
     * expressions like `n * 10` that reference bound params). */
    if (body->type && strcmp(body->type, "LIST") == 0) {
        ASTNode *result = create_list_node(NULL);
        ASTNode *it = body->left;
        while (it) {
            ASTNode *valNode = NULL;
            if (it->type && (strcmp(it->type, "STRING") == 0 ||
                             strcmp(it->type, "LIST") == 0 ||
                             strcmp(it->type, "MAP") == 0 ||
                             strcmp(it->type, "OBJECT_LITERAL") == 0 ||
                             strcmp(it->type, "NULL") == 0)) {
                valNode = it;
            } else if (is_string_type(it)) {
                char *s = get_node_string(it);
                valNode = create_ast_leaf("STRING", 0, s, NULL);
                free(s);
            } else {
                double dv = evaluate_expression(it);
                if (dv == (double)(long long)dv) valNode = create_ast_leaf_number("NUMBER", (long long)dv, NULL, NULL);
                else { char buf[64]; te_fmt_double(buf, sizeof(buf), dv); valNode = create_ast_leaf("FLOAT", 0, buf, NULL); }
            }
            te_list_append(result, valNode);
            it = it->next;
        }
        return result;
    }
    if (body->type && (strcmp(body->type, "MAP") == 0 ||
                       strcmp(body->type, "OBJECT_LITERAL") == 0)) {
        return body;
    }
    /* v1.0.0: lambda body is a CALL_FUNC/CALL_METHOD. Dispatch the call,
     * read __ret__, and return a typed leaf (preserves STRING vs NUMBER —
     * needed for builtins like uuid_v4()/now() (STRING) and date_parse()
     * (INT) used inside LINQ where/select). Without this, generic
     * evaluate_expression collapses everything to a double and string
     * results come back as empty. */
    if (body->type && (strcmp(body->type, "CALL_FUNC") == 0 ||
                       strcmp(body->type, "CALL_METHOD") == 0)) {
        if (strcmp(body->type, "CALL_FUNC") == 0) interpret_call_func(body);
        else                                       interpret_call_method(body);
        Variable *r = find_variable("__ret__");
        if (!r) return create_ast_leaf("NULL", 0, NULL, NULL);
        if (r->vtype == VAL_STRING) {
            const char *s = r->value.string_value ? r->value.string_value : "";
            const char *t = (r->type && (strcmp(r->type, "DATETIME") == 0 ||
                                         strcmp(r->type, "UUID") == 0))
                            ? r->type : "STRING";
            return create_ast_leaf((char*)t, 0, (char*)s, NULL);
        }
        if (r->vtype == VAL_INT) {
            const char *t = (r->type && strcmp(r->type, "BOOL") == 0) ? "BOOL" : "NUMBER";
            return create_ast_leaf_number((char*)t, r->value.int_value, NULL, NULL);
        }
        if (r->vtype == VAL_FLOAT) {
            char buf[64]; te_fmt_double(buf, sizeof(buf), r->value.float_value);
            return create_ast_leaf("FLOAT", 0, buf, NULL);
        }
        if (r->vtype == VAL_OBJECT && r->type) {
            if (strcmp(r->type, "LIST") == 0 || strcmp(r->type, "MAP") == 0)
                return (ASTNode*)(intptr_t)r->value.object_value;
            if (strcmp(r->type, "NULL") == 0)
                return create_ast_leaf("NULL", 0, NULL, NULL);
        }
        return create_ast_leaf("NULL", 0, NULL, NULL);
    }
    /* Si la expresión produce un OBJECT/LIST/MAP, evaluate_expression no lo refleja.
     * Detectar referencias a IDENTIFIER que apunten a OBJECT/LIST/MAP. */
    if (body->type && (strcmp(body->type, "ID") == 0 || strcmp(body->type, "IDENTIFIER") == 0)) {
        Variable *v = find_variable(body->id);
        if (v && v->vtype == VAL_OBJECT && v->type) {
            if (strcmp(v->type, "LIST") == 0 || strcmp(v->type, "MAP") == 0) {
                return (ASTNode*)(intptr_t)v->value.object_value;
            }
            if (strcmp(v->type, "OBJECT") == 0) {
                ASTNode *r = (ASTNode*)calloc(1, sizeof(ASTNode));
                r->type = strdup("OBJECT");
                r->extra = (struct ASTNode*)v->value.object_value;
                return r;
            }
            if (strcmp(v->type, "NULL") == 0) {
                return create_ast_leaf("NULL", 0, NULL, NULL);
            }
        }
    }
    long long i64v;
    if (te_eval_i64(body, &i64v)) return create_ast_leaf_number("NUMBER", i64v, NULL, NULL);   /* Fase 1b */
    double r = evaluate_expression(body);
    if (r == (double)(long long)r) {
        return create_ast_leaf_number("NUMBER", (long long)r, NULL, NULL);
    }
    char buf[64]; te_fmt_double(buf, sizeof(buf), r);
    return create_ast_leaf("FLOAT", 0, buf, NULL);
}


/* interpret_assign_attr: movida a te_interp_decl.c (Fase 2). */

/* interpret_assign: movida a te_interp_decl.c (Fase 2). */

/* te_print_list_node: movida a te_print.c (Fase 2). */

/* te_map_field_display: movida a te_print.c (Fase 2). */

/* interpret_print: movida a te_print.c (Fase 2). */

static void interpret_fprint(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_printf( "Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_eprintf( "%s", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_eprintf( "Error: object '%s' is not defined or is not an object.\n", o->id);
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_eprintf( "Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        if (!te_attr_access_ok(obj->class, idx, o)) return;
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            dbg_eprintf( "%s", attr->value.string_value);
        else
            dbg_eprintf( "%lld", (long long)attr->value.int_value);
        return;
    }

    if (arg->id) { 
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_eprintf( "Error: variable '%s' is not defined.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_eprintf( "[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_eprintf( "]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_eprintf( "%s", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_eprintf( "%lld", (long long)v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            { char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value); dbg_eprintf("%s", b); }
        else
            dbg_eprintf( "Object of class: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_eprintf( "%d", (int)val);
        } else {
            char b[64]; te_fmt_double(b, sizeof(b), val); dbg_eprintf("%s", b);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}

static void interpret_fprintln(ASTNode *node) {
    ASTNode *arg = node->left;
    if (!arg) {
        dbg_eprintf( "Error: print without argument\n");
        return;
    }
    if (arg->type && strcmp(arg->type, "STRING") == 0) {
        dbg_eprintf( "%s\n", arg->str_value);
        return;
    }
    if (arg->type && strcmp(arg->type, "ACCESS_ATTR") == 0) {
        ASTNode *o = arg->left;
        ASTNode *a = arg->right;
        Variable *v = find_variable(o->id);
        if (!v || v->vtype != VAL_OBJECT) {
            dbg_eprintf( "Error: object '%s' is not defined or is not an object.\n", o->id);
            return;
        }
        ObjectNode *obj = v->value.object_value;
        int idx = -1;
        for (int i = 0; i < obj->class->attr_count; i++) {
            if (strcmp(obj->class->attributes[i].id, a->id) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            dbg_eprintf( "Error: attribute '%s' not found in class '%s'.\n", a->id, obj->class->name);
            return;
        }
        if (!te_attr_access_ok(obj->class, idx, o)) return;
        Variable *attr = &obj->attributes[idx];
        if (attr->vtype == VAL_STRING)
            dbg_eprintf( "%s\n", attr->value.string_value);
        else
            dbg_eprintf( "%d\n", attr->value.int_value);
        return;
    }

    if (arg->id) {
        Variable *v = find_variable(arg->id);
        if (!v) {
            dbg_eprintf( "Error: variable '%s' is not defined.\n", arg->id);
            return;
        }
        if (v->vtype == VAL_OBJECT && v->type && strcmp(v->type, "LIST") == 0) {
            ASTNode *listNode = (ASTNode *)(intptr_t)v->value.object_value;
            if (listNode && strcmp(listNode->type, "LIST") == 0) {
                ASTNode *cur = listNode->left;
                dbg_eprintf( "[\n");
                while (cur) {
                    if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                        ObjectNode *obj = (ObjectNode *)(intptr_t)cur->value;
                        call_method(obj, "Mostrar");
                    }
                    cur = cur->next; // CORRECCIÓN
                }
                dbg_eprintf( "]\n");
                return;
            }
        }
        if (v->vtype == VAL_STRING)
            dbg_eprintf( "%s\n", v->value.string_value);
        else if (v->vtype == VAL_INT)
            dbg_eprintf( "%d\n", v->value.int_value);
        else if (v->vtype == VAL_FLOAT)
            { char b[64]; te_fmt_double(b, sizeof(b), v->value.float_value); dbg_eprintf("%s\n", b); }
        else
            dbg_eprintf( "Object of class: %s\n", v->value.object_value->class->name);
    } else {
        double val = evaluate_expression(arg);
        if (val == (int)val) {
            dbg_eprintf( "%d\n", (int)val);
        } else {
            char b[64]; te_fmt_double(b, sizeof(b), val); dbg_eprintf("%s\n", b);
        }
    }

    if (__ret_var_active) {
        if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) free(__ret_var.value.string_value);
        if (__ret_var.id) free(__ret_var.id);
        if (__ret_var.type) free(__ret_var.type);
        memset(&__ret_var, 0, sizeof(Variable));
        // __ret_var_active = 0;  // COMMENTED: Keep active for embedded API
    }
}

/* interpret_println: movida a te_print.c (Fase 2). */


/* Capacidad del buffer inline (en pila) del recorrido iterativo de bloques.
 * NO es un limite: si el bloque tiene mas statements se hace malloc y la
 * capacidad se duplica. Centralizado aqui para tuning en un solo sitio. */
#define TE_STMTLIST_SPINE_INLINE 256
void interpret_statement_list(ASTNode *node) {
    /* v0.0.30: recorrer la lista de statements de forma ITERATIVA, no recursiva.
     * La gramática es left-recursive (`statement_list: statement_list statement`),
     * así que la lista queda anidada por ->left: node->left es la sublista con los
     * statements previos y node->right el statement actual. El `interpret_ast(
     * node->left)` recursivo hacía que la profundidad de interpret_ast fuera ==
     * nº de statements del bloque -> desbordaba el stack del worker en handlers
     * grandes (SIGSEGV silencioso, sin log; ese era el bug #5 reportado por ERP).
     * Mismo criterio que free_ast (Ola 14c): usar un stack en heap, no la pila de
     * C. El orden de ejecución y los chequeos de flags de control son idénticos
     * al original. */
    ASTNode *inline_buf[TE_STMTLIST_SPINE_INLINE];
    ASTNode **spine = inline_buf;
    int cap = (int)(sizeof(inline_buf) / sizeof(inline_buf[0]));
    int n = 0;

    /* 1) Descender el espinazo ->left apilando los nodos STATEMENT_LIST. */
    ASTNode *cur = node;
    while (cur && nk_of(cur) == NK_STATEMENT_LIST) {
        if (n == cap) {
            int ncap = cap * 2;
            ASTNode **grown = (ASTNode **)malloc((size_t)ncap * sizeof(ASTNode *));
            if (!grown) break;  /* OOM: degradar procesando el resto recursivamente */
            memcpy(grown, spine, (size_t)n * sizeof(ASTNode *));
            if (spine != inline_buf) free(spine);
            spine = grown;
            cap = ncap;
        }
        spine[n++] = cur;
        cur = cur->left;
    }

    /* 2) cur = primer statement del bloque (nodo no-STATEMENT_LIST, o NULL). */
    interpret_ast(cur);
    if (!(g_vm.throw_flag || g_vm.return_flag || g_vm.break_flag || g_vm.continue_flag)) {
        /* 3) Procesar los ->right del más profundo (más antiguo) al más reciente,
         *    preservando el orden fuente. */
        for (int i = n - 1; i >= 0; i--) {
            interpret_ast(spine[i]->right);
            if (g_vm.throw_flag || g_vm.return_flag || g_vm.break_flag || g_vm.continue_flag) break;
        }
    }

    if (spine != inline_buf) free(spine);
}

// ====================== MANEJO DE ATRIBUTOS ======================

int get_attribute_value(ObjectNode *obj, const char *attr_name) {
    if (!obj) return 0;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (strcmp(obj->attributes[i].id, attr_name) == 0) {
            return obj->attributes[i].value.int_value;
        }
    }
    printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name);
    return 0;
}

void set_attribute_value(ObjectNode *obj, const char *attr_name, int value) {
    if (!obj) return;
    for (int i = 0; i < obj->class->attr_count; i++) {
        if (strcmp(obj->attributes[i].id, attr_name) == 0) {
            obj->attributes[i].value.int_value = value;
            return;
        }
    }
    printf("Error: attribute '%s' not found in class '%s'.\n", attr_name, obj->class->name);
    }
// ====================== LIBERACIÓN DE MEMORIA ======================

/* Capacidad del buffer inline del teardown iterativo (mismo criterio que
 * TE_STMTLIST_SPINE_INLINE): crece a heap si el arbol es mas ancho. */
#define TE_FREE_AST_STACK_INLINE 64
void free_ast(ASTNode *node) {
    /* v0.0.30 (teardown iterativo): liberar el arbol SIN recursion de C. La
     * cadena ->next se recorre en bucle (Ola 14c) y los hijos ->left/->right se
     * apilan en un stack en HEAP en vez de free_ast(...) recursivo. El
     * statement_list es left-recursive (espinazo ->left == nº de statements) y
     * los maps encadenan KV por ->right; ambos podian desbordar el stack de C
     * en el TEARDOWN de arboles grandes (win64 ~1MB revienta a ~32k-36k niveles,
     * imprimiendo el resultado y crasheando al salir con STATUS_STACK_OVERFLOW).
     * Espejo del fix iterativo de interpret_statement_list/interpret_if. */
    ASTNode *inline_stack[TE_FREE_AST_STACK_INLINE];
    ASTNode **stack = inline_stack;
    int cap = (int)(sizeof(inline_stack) / sizeof(inline_stack[0]));
    int sp = 0;
    if (node) stack[sp++] = node;

    while (sp > 0) {
        ASTNode *n = stack[--sp];
        while (n) {                          /* recorrer la cadena ->next en bucle */
            ASTNode *next = n->next;
            /* Pool nodes (CSV wrappers): no liberar nada — type es sentinel, id
             * interned, str_value NULL, left/right NULL. Vive en el pool. */
            if (n->from_pool) { n = next; continue; }
            /* CSV wrapper sin pool: type apunta al literal global compartido. */
            if (n->type && n->type != g_csv_wrapper_obj_type) free(n->type);
            /* Ola 3 Fase A: never free interned strings (immortal). */
            if (n->str_value && !n->str_interned) free(n->str_value);
            /* Ola 15: same for id slot. */
            if (n->id && !n->id_interned) free(n->id);
            /* Gotcha 30c: item copy of a LIST instance; left/right are the template's. */
            int borrowed = n->borrowed_children;
            ASTNode *l = n->left, *r = n->right;   /* leer antes de free(n) */
            free(n);
            if (!borrowed && (l || r)) {                    /* apilar hijos: liberacion sin recursion */
                if (sp + 2 > cap) {
                    int ncap = cap * 2;
                    ASTNode **grown = (ASTNode **)malloc((size_t)ncap * sizeof(ASTNode *));
                    if (grown) {
                        memcpy(grown, stack, (size_t)sp * sizeof(ASTNode *));
                        if (stack != inline_stack) free(stack);
                        stack = grown; cap = ncap;
                    }
                }
                if (sp + 2 <= cap) {
                    if (l) stack[sp++] = l;
                    if (r) stack[sp++] = r;
                } else {                     /* OOM al crecer: fallback recursivo (raro) */
                    free_ast(l);
                    free_ast(r);
                }
            }
            n = next;
        }
    }
    if (stack != inline_stack) free(stack);
}

/* ==========================================================================
 * Residual B (ERP): validacion ESTATICA de ARIDAD para --syntax-check.
 * El runtime ya lanza TypeError al llamar una `fn` nombrada de aridad fija con
 * un nº de args distinto (interpret_call_func_impl), pero eso solo salta si esa
 * rama se EJECUTA. Aqui recorremos el AST parseado y marcamos el desajuste en
 * COMPILACION, para atraparlo antes de desplegar (incluso en ramas no
 * ejercitadas — el caso que rompio el keygen de licencias de ERP).
 *
 * Conservador para NO producir falsos positivos en el editor/LSP:
 *   - Solo `fn` nombradas a NIVEL SUPERIOR (`let f = fn(...) => ...`).
 *   - Se saltan nombres (re)declarados de forma ambigua (misma etiqueta con
 *     distinta aridad, o mezclada con un valor no-lambda).
 *   - Se saltan nombres SOMBREADOS por un parametro de un lambda envolvente
 *     (p.ej. `let apply = fn(f) => { f(1); }`).
 * Solo se invoca desde run_syntax_check(); cero coste/riesgo en ejecucion normal.
 * ========================================================================== */
typedef struct { const char *name; int nparams; int ambiguous; } TeArityFn;

static int te_arity_of_lambda(ASTNode *lam) {
    if (!lam || !lam->id || !lam->id[0]) return 0;   /* fn() => ... : 0 params */
    int n = 1;
    for (const char *p = lam->id; *p; p++) if (*p == '\1') n++;
    return n;
}

/* Registra (o marca ambiguo) un `let NAME = fn(...)` de nivel superior. */
static void te_arity_record(ASTNode *stmt, TeArityFn *tbl, int *count, int cap) {
    if (!stmt || !stmt->type || strcmp(stmt->type, "VAR_DECL") != 0 || !stmt->id) return;
    ASTNode *val = stmt->left;
    int is_lambda = (val && val->type && strcmp(val->type, "LAMBDA") == 0);
    int np = is_lambda ? te_arity_of_lambda(val) : -1;
    for (int i = 0; i < *count; i++) {
        if (tbl[i].name && strcmp(tbl[i].name, stmt->id) == 0) {
            if (!is_lambda || tbl[i].nparams != np) tbl[i].ambiguous = 1;
            return;
        }
    }
    if (*count < cap) {
        TeArityFn *e = &tbl[(*count)++];
        e->name = stmt->id;
        e->nparams = np;
        e->ambiguous = is_lambda ? 0 : 1;   /* no-lambda: registrado pero no chequeable */
    }
}

/* Recolecta solo statements de NIVEL SUPERIOR (espinazo ->left del program). */
static void te_arity_collect_toplevel(ASTNode *root, TeArityFn *tbl, int *count, int cap) {
    ASTNode *cur = root;
    while (cur && cur->type && strcmp(cur->type, "STATEMENT_LIST") == 0) {
        te_arity_record(cur->right, tbl, count, cap);   /* statement actual */
        cur = cur->left;                                /* sublista mas antigua */
    }
    te_arity_record(cur, tbl, count, cap);              /* primer statement / hoja */
}

/* Recorre el AST marcando llamadas directas `NAME(args)` con aridad incorrecta.
 * Itera ->next y el espinazo de STATEMENT_LIST en bucle (no recursa por esos
 * ejes) para no desbordar el stack en archivos con muchos statements. */
static void te_arity_walk(ASTNode *n, TeArityFn *tbl, int count,
                          const char *shadow, int *lastLine) {
    while (n) {
        if (!n->type) { n = n->next; continue; }
        if (n->line > 0) *lastLine = n->line;

        if (strcmp(n->type, "STATEMENT_LIST") == 0) {
            ASTNode *cur = n;
            while (cur && cur->type && strcmp(cur->type, "STATEMENT_LIST") == 0) {
                te_arity_walk(cur->right, tbl, count, shadow, lastLine);
                cur = cur->left;
            }
            te_arity_walk(cur, tbl, count, shadow, lastLine);
            n = n->next; continue;
        }

        /* Llamada DIRECTA a una fn nombrada. Dos formas de nodo:
         *   - CALL_FUNC           : resultado CONSUMIDO (arg de otra, `let r=`,
         *                           `x=`), args en ->left.
         *   - METHOD_CALL_ALONE   : statement SUELTO con resultado DESCARTADO
         *                           (`f(x);`), sin receptor (->left==NULL), args
         *                           en ->right.  (Residual C: antes solo se
         *                           validaba la primera forma.) */
        {
            ASTNode *ca_args = NULL; int is_direct_call = 0;
            if (strcmp(n->type, "CALL_FUNC") == 0) {
                ca_args = n->left; is_direct_call = 1;
            } else if (strcmp(n->type, "METHOD_CALL_ALONE") == 0 && n->left == NULL) {
                ca_args = n->right; is_direct_call = 1;
            }
            if (is_direct_call && n->id && !te_name_in_shadow(shadow, n->id)) {
                for (int i = 0; i < count; i++) {
                    if (tbl[i].ambiguous || !tbl[i].name ||
                        strcmp(tbl[i].name, n->id) != 0) continue;
                    int nargs = 0;
                    for (ASTNode *a = ca_args; a; a = a->next) nargs++;
                    if (nargs != tbl[i].nparams) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                            "TypeError: '%s' expects %d argument%s but %d %s passed.",
                            n->id, tbl[i].nparams, tbl[i].nparams == 1 ? "" : "s",
                            nargs, nargs == 1 ? "was" : "were");
                        g_lex_file_id = n->file_id;   /* te_capture_error reads the current file */
                        te_capture_error(n->line > 0 ? n->line : *lastLine, msg, n->id);
                    }
                    break;
                }
            }
        }

        if (strcmp(n->type, "LAMBDA") == 0 && n->id && n->id[0]) {
            /* Sombrear los parametros del lambda dentro de su cuerpo (n->left).
             * NO se recorre ->right/->extra: en lambdas `extra` puede guardar el
             * entorno de captura (no un nodo estandar). El cuerpo esta en left. */
            char buf[512];
            char *ext = buf;
            size_t sl = shadow ? strlen(shadow) : 0;
            size_t pl = strlen(n->id);
            size_t need = sl + (sl ? 1 : 0) + pl + 1;
            if (need > sizeof(buf)) ext = (char *)malloc(need);
            if (ext) {
                if (sl) { memcpy(ext, shadow, sl); ext[sl] = '\1'; memcpy(ext + sl + 1, n->id, pl + 1); }
                else    { memcpy(ext, n->id, pl + 1); }
                te_arity_walk(n->left, tbl, count, ext, lastLine);
                if (ext != buf) free(ext);
            } else {
                te_arity_walk(n->left, tbl, count, shadow, lastLine);
            }
            n = n->next; continue;
        }

        /* left y right son siempre hijos ASTNode. El campo extra esta
         * SOBRECARGADO: en nodos OBJECT/LIST/MAP guarda ObjectNode, TEListIdx o
         * TEMapHash (punteros que NO son ASTNode). Solo es un nodo hijo real en
         * TERNARY (rama else). Recorrerlo en otros tipos leeria un puntero
         * no-nodo y corromperia la memoria. El else de un if va en next
         * (create_if_node), asi que queda cubierto por la iteracion del bucle. */
        te_arity_walk(n->left,  tbl, count, shadow, lastLine);
        te_arity_walk(n->right, tbl, count, shadow, lastLine);
        if (strcmp(n->type, "TERNARY") == 0 || strcmp(n->type, "FOR_C") == 0)
            te_arity_walk(n->extra, tbl, count, shadow, lastLine);
        n = n->next;
    }
}

/* Punto de entrada llamado por run_syntax_check() tras un parseo sin errores. */
void te_syntax_check_arity(ASTNode *root) {
    if (!root) return;
    TeArityFn tbl[512];
    int count = 0;
    te_arity_collect_toplevel(root, tbl, &count, 512);
    if (count == 0) return;   /* no hay fn nombradas -> nada que chequear */
    int lastLine = 0;
    te_arity_walk(root, tbl, count, "", &lastLine);
}

/* ==========================================================================
 * --syntax-check: chequeos SEMANTICOS estaticos (los tropiezos de la primera
 * hora con TypeEasy, que hasta ahora solo se veian en runtime):
 *   S1  reasignar un `let`/`const`      -> "cannot assign to constant variable"
 *   S2  `for (i = 0; i < n; 1)`         -> el 2o campo es LIMITE, no condicion
 *   S3  `for (i = 0; n; i < 5)` etc.    -> el 3o campo es PASO, no condicion
 *   S4  `for (let x in 5)` / in "str"   -> el operando debe ser una lista
 * Ambitos: cada LAMBDA y cada cuerpo de bloque abre un scope; una `let x` dentro
 * de una fn no bloquea un `x = ...` externo. Conservador: si el nombre no fue
 * declarado en un scope visible, no se marca (puede venir de otro archivo).
 * ========================================================================== */
typedef struct { const char *name; int is_const; } TeScopeVar;
typedef struct TeScope { TeScopeVar v[256]; int n; struct TeScope *up; } TeScope;

static int te_scope_lookup_const(TeScope *s, const char *name) {
    for (; s; s = s->up)
        for (int i = s->n - 1; i >= 0; i--)
            if (s->v[i].name && strcmp(s->v[i].name, name) == 0) return s->v[i].is_const;
    return -1;   /* not declared in a visible scope */
}
static void te_scope_declare(TeScope *s, const char *name, int is_const) {
    for (int i = 0; i < s->n; i++)
        if (s->v[i].name && strcmp(s->v[i].name, name) == 0) { s->v[i].is_const = is_const; return; }
    if (s->n < 256) { s->v[s->n].name = name; s->v[s->n].is_const = is_const; s->n++; }
}
static int te_node_is_comparison(ASTNode *n) {
    if (!n || !n->type) return 0;
    return strcmp(n->type, "LT") == 0 || strcmp(n->type, "GT") == 0 || strcmp(n->type, "LT_EQ") == 0 ||
           strcmp(n->type, "GT_EQ") == 0 || strcmp(n->type, "EQ") == 0 || strcmp(n->type, "DIFF") == 0 ||
           strcmp(n->type, "AND") == 0 || strcmp(n->type, "OR") == 0 || strcmp(n->type, "NOT") == 0;
}
static void te_sem_error(ASTNode *n, int fallbackLine, const char *msg) {
    g_lex_file_id = n->file_id;
    te_capture_error(n->line > 0 ? n->line : fallbackLine, msg, n->id ? n->id : "");
}

static void te_sem_walk(ASTNode *n, TeScope *scope, int *lastLine) {
    while (n) {
        if (!n->type) { n = n->next; continue; }
        if (n->line > 0) *lastLine = n->line;

        if (strcmp(n->type, "STATEMENT_LIST") == 0) {
            /* The spine is left-recursive (newest statement in ->right); collect
             * and replay in PROGRAM order so declarations precede their uses. */
            int cap = 64, cnt = 0;
            ASTNode **stack = (ASTNode **)malloc((size_t)cap * sizeof(ASTNode *));
            ASTNode *cur = n;
            while (cur && cur->type && strcmp(cur->type, "STATEMENT_LIST") == 0) {
                if (cnt >= cap) { cap *= 2; stack = (ASTNode **)realloc(stack, (size_t)cap * sizeof(ASTNode *)); }
                stack[cnt++] = cur->right;
                cur = cur->left;
            }
            te_sem_walk(cur, scope, lastLine);               /* oldest statement / leaf */
            for (int i = cnt - 1; i >= 0; i--) te_sem_walk(stack[i], scope, lastLine);
            free(stack);
            n = n->next; continue;
        }

        if (strcmp(n->type, "VAR_DECL") == 0 && n->id) {
            te_sem_walk(n->left, scope, lastLine);          /* RHS first (may contain lambdas) */
            te_scope_declare(scope, n->id, n->value == 1);
            n = n->next; continue;
        }

        if (strcmp(n->type, "ASSIGN") == 0 && n->left && n->left->id &&
            n->left->type && (strcmp(n->left->type, "IDENTIFIER") == 0 || strcmp(n->left->type, "ID") == 0)) {
            if (te_scope_lookup_const(scope, n->left->id) == 1) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Error: cannot assign to constant variable '%s' (declared with let/const; use var).", n->left->id);
                te_sem_error(n->left->line > 0 ? n->left : n, *lastLine, msg);
            }
            te_sem_walk(n->right, scope, lastLine);
            n = n->next; continue;
        }

        if (strcmp(n->type, "FOR") == 0) {
            /* left=init, right=LIMIT (whose ->right is FOR_BODY{left=STEP,right=body}) */
            ASTNode *limit = n->right;
            ASTNode *fb = limit ? limit->right : NULL;
            ASTNode *step = fb ? fb->left : NULL;
            ASTNode *body = fb ? fb->right : NULL;
            if (te_node_is_comparison(limit)) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Error: for(init; LIMIT; STEP): the 2nd field is an exclusive LIMIT, not a condition "
                    "(write 'for (%s = 0; n; 1)', not 'i < n').", n->id ? n->id : "i");
                te_sem_error(n, *lastLine, msg);
            }
            if (te_node_is_comparison(step)) {
                te_sem_error(n, *lastLine,
                    "Error: for(init; LIMIT; STEP): the 3rd field is the STEP to add (e.g. 1), not a condition.");
            }
            TeScope inner = { .n = 0, .up = scope };
            if (n->id) te_scope_declare(&inner, n->id, 0);
            te_sem_walk(body, &inner, lastLine);
            n = n->next; continue;
        }

        if (strcmp(n->type, "FOR_C") == 0) {
            TeScope inner = { .n = 0, .up = scope };
            ASTNode *fb = (ASTNode *)n->extra;
            te_sem_walk(n->left, &inner, lastLine);           /* init (declares in loop scope) */
            te_sem_walk(n->right, &inner, lastLine);          /* cond */
            if (fb) { te_sem_walk(fb->left, &inner, lastLine); te_sem_walk(fb->right, &inner, lastLine); }
            n = n->next; continue;
        }

        if (strcmp(n->type, "FOR_IN") == 0) {
            ASTNode *src = n->left;
            if (src && src->type && (strcmp(src->type, "NUMBER") == 0 || strcmp(src->type, "INT") == 0 ||
                                     strcmp(src->type, "FLOAT") == 0 || strcmp(src->type, "STRING") == 0 ||
                                     strcmp(src->type, "STRING_LITERAL") == 0 || strcmp(src->type, "BOOL") == 0)) {
                te_sem_error(n, *lastLine, "Error: for-in expects a list; a scalar literal is not iterable.");
            }
            TeScope inner = { .n = 0, .up = scope };
            if (n->id) te_scope_declare(&inner, n->id, 0);
            te_sem_walk(n->right, &inner, lastLine);
            n = n->next; continue;
        }

        if (strcmp(n->type, "LAMBDA") == 0) {
            TeScope inner = { .n = 0, .up = scope };
            /* params ('\1'-separated in id) are mutable locals of the body */
            if (n->id && n->id[0]) {
                static char pbuf[64][128]; static int pi = 0;   /* names must outlive the walk of this body */
                const char *p = n->id;
                while (*p) {
                    const char *e = p; while (*e && *e != '\1') e++;
                    size_t len = (size_t)(e - p); if (len >= 128) len = 127;
                    char *slot = pbuf[pi++ % 64];
                    memcpy(slot, p, len); slot[len] = 0;
                    te_scope_declare(&inner, slot, 0);
                    p = *e ? e + 1 : e;
                }
            }
            te_sem_walk(n->left, &inner, lastLine);
            n = n->next; continue;
        }

        if (strcmp(n->type, "IF") == 0 || strcmp(n->type, "WHILE") == 0) {
            te_sem_walk(n->left, scope, lastLine);           /* condition */
            TeScope inner = { .n = 0, .up = scope };
            te_sem_walk(n->right, &inner, lastLine);         /* body */
            n = n->next; continue;                           /* else chain lives in ->next */
        }

        te_sem_walk(n->left,  scope, lastLine);
        te_sem_walk(n->right, scope, lastLine);
        if (strcmp(n->type, "TERNARY") == 0) te_sem_walk(n->extra, scope, lastLine);
        n = n->next;
    }
}

void te_syntax_check_semantics(ASTNode *root) {
    if (!root) return;
    TeScope top = { .n = 0, .up = NULL };
    int lastLine = 0;
    te_sem_walk(root, &top, &lastLine);
}

// Serializa un objeto a XML string dado su id y lo guarda en __ret__
void print_object_as_xml_by_id(const char* id) {
    Variable *var = find_variable((char*)id);
    if (!var) {
        fprintf(stderr, "Error: no variable with id '%s' found to serialize to XML.\n", id);
        return;
    }
    
    if (var->vtype == VAL_STRING) {
        ASTNode *result_node = create_ast_leaf("STRING", 0, var->value.string_value, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        return;
    }

    // Handle LISTs
    if (var->type && strcmp(var->type, "LIST") == 0) {
        ASTNode *listNode = (ASTNode *)(intptr_t)var->value.object_value;
        if (!listNode) {
             ASTNode *empty = create_ast_leaf("STRING", 0, "<Items></Items>", NULL);
             add_or_update_variable("__ret__", empty);
             free_ast(empty);
             return;
        }
        
        int estimated_size = 4096; 
        char *xml_buffer = malloc(estimated_size);
        if(!xml_buffer) return;
        strcpy(xml_buffer, "<Items>\n");
        
        ASTNode *cur = listNode->left;
        
        while (cur) {
            if (cur->type && strcmp(cur->type, "OBJECT") == 0) {
                ObjectNode *obj = NULL;
                if (cur->extra) obj = (ObjectNode *)cur->extra;
                else obj = (ObjectNode *)(intptr_t)cur->value;
                
                if (obj && obj->class) {
                    if (strlen(xml_buffer) + 1024 > estimated_size) {
                        estimated_size *= 2;
                        xml_buffer = realloc(xml_buffer, estimated_size);
                    }
                    
                    strcat(xml_buffer, "  <Item>\n");
                    for (int i = 0; i < obj->class->attr_count; i++) {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "    <%s>", obj->class->attributes[i].id);
                        strcat(xml_buffer, temp);
                        
                        Variable *attr = &obj->attributes[i];
                        if (attr->vtype == VAL_STRING) {
                            strcat(xml_buffer, attr->value.string_value ? attr->value.string_value : "");
                        } else if (attr->vtype == VAL_FLOAT) {
                            te_fmt_double(temp, sizeof(temp), attr->value.float_value);
                            strcat(xml_buffer, temp);
                        } else {
                            snprintf(temp, sizeof(temp), "%lld", (long long)attr->value.int_value);
                            strcat(xml_buffer, temp);
                        }
                        snprintf(temp, sizeof(temp), "</%s>\n", obj->class->attributes[i].id);
                        strcat(xml_buffer, temp);
                    }
                    strcat(xml_buffer, "  </Item>\n");
                }
            }
            cur = cur->next;
        }
        strcat(xml_buffer, "</Items>");
        
        ASTNode *result_node = create_ast_leaf("STRING", 0, xml_buffer, NULL);
        add_or_update_variable("__ret__", result_node);
        free_ast(result_node);
        free(xml_buffer);
        return;
    }

    if (var->vtype != VAL_OBJECT || !var->value.object_value) {
        fprintf(stderr, "Error: variable '%s' is not a valid object to serialize to XML.\n", id);
        return;
    }

    ObjectNode *obj = var->value.object_value;
    char *xml_buffer = malloc(4096);
    snprintf(xml_buffer, 4096, "<%s>\n", obj->class->name);
    for (int i = 0; i < obj->class->attr_count; i++) {
        char temp[512];
        snprintf(temp, sizeof(temp), "  <%s>", obj->attributes[i].id);
        strcat(xml_buffer, temp);
        if (obj->attributes[i].vtype == VAL_STRING) {
            strcat(xml_buffer, obj->attributes[i].value.string_value ? obj->attributes[i].value.string_value : "");
        } else if (obj->attributes[i].vtype == VAL_INT) {
            snprintf(temp, sizeof(temp), "%lld", (long long)obj->attributes[i].value.int_value);
            strcat(xml_buffer, temp);
        } else if (obj->attributes[i].vtype == VAL_FLOAT) {
            te_fmt_double(temp, sizeof(temp), obj->attributes[i].value.float_value);
            strcat(xml_buffer, temp);
        } else {
            strcat(xml_buffer, "null");
        }
        snprintf(temp, sizeof(temp), "</%s>\n", obj->attributes[i].id);
        strcat(xml_buffer, temp);
    }
    char end_tag[256];
    snprintf(end_tag, sizeof(end_tag), "</%s>", obj->class->name);
    strcat(xml_buffer, end_tag);
    
    ASTNode *result_node = create_ast_leaf("STRING", 0, xml_buffer, NULL);
    add_or_update_variable("__ret__", result_node);
    free_ast(result_node);
    free(xml_buffer);
}

void print_object_as_json_by_id(const char* id) {
    ASTNode *arg = (ASTNode*)calloc(1, sizeof(ASTNode));
    if(!arg) return;
    arg->type = strdup("IDENTIFIER");
    arg->id = strdup(id);
    arg->left = NULL;
    arg->right = NULL;
    arg->str_value = NULL;
    arg->value = 0;
    arg->next = NULL;
    arg->extra = NULL;

    native_json(arg);
    
    free(arg->id);
    free(arg->type);
    free(arg);
}

/* Return a freshly malloc'd text/plain rendering of a "bare value" return
 * expression, or NULL if the expression is not a plain scalar/string we should
 * surface as a raw HTTP body. Only side-effect-free node kinds are handled here
 * (no CALL_FUNC / CALL_METHOD — those already populate __ret__ when invoked and
 * must not be re-evaluated). Object/list/map identifiers return NULL so they
 * keep the existing behavior (only json()/xml() serialize them). */
static char *te_return_raw_text(ASTNode *e) {
    if (!e || !e->type) return NULL;
    const char *t = e->type;
    if (!strcmp(t, "STRING") || !strcmp(t, "STRING_LITERAL"))
        return e->str_value ? strdup(e->str_value) : strdup("");
    if (!strcmp(t, "STRING_INTERP"))
        return expand_interp_string(e->str_value ? e->str_value : "");
    if (!strcmp(t, "NUMBER") || !strcmp(t, "INT")) {
        char b[32]; snprintf(b, sizeof(b), "%lld", (long long)e->value); return strdup(b);
    }
    if (!strcmp(t, "FLOAT")) {
        char b[48]; te_fmt_double(b, sizeof(b), e->str_value ? atof(e->str_value) : 0.0); return strdup(b);
    }
    if (!strcmp(t, "IDENTIFIER") || !strcmp(t, "ID")) {
        Variable *v = find_variable(e->id);
        if (v && v->vtype == VAL_STRING) return strdup(v->value.string_value ? v->value.string_value : "");
        if (v && v->vtype == VAL_INT)   { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)v->value.int_value); return strdup(b); }
        if (v && v->vtype == VAL_FLOAT) { char b[48]; te_fmt_double(b, sizeof(b), v->value.float_value); return strdup(b); }
        return NULL; /* object/list/map variable -> not raw text */
    }
    if (!strcmp(t, "ADD") && is_string_type(e))
        return get_node_string(e); /* string concatenation only */
    return NULL;
}

static void interpret_return_node(ASTNode *node) {
   // fprintf(stderr, "[DEBUG] interpret_return_node called\n"); fflush(stderr);
    ASTNode *ret_expr = node->left;
    g_vm.return_node = ret_expr;

    if (ret_expr) {
       // fprintf(stderr, "[DEBUG] interpret_return_node: executing return expression type=%s\n", return_node->type); fflush(stderr);
        /* Bugfix v0.0.29: `return obj["k"]` / `return obj.k` cuyo valor es
         * string. La ruta numerica (interpret_ast -> evaluate_expression ->
         * NK_ACCESS_EXPR) emite "value at Map[...] is a string" y devuelve 0,
         * asi que la funcion retornaba 0 en vez del string. Capturamos el valor
         * con el getter string-aware (el mismo que usa concat/print, que ya
         * funcionaba) y lo guardamos en __ret__ como STRING, evitando la ruta
         * numerica. Solo aplica cuando el acceso ES string (is_string_type),
         * para no convertir retornos numericos en texto. */
        if (ret_expr->type &&
            (strcmp(ret_expr->type, "ACCESS_EXPR") == 0 ||
             strcmp(ret_expr->type, "ACCESS_ATTR") == 0) &&
            is_string_type(ret_expr)) {
            char *s = get_node_string(ret_expr);
            ASTNode *leaf = create_ast_leaf("STRING", 0, s ? s : "", NULL);
            add_or_update_variable("__ret__", leaf);
            free_ast(leaf);
            free(s);
            g_vm.return_node = ret_expr;
            g_response_is_raw_text = 1;
            g_vm.return_flag = 1;
            return;
        }
        interpret_ast(ret_expr);
        //fprintf(stderr, "[DEBUG] interpret_return_node: expression executed\n"); fflush(stderr);
        /* A nested call inside the return expression (e.g. `return Math.sqrt(x)`
         * or `return helper()`) runs through interpret_call_method/func, which
         * clears the global return_node. Restore it so the caller's return-type
         * validation and value extraction still see the return expression. The
         * produced value is already stored in __ret__. */
        g_vm.return_node = ret_expr;

        /* HTTP response content-type intent. `return json(...)`/`return xml(...)`
         * stay structured. A bare value return (`return "text"`, `return 42`,
         * `return msg`, string concat) is surfaced as a text/plain body: it is
         * stringified into __ret__ here so the embedded server has a body to
         * write, and g_response_is_raw_text flips the Content-Type. This
         * eliminates the onboarding gotcha where `return "text"` produced an
         * empty body. The flag is "last writer wins": json()/xml() reset it to 0
         * via their own return node, nested helpers propagate naturally. */
        const char *t = ret_expr->type;
        int is_json_xml_call =
            (t && strcmp(t, "CALL_FUNC") == 0 && ret_expr->id &&
             (strcmp(ret_expr->id, "json") == 0 || strcmp(ret_expr->id, "xml") == 0));
        if (is_json_xml_call) {
            g_response_is_raw_text = 0;
        } else {
            char *raw = te_return_raw_text(ret_expr);
            if (raw) {
                ASTNode *leaf = create_ast_leaf("STRING", 0, raw, NULL);
                add_or_update_variable("__ret__", leaf);
                free_ast(leaf);
                free(raw);
                g_response_is_raw_text = 1;
            }
        }
    }
    g_vm.return_flag = 1;
}
