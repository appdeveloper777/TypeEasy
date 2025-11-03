#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ast.h" 

/* --- Prototipos de las funciones en tu "Motor" --- */
ASTNode* parse_file(FILE* file);
extern int g_debug_mode; // Para acceder a la variable global de parser.y
/* --- Fin Prototipos --- */

/**
 * Main para el EJECUTABLE 'typeeasy'.
 * Ejecuta un script y termina.
 * Esto es lo que 'servidor_api.c' llamará.
 */
int main(int argc, char *argv[]) {

    // (Tu código original de 'main' para parsear args, getenv, etc.) [cite: 205-213]
    const char* debug_env = getenv("TYPEEASY_DEBUG");
    if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
        g_debug_mode = 1;
        printf("[INFO] Modo de depuración activado.\n");
    }
  
    clock_t inicio = clock();
    
    // (Tu código original de 'main' para flags --interpret, --run, etc.) [cite: 213-214]
    int interpret_mode = 0;
    if (argc == 3 && strcmp(argv[2], "--run") == 0) {
        interpret_mode = 1;
    }

    if (argc < 2) {
        printf("Uso: %s <archivo.te> [--run]\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo %s\n", argv[1]);
        return 1;
    }

    // 1. Parsea el archivo usando el "Motor"
    ASTNode* script_ast = parse_file(file);
    fclose(file);

    if (!script_ast) {
        fprintf(stderr, "Error fatal: No se pudo construir el AST del script.\n");
        return 1;
    }

    // 2. Interpreta el script
    // (Tu lógica original de 'if (root)' e 'if (interpret_mode)') [cite: 215-223]
    if (g_debug_mode) {
        printf("[DEBUG] Ejecutando con AST normal\n");
    }
    
    interpret_ast(script_ast); // <-- Ejecuta la lógica

    if(interpret_mode){
        // (Tu lógica de system("g++ ...") y system("typeeasy_output.exe")) [cite: 217-220]
    }

    // 3. Libera memoria y TERMINA
    free_ast(script_ast);
    
    // (Tu código original de medición de tiempo) [cite: 223-225]
    if (g_debug_mode) {
        clock_t fin = clock();
        double tiempo = (double)(fin - inicio) / CLOCKS_PER_SEC;
        printf("Tiempo de ejecución: %.6f segundos\n", tiempo);
    }

    return 0;
}