/* te_vm.c — Ciclo de vida de la VM del intérprete.
 *
 * TODO el estado del intérprete vive en `TeVM` (te_vm.h): variables, clases,
 * endpoints, request/response HTTP, tabla de archivos, intern, profiler, debugger,
 * servidor embebido, event loop, CSV, builtins, tasks async, bridges. `g_vm` es
 * una macro sobre `*te_vm_cur`, y `te_vm_cur` es THREAD-LOCAL: cada hilo arranca
 * apuntando a la VM principal y puede conmutar a la suya (te_vm_set_current).
 * Con el lexer/parser reentrantes (te_parse.h) dos hilos pueden parsear y
 * ejecutar programas distintos a la vez sin compartir nada: lo verifica el
 * selftest de hilos de abajo (--selftest-vm). Lo único de proceso que queda son
 * los flags que escribe un signal handler (typeeasy_api_server.c). */
#include "te_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#  include <process.h>
#  define TE_GETPID _getpid
#else
#  include <unistd.h>
#  define TE_GETPID getpid
#endif

ASTNode *parse_file(FILE *file);        /* parser.y (sin prototipo en ast.h). Sin esto, en LLP64 el
                                           puntero se trunca a int -> segfault fuera de gdb. */

static TeVM g_vm_main_storage = { TE_VM_DEFAULTS };   /* VM principal: estática, sin malloc en arranque */
__thread TeVM *te_vm_cur = &g_vm_main_storage;

TeVM *te_vm_main(void) { return &g_vm_main_storage; }

/* Accessor para código que no incluye headers del intérprete (api_server/te_websocket.c). */
struct MethodNode *te_global_methods(void) { return g_vm.global_methods; }

TeVM *te_vm_create(void) {
    TeVM *vm = (TeVM *)calloc(1, sizeof(TeVM));
    if (vm) { vm->profile_enabled = -1; vm->intern_enabled = 1; vm->resp_status = 200; }   /* = TE_VM_DEFAULTS */
    return vm;
}

void te_vm_destroy(TeVM *vm) {
    if (!vm || vm == te_vm_cur || vm == &g_vm_main_storage) return;
    free(vm);
}

TeVM *te_vm_set_current(TeVM *vm) {
    TeVM *prev = te_vm_cur;
    if (vm) te_vm_cur = vm;
    return prev;
}

/* ---------- selftest: dos VMs, mismo nombre de variable, valores distintos ---------- */

static ASTNode *te_vm_parse_src(const char *src, int idx) {
    const char *base = getenv("TEMP");
    if (!base || !*base) base = getenv("TMP");
    if (!base || !*base) base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    char path[1024];
    snprintf(path, sizeof(path), "%s/te_selftest_vm_%d_%d.te", base, (int)TE_GETPID(), idx);
    FILE *w = fopen(path, "w");
    if (!w) return NULL;
    fputs(src, w); fputc('\n', w); fclose(w);
    FILE *r = fopen(path, "r");
    if (!r) { remove(path); return NULL; }
    ASTNode *ast = parse_file(r);
    fclose(r); remove(path);
    return ast;
}

static long long te_vm_int_of(const char *name, int *found) {
    Variable *v = find_variable((char *)name);
    *found = (v != NULL);
    return (v && v->vtype == VAL_INT) ? (long long)v->value.int_value : -1;
}

/* Un caso del selftest de hilos: programa + valores esperados + resultados. */
typedef struct TeThreadCase {
    int idx; const char *src; const char *cls; const char *route;
    long long exp_acc, exp_r; const char *exp_tag;
    long long got_acc, got_r; int got_classes; int fails; char *got_tag; const char *got_route;
} TeThreadCase;

