#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ast.h" 
#include "wasm_backend.h"
#include "debugger.h"

/* --- Prototipos de las funciones en tu "Motor" --- */
ASTNode* parse_file(FILE* file);
extern int g_debug_mode; // Para acceder a la variable global de parser.y
/* --- Fin Prototipos --- */

static int convert_wat_to_wasm(const char *wat_path, const char *wasm_path) {
    char command[1024];
    int written = snprintf(command, sizeof(command), "wat2wasm \"%s\" -o \"%s\"", wat_path, wasm_path);
    if (written < 0 || written >= (int)sizeof(command)) {
        fprintf(stderr, "[WASM] Error: comando wat2wasm demasiado largo.\n");
        return 0;
    }

    int result = system(command);
    if (result != 0) {
        fprintf(stderr, "[WASM] Error: no se pudo ejecutar wat2wasm. Instala WABT o usa --emit-wat.\n");
        return 0;
    }
    return 1;
}

// Helper function to detect response type from AST
const char* detect_response_type(ASTNode *body) {
    if (!body) return "json"; // default
    
    // Recursively search for RETURN_XML or RETURN_JSON nodes
    if (body->type) {
        if (strcmp(body->type, "RETURN_XML") == 0) return "xml";
        if (strcmp(body->type, "RETURN_JSON") == 0) return "json";
        
        // Handle explicit return xml() / return json()
        if (strcmp(body->type, "RETURN") == 0 && body->left) {
            if (body->left->type && strcmp(body->left->type, "CALL_FUNC") == 0 && body->left->id) {
                if (strcmp(body->left->id, "xml") == 0) return "xml";
                if (strcmp(body->left->id, "json") == 0) return "json";
            }
        }
    }
    
    // Check children
    if (body->left) {
        const char *left_type = detect_response_type(body->left);
        if (strcmp(left_type, "xml") == 0) return "xml";
    }
    if (body->right) {
        const char *right_type = detect_response_type(body->right);
        if (strcmp(right_type, "xml") == 0) return "xml";
    }
    
    // Check next (for statement lists)
    if (body->next) {
        const char *next_type = detect_response_type(body->next);
        if (strcmp(next_type, "xml") == 0) return "xml";
    }
    
    return "json"; // default
}

/**
 * Main para el EJECUTABLE 'typeeasy'.
 * Ejecuta un script y termina.
 * Esto es lo que 'servidor_api.c' llamará.
 */

/* ========== Phase F: REPL & test runner helpers ========== */
#include <dirent.h>
#include <sys/stat.h>

extern int throw_flag;
extern char *get_throw_message(void);
extern void __ret_var_clear(void);

/* Parse a string of TypeEasy source by writing it to a temp file and
 * reusing parse_file. Returns the AST or NULL on parse error. */
static ASTNode *parse_string(const char *src) {
    char path[] = "/tmp/te_repl_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return NULL;
    size_t L = strlen(src);
    if (write(fd, src, L) != (ssize_t)L) { close(fd); unlink(path); return NULL; }
    if (L == 0 || src[L-1] != '\n') write(fd, "\n", 1);
    close(fd);
    FILE *fp = fopen(path, "r");
    if (!fp) { unlink(path); return NULL; }
    ASTNode *ast = parse_file(fp);
    fclose(fp);
    unlink(path);
    return ast;
}

/* REPL: read line, wrap in `let __r = (...)` if it looks like a bare
 * expression, otherwise execute as a statement. Persists vars/classes
 * between iterations because we never reset them. */
