#ifndef BYTECODE_H
#define BYTECODE_H

#include "ast.h"

typedef enum {
    OP_LOAD_CONST,
    OP_LOAD_STRING,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_LT,
    OP_GT,
    OP_EQ,
    OP_STORE_VAR,
    OP_ASSIGN,
    OP_LOAD_VAR,
    OP_PRINT_INT,
    OP_PRINT_STR,
    OP_JMP_IF_FALSE,
    OP_JMP,
    OP_HALT
} Opcode;

typedef struct {
    Opcode opcode;
    int operand;
    const char* varname;
    const char* str_value;
} Instruction;
typedef struct {
    const char* name;
    const char* value;
} StringVar;

extern StringVar string_vars[100];
extern int string_var_count;


extern Instruction bytecode[];   
extern int code_index;          

void clear_bytecode();
void emit(Opcode op, int operand, const char* varname, const char* str_value);
void compile(ASTNode* node);
void run_bytecode();

#endif
