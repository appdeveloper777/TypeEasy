#include "wasm_backend.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_MAX_VARS 512
#define WASM_NAME_LEN 128

typedef struct {
    const char *source_name;
    char wasm_name[WASM_NAME_LEN];
} WasmLocal;

typedef struct {
    FILE *out;
    WasmLocal locals[WASM_MAX_VARS];
    int local_count;
    int indent;
    char error[512];
} WasmContext;

static int is_node(ASTNode *node, const char *type) {
    return node && node->type && strcmp(node->type, type) == 0;
}

static void wasm_set_error(WasmContext *ctx, const char *message, ASTNode *node) {
    if (ctx->error[0]) return;
    if (node && node->type) {
        snprintf(ctx->error, sizeof(ctx->error), "%s: %s", message, node->type);
    } else {
        snprintf(ctx->error, sizeof(ctx->error), "%s", message);
    }
}

static void wasm_indent(WasmContext *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        fputs("  ", ctx->out);
    }
}

static void wasm_line(WasmContext *ctx, const char *line) {
    wasm_indent(ctx);
    fprintf(ctx->out, "%s\n", line);
}

static void sanitize_name(const char *source, char *target, size_t size) {
    size_t pos = 0;
    if (!source || !source[0]) source = "var";

    for (size_t i = 0; source[i] && pos + 1 < size; i++) {
        unsigned char ch = (unsigned char)source[i];
        if (isalnum(ch) || ch == '_' || ch == '.') {
            target[pos++] = (char)ch;
        } else {
            target[pos++] = '_';
        }
    }
    target[pos] = '\0';

    if (isdigit((unsigned char)target[0])) {
        memmove(target + 1, target, strlen(target) + 1);
        target[0] = '_';
    }
}

static int find_local(WasmContext *ctx, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < ctx->local_count; i++) {
        if (strcmp(ctx->locals[i].source_name, name) == 0) return i;
    }
    return -1;
}

static int add_local(WasmContext *ctx, const char *name) {
    if (!name || !name[0]) return -1;
    int existing = find_local(ctx, name);
    if (existing >= 0) return existing;
    if (ctx->local_count >= WASM_MAX_VARS) {
        wasm_set_error(ctx, "Demasiadas variables para backend Wasm", NULL);
        return -1;
    }

    int index = ctx->local_count++;
    ctx->locals[index].source_name = name;
    sanitize_name(name, ctx->locals[index].wasm_name, sizeof(ctx->locals[index].wasm_name));
    return index;
}

static const char *local_name(WasmContext *ctx, const char *source_name) {
    int index = find_local(ctx, source_name);
    if (index < 0) {
        wasm_set_error(ctx, "Variable usada antes de declararse en Wasm", NULL);
        return NULL;
    }
    return ctx->locals[index].wasm_name;
}

static void collect_locals(WasmContext *ctx, ASTNode *node) {
    if (!node || ctx->error[0]) return;

    if (is_node(node, "STATEMENT_LIST")) {
        collect_locals(ctx, node->left);
        collect_locals(ctx, node->right);
        collect_locals(ctx, node->next);
        return;
    }

    if (is_node(node, "VAR_DECL") && node->id) {
        add_local(ctx, node->id);
    } else if (is_node(node, "DECLARE") && node->left && node->left->id) {
        add_local(ctx, node->left->id);
    } else if (is_node(node, "ASSIGN") && node->left && node->left->id) {
        add_local(ctx, node->left->id);
    } else if (is_node(node, "FOR") && node->id) {
        add_local(ctx, node->id);
    }

    collect_locals(ctx, node->left);
    collect_locals(ctx, node->right);
    collect_locals(ctx, node->next);
}

static void emit_expr(WasmContext *ctx, ASTNode *node);
static void emit_stmt(WasmContext *ctx, ASTNode *node);

static void emit_binary(WasmContext *ctx, ASTNode *node, const char *op) {
    emit_expr(ctx, node->left);
    emit_expr(ctx, node->right);
    wasm_line(ctx, op);
}

static void emit_expr(WasmContext *ctx, ASTNode *node) {
    if (!node || ctx->error[0]) return;

    if (is_node(node, "NUMBER") || is_node(node, "INT")) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "i32.const %d", node->value);
        wasm_line(ctx, buffer);
    } else if (is_node(node, "IDENTIFIER") || is_node(node, "ID")) {
        const char *name = local_name(ctx, node->id);
        if (!name) return;
        char buffer[WASM_NAME_LEN + 32];
        snprintf(buffer, sizeof(buffer), "local.get $%s", name);
        wasm_line(ctx, buffer);
    } else if (is_node(node, "ADD")) {
        emit_binary(ctx, node, "i32.add");
    } else if (is_node(node, "SUB")) {
        emit_binary(ctx, node, "i32.sub");
    } else if (is_node(node, "MUL")) {
        emit_binary(ctx, node, "i32.mul");
    } else if (is_node(node, "DIV")) {
        emit_binary(ctx, node, "i32.div_s");
    } else if (is_node(node, "LT")) {
        emit_binary(ctx, node, "i32.lt_s");
    } else if (is_node(node, "GT")) {
        emit_binary(ctx, node, "i32.gt_s");
    } else if (is_node(node, "EQ")) {
        emit_binary(ctx, node, "i32.eq");
    } else if (is_node(node, "GT_EQ")) {
        emit_binary(ctx, node, "i32.ge_s");
    } else if (is_node(node, "LT_EQ")) {
        emit_binary(ctx, node, "i32.le_s");
    } else if (is_node(node, "DIFF")) {
        emit_binary(ctx, node, "i32.ne");
    } else {
        wasm_set_error(ctx, "Expresion no soportada por backend Wasm", node);
    }
}

