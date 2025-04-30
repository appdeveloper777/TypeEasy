#ifndef AST_INTERPRET_H
#define AST_INTERPRET_H

#include "ast.h"

/* Funciones de interpretación específicas */
void interpret_for(ASTNode*);
void interpret_model_object(ASTNode*);
void interpret_train(ASTNode*);
void interpret_predict(ASTNode*);
void interpret_call_func(ASTNode*);
void interpret_return(ASTNode*);
void interpret_call_method(ASTNode*);
void interpret_var_decl(ASTNode*);
void interpret_assign_attr(ASTNode*);
void interpret_assign(ASTNode*);
void interpret_print(ASTNode*);
void interpret_statement_list(ASTNode*);

/* Funciones auxiliares de interpretación */
ASTNode* handle_return_value(ASTNode*);
ASTNode* handle_attribute_access_return(ASTNode*);
void handle_method_or_predict_var_decl(ASTNode*);
void cleanup_return_variable();
void handle_object_constructor(ASTNode*);
int find_attribute_index(ObjectNode*, const char*);
void handle_string_attribute_assignment(ObjectNode*, int, ASTNode*);
void print_attribute(ASTNode*);
void print_variable(const char*);

#endif