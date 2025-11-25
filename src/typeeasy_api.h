#ifndef TYPEEASY_API_H
#define TYPEEASY_API_H

#include "ast.h"
#include <stdio.h>
#include <time.h>

/**
 * TypeEasy Embedded API
 * 
 * Esta API permite ejecutar scripts TypeEasy de forma embebida,
 * sin necesidad de crear procesos externos (popen).
 */

// Estructura para almacenar un script parseado
typedef struct LoadedScript {
    char *script_path;
    ASTNode *parsed_ast;
    time_t load_time;  // Timestamp de cuando se cargó
    struct LoadedScript *next;
} LoadedScript;

// Contexto del intérprete embebido
typedef struct TypeEasyEmbeddedContext {
    LoadedScript *scripts;
    int script_count;
    FILE *stdout_capture;  // Para capturar salida
    char *captured_output;
    size_t capture_size;
    size_t capture_capacity;
} TypeEasyEmbeddedContext;

/**
 * Inicializa el contexto del intérprete embebido
 * @return Contexto inicializado o NULL en caso de error
 */
TypeEasyEmbeddedContext* typeeasy_embedded_init(void);

/**
 * Libera el contexto y todos los recursos asociados
 * @param ctx Contexto a liberar
 */
void typeeasy_embedded_cleanup(TypeEasyEmbeddedContext* ctx);

/**
 * Carga y parsea un script TypeEasy
 * @param ctx Contexto del intérprete
 * @param script_path Ruta al archivo .te
 * @return 1 si se cargó correctamente, 0 en caso de error
 */
int typeeasy_embedded_load_script(TypeEasyEmbeddedContext* ctx, const char* script_path);

/**
 * Descubre endpoints en un script cargado
 * @param ctx Contexto del intérprete
 * @param script_path Ruta al script previamente cargado
 * @return JSON string con los endpoints descubiertos (debe liberarse con free())
 */
char* typeeasy_embedded_discover(TypeEasyEmbeddedContext* ctx, const char* script_path);

/**
 * Ejecuta una función específica de un script cargado
 * @param ctx Contexto del intérprete
 * @param script_path Ruta al script previamente cargado
 * @param function_name Nombre de la función a ejecutar
 * @return Salida capturada de la función (debe liberarse con free())
 */
char* typeeasy_embedded_invoke(TypeEasyEmbeddedContext* ctx, const char* script_path, const char* function_name);

/**
 * Busca un script cargado por su ruta
 * @param ctx Contexto del intérprete
 * @param script_path Ruta del script a buscar
 * @return Puntero al script cargado o NULL si no se encuentra
 */
LoadedScript* find_loaded_script(TypeEasyEmbeddedContext* ctx, const char* script_path);

/**
 * Detecta el tipo de respuesta (json/xml) de un cuerpo de función
 * @param body AST del cuerpo de la función
 * @return "json" o "xml"
 */
const char* detect_response_type_embedded(ASTNode *body);

#endif // TYPEEASY_API_H
