#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "variables.h" 

Node* newStatementList() {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_STMT_LIST;
    n->stmtList.count = 0;
    n->stmtList.capacity = 4;
    n->stmtList.stmts = malloc(sizeof(Node*) * n->stmtList.capacity);
    return n;
}
void appendStatement(Node* list, Node* stmt) {
    if (!list || list->type != NODE_STMT_LIST) return;
    if (list->stmtList.count >= list->stmtList.capacity) {
        list->stmtList.capacity *= 2;
        list->stmtList.stmts = realloc(list->stmtList.stmts, sizeof(Node*) * list->stmtList.capacity);
    }
    list->stmtList.stmts[list->stmtList.count++] = stmt;
}

Node* newForNode(char* var_index, char* var_limit, char* var_inc, Node* body) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_FOR;
    n->forNode.var_index = strdup(var_index);
    n->forNode.var_limit = strdup(var_limit);
    n->forNode.var_inc   = strdup(var_inc);
    n->forNode.body      = body;
    return n;
}
Node* newVarDeclNode(VariableType vtype, char* name, Node* expr) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_VARDECL;
    n->varDecl.vtype = vtype;
    n->varDecl.var_name = strdup(name);
    n->varDecl.expr = expr;
    return n;
}

Node* newAssignNode(char* var_name, Node* expr) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_ASSIGN;
    n->assign.var_name = strdup(var_name);
    n->assign.expr = expr;
    return n;
}


Node* newPrintNode(char* var_name) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_PRINT;
    n->print.var_name = strdup(var_name);
    return n;
}


Node* newNumberNode(float val) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_NUMBER;
    n->number.value = val;
    return n;
}
Node* newBinaryOpNode(int op, Node* left, Node* right) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_BINARYOP;
    n->binaryop.op   = op;
    n->binaryop.left = left;
    n->binaryop.right= right;
    return n;
}
Node* newStringNode(const char* s) {
    Node* n = malloc(sizeof(Node));
    n->type = NODE_STRING;
    n->string.value = strdup(s);
    return n;
}


void interpret(Node* root) {
    if (!root) return;
    interpretNode(root);
}

void interpretNode(Node* n) {
    if (!n) return;

    switch (n->type) {
        case NODE_STMT_LIST: {
            for (int i = 0; i < n->stmtList.count; i++) {
                interpretNode(n->stmtList.stmts[i]);
            }
            break;
        }
        case NODE_FOR: {
            Variable vindex = get_variable(n->forNode.var_index);
            Variable vlimit = get_variable(n->forNode.var_limit);
            Variable vinc   = get_variable(n->forNode.var_inc);

            int i     = vindex.num;
            int limit = vlimit.num;
            int inc   = vinc.num;

            printf("Iniciando ciclo for i=%d, limit=%d, inc=%d\n", i, limit, inc);
            while (i < limit) {
                set_variable_int(get_variable_index(n->forNode.var_index), i);
                interpretNode(n->forNode.body);
                i += inc;
            }
            
            set_variable_int(get_variable_index(n->forNode.var_index), i);
            printf("Fin for...\n");
            break;
        }
        case NODE_VARDECL: {
            if (n->varDecl.vtype == STRING_TYPE) {
                set_variable_string(add_variable(n->varDecl.var_name, STRING_TYPE), n->varDecl.expr->string.value);
                printf("Decl var STRING: %s = %s\n", n->varDecl.var_name, n->varDecl.expr->string.value);
            } else {
                float val = evalExpression(n->varDecl.expr);
                int idx = add_variable(n->varDecl.var_name, n->varDecl.vtype);
                if (n->varDecl.vtype == INT_TYPE) {
                    set_variable_int(idx, (int)val);
                    printf("Decl var INT: %s = %d\n", n->varDecl.var_name, (int)val);
                } else if (n->varDecl.vtype == FLOAT_TYPE) {
                    set_variable_float(idx, val);
                    printf("Decl var FLOAT: %s = %.2f\n", n->varDecl.var_name, val);
                }
            }
            break;
        }
        case NODE_ASSIGN: {
            int idx = get_variable_index(n->assign.var_name);
            Variable var = variables[idx];
            if (var.type == INT_TYPE) {
                float val = evalExpression(n->assign.expr);
                set_variable_int(idx, (int)val);
                printf("Asign %s = %d\n", n->assign.var_name, (int)val);
            } else if (var.type == FLOAT_TYPE) {
                float val = evalExpression(n->assign.expr);
                set_variable_float(idx, val);
                printf("Asign %s = %.2f\n", n->assign.var_name, val);
            } else if (var.type == STRING_TYPE) {
                set_variable_string(idx, n->assign.expr->string.value);
                printf("Asign %s = %s\n", n->assign.var_name, n->assign.expr->string.value);
            }
            break;
        }
        case NODE_PRINT: {
            Variable var = get_variable(n->print.var_name);
            if (var.type == STRING_TYPE) {
                printf("Imprimiendo %s: STRING: %s\n", n->print.var_name, var.str);
            } else if (var.type == INT_TYPE) {
                printf("Imprimiendo %s: INT: %d\n", n->print.var_name, var.num);
            } else if (var.type == FLOAT_TYPE) {
                printf("Imprimiendo %s: FLOAT: %.2f\n", n->print.var_name, var.fnum);
            }
            break;
        }
        case NODE_NUMBER:
        case NODE_BINARYOP:
            
            break;
        default:
            fprintf(stderr,"interpretNode: tipo %d no manejado\n", n->type);
    }
}

float evalExpression(Node* expr) {
    if (!expr) return 0.0;
    switch (expr->type) {
      case NODE_NUMBER:
         return expr->number.value;
      case NODE_BINARYOP: {
         float leftVal  = evalExpression(expr->binaryop.left);
         float rightVal = evalExpression(expr->binaryop.right);
         switch (expr->binaryop.op) {
            case '+': return leftVal + rightVal;
            case '-': return leftVal - rightVal;
            case '*': return leftVal * rightVal;
            case '/': {
               if (rightVal == 0) {
                 fprintf(stderr,"error: div/0\n"); 
                 return 0.0;
               }
               return leftVal / rightVal;
            }
         }
         break;
      }
      default:
         fprintf(stderr,"evalExpression: tipo %d no soportado\n", expr->type);
    }
    return 0.0;
}