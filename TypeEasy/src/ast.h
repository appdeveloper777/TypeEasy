#ifndef AST_H
#define AST_H

#include "variables.h" 

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NODE_STMT_LIST,
    NODE_FOR,
    NODE_VARDECL,
    NODE_ASSIGN,
    NODE_PRINT,
    NODE_NUMBER,
    NODE_BINARYOP,
    NODE_STRING
} NodeType;

typedef struct Node {
    NodeType type;

    union {
        struct {
            struct Node** stmts;
            int count;
            int capacity;
        } stmtList;

    
        struct {
            char* var_index;
            char* var_limit;
            char* var_inc;
            struct Node* body;
        } forNode;

        struct {
            VariableType vtype; 
            char* var_name;
            struct Node* expr;
        } varDecl;
        struct {
            char* var_name;
            struct Node* expr;
        } assign;

        
        struct {
            char* var_name;
        } print;

        struct {
            float value;
        } number;
        struct {
            int op;  /* '+', '-', '*', '/' */
            struct Node* left;
            struct Node* right;
        } binaryop;

        struct {
            char* value;
        } string;
    };
} Node;

/* Funciones constructoras */
Node* newStatementList();
void  appendStatement(Node* list, Node* stmt);
Node* newForNode(char* var_index, char* var_limit, char* var_inc, Node* body);
Node* newVarDeclNode(VariableType vtype, char* name, Node* expr);
Node* newAssignNode(char* var_name, Node* expr);
Node* newPrintNode(char* var_name);
Node* newNumberNode(float val);
Node* newBinaryOpNode(int op, Node* left, Node* right);
Node* newStringNode(const char* s);

/* Interpretación */
void  interpret(Node* root);
float evalExpression(Node* expr);
void  interpretNode(Node* n);

#ifdef __cplusplus
}
#endif

#endif