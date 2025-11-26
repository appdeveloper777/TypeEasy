#include "typeeasy_api.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// Declaraciones externas de funciones del intérprete
extern ASTNode* parse_file(FILE* file);
extern MethodNode* global_methods;
extern int g_debug_mode;
extern FILE *yyin;  // Variable global de Flex para el parser

// Declaraciones externas para manejo de return values
extern Variable __ret_var;
extern int __ret_var_active;
extern int return_flag;

// Funciones auxiliares para captura de stdout
static int stdout_backup = -1;
static int pipe_fds[2];

static void start_stdout_capture(TypeEasyEmbeddedContext* ctx) {
    // Crear pipe para capturar stdout
    if (pipe(pipe_fds) == -1) {
        return;
    }
    
    // Guardar stdout original
    stdout_backup = dup(STDOUT_FILENO);
    
    // Redirigir stdout al pipe
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    
    // Inicializar buffer de captura
    ctx->capture_capacity = 65536;
    ctx->captured_output = (char*)malloc(ctx->capture_capacity);
    ctx->capture_size = 0;
    if (ctx->captured_output) {
        ctx->captured_output[0] = '\0';
    }
}

static char* end_stdout_capture(TypeEasyEmbeddedContext* ctx) {
    // Restaurar stdout original
    fflush(stdout);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);
    
    // Leer del pipe
    char buffer[4096];
    ssize_t bytes_read;
    
    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        // Expandir buffer si es necesario
        if (ctx->capture_size + bytes_read + 1 > ctx->capture_capacity) {
            ctx->capture_capacity *= 2;
            char* new_buffer = (char*)realloc(ctx->captured_output, ctx->capture_capacity);
            if (!new_buffer) {
                break;
            }
            ctx->captured_output = new_buffer;
        }
        
        // Agregar al buffer
        memcpy(ctx->captured_output + ctx->capture_size, buffer, bytes_read);
        ctx->capture_size += bytes_read;
        ctx->captured_output[ctx->capture_size] = '\0';
    }
    
    close(pipe_fds[0]);
    
    // Retornar copia del output capturado
    char* result = ctx->captured_output ? strdup(ctx->captured_output) : NULL;
    
    // Limpiar buffer de captura
    if (ctx->captured_output) {
        free(ctx->captured_output);
        ctx->captured_output = NULL;
    }
    ctx->capture_size = 0;
    
    return result;
}

