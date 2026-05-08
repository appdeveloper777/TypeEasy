#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ast.h" 
#include "wasm_backend.h"

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
int main(int argc, char *argv[]) {

    const char* debug_env = getenv("TYPEEASY_DEBUG");
    if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
        g_debug_mode = 1;
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

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--emit-wat") == 0) {
            emit_wat_mode = 1;
        } else if (strcmp(argv[i], "--emit-wasm") == 0) {
            emit_wasm_mode = 1;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requiere una ruta de salida.\n");
                return 1;
            }
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_debug_mode = 1;
        } else if (argv[i][0] != '-' && !script_path) {
            script_path = argv[i];
        }
    }

    if (!script_path) {
        fprintf(stderr, "Error: se requiere un archivo .te.\n");
        return 1;
    }

    if (emit_wat_mode && emit_wasm_mode) {
        fprintf(stderr, "Error: usa --emit-wat o --emit-wasm, no ambos.\n");
        return 1;
    }

    if (!output_path) {
        output_path = emit_wasm_mode ? "out.wasm" : "out.wat";
    }

    // 1. Parsear el archivo
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
                // Ejecutar el cuerpo de la función
                interpret_ast(m->body);
                
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

    return 0;
}