#include "typeeasy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct TypeEasyContext {
    int initialized;
};

TypeEasyContext* typeeasy_init(void) {
    TypeEasyContext* ctx = (TypeEasyContext*)malloc(sizeof(TypeEasyContext));
    if (ctx) {
        ctx->initialized = 1;
        printf("[TYPEEASY] Contexto inicializado (modo compatibilidad popen)\n");
    }
    return ctx;
}

void typeeasy_cleanup(TypeEasyContext* ctx) {
    if (ctx) {
        free(ctx);
        printf("[TYPEEASY] Contexto liberado\n");
    }
}

int typeeasy_load_script(TypeEasyContext* ctx, const char* script_path, const char* script_content) {
    if (!ctx || !script_path) {
        return 0;
    }
    
    printf("[TYPEEASY] Script registrado: %s\n", script_path);
    return 1;
}

char* typeeasy_discover(TypeEasyContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    printf("[TYPEEASY] Descubriendo endpoints en: %s\n", script_path);
    
    // Usar popen como antes
    char command[512];
    snprintf(command, sizeof(command), "/app/typeeasy \"%s\" --discover 2>&1", script_path);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    char *result = malloc(65536);
    if (!result) {
        pclose(fp);
        return NULL;
    }
    
    result[0] = '\0';
    char buffer[4096];
    size_t total_length = 0;
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t buffer_len = strlen(buffer);
        if (total_length + buffer_len < 65536 - 1) {
            strcat(result, buffer);
            total_length += buffer_len;
        } else {
            break;
        }
    }
    
    pclose(fp);
    return result;
}

char* typeeasy_invoke_with_script(TypeEasyContext* ctx, const char* script_path, const char* function_name, const char* parameters) {
    if (!ctx || !script_path || !function_name) {
        return NULL;
    }
    
    printf("[TYPEEASY] Invocando función: %s desde %s\n", function_name, script_path);
    
    // Usar popen como antes  
    char command[512];
    snprintf(command, sizeof(command), "/app/typeeasy \"%s\" --invoke %s 2>/dev/null", 
             script_path, function_name);
    
    printf("[TYPEEASY] Ejecutando: %s\n", command);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    // Leer toda la salida
    char *result = malloc(524288);
    if (!result) {
        pclose(fp);
        return NULL;
    }
    
    result[0] = '\0';
    char buffer[4096];
    size_t total_length = 0;
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t buffer_len = strlen(buffer);
        if (total_length + buffer_len < 524288 - 1) {
            strcat(result, buffer);
            total_length += buffer_len;
        } else {
            fprintf(stderr, "[TYPEEASY] Buffer overflow, truncando\n");
            break;
        }
    }
    
    int ret = pclose(fp);
    if (ret != 0) {
        free(result);
        return NULL;
    }
    
    return result;
}

const char* typeeasy_version(void) {
    return "TypeEasy 1.0.0 (Modo Compatibilidad popen)";
}