static void *te_vm_thread_main(void *arg) {
    TeThreadCase *c = (TeThreadCase *)arg;
    TeVM *vm = te_vm_create();
    if (!vm) { c->fails = 1; return NULL; }
    TeVM *prev = te_vm_set_current(vm);              /* te_vm_cur es thread-local: solo este hilo */
    ASTNode *ast = te_vm_parse_src(c->src, 100 + c->idx);
    if (!ast) { c->fails = 1; te_vm_set_current(prev); te_vm_destroy(vm); return NULL; }
    interpret_ast(ast);
    int f;
    c->got_acc = te_vm_int_of("acc", &f); if (!f || c->got_acc != c->exp_acc) c->fails++;
    c->got_r   = te_vm_int_of("r", &f);   if (!f || c->got_r != c->exp_r) c->fails++;
    Variable *tag = find_variable("tag");
    c->got_tag = (tag && tag->vtype == VAL_STRING && tag->value.string_value) ? strdup(tag->value.string_value) : NULL;
    if (!c->got_tag || strcmp(c->got_tag, c->exp_tag) != 0) c->fails++;
    c->got_classes = g_vm.class_count;
    c->got_route = (g_vm.global_methods && g_vm.global_methods->name) ? g_vm.global_methods->name : "";
    if (c->got_classes != 1 || !find_class((char *)c->cls) || strcmp(c->got_route, c->route) != 0 || (g_vm.global_methods && g_vm.global_methods->next)) c->fails++;
    te_vm_set_current(prev);
    te_vm_destroy(vm);
    return NULL;
}