static int run_repl(void) {
    fprintf(stdout, "TypeEasy REPL — escribe :help para ayuda, :quit para salir.\n");
    fflush(stdout);
    runtime_save_initial_var_count();
    char buf[8192];
    int line_no = 0;
    while (1) {
        line_no++;
        fprintf(stdout, "te> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) { fprintf(stdout, "\n"); break; }
        /* trim trailing newline + spaces */
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ' || buf[n-1] == '\t')) buf[--n] = 0;
        if (n == 0) continue;
        if (strcmp(buf, ":quit") == 0 || strcmp(buf, ":q") == 0) break;
        if (strcmp(buf, ":help") == 0) {
            fprintf(stdout, "Comandos: :quit  :help  :vars\n"
                            "Cualquier otra cosa se evalúa como TypeEasy.\n"
                            "Termina expresiones con `;`. Para imprimir, usa println(...).\n");
            continue;
        }
        if (strcmp(buf, ":vars") == 0) {
            extern Variable vars[];
            extern int var_count;
            for (int i = 0; i < var_count; i++) {
                if (vars[i].id && vars[i].id[0] == '_' && vars[i].id[1] == '_') continue;
                fprintf(stdout, "  %s : %s = ", vars[i].id ? vars[i].id : "?", vars[i].type ? vars[i].type : "?");
                switch (vars[i].vtype) {
                    case VAL_INT:    fprintf(stdout, "%d\n", vars[i].value.int_value); break;
                    case VAL_FLOAT:  fprintf(stdout, "%g\n", vars[i].value.float_value); break;
                    case VAL_STRING: fprintf(stdout, "\"%s\"\n", vars[i].value.string_value ? vars[i].value.string_value : ""); break;
                    default:         fprintf(stdout, "<object>\n"); break;
                }
            }
            continue;
        }
        /* Heuristic: if line ends in ';' or '}' treat as statement; else
         * wrap in println(...) so user gets to see expression value. */
        char src[8200];
        int has_term = (n > 0 && (buf[n-1] == ';' || buf[n-1] == '}'));
        int starts_with_kw = (strncmp(buf, "let ", 4) == 0 || strncmp(buf, "var ", 4) == 0 ||
                              strncmp(buf, "const ", 6) == 0 || strncmp(buf, "if ", 3) == 0 ||
                              strncmp(buf, "while ", 6) == 0 || strncmp(buf, "for ", 4) == 0 ||
                              strncmp(buf, "class ", 6) == 0 || strncmp(buf, "println", 7) == 0 ||
                              strncmp(buf, "print", 5) == 0 || strncmp(buf, "return ", 7) == 0 ||
                              strncmp(buf, "import ", 7) == 0 || strncmp(buf, "throw ", 6) == 0);
        if (has_term || starts_with_kw) {
            snprintf(src, sizeof(src), "%s%s", buf, has_term ? "" : ";");
        } else {
            snprintf(src, sizeof(src), "println(%s);", buf);
        }
        ASTNode *ast = parse_string(src);
        if (!ast) {
            fprintf(stderr, "(error de sintaxis)\n");
            continue;
        }
        interpret_ast(ast);
        if (throw_flag) {
            const char *m = get_throw_message();
            fprintf(stderr, "Uncaught: %s\n", m ? m : "(sin mensaje)");
            throw_flag = 0;
        }
    }
    return 0;
}

/* Test runner: discover *_test.te files in dir (default ./), execute
 * each, count passed/failed. A test "passes" if interpret_ast finished
 * without uncaught throw and __test_failed remains 0. The script can
 * call assert(cond) / assert_eq(a,b) which set __test_failed=1 on fail. */
/* Defined here (non-static) so the assert/assert_eq builtins in ast.c
 * (which declare them extern) refer to the same variables. */
int g_test_failed = 0;
int g_test_assertions = 0;

/* Phase F.3: error capture buffer used by yyerror in --syntax-check mode.
 * yyerror checks g_capture_errors; if non-zero, it appends to g_errors[]
 * instead of printing. */
int g_capture_errors = 0;
typedef struct { int line; char msg[256]; char near[128]; } TeErr;
static TeErr g_errors[64];
static int g_error_count = 0;

void te_capture_error(int line, const char *msg, const char *near) {
    if (g_error_count >= 64) return;
    TeErr *e = &g_errors[g_error_count++];
    e->line = line;
    snprintf(e->msg, sizeof(e->msg), "%s", msg ? msg : "");
    snprintf(e->near, sizeof(e->near), "%s", near ? near : "");
}

static void json_emit_str(FILE *fp, const char *s) {
    fputc('"', fp);
    for (const unsigned char *p = (const unsigned char*)(s ? s : ""); *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if (*p < 0x20) fprintf(fp, "\\u%04x", *p);
                else fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

/* --syntax-check <file>: parse only, emit JSON {ok, errors:[{line,msg,near}]} */
static int run_syntax_check(const char *path) {
    g_capture_errors = 1;
    g_error_count = 0;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("{\"ok\":false,\"errors\":[{\"line\":0,\"msg\":\"cannot open file\",\"near\":\"\"}]}\n");
        return 1;
    }
    parse_file(fp);
    fclose(fp);
    printf("{\"ok\":%s,\"errors\":[", g_error_count == 0 ? "true" : "false");
    for (int i = 0; i < g_error_count; i++) {
        if (i) fputc(',', stdout);
        printf("{\"line\":%d,\"msg\":", g_errors[i].line);
        json_emit_str(stdout, g_errors[i].msg);
        printf(",\"near\":");
        json_emit_str(stdout, g_errors[i].near);
        fputc('}', stdout);
    }
    printf("]}\n");
    return 0;
}

/* --symbols <file>: parse, then dump classes + global methods + top-level vars
 * as JSON {classes:[{name,line,methods:[{name,params,line}],attrs:[{name,type}]}],
 *          functions:[{name,params,line}], variables:[{name,type,line}]} */
extern Variable vars[];
extern int var_count;
extern ClassNode *classes[];
extern int class_count;

static int run_symbols(const char *path) {
    g_capture_errors = 1;
    g_error_count = 0;
    FILE *fp = fopen(path, "r");
    if (!fp) { printf("{\"ok\":false}\n"); return 1; }
    ASTNode *ast = parse_file(fp);
    fclose(fp);
    (void)ast;
    printf("{\"ok\":true,\"classes\":[");
    for (int i = 0; i < class_count; i++) {
        ClassNode *c = classes[i];
        if (i) fputc(',', stdout);
        printf("{\"name\":");
        json_emit_str(stdout, c->name ? c->name : "");
        printf(",\"parent\":");
        json_emit_str(stdout, c->parent && c->parent->name ? c->parent->name : "");
        printf(",\"attrs\":[");
        for (int j = 0; j < c->attr_count; j++) {
            if (j) fputc(',', stdout);
            printf("{\"name\":");
            json_emit_str(stdout, c->attributes[j].id ? c->attributes[j].id : "");
            printf(",\"type\":");
            json_emit_str(stdout, c->attributes[j].type ? c->attributes[j].type : "");
            fputc('}', stdout);
        }
        printf("],\"methods\":[");
        int first = 1;
        for (MethodNode *m = c->methods; m; m = m->next) {
            if (!first) fputc(',', stdout);
            first = 0;
            printf("{\"name\":");
            json_emit_str(stdout, m->name ? m->name : "");
            printf(",\"return_type\":");
            json_emit_str(stdout, m->return_type ? m->return_type : "");
            fputc('}', stdout);
        }
        printf("]}");
    }
    printf("],\"functions\":[");
    int first = 1;
    for (MethodNode *m = global_methods; m; m = m->next) {
        if (!first) fputc(',', stdout);
        first = 0;
        printf("{\"name\":");
        json_emit_str(stdout, m->name ? m->name : "");
        printf(",\"return_type\":");
        json_emit_str(stdout, m->return_type ? m->return_type : "");
        fputc('}', stdout);
    }
    printf("],\"variables\":[");
    first = 1;
    for (int i = 0; i < var_count; i++) {
        if (!vars[i].id || (vars[i].id[0] == '_' && vars[i].id[1] == '_')) continue;
        if (!first) fputc(',', stdout);
        first = 0;
        printf("{\"name\":");
        json_emit_str(stdout, vars[i].id);
        printf(",\"type\":");
        json_emit_str(stdout, vars[i].type ? vars[i].type : "");
        fputc('}', stdout);
    }
    printf("]}\n");
    return 0;
}

/* Phase F: REPL & test runner helpers */

extern void add_or_update_variable(char *id, ASTNode *value);
extern Variable *find_variable(char *id);

static void test_runner_register_globals(void) {
    /* Pre-create __test_failed = 0 (script can read/write via builtins
     * defined in ast.c — we add `assert`/`assert_eq` to te_builtin_dispatch). */
    ASTNode *zero = create_ast_leaf_number("INT", 0, NULL, NULL);
    add_or_update_variable("__test_failed", zero);
}

static int run_test_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "  ERROR: no se pudo abrir %s\n", path); return 0; }
    g_test_failed = 0;
    g_test_assertions = 0;
    test_runner_register_globals();
    ASTNode *ast = parse_file(fp);
    fclose(fp);
    if (!ast) { fprintf(stderr, "  PARSE ERROR\n"); return 0; }
    interpret_ast(ast);
    int ok = 1;
    if (throw_flag) {
        const char *m = get_throw_message();
        fprintf(stdout, "  THROW: %s\n", m ? m : "(sin mensaje)");
        throw_flag = 0;
        ok = 0;
    }
    Variable *tf = find_variable("__test_failed");
    if (tf && tf->vtype == VAL_INT && tf->value.int_value != 0) ok = 0;
    if (g_test_failed) ok = 0;
    return ok;
}

