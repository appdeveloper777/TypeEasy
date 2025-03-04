%{
    #include "semantics.c"
    #include "symtab.c"
    #include "ast.h"
    #include "ast.c"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    extern FILE *yyin;
    extern FILE *yyout;
    extern int lineno;
    extern int yylex();

    // for declarations
    void add_to_names(list_t *entry);
    list_t **names;
    int nc = 0;
    AST_Node **elsifs;
    // for the initializations of arrays
    void add_to_vals(Value val);
    Value *vals;
    int vc = 0;

    int elseif_count = 0;

    void yyerror();
%}

/* YYSTYPE union */
%union{
    char char_val;
    int int_val;
    double double_val;
    char* str_val;
    int ival;

    // different types of values
    Value val;   
	
    // structures
    list_t* symtab_item;
    AST_Node* node;
}

/* token definition */
%token<int_val> CHAR INT FLOAT DOUBLE IF ELSE FOR CONTINUE BREAK VOID RETURN
%token<int_val> ADDOP MULOP DIVOP INCR OROP ANDOP NOTOP EQUOP RELOP
%token<int_val> LPAREN RPAREN LBRACK RBRACK LBRACE RBRACE SEMI DOT COMMA ASSIGN REFER
%token <symtab_item>   ID
%token <int_val>       CONST
%token <int_val>       ICONST

%token <val> WHILE
%type <node> statement if_statement for_statement while_statement
%type <node> expression assigment function_call
%token PRINT
%type <node> statements tail
%type <node> var_ref

%token <double_val>    FCONST
%token <char_val>      CCONST
%token <str_val>       STRING

/* precedencies and associativities */
%left LPAREN RPAREN LBRACK RBRACK
%right NOTOP INCR REFER
%left MULOP DIVOP
%left ADDOP
%left RELOP
%left EQUOP
%left OROP
%left ANDOP
%right ASSIGN
%left COMMA


%start program

/* expression rules */

%%

program: declarations statements RETURN SEMI functions_optional ;

declarations: declarations declaration | declaration;

declaration: type names SEMI ;

type: INT | CHAR | FLOAT | DOUBLE | VOID ;

names: names COMMA variable | names COMMA init | variable | init ;

variable: ID |
    pointer ID |
    ID array
;

pointer: pointer MULOP | MULOP ;

array: array LBRACK expression RBRACK | LBRACK expression RBRACK ;

init: var_init | array_init ;

var_init : ID ASSIGN constant;

array_init: ID array ASSIGN LBRACE values RBRACE ;

values: values COMMA constant | constant ;

/* statements */
statements: statements statement | statement  ;

statement:
    if_statement { $$ = $1; }
    | for_statement { $$ = $1; }
    | while_statement { $$ = $1; }
    | assigment SEMI { $$ = $1; }
    | CONTINUE SEMI { $$ = new_ast_simple_node(0); }
    | BREAK SEMI { $$ = new_ast_simple_node(1); }
    | function_call SEMI { $$ = $1; }
    | ID INCR SEMI { $$ = new_ast_incr_node($1, 0, 0); }
    | INCR ID SEMI { $$ = new_ast_incr_node($1, 1, 0); }
    | PRINT LPAREN ID RPAREN SEMI { printf("%s\n",$3); }
    | PRINT LPAREN CONST RPAREN SEMI { printf("%s\n",$3); }
    | PRINT LPAREN ICONST RPAREN SEMI { printf("%s\n",$3); }
    | PRINT LPAREN FCONST RPAREN SEMI { printf("%s\n",$3); }
    | PRINT LPAREN CCONST RPAREN SEMI { printf("%s\n",$3); }
    | PRINT LPAREN STRING RPAREN SEMI { printf("%s\n",$3); }
;

if_statement:
    IF LPAREN expression RPAREN tail else_if optional_else |
    IF LPAREN expression RPAREN tail optional_else
;

