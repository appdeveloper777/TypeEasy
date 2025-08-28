// bytecode.c - VM extendida para TypeEasy con soporte completo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strvars.h"
#include "bytecode.h"


#define MAX_CODE 8192
#define MAX_STACK 4096
#define MAX_VARS 512

Instruction bytecode[MAX_CODE];
int code_index = 0;

int stack[MAX_STACK];
char *str_stack[MAX_STACK];
int sp = -1;

typedef struct {
    char *name;
    int value;
    char *string_value;
    int is_string;
} VariableBC;

VariableBC bc_vars[MAX_VARS];
int bc_var_count = 0;


void compile_for_node(ASTNode* node) {
    int loop_start;
    int jmp_false_index;

    ASTNode* init = node->left;
    ASTNode* loop_info = node->right;

    ASTNode* condition = loop_info->left;
    ASTNode* for_body = loop_info->right; // <- contenedor
    ASTNode* step = for_body->left;
    ASTNode* body = for_body->right;

    compile(init);                // inicialización
    loop_start = code_index;

    compile(condition);           // condición
    jmp_false_index = code_index;
    emit(OP_JMP_IF_FALSE, 0, NULL, NULL);

    compile(body);                // cuerpo del for
    compile(step);                // incremento
    emit(OP_JMP, loop_start, NULL, NULL);

    bytecode[jmp_false_index].operand = code_index; // salto fuera del for
}



int get_var_value(const char *name) {
    for (int i = 0; i < bc_var_count; i++) {
        if (strcmp(bc_vars[i].name, name) == 0)
            return bc_vars[i].value;
    }
    printf("[Bytecode Error] Variable '%s' no definida\n", name);
    return 0;
}



void set_var_value(const char *name, int value) {
    for (int i = 0; i < bc_var_count; i++) {
        if (strcmp(bc_vars[i].name, name) == 0) {
            bc_vars[i].value = value;
            bc_vars[i].is_string = 0;
            return;
        }
    }
    bc_vars[bc_var_count++] = (VariableBC){strdup(name), value, NULL, 0};
}


void emit(Opcode op, int operand, const char *varname, const char *str_value) {
    bytecode[code_index++] = (Instruction){op, operand, varname ? strdup(varname) : NULL, str_value ? strdup(str_value) : NULL};
}

void clear_bytecode() {
    code_index = 0;
}

void compile(ASTNode *node) {
    if (!node) return;

    if (strcmp(node->type, "STATEMENT_LIST") == 0) {
        compile(node->left);
        compile(node->right);
    }

    else if (strcmp(node->type, "VAR_DECL") == 0) {
        compile(node->right);
        if (node->right && strcmp(node->right->type, "STRING") == 0) {
            emit(OP_STORE_VAR, 0, node->str_value, node->right->str_value);
        } else {
            emit(OP_STORE_VAR, 0, node->str_value, NULL);
        }
    }

    else if (strcmp(node->type, "ASSIGN") == 0) {
        compile(node->right);
        emit(OP_ASSIGN, 0, node->value, NULL);
    }

    else if (strcmp(node->type, "PRINT") == 0) {
        compile(node->left);
        if (node->left && strcmp(node->left->type, "STRING") == 0) {
            emit(OP_PRINT_STR, 0, NULL, NULL);
        } else {
            emit(OP_PRINT_INT, 0, NULL, NULL);
        }
    }

    else if (strcmp(node->type, "FOR") == 0) {
        compile_for_node(node);  // <- importante
    }

    else if (strcmp(node->type, "STRING") == 0) {
        emit(OP_LOAD_STRING, 0, NULL, node->str_value);
    }

    else if (strcmp(node->type, "NUMBER") == 0) {
        emit(OP_LOAD_CONST, node->value, NULL, NULL);
    }

    else if (strcmp(node->type, "IDENTIFIER") == 0) {
        emit(OP_LOAD_VAR, 0, node->id, NULL);
    }

    else if (strcmp(node->type, "ADD") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_ADD, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "SUB") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_SUB, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "MUL") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_MUL, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "DIV") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_DIV, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "LT") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_LT, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "GT") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_GT, 0, NULL, NULL);
    }

    else if (strcmp(node->type, "EQ") == 0) {
        compile(node->left);
        compile(node->right);
        emit(OP_EQ, 0, NULL, NULL);
    }

    else {
        printf("[WARNING] Nodo no manejado en compile(): %s\n", node->type);
    }
}


void compile_to_bytecode(ASTNode *node) {
    code_index = 0;
    compile(node);
    emit(OP_HALT, 0, NULL, NULL);
    print_bytecode();
}

void print_bytecode() {
    printf("\n[DEBUG] Bytecode generado:\n");
    for (int i = 0; i < code_index; i++) {
        Instruction in = bytecode[i];
        printf("[%02d] %d %s %s\n", i, in.opcode,
               in.varname ? in.varname : "-",
               in.str_value ? in.str_value : "-");
    }
}


void run_bytecode() {
    int ip = 0;
    while (1) {
        Instruction instr = bytecode[ip++];
        printf("[BC] ip=%d opcode=%d operand=%d var=%s str=%s\n",
            ip - 1, instr.opcode, instr.operand,
            instr.varname ? instr.varname : "-", 
            instr.str_value ? instr.str_value : "-");
     
        switch (instr.opcode) {
            case OP_LOAD_CONST: stack[++sp] = instr.operand; break;
            case OP_LOAD_STRING: str_stack[++sp] = instr.str_value; break;
            case OP_ADD: stack[sp-1] += stack[sp--]; break;
            case OP_SUB: stack[sp-1] -= stack[sp--]; break;
            case OP_MUL: stack[sp-1] *= stack[sp--]; break;
            case OP_DIV: stack[sp-1] /= stack[sp--]; break;
            case OP_LT: { int b = stack[sp--], a = stack[sp--]; stack[++sp] = a < b; } break;
            case OP_GT: { int b = stack[sp--], a = stack[sp--]; stack[++sp] = a > b; } break;
            case OP_EQ: { int b = stack[sp--], a = stack[sp--]; stack[++sp] = a == b; } break;
            case OP_STORE_VAR:
                if (instr.str_value)
                    set_var_string(instr.varname, instr.str_value);
                else
                    set_var_value(instr.varname, stack[sp--]);
                break;
            case OP_ASSIGN:
                set_var_value(instr.varname, stack[sp--]);
                break;
                case OP_LOAD_VAR:
                if (is_var_string(instr.varname)) {
                    str_stack[++sp] = get_var_string(instr.varname);  
                } else {
                    stack[++sp] = get_var_value(instr.varname);
                }
                break;
            
            case OP_PRINT_INT:
                printf("[Bytecode Output] %d\n", stack[sp--]);
                break;
                case OP_PRINT_STR:
                if (str_stack[sp] != NULL)
                    printf("[Bytecode Output] %s\n", str_stack[sp]);
                else
                    printf("[Bytecode Output] <null string>\n");
                sp--;
                break;
            
            case OP_JMP_IF_FALSE:
                if (!stack[sp--]) ip = instr.operand;
                break;
            case OP_JMP:
                ip = instr.operand;
                break;
            case OP_HALT:
                return;
            default:
                printf("[Bytecode VM] Instrucción no soportada: %d\n", instr.opcode);
                return;
        }
    }
}
