%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "variables.h"
#include "csvparser.h"

/* Variable global: AST root */
Node* root = NULL;
extern int yylex();
extern FILE *yyin; 
void yyerror(const char *s);
#define MAX_LINE_LENGTH 1024
%}
%union {
    int   ival;   
    float fval;  
    char* id;     
    char* sval;   
    Node* node;   
}

/* Tokens con su campo */
%token <ival> NUMBER
%token <fval> FLOAT_LITERAL
%token <id>   IDENTIFIER
%token <sval> STRING_LITERAL

%token VAR PRINT ASSIGN
%token LPAREN RPAREN SEMICOLON
%token PLUS MINUS MUL DIV GREATERTHAN
%token LBRACKET RBRACKET COMMA
%token STRING
%token FOR
%token FLOAT


%type <node> program statement statement_list expression


%left PLUS MINUS
%left MUL DIV
%nonassoc GREATERTHAN

%%
program:
    statement_list  { 
        root = $1; 
    }
;

statement_list:
    statement 
    {
       Node* list = newStatementList();
       appendStatement(list, $1);
       $$ = list;
    }
    | statement_list statement
    {
       appendStatement($1, $2);
       $$ = $1;
    }
;

statement:
    VAR IDENTIFIER ASSIGN expression
    {
       $$ = newVarDeclNode(INT_TYPE, $2, $4);
    }
    | FLOAT IDENTIFIER ASSIGN expression
    {
       $$ = newVarDeclNode(FLOAT_TYPE, $2, $4);
    }
    | STRING IDENTIFIER ASSIGN STRING_LITERAL
    {
       $$ = newVarDeclNode(STRING_TYPE, $2, newStringNode($4));
    }
    | IDENTIFIER ASSIGN expression
    {
       $$ = newAssignNode($1, $3);
    }
    | FOR LPAREN IDENTIFIER SEMICOLON IDENTIFIER SEMICOLON IDENTIFIER RPAREN LBRACKET statement_list RBRACKET
    {
       $$ = newForNode($3, $5, $7, $10);
    }
    | PRINT IDENTIFIER
    {
       $$ = newPrintNode($2);
    }
;

expression:
    NUMBER
    {
      $$ = newNumberNode((float)$1);
    }
    | FLOAT_LITERAL
    {
      $$ = newNumberNode($1);
    }
    | IDENTIFIER
    {
      $$ = newNumberNode(0.0f);
    }
    | expression PLUS expression
    {
      $$ = newBinaryOpNode('+', $1, $3);
    }
    | expression MINUS expression
    {
      $$ = newBinaryOpNode('-', $1, $3);
    }
    | expression MUL expression
    {
      $$ = newBinaryOpNode('*', $1, $3);
    }
    | expression DIV expression
    {
      $$ = newBinaryOpNode('/', $1, $3);
    }
;

%%

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <archivo>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1],"r");
    if(!f){
        perror("Error abriendo archivo");
        return 1;
    }
    yyin = f;
    yyparse();
    fclose(f);

    /* interpretar el AST */
    if (root) {
       interpret(root);
    } else {
       printf("No hay AST...\n");
    }
    return 0;
}

void yyerror(const char *s) {
    fprintf(stderr, "Error de sintaxis: %s\n", s);
}