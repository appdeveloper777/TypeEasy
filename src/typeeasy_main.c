#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ast.h" 

/* --- Prototipos de las funciones en tu "Motor" --- */
ASTNode* parse_file(FILE* file);
extern int g_debug_mode; // Para acceder a la variable global de parser.y
/* --- Fin Prototipos --- */

// Helper function to detect response type from AST
const char* detect_response_type(ASTNode *body) {
    if (!body) return "json"; // default
    
    // Recursively search for RETURN_XML or RETURN_JSON nodes
    if (body->type) {
        if (strcmp(body->type, "RETURN_XML") == 0) return "xml";
        if (strcmp(body->type, "RETURN_JSON") == 0) return "json";
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
        fprintf(stderr, "Uso: %s <archivo.te> [--discover | --invoke <funcName> | --run]\n", argv[0]);
        return 1;
    }

    // 1. Parsear el archivo
    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Error al abrir el archivo");
        return 1;
    }

    ASTNode *script_ast = parse_file(file);
    fclose(file);

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