#include "typeeasy.h"
#include "../src/typeeasy_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Contexto embebido real
static TypeEasyEmbeddedContext* g_embedded_ctx = NULL;

struct TypeEasyContext {
    int initialized;
    // El contexto real está en g_embedded_ctx (global)
};

TypeEasyContext* typeeasy_init(void) {
    TypeEasyContext* ctx = (TypeEasyContext*)malloc(sizeof(TypeEasyContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->initialized = 1;
    
    // Inicializar contexto embebido (una sola vez)
    if (!g_embedded_ctx) {
        g_embedded_ctx = typeeasy_embedded_init();
        if (!g_embedded_ctx) {
            free(ctx);
            return NULL;
        }
        printf("[TYPEEASY] Contexto embebido inicializado (MODO VERDADERAMENTE EMBEBIDO)\n");
    }
    
    return ctx;
}

void typeeasy_cleanup(TypeEasyContext* ctx) {
    if (ctx) {
        free(ctx);
    }
    
    // Limpiar contexto embebido global
    if (g_embedded_ctx) {
        typeeasy_embedded_cleanup(g_embedded_ctx);
        g_embedded_ctx = NULL;
        printf("[TYPEEASY] Contexto embebido liberado\n");
    }
}

int typeeasy_load_script(TypeEasyContext* ctx, const char* script_path, const char* script_content) {
    if (!ctx || !script_path) {
        return 0;
    }
    
    if (!g_embedded_ctx) {
        fprintf(stderr, "[TYPEEASY] Error: Contexto embebido no inicializado\n");
        return 0;
    }
    
    // Cargar y parsear script usando API embebida
    int result = typeeasy_embedded_load_script(g_embedded_ctx, script_path);
    
    if (result) {
        printf("[TYPEEASY] Script cargado y parseado: %s\n", script_path);
    } else {
        fprintf(stderr, "[TYPEEASY] Error al cargar script: %s\n", script_path);
    }
    
    return result;
}

char* typeeasy_discover(TypeEasyContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    if (!g_embedded_ctx) {
        fprintf(stderr, "[TYPEEASY] Error: Contexto embebido no inicializado\n");
        return NULL;
    }
    
    printf("[TYPEEASY] Descubriendo endpoints en: %s (EMBEBIDO)\n", script_path);
    
    // Usar API embebida para descubrir endpoints
    char* result = typeeasy_embedded_discover(g_embedded_ctx, script_path);
    
    return result;
}

char* typeeasy_invoke_with_script(TypeEasyContext* ctx, const char* script_path, const char* function_name, const char* parameters) {
    if (!ctx || !script_path || !function_name) {
        return NULL;
    }
    
    if (!g_embedded_ctx) {
        fprintf(stderr, "[TYPEEASY] Error: Contexto embebido no inicializado\n");
        return NULL;
    }
    
    printf("[TYPEEASY] Invocando función: %s desde %s (EMBEBIDO)\n", function_name, script_path);
    
    // Usar API embebida para ejecutar función
    char* result = typeeasy_embedded_invoke(g_embedded_ctx, script_path, function_name);
    
    if (!result) {
        fprintf(stderr, "[TYPEEASY] Error al invocar función: %s\n", function_name);
    }
    
    return result;
}

const char* typeeasy_version(void) {
    return "TypeEasy 1.0.0 (Modo Verdaderamente Embebido - Sin popen)";
}