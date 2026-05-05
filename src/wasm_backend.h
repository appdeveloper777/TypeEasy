#ifndef WASM_BACKEND_H
#define WASM_BACKEND_H

#include "ast.h"

int wasm_emit_wat(ASTNode *root, const char *output_path);

#endif