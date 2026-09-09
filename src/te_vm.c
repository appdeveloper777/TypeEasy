/* te_vm.c — Ciclo de vida de la VM del intérprete (Fase 3, paso B).
 *
 * El estado del intérprete vive en `TeVM` (te_vm.h). `g_vm` es una macro sobre
 * `*te_vm_cur`, así que TODO el código existente sigue escribiendo `g_vm.x` y, a la
 * vez, es posible tener varias VMs por proceso y conmutarlas (te_vm_set_current).
 *
 * Lo que todavía NO es por-VM (Fase 3C, no bloqueante para el ERP): las clases
 * declaradas (`classes[]`/`class_count`), el arena de strings interned, el estado
 * del lexer/parser (flex/bison no reentrantes) y los pools de conexiones DB. Por
 * eso el selftest prueba aislamiento de VARIABLES entre dos VMs, que es lo que
 * el runtime necesita para aislar requests; la concurrencia real sigue siendo
 * por prefork (`--workers N`). */
#include "te_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <process.h>
#  define TE_GETPID _getpid
#else
#  include <unistd.h>
#  define TE_GETPID getpid
#endif

ASTNode *parse_file(FILE *file);        /* parser.y (sin prototipo en ast.h). Sin esto, en LLP64 el
                                           puntero se trunca a int -> segfault fuera de gdb. */

static TeVM g_vm_main_storage;          /* VM principal: estática, sin malloc en arranque */
TeVM *te_vm_cur = &g_vm_main_storage;

TeVM *te_vm_main(void) { return &g_vm_main_storage; }

/* Accessor para código que no incluye headers del intérprete (api_server/te_websocket.c). */
struct MethodNode *te_global_methods(void) { return g_vm.global_methods; }

TeVM *te_vm_create(void) {
    return (TeVM *)calloc(1, sizeof(TeVM));
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

    printf("{\"ok\":%s,\"vm_a\":{\"x\":%lld,\"acc\":%lld,\"dob\":%lld,\"var_count\":%d,\"classes\":%d,\"route\":\"%s\"},"
           "\"vm_b\":{\"x\":%lld,\"y\":%lld,\"var_count\":%d,\"classes\":%d,\"route\":\"%s\"},\"main\":{\"var_count\":%d,\"classes\":%d},\"sizeof_TeVM\":%zu}\n",
           fails ? "false" : "true", ax, aacc, adob, a_count, a_classes, a_route, bx, by, b_count, b_classes, b_route, g_vm.var_count, g_vm.class_count, sizeof(TeVM));
    fflush(stdout);
    return fails ? 1 : 0;
}
