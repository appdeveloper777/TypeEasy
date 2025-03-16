#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ASTNode {
    char *type;
    char *id;
    int value;
    char *str_value;
    struct ASTNode *left;
    struct ASTNode *right;
} ASTNode;

typedef struct Variable {
    char *id;
    int value;
} Variable;

ASTNode *add_statement(ASTNode *list, ASTNode *stmt);
ASTNode *create_ast_node(char *type, ASTNode *left, ASTNode *right);
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
ASTNode *create_ast_node_for(char *type, ASTNode *var, ASTNode *init, ASTNode *condition, ASTNode *update, ASTNode *body);
void interpret_ast(ASTNode *node);
Variable *find_variable_for(char *id);
void add_or_update_variable(char *id, int value);
void add_variable_for(char *id, int value);
void store_variable(char *id, ASTNode *value);
ASTNode *find_variable(char *id);
void free_ast(ASTNode *node);

#endif