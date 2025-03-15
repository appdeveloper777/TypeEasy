%{
    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    
    ASTNode *root;
    
    FILE *yyin;    void yyerror(const char *s);
    int yylex();
    %}
    
    %union {
        int ival;
        char *sval;
        ASTNode *node;
    }
    
    %token FLOAT VAR ASSIGN PRINT FOR LPAREN RPAREN SEMICOLON 
    %token PLUS MINUS MULTIPLY DIVIDE STRING INT LBRACKET RBRACKET
    %token <sval> IDENTIFIER STRING_LITERAL 
    %token <ival> NUMBER
    
    %type <node> statement expression program statement_list
    
    %%

        program:
        program statement { $$ = create_ast_node("STATEMENT_LIST", $1, $2); root = $$; }
        | statement {  root = $1; }
        ;
    
    
    statement:
    IDENTIFIER ASSIGN IDENTIFIER PLUS NUMBER SEMICOLON {
        
        $$ = create_ast_node("ASSIGN", 
            create_ast_leaf("IDENTIFIER", 0, NULL, $1), $5); 
    } | 
        STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON { $$ = create_var_decl_node($2, create_string_node($4)); } 
        |  INT IDENTIFIER ASSIGN expression SEMICOLON { 
            //printf("Valor de $4: %d\n", $4->value); 
            $$ = create_var_decl_node($2, create_int_node($4->value)); 
        }
        |  FLOAT IDENTIFIER ASSIGN expression SEMICOLON { 
            //printf("Valor de $4: %d\n", $4->value); 
            $$ = create_var_decl_node($2, create_float_node($4->value)); 
        }
        | VAR IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, $2), $4); }
        | IDENTIFIER ASSIGN expression SEMICOLON { 
            printf("test..");
            $$ = create_ast_node("ASSIGN", 
                create_ast_leaf("IDENTIFIER", 0, NULL, $1), $3); 
        }
        | PRINT LPAREN expression RPAREN SEMICOLON { $$ = create_ast_node("PRINT", $3, NULL); }
        | 
        FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list  RBRACKET {
            
               
            $$ = create_ast_node_for("FOR", 
                create_ast_leaf("IDENTIFIER", 0, NULL, $3),  
                create_ast_leaf("NUMBER", $5, NULL, NULL),   
                $7,  // Condición
                $9,  // Incremento
                $12  // Cuerpo
            );
            
        }
    
    | LBRACKET statement_list RBRACKET
        ;
    statement_list:
        statement
        | statement statement_list
        ;
    expression:
        NUMBER {
           // printf("Número capturado: %d\n", $1);
            $$ = create_ast_leaf("NUMBER", $1, NULL, NULL);
        }    
        | STRING_LITERAL { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
        | IDENTIFIER { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
        | expression PLUS expression { $$ = create_ast_node("ADD", $1, $3); }
        | expression MINUS expression { $$ = create_ast_node("SUB", $1, $3); }
        | expression MULTIPLY expression { $$ = create_ast_node("MUL", $1, $3); }
        | expression DIVIDE expression { $$ = create_ast_node("DIV", $1, $3); }
        | LPAREN expression RPAREN
        ;
    
    %%
    
    void yyerror(const char *s) {
        fprintf(stderr, "Error: %s\n", s);
    }
    
    int main(int argc, char *argv[]) {
        if (argc != 2) {
            printf("Uso: %s <archivo de entrada>\n", argv[0]);
            return 1;
        }
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            printf("Error abriendo el archivo %s\n", argv[1]);
            return 1;
        }
        yyin = file;
        yyparse();
        fclose(file);

        if (root) {
           // printf("Parsing exitoso. Ejecutando el AST:\n");
            interpret_ast(root);
            free_ast(root);
        }

        return 0;
    }
    