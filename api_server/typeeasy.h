#ifndef TYPEEASY_H
#define TYPEEASY_H

#include <stdlib.h>

typedef struct TypeEasyContext TypeEasyContext;

// Funciones de la API de TypeEasy
TypeEasyContext* typeeasy_init(void);
void typeeasy_cleanup(TypeEasyContext* ctx);
int typeeasy_load_script(TypeEasyContext* ctx, const char* script_path, const char* script_content);
char* typeeasy_discover(TypeEasyContext* ctx, const char* script_path);
char* typeeasy_invoke_with_script(TypeEasyContext* ctx, const char* script_path, const char* function_name, const char* parameters);
const char* typeeasy_version(void);

#endif // TYPEEASY_H