static int run_test_runner(const char *dir) {
    if (!dir) dir = ".";
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "Error: no se pudo abrir directorio '%s'\n", dir); return 1; }
    runtime_save_initial_var_count();
    int total = 0, passed = 0;
    struct dirent *ent;
    char path[1024];
    char *files[1024]; int nfiles = 0;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t L = strlen(name);
        if (L < 8) continue;
        if (strcmp(name + L - 8, "_test.te") != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (nfiles < 1024) files[nfiles++] = strdup(path);
    }
    closedir(d);
    fprintf(stdout, "Ejecutando %d test files en %s\n", nfiles, dir);
    for (int i = 0; i < nfiles; i++) {
        fprintf(stdout, "  %s ... ", files[i]);
        fflush(stdout);
        int ok = run_test_file(files[i]);
        if (ok) { fprintf(stdout, "PASS (%d asserts)\n", g_test_assertions); passed++; }
        else    { fprintf(stdout, "FAIL\n"); }
        total++;
        runtime_reset_vars_to_initial_state();
        free(files[i]);
    }
    fprintf(stdout, "\nResumen: %d/%d passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

int main(int argc, char *argv[]) {

    const char* debug_env = getenv("TYPEEASY_DEBUG");
    if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
        g_debug_mode = 1;
    }

    /* --version / -v / --help / -h: respondidos antes de parsear nada mas. */
#ifndef TYPEEASY_VERSION
#define TYPEEASY_VERSION 0.0.1
#endif
#define TE_XSTR_(x) #x
#define TE_XSTR(x) TE_XSTR_(x)
    const char *TE_VERSION_STR = TE_XSTR(TYPEEASY_VERSION);
    for (int vi = 1; vi < argc; vi++) {
        if (strcmp(argv[vi], "--version") == 0 || strcmp(argv[vi], "-v") == 0 || strcmp(argv[vi], "-V") == 0) {
            printf("TypeEasy %s\n", TE_VERSION_STR);
            return 0;
        }
        if (strcmp(argv[vi], "--help") == 0 || strcmp(argv[vi], "-h") == 0) {
            printf("TypeEasy %s\n", TE_VERSION_STR);
            printf("Uso: %s <archivo.te> [opciones]\n", argv[0]);
            printf("Opciones:\n");
            printf("  --version, -v          Muestra la version y sale\n");
            printf("  --help, -h             Muestra esta ayuda\n");
            printf("  --repl                 Inicia el REPL interactivo\n");
            printf("  --test [dir]           Ejecuta tests *_test.te\n");
            printf("  --syntax-check <f>     Valida sintaxis (JSON output)\n");
            printf("  --symbols <f>          Lista simbolos (JSON output)\n");
            printf("  --emit-wat <f> [-o]    Genera WebAssembly text\n");
            printf("  --emit-wasm <f> [-o]   Genera WebAssembly binary\n");
            printf("  --debug                Activa logs de debug\n");
            printf("  --debug-port <p>       Inicia debug server DAP en puerto p\n");
            return 0;
        }
    }

    clock_t inicio = clock();
    
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo.te> [--discover | --invoke <funcName> | --run | --emit-wat | --emit-wasm [-o salida]]\n", argv[0]);
        fprintf(stderr, "     %s --emit-wat <archivo.te> [-o salida.wat]\n", argv[0]);
        fprintf(stderr, "     %s --emit-wasm <archivo.te> [-o salida.wasm]\n", argv[0]);
        return 1;
    }

    const char *script_path = NULL;
    const char *output_path = NULL;
    int emit_wat_mode = 0;
    int emit_wasm_mode = 0;
    int debug_port = 0;
    int repl_mode = 0;
    int test_mode = 0;
    const char *test_dir = NULL;
    int syntax_check_mode = 0;
    int symbols_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--emit-wat") == 0) {
            emit_wat_mode = 1;
        } else if (strcmp(argv[i], "--emit-wasm") == 0) {
            emit_wasm_mode = 1;
        } else if (strcmp(argv[i], "--repl") == 0) {
            repl_mode = 1;
        } else if (strcmp(argv[i], "--test") == 0) {
            test_mode = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') test_dir = argv[++i];
        } else if (strcmp(argv[i], "--syntax-check") == 0) {
            syntax_check_mode = 1;
        } else if (strcmp(argv[i], "--symbols") == 0) {
            symbols_mode = 1;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requiere una ruta de salida.\n");
                return 1;
            }
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_debug_mode = 1;
        } else if (strcmp(argv[i], "--debug-port") == 0 && i + 1 < argc) {
            debug_port = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--debug-port=", 13) == 0) {
            debug_port = atoi(argv[i] + 13);
        } else if (argv[i][0] != '-' && !script_path) {
            script_path = argv[i];
        }
    }

    if (!script_path) {
        if (repl_mode || test_mode) {
            /* OK: REPL/test no requieren script */
        } else {
            fprintf(stderr, "Error: se requiere un archivo .te.\n");
            return 1;
        }
    }
    if ((syntax_check_mode || symbols_mode) && !script_path) {
        fprintf(stderr, "Error: --syntax-check / --symbols requieren un archivo.\n");
        return 1;
    }

    if (emit_wat_mode && emit_wasm_mode) {
        fprintf(stderr, "Error: usa --emit-wat o --emit-wasm, no ambos.\n");
        return 1;
    }

    if (!output_path) {
        output_path = emit_wasm_mode ? "out.wasm" : "out.wat";
    }

    // 1. Pre-inicializar pool de threads CSV ANTES de parsear el script.
    //    El parser ejecuta `from "file.csv"` directamente durante yyparse(),
    //    así que el pool debe estar listo antes de abrir el archivo.
    {
        const char *te_threads = getenv("TE_CSV_THREADS");
        int nw = te_threads ? atoi(te_threads) : 12;
        if (nw < 2) nw = 2;
        if (nw > 16) nw = 16;
        te_csv_pool_init(nw);
    }

    if (repl_mode) return run_repl();
    if (test_mode) return run_test_runner(test_dir);
    if (syntax_check_mode) return run_syntax_check(script_path);
    if (symbols_mode) return run_symbols(script_path);

    // 2. Parsear el archivo
    FILE *file = fopen(script_path, "r");
    if (!file) {
        perror("Error al abrir el archivo");
        return 1;
    }

    ASTNode *script_ast = parse_file(file);
    fclose(file);

    if (!script_ast) {
        fprintf(stderr, "Error: no se pudo parsear el archivo '%s'.\n", script_path);
        return 1;
    }

    if (emit_wat_mode || emit_wasm_mode) {
        char temp_wat_path[512];
        const char *wat_path = output_path;

        if (emit_wasm_mode) {
            int written = snprintf(temp_wat_path, sizeof(temp_wat_path), "%s.wat.tmp", output_path);
            if (written < 0 || written >= (int)sizeof(temp_wat_path)) {
                fprintf(stderr, "[WASM] Error: ruta temporal demasiado larga.\n");
                free_ast(script_ast);
                return 1;
            }
            wat_path = temp_wat_path;
        }

        int ok = wasm_emit_wat(script_ast, wat_path);
        free_ast(script_ast);
        if (!ok) return 1;

        if (emit_wasm_mode) {
            ok = convert_wat_to_wasm(wat_path, output_path);
            remove(wat_path);
            if (!ok) return 1;
            printf("[WASM] Wasm generado: %s\n", output_path);
        } else {
            printf("[WASM] WAT generado: %s\n", output_path);
        }
        return 0;
    }

    // 2. Verificar flags
    int discover_mode = 0;
    char *invoke_func = NULL;
    int interpret_mode = 0;

    if (argc >= 3) {
        if (strcmp(argv[2], "--discover") == 0) {
            discover_mode = 1;
        } else if (strcmp(argv[2], "--invoke") == 0) {
            if (argc >= 4) {
                invoke_func = argv[3];
            } else {
                fprintf(stderr, "Error: --invoke requiere el nombre de la función.\n");
                return 1;
            }
        } else if (strcmp(argv[2], "--run") == 0) {
            interpret_mode = 1;
        }
    }

    // 3. Modo Descubrimiento
    if (discover_mode) {
        printf("[");
        MethodNode *m = global_methods;
        int first = 1;
        while (m) {
            if (m->route_path) {
                if (!first) printf(",");
                const char *response_type = detect_response_type(m->body);
                printf("{\"route\": \"%s\", \"method\": \"%s\", \"function\": \"%s\", \"response_type\": \"%s\"}", 
                       m->route_path, m->http_method ? m->http_method : "GET", m->name, response_type);
                first = 0;
            }
            m = m->next;
        }
        printf("]\n");
        // Liberar memoria y salir
        free_ast(script_ast);
        return 0;
    }

    // 4. Inicializar Runtime
    runtime_save_initial_var_count();


    // 4b. Iniciar el debugger ANTES de ejecutar el script (bloquea hasta que
    // el adapter se conecte y envíe `start`).
    if (debug_port > 0) {
        debugger_init(debug_port, script_path);
    }

    // 5. Ejecutar Script (Global scope)
    // Esto es necesario para inicializar variables globales, clases, etc.
    if (script_ast) {
        interpret_ast(script_ast); 
    }

    /* Fase 2: si quedó un throw sin capturar, imprimir y salir con código de error */
    {
        extern int throw_flag;
        extern char *get_throw_message(void);
        if (throw_flag) {
            const char *m = get_throw_message();
            fprintf(stderr, "Uncaught: %s\n", m ? m : "(sin mensaje)");
            return 1;
        }
    }

    // 6. Modo Invocación
    if (invoke_func) {
        MethodNode *m = global_methods;
        int found = 0;
        while (m) {
            if (strcmp(m->name, invoke_func) == 0) {
                /* Suprimir stdout en vivo y limpiar el buffer de captura
                 * para que el cuerpo no se duplique con __ret__. */
                extern int g_suppress_stdout;
                extern char *g_stdout_buffer;
                if (g_stdout_buffer) { free(g_stdout_buffer); g_stdout_buffer = NULL; }
                g_suppress_stdout = 1;
                interpret_ast(m->body);
                g_suppress_stdout = 0;

                // Verificar si hubo un retorno (return json(...))
                Variable *ret_var = find_variable("__ret__");
                if (ret_var && ret_var->vtype == VAL_STRING) {
                    // Imprimir el resultado (JSON/XML) a stdout
                    printf("%s", ret_var->value.string_value);
                }
                found = 1;
                break;
            }
            m = m->next;
        }
        if (!found) {
            fprintf(stderr, "Error: Función '%s' no encontrada.\n", invoke_func);
            return 1;
        }
    }

    // 7. Modo Interpretación (Legacy/Default)
    // Si no es discover ni invoke, ya se ejecutó interpret_ast(script_ast) arriba.
    // Si había lógica adicional para --run, iría aquí.
    if(interpret_mode){
        // Lógica extra si fuera necesaria
    }

    // 8. Libera memoria y TERMINA
    free_ast(script_ast);
    
    if (g_debug_mode) {
        clock_t fin = clock();
        double tiempo = (double)(fin - inicio) / CLOCKS_PER_SEC;
        printf("Tiempo de ejecución: %.6f segundos\n", tiempo);
    }

    debugger_terminate(0);
    return 0;
}