TypeEasyEmbeddedContext* typeeasy_embedded_init(void) {
    TypeEasyEmbeddedContext* ctx = (TypeEasyEmbeddedContext*)malloc(sizeof(TypeEasyEmbeddedContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->scripts = NULL;
    ctx->script_count = 0;
    ctx->stdout_capture = NULL;
    ctx->captured_output = NULL;
    ctx->capture_size = 0;
    ctx->capture_capacity = 0;
    
    // Inicializar runtime del intérprete
    runtime_save_initial_var_count();
    
    return ctx;
}

void typeeasy_embedded_cleanup(TypeEasyEmbeddedContext* ctx) {
    if (!ctx) {
        return;
    }
    
    // Liberar todos los scripts cargados
    LoadedScript* script = ctx->scripts;
    while (script) {
        LoadedScript* next = script->next;
        if (script->script_path) {
            free(script->script_path);
        }
        if (script->parsed_ast) {
            free_ast(script->parsed_ast);
        }
        free(script);
        script = next;
    }
    
    if (ctx->captured_output) {
        free(ctx->captured_output);
    }
    
    free(ctx);
}

LoadedScript* find_loaded_script(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    LoadedScript* script = ctx->scripts;
    while (script) {
        if (strcmp(script->script_path, script_path) == 0) {
            return script;
        }
        script = script->next;
    }
    
    return NULL;
}

int typeeasy_embedded_load_script(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return 0;
    }
    
    // Verificar si ya está cargado
    if (find_loaded_script(ctx, script_path)) {
        return 1; // Ya está cargado
    }
    
    printf("[TYPEEASY_API] Intentando cargar script: %s\n", script_path);
    
    // Abrir archivo
    FILE* file = fopen(script_path, "r");
    if (!file) {
        fprintf(stderr, "[TYPEEASY_API] Error al abrir: %s\n", script_path);
        perror("fopen");
        return 0;
    }
    
    printf("[TYPEEASY_API] Archivo abierto correctamente\n");
    
    // Configurar yyin para el parser de Flex (aunque parse_file lo hace, es bueno asegurarse)
    yyin = file;
    
    printf("[TYPEEASY_API] Llamando a parse_file()...\n");
    
    // Parsear archivo
    ASTNode* ast = parse_file(file);
    fclose(file);
    
    if (!ast) {
        fprintf(stderr, "[TYPEEASY_API] ERROR: parse_file() retornó NULL para: %s\n", script_path);
        fprintf(stderr, "[TYPEEASY_API] El archivo contiene errores de sintaxis y no se cargará.\n");
        // Crear un AST dummy para evitar crashes posteriores
        ast = create_ast_node("STATEMENT_LIST", NULL, NULL);
        if (!ast) {
            fprintf(stderr, "[TYPEEASY_API] CRITICAL: No se pudo crear AST dummy\n");
            return 0;
        }
    }
    
    printf("[TYPEEASY_API] parse_file() exitoso, AST creado\n");
    
    // Crear entrada de script cargado
    LoadedScript* script = (LoadedScript*)malloc(sizeof(LoadedScript));
    if (!script) {
        free_ast(ast);
        return 0;
    }
    
    script->script_path = strdup(script_path);
    script->parsed_ast = ast;
    script->next = ctx->scripts;
    ctx->scripts = script;
    ctx->script_count++;
    
    printf("[TYPEEASY_API] Ejecutando scope global...\n");
    
    // CRITICAL: Activar __ret_var antes de ejecutar scope global
    if (!__ret_var_active) {
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 1;
    }
    fprintf(stderr, "[DEBUG] ANTES de interpret_ast(scope global): __ret_var_active=%d\n", __ret_var_active);
    
    // Ejecutar scope global para inicializar clases, variables globales, etc.
    interpret_ast(ast);
    
    fprintf(stderr, "[DEBUG] DESPUÉS de interpret_ast(scope global): __ret_var_active=%d\n", __ret_var_active);
    
    printf("[TYPEEASY_API] Script cargado exitosamente (embebido verdadero): %s\n", script_path);
    
    return 1;
}

const char* detect_response_type_embedded(ASTNode *body) {
    if (!body) return "json";
    
    if (body->type) {
        if (strcmp(body->type, "RETURN_XML") == 0) return "xml";
        if (strcmp(body->type, "RETURN_JSON") == 0) return "json";
    }
    
    if (body->left) {
        const char *left_type = detect_response_type_embedded(body->left);
        if (strcmp(left_type, "xml") == 0) return "xml";
    }
    if (body->right) {
        const char *right_type = detect_response_type_embedded(body->right);
        if (strcmp(right_type, "xml") == 0) return "xml";
    }
    
    return "json";
}

char* typeeasy_embedded_discover(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    // Verificar que el script esté cargado
    LoadedScript* script = find_loaded_script(ctx, script_path);
    if (!script) {
        fprintf(stderr, "[TYPEEASY_API] Script no cargado: %s\n", script_path);
        return NULL;
    }
    
    // NUEVO ENFOQUE: Descubrir endpoints directamente desde global_methods
    // Esto evita re-parsear el archivo (que causaría segfault con imports)
    char* result = (char*)malloc(65536);
    if (!result) {
        return NULL;
    }
    
    strcpy(result, "[");
    int first = 1;
    
    MethodNode *m = global_methods;
    while (m) {
        if (m->route_path) {
            if (!first) strcat(result, ",");
            
            // Detect response type
            const char *response_type = detect_response_type_embedded(m->body);
            
            // Build JSON entry
            char entry[1024];
            snprintf(entry, sizeof(entry), 
                     "{\"route\": \"%s\", \"method\": \"%s\", \"function\": \"%s\", \"response_type\": \"%s\"}", 
                     m->route_path, 
                     m->http_method ? m->http_method : "GET", 
                     m->name, 
                     response_type);
            strcat(result, entry);
            first = 0;
        }
        m = m->next;
    }
    
    strcat(result, "]");
    
    return result;
}

char* typeeasy_embedded_invoke(TypeEasyEmbeddedContext* ctx, const char* script_path, const char* function_name) {
    if (!ctx || !script_path || !function_name) {
        return NULL;
    }
    
    // Verificar que el script esté cargado
    LoadedScript* script = find_loaded_script(ctx, script_path);
    if (!script) {
        fprintf(stderr, "[TYPEEASY_API] Script no cargado: %s\n", script_path);
        return NULL;
    }
    
    printf("[TYPEEASY_API] Invocando función: %s\n", function_name);
    
    // Buscar la función en global_methods
    MethodNode* m = global_methods;
    int found = 0;
    
    printf("[TYPEEASY_API] Buscando en global_methods...\n");
    
    while (m) {
        printf("[TYPEEASY_API] Encontrado método: %s\n", m->name);
        if (strcmp(m->name, function_name) == 0) {
            found = 1;
            printf("[TYPEEASY_API] ¡Función encontrada! Ejecutando...\n");
            
            // CRITICAL: Activar __ret_var si no está activo
            if (!__ret_var_active) {
                memset(&__ret_var, 0, sizeof(Variable));
                __ret_var_active = 1;
            }
            
            // CRITICAL: Limpiar __ret__ antes de ejecutar la función
            if (__ret_var_active) {
                // Liberar el valor anterior pero mantener la variable activa
                if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
                    free(__ret_var.value.string_value);
                    __ret_var.value.string_value = NULL;
                }
                if (__ret_var.id) {
                    free(__ret_var.id);
                    __ret_var.id = NULL;
                }
                if (__ret_var.type) {
                    free(__ret_var.type);
                    __ret_var.type = NULL;
                }
                __ret_var.vtype = VAL_INT;  // Reset to default type
                __ret_var.value.int_value = 0;
                // NO desactivar: __ret_var_active sigue siendo 1
            }
            
            // Resetear return_flag
            return_flag = 0;
            
            // Ejecutar el cuerpo de la función
            interpret_ast(m->body);
            
            // Guardar el resultado ANTES de limpiar variables
            char* result = NULL;
            Variable* ret_var = find_variable("__ret__");
            if (ret_var) {
                fprintf(stderr, "[TYPEEASY_API] __ret__ encontrado. vtype=%d (esperado VAL_STRING=%d)\n", ret_var->vtype, VAL_STRING); fflush(stderr);
                if (ret_var->vtype == VAL_STRING) {
                    fprintf(stderr, "[TYPEEASY_API] Retorno encontrado en __ret__: %s\n", ret_var->value.string_value); fflush(stderr);
                    result = strdup(ret_var->value.string_value);
                }
            } else {
                fprintf(stderr, "[TYPEEASY_API] __ret__ NO encontrado via find_variable\n"); fflush(stderr);
            }
            
            // CRITICAL: Limpiar variables DESPUÉS de ejecutar (para cerrar conexiones MySQL)
            runtime_reset_vars_to_initial_state();
            
            if (result) {
                return result;
            }
            
            fprintf(stderr, "[TYPEEASY_API] Ejecución completada sin retorno en __ret__\n"); fflush(stderr);
            return strdup("");
        }
        m = m->next;
    }
    
    if (!found) {
        fprintf(stderr, "[TYPEEASY_API] Función no encontrada en global_methods: %s\n", function_name);
    }
    
    return NULL;
}