static void emit_var_set(WasmContext *ctx, const char *source_name, ASTNode *expr) {
    const char *name = local_name(ctx, source_name);
    if (!name) return;

    emit_expr(ctx, expr);
    if (ctx->error[0]) return;

    char buffer[WASM_NAME_LEN + 32];
    snprintf(buffer, sizeof(buffer), "local.set $%s", name);
    wasm_line(ctx, buffer);
}

static void emit_print(WasmContext *ctx, ASTNode *expr, int newline) {
    emit_expr(ctx, expr);
    if (ctx->error[0]) return;
    wasm_line(ctx, newline ? "call $print_i32_ln" : "call $print_i32");
}

static void emit_stmt(WasmContext *ctx, ASTNode *node) {
    if (!node || ctx->error[0]) return;

    if (is_node(node, "STATEMENT_LIST")) {
        emit_stmt(ctx, node->left);
        emit_stmt(ctx, node->right);
    } else if (is_node(node, "VAR_DECL")) {
        emit_var_set(ctx, node->id, node->left);
    } else if (is_node(node, "DECLARE")) {
        emit_var_set(ctx, node->left->id, node->right);
    } else if (is_node(node, "ASSIGN")) {
        emit_var_set(ctx, node->left->id, node->right);
    } else if (is_node(node, "PRINT")) {
        emit_print(ctx, node->left, 0);
    } else if (is_node(node, "PRINTLN")) {
        emit_print(ctx, node->left, 1);
    } else if (is_node(node, "IF")) {
        emit_expr(ctx, node->left);
        if (ctx->error[0]) return;
        wasm_line(ctx, "if");
        ctx->indent++;
        emit_stmt(ctx, node->right);
        if (node->next) {
            ctx->indent--;
            wasm_line(ctx, "else");
            ctx->indent++;
            emit_stmt(ctx, node->next);
        }
        ctx->indent--;
        wasm_line(ctx, "end");
    } else if (is_node(node, "FOR")) {
        wasm_set_error(ctx, "for numerico todavia no esta soportado por backend Wasm", node);
    } else if (is_node(node, "ENDPOINT_DECL")) {
        wasm_set_error(ctx, "endpoint no esta soportado por backend Wasm", node);
    } else if (is_node(node, "CALL_FUNC") || is_node(node, "CALL_METHOD")) {
        wasm_set_error(ctx, "llamadas nativas/metodos no soportadas por backend Wasm", node);
    } else if (is_node(node, "RETURN")) {
        wasm_set_error(ctx, "return no esta soportado en el main Wasm inicial", node);
    } else {
        wasm_set_error(ctx, "Sentencia no soportada por backend Wasm", node);
    }
}

int wasm_emit_wat(ASTNode *root, const char *output_path) {
    if (!root || !output_path) {
        fprintf(stderr, "[WASM] Error: AST o ruta de salida invalida.\n");
        return 0;
    }

    WasmContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    collect_locals(&ctx, root);
    if (ctx.error[0]) {
        fprintf(stderr, "[WASM] Error: %s\n", ctx.error);
        return 0;
    }

    ctx.out = fopen(output_path, "w");
    if (!ctx.out) {
        perror("[WASM] Error al crear archivo WAT");
        return 0;
    }

    wasm_line(&ctx, "(module");
    ctx.indent++;
    wasm_line(&ctx, "(import \"env\" \"print_i32\" (func $print_i32 (param i32)))");
    wasm_line(&ctx, "(import \"env\" \"print_i32_ln\" (func $print_i32_ln (param i32)))");
    wasm_line(&ctx, "(func $main");
    ctx.indent++;
    for (int i = 0; i < ctx.local_count; i++) {
        char buffer[WASM_NAME_LEN + 32];
        snprintf(buffer, sizeof(buffer), "(local $%s i32)", ctx.locals[i].wasm_name);
        wasm_line(&ctx, buffer);
    }
    emit_stmt(&ctx, root);
    ctx.indent--;
    wasm_line(&ctx, ")");
    wasm_line(&ctx, "(export \"main\" (func $main))");
    ctx.indent--;
    wasm_line(&ctx, ")");

    fclose(ctx.out);

    if (ctx.error[0]) {
        remove(output_path);
        fprintf(stderr, "[WASM] Error: %s\n", ctx.error);
        return 0;
    }

    return 1;
}