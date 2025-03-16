%{
    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    
    ASTNode *root;
    extern int yylineno;
    FILE *yyin;    
    void yyerror(const char *s);
    ClassNode *last_class = NULL; 
    int yylex();
%}

%union {
    int ival;
    char *sval;
    ASTNode *node;
}

%token FLOAT VAR ASSIGN PRINT FOR LPAREN RPAREN SEMICOLON 
%token PLUS MINUS MULTIPLY DIVIDE STRING INT LBRACKET RBRACKET
%token CLASS CONSTRUCTOR THIS NEW LET COLON COMMA

%token <sval> IDENTIFIER STRING_LITERAL 
%token <ival> NUMBER
%type <sval> method_name
%type <node> expression_list var_decl

%type <node> statement expression program statement_list class_decl class_body 
%type <node> attribute_decl method_decl constructor_decl parameter_list

%%

program:
    program statement { $$ = create_ast_node("STATEMENT_LIST", $1, $2); root = $$; }
    | statement { root = $1; }
    | program class_decl { $$ = add_statement($1, $2); }
    | class_decl { root = $1; }  
    ;

class_decl:
    CLASS IDENTIFIER
    {
        printf("📌 Se detectó una clase: %s\n", $2);
        last_class = create_class($2);  
        add_class(last_class);
    }
    LBRACKET class_body RBRACKET
    {
        printf("📌 Fin de la definición de clase %s\n", $2);
    }
    ;

class_body:
    /* vacío */ { $$ = NULL; }
    | attribute_decl { $$ = $1; }
    | class_body attribute_decl { $$ = add_statement($1, $2); }
    | constructor_decl { $$ = $1; }  
    | class_body constructor_decl { $$ = add_statement($1, $2); }  
    | method_decl { $$ = $1; }
    | class_body method_decl { $$ = add_statement($1, $2); }
    ;

attribute_decl:
    IDENTIFIER COLON INT SEMICOLON
    {
        if (last_class) {
            add_attribute_to_class(last_class, $1, "int");  
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", $1);
        }
    }
    ;

constructor_decl:
    CONSTRUCTOR LPAREN  RPAREN LBRACKET  RBRACKET
    {
        printf("📌 Se detectó un constructor en la clase %s\n", last_class->name);
        if (last_class) {
            //add_constructor_to_class(last_class, $6);
        } else {
            printf("Error: No hay clase definida para el constructor.\n");
        }
    }
    ;

    parameter_list:
    /* vacío */ { $$ = NULL; }
    | IDENTIFIER COLON IDENTIFIER { $$ = create_parameter_node($1, $3); }
    | parameter_list COMMA IDENTIFIER COLON IDENTIFIER { $$ = add_parameter($1, $3, $5); }
    ;

method_decl:
    IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET
    {
        add_method_to_class(find_class($1), strdup($1), $5);
    }
    ;

var_decl:
    LET IDENTIFIER ASSIGN expression SEMICOLON
    {
        $$ = create_var_decl_node($2, $4);
        printf("📌 Declaración de variable: %s\n", $2);
    }
    ;

statement:
var_decl    
    | STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON { $$ = create_var_decl_node($2, create_string_node($4)); }
    | INT IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_var_decl_node($2, create_int_node($4->value)); }
    | FLOAT IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_var_decl_node($2, create_float_node($4->value)); }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, $2), $4); }
    | IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, $1), $3); }
    | PRINT LPAREN expression RPAREN SEMICOLON { $$ = create_ast_node("PRINT", $3, NULL); }
    | FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET
    {
        $$ = create_ast_node_for("FOR", 
            create_ast_leaf("IDENTIFIER", 0, NULL, $3),  
            create_ast_leaf("NUMBER", $5, NULL, NULL),   
            $7,  
            $9,  
            $12  
        );
    }
    | LBRACKET statement_list RBRACKET
    | 
    statement:
    var_decl
    | NEW IDENTIFIER LPAREN RPAREN SEMICOLON
    {        
        ClassNode *cls = find_class($2);
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", $2);
            $$ = NULL;
        } else {
            $$ = (ASTNode *)create_object_with_args(cls, NULL);
            printf("📌 Creación de objeto: %s\n", $2);
        }
    }
    
    ;

statement_list:
    statement { $$ = $1; } 
    | statement_list statement { $$ = add_statement($1, $2); } 
    ;

expression_list:
    expression { $$ = $1; }
    | expression_list COMMA expression { $$ = add_statement($1, $3); }
    ;

expression:
    NUMBER { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }
    | STRING_LITERAL { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
    | IDENTIFIER { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
    | expression PLUS expression { $$ = create_ast_node("ADD", $1, $3); }
    | expression MINUS expression { $$ = create_ast_node("SUB", $1, $3); }
    | expression MULTIPLY expression { $$ = create_ast_node("MUL", $1, $3); }
    | expression DIVIDE expression { $$ = create_ast_node("DIV", $1, $3); }
    | LPAREN expression 
    | NEW IDENTIFIER LPAREN RPAREN
    {
       
        ClassNode *cls = find_class($2);
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", $2);
            $$ = NULL;
        } else {
           $$ = (ASTNode *)create_object_with_args(cls, $2);
        }
    }
    ;

%%

void yyerror(const char *s) {
    extern char *yytext;
    fprintf(stderr, "Error: %s en la línea %d (token: '%s')\n", s, yylineno, yytext);
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
        interpret_ast(root);
        free_ast(root);
    }

    return 0;
}