else_if: 
    else_if ELSE IF LPAREN expression RPAREN tail |
    ELSE IF LPAREN expression RPAREN tail
;

optional_else: ELSE tail | /* empty */ ;

for_statement: FOR LPAREN assigment SEMI expression SEMI ID INCR RPAREN tail
{
    // create increment node
    AST_Node *incr_node;
    if ($8 == INC) { /* increment */
        incr_node = new_ast_incr_node($7, 0, 0);
    } else {
        incr_node = new_ast_incr_node($7, 1, 0);
    }

    // Create the 'for' node
    $$ = new_ast_for_node($3, $5, incr_node, $10);
    set_loop_counter($$);
} ;

while_statement: WHILE LPAREN expression RPAREN tail
{
    //$$ = new_ast_while_node($3, $4);
} ;

tail: LBRACE statements RBRACE { $$ = $2; }

expression:
    expression ADDOP expression |
    expression MULOP expression |
    expression DIVOP expression |
    ID INCR |
    INCR ID |
    expression OROP expression |
    expression ANDOP expression |
    NOTOP expression |
    expression EQUOP expression |
    expression RELOP expression |
    LPAREN expression RPAREN |
    var_ref |
    sign constant |
    function_call
;

sign: ADDOP | /* empty */ ; 

constant: ICONST | FCONST | CCONST ;

assigment: var_ref ASSIGN expression;

var_ref  : variable | REFER variable ; 

function_call: ID LPAREN call_params RPAREN;

call_params: call_param | STRING | /* empty */

call_param : call_param COMMA expression | expression ; 

/* functions */
functions_optional: functions | /* empty */ ;

functions: functions function | function ;

function: function_head function_tail ;
		
function_head: return_type ID LPAREN parameters_optional RPAREN ;

return_type: type | type pointer ;

parameters_optional: parameters | /* empty */ ;

parameters: parameters COMMA parameter | parameter ;

parameter : type variable ;

function_tail: LBRACE declarations_optional statements_optional return_optional RBRACE ;

declarations_optional: declarations | /* empty */ ;

statements_optional: statements | /* empty */ ;

return_optional: RETURN expression SEMI | /* empty */ ;

%%

void add_to_names(list_t *entry) {
    if (!entry) {
        fprintf(stderr, "Error: Null entry passed to add_to_names\n");
        return;
    }
    
    if (nc == 0) {
        nc = 1;
        names = (list_t **) malloc(sizeof(list_t *));
        if (!names) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit(1);
        }
        names[0] = entry;
    } else {
        list_t **temp = (list_t **) realloc(names, (nc + 1) * sizeof(list_t *));
        if (!temp) {
            fprintf(stderr, "Error: Memory reallocation failed\n");
            free(names);
            exit(1);
        }
        names = temp;
        names[nc] = entry;
        nc++;
    }
}

void add_to_vals(Value val){
    if (vc == 0) {
        vc = 1;
        vals = (Value *) malloc(1 * sizeof(Value));
        vals[0] = val;
    } else {
        vc++;
        vals = (Value *) realloc(vals, vc * sizeof(Value));
        vals[vc - 1] = val;
    }
}

void add_elseif(AST_Node *elsif){
    if (elseif_count == 0) {
        elseif_count = 1;
        elsifs = (AST_Node **) malloc(1 * sizeof(AST_Node));
        elsifs[0] = elsif;
    } else {
        elseif_count++;
        elsifs = (AST_Node **) realloc(elsifs, elseif_count * sizeof(AST_Node));
        elsifs[elseif_count - 1] = elsif;
    }
}

void yyerror () {
    fprintf(stderr, "Syntax error at line %d\n", lineno);
    exit(1);
}

int main (int argc, char *argv[]) {
    // initialize symbol table
    init_hash_table();

    // parsing
    int flag;
    yyin = fopen(argv[1], "r");
    flag = yyparse();
    fclose(yyin);
    
    printf("Parsing finished!\n");
    
    return flag;
}