int te_vm_selftest(void) {
    int fails = 0, f;
    TeVM *a = te_vm_create(), *b = te_vm_create();
    if (!a || !b) { printf("{\"ok\":false,\"error\":\"calloc\"}\n"); return 1; }
    TeVM *prev = te_vm_set_current(a);
    /* Fase 3C: cada VM declara su PROPIA clase y su PROPIO endpoint (registros que antes eran
     * globales de proceso: classes[] / global_methods). */
    ASTNode *ast_a = te_vm_parse_src(
        "class Punto { public int px = 1; Dob() : int { return this.px * 2; } }\n"
        "endpoint { [HttpGet(\"/a\")] RutaA() { return json({ vm: \"a\" }); } }\n"
        "let x = 10; var acc = 0; for (i = 0; 5; 1) { acc = acc + i; }\n"
        "let p = new Punto(); p.px = 21; let dob = p.Dob();", 1);
    if (ast_a) interpret_ast(ast_a); else fails++;
    int a_classes = g_vm.class_count; const char *a_route = g_vm.global_methods && g_vm.global_methods->name ? g_vm.global_methods->name : "";
    te_vm_set_current(b);
    ASTNode *ast_b = te_vm_parse_src(
        "class Caja { public string tag = \"b\"; }\n"
        "endpoint { [HttpGet(\"/b\")] RutaB() { return json({ vm: \"b\" }); } }\n"
        "let x = 20; let y = 5;", 2);
    if (ast_b) interpret_ast(ast_b); else fails++;
    int b_classes = g_vm.class_count; const char *b_route = g_vm.global_methods && g_vm.global_methods->name ? g_vm.global_methods->name : "";
    /* B: una sola clase (Caja), un solo endpoint (RutaB), sin rastro de A */
    if (b_classes != 1 || strcmp(b_route, "RutaB") != 0 || (g_vm.global_methods && g_vm.global_methods->next)) fails++;
    if (find_class("Punto") != NULL) fails++;

    /* B ve lo suyo y NO ve lo de A */
    long long bx = te_vm_int_of("x", &f); if (!f || bx != 20) fails++;
    long long by = te_vm_int_of("y", &f); if (!f || by != 5) fails++;
    te_vm_int_of("acc", &f); if (f) fails++;
    int b_count = g_vm.var_count;

    /* A conserva lo suyo y NO ve lo de B */
    te_vm_set_current(a);
    long long ax = te_vm_int_of("x", &f); if (!f || ax != 10) fails++;
    long long aacc = te_vm_int_of("acc", &f); if (!f || aacc != 10) fails++;
    long long adob = te_vm_int_of("dob", &f); if (!f || adob != 42) fails++;
    te_vm_int_of("y", &f); if (f) fails++;
    if (a_classes != 1 || strcmp(a_route, "RutaA") != 0) fails++;
    if (find_class("Caja") != NULL || find_class("Punto") == NULL) fails++;
    int a_count = g_vm.var_count;

    /* la VM principal sigue vacía (sin variables, clases ni endpoints) */
    te_vm_set_current(prev);
    if (g_vm.var_count != 0 || g_vm.class_count != 0 || g_vm.global_methods != NULL) fails++;
    te_vm_destroy(a); te_vm_destroy(b);

    /* ---- 2 HILOS en paralelo, cada uno con su VM: parsea (lexer/parser reentrantes)
     * y ejecuta un programa distinto (clase, endpoint, bucle) al mismo tiempo. ---- */
    TeThreadCase tc[2] = {
        { 1, "class Th1 { public int k = 3; Mul() : int { return this.k * 1000; } }\n"
             "endpoint { [HttpGet(\"/t1\")] RutaT1() { return json({ t: 1 }); } }\n"
             "var acc = 0; for (i = 0; 300000; 1) { acc = acc + 1; }\n"
             "let o = new Th1(); let r = o.Mul(); let tag = \"uno\" + 1;", "Th1", "RutaT1", 300000, 3000, "uno1", 0, 0, 0, 0, NULL, 0 },
        { 2, "class Th2 { public int k = 7; Mul() : int { return this.k * 1000; } }\n"
             "endpoint { [HttpGet(\"/t2\")] RutaT2() { return json({ t: 2 }); } }\n"
             "var acc = 0; for (i = 0; 200000; 1) { acc = acc + 2; }\n"
             "let o = new Th2(); let r = o.Mul(); let tag = \"dos\" + 2;", "Th2", "RutaT2", 400000, 7000, "dos2", 0, 0, 0, 0, NULL, 0 },
    };
    pthread_t th[2];
    int thr_fails = 0;
    for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, te_vm_thread_main, &tc[i]);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < 2; i++) thr_fails += tc[i].fails;
    /* el hilo principal no vio nada de lo que hicieron los hilos */
    if (g_vm.var_count != 0 || g_vm.class_count != 0 || g_vm.global_methods != NULL || g_vm.src_file_count != 0) thr_fails++;
    fails += thr_fails;

    printf("{\"ok\":%s,\"vm_a\":{\"x\":%lld,\"acc\":%lld,\"dob\":%lld,\"var_count\":%d,\"classes\":%d,\"route\":\"%s\"},"
           "\"vm_b\":{\"x\":%lld,\"y\":%lld,\"var_count\":%d,\"classes\":%d,\"route\":\"%s\"},\"main\":{\"var_count\":%d,\"classes\":%d},"
           "\"threads\":{\"ok\":%s,\"t1\":{\"acc\":%lld,\"r\":%lld,\"tag\":\"%s\",\"classes\":%d,\"route\":\"%s\"},"
           "\"t2\":{\"acc\":%lld,\"r\":%lld,\"tag\":\"%s\",\"classes\":%d,\"route\":\"%s\"}},\"sizeof_TeVM\":%zu}\n",
           fails ? "false" : "true", ax, aacc, adob, a_count, a_classes, a_route, bx, by, b_count, b_classes, b_route, g_vm.var_count, g_vm.class_count,
           thr_fails ? "false" : "true", tc[0].got_acc, tc[0].got_r, tc[0].got_tag ? tc[0].got_tag : "", tc[0].got_classes, tc[0].got_route,
           tc[1].got_acc, tc[1].got_r, tc[1].got_tag ? tc[1].got_tag : "", tc[1].got_classes, tc[1].got_route, sizeof(TeVM));
    fflush(stdout);
    free(tc[0].got_tag); free(tc[1].got_tag);
    return fails ? 1 : 0;
}
