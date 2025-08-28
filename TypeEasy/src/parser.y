%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "variables.h"
    #include <stdbool.h>
    #include <time.h>
    #include "csvparser.h"
    
    #define MAX_LINE_LENGTH 1024
    #define MAX_STATEMENTS 100
    
    // Declaraciones de funciones
    extern int yylex();
    void yyerror(const char *s);
    FILE *yyin;
    void execute_statement_body(StatementBody body);
    void execute_statement(Statement stmt);
    void print_variable(Variable var);
    void set_variable(char *name, int value);
    void set_variable_float(int index, float value);   

%}
    
%union {
    int num;
    float fval;
    char* sval;
    char* id;
    Statement stmt; // Nuevo tipo para declaraciones
    StatementBody body; // Nuevo tipo para cuerpos de declaraciones
}
    
%token <num> NUMBER
%token <id> IDENTIFIER
%token VAR PRINT ASSIGN
%token LPAREN RPAREN SEMICOLON
%token PLUS MINUS MUL DIV GREATERTHAN
%token <fval> FLOAT FLOAT_LITERAL
%token LBRACKET RBRACKET COMMA
%token <sval> STRING STRING_LITERAL
%token FOR
    
%type <num> expression
%type <stmt> statement
%type <body> statement_body

%type <node> expr_list arg_list arg_list_opt

    
%left PLUS MINUS
%left MUL DIV
    
%%
    
program:
    | program statement
    ;
    
statement:
    VAR IDENTIFIER ASSIGN expression {
        int index = add_variable($2, INT_TYPE);
        set_variable_int(index, $4);      
    }
    | FLOAT IDENTIFIER ASSIGN expression {
        int index = add_variable($2, FLOAT_TYPE);
        set_variable_float(index, $4);      
    }
    | STRING IDENTIFIER ASSIGN STRING_LITERAL {
        int index = add_variable($2, STRING_TYPE);
        set_variable_string(index, $4);       
    }
    | IDENTIFIER ASSIGN expression {
        Variable var = get_variable($1);        
        if (var.type != UNDEFINED) {
            set_variable($1, $3);
        } else {
            printf("Error: La variable %s no está declarada\n", $1);
        }       
    }
    
    | FOR LPAREN IDENTIFIER SEMICOLON IDENTIFIER SEMICOLON IDENTIFIER RPAREN LBRACKET statement_body RBRACKET {
        printf("Iniciando ciclo for...\n");

        // Obtener las variables
        Variable var_init = get_variable($3);
        Variable var_cond = get_variable($5);
        Variable var_inc = get_variable($7);

        if (var_init.type == UNDEFINED || var_cond.type == UNDEFINED || var_inc.type == UNDEFINED ||
            var_init.type != INT_TYPE || var_cond.type != INT_TYPE || var_inc.type != INT_TYPE) {
            fprintf(stderr, "Error: Variables del ciclo for no definidas o tipo incorrecto.\n");
            exit(1);
        }

       // Ejecutar el ciclo for
        for (int i = var_init.num; i <= var_cond.num; i += var_inc.num) {            
            // Ejecutar el cuerpo del ciclo (statement_body)
            execute_statement_body($10);
        }
    } 
    |
    IDENTIFIER ASSIGN IDENTIFIER PLUS expression {
        $$.type = STMT_ADD;
        strcpy($$.identifier, $1);
        $$.operand1 = $3;
        $$.operand2 = $5;
        Variable var = get_variable($1);
        if (var.type == INT_TYPE) {
            int value = var.num + $5;  // $5 es el valor de la expresión
            set_variable($1, value);            
        } else if (var.type == FLOAT_TYPE) {
            float value = var.fnum + $5;  // $5 es el valor de la expresión
            set_variable_float(get_variable_index($1), value);           
        } else {
            yyerror("Tipo de variable inválido para la acumulación.");
        }
    }
    | PRINT IDENTIFIER {
        $$.type = STMT_PRINT;
        strcpy($$.identifier, $2);
        printf("Imprimiendo %s: ", $2);
        print_variable(get_variable($2));
    }
    | IDENTIFIER ASSIGN IDENTIFIER PLUS expression {        
        $$.type = STMT_ACCUMULATE;
        strcpy($$.identifier, $1);
        strcpy($$.operand2, $3);
        $$.operand1 = $5;
    }
    | IDENTIFIER ASSIGN expression PLUS expression {       
        $$.type = STMT_ADD;
        strcpy($$.identifier, $1);
        $$.operand1 = $3;
        $$.operand2 = $5;
    }
    | IDENTIFIER ASSIGN expression MINUS expression {        
        $$.type = STMT_SUBTRACT;
        strcpy($$.identifier, $1);
        $$.operand1 = $3;
        $$.operand2 = $5;
    }
    | IDENTIFIER ASSIGN expression MUL expression {
        $$.type = STMT_MULTIPLY;
        strcpy($$.identifier, $1);
        $$.operand1 = $3;
        $$.operand2 = $5;
    };
    
statement_body:
    statement {
        $$.statements[0] = $1;
        $$.count = 1;
    }
    | statement_body statement {
        if ($1.count < MAX_STATEMENTS) {
            $1.statements[$1.count] = $2;
            $1.count++;
        } else {
            fprintf(stderr, "Error: Demasiadas declaraciones en el cuerpo del ciclo.\n");
            exit(1);
        }
        $$ = $1;
       
    }
;
    
expression:
    NEW IDENTIFIER LPAREN arg_list_opt RPAREN { $$ = create_object_with_args(find_class($2), $4); }
    |
    NUMBER {   $$ = $1; }
    | FLOAT_LITERAL { $$ = $1; }
    | IDENTIFIER {
        Variable var = get_variable($1);
        if (var.type == INT_TYPE) $$ = var.num;
        else if (var.type == FLOAT_TYPE) $$ = var.fnum;
        else {
            yyerror("Tipo de variable inválido en la expresión.");
            $$ = 0;
        }
    }
    | expression PLUS expression {  $$ = $1 + $3; }
    | expression MINUS expression { $$ = $1 - $3; }
    | expression MUL expression { $$ = $1 * $3; }
    | expression DIV expression {
        if ($3 == 0) {
            yyerror("Error: división por cero");
            $$ = 0;
        } else {
            $$ = $1 / $3;
        }
    }
    | expression GREATERTHAN expression { $$ = $1 < $3; };

    arg_list_opt:
    /* vacío */ { $$ = NULL; }
  | arg_list   { $$ = $1; }
;

arg_list:
    expression               { $$ = $1; }
  | arg_list COMMA expression { printf(" [DEBUG] Reconocido COMMA\n"); $$ = add_argument($1, $3); }
;

    
%%
void execute_statement_body(StatementBody body) {
    // Ejecutar cada declaración en el cuerpo del ciclo
    for (int i = 0; i < body.count; i++) {
        execute_statement(body.statements[i]);
    }
}

void execute_statement(Statement stmt) {
    switch (stmt.type) {
        case STMT_PRINT:           
            print_variable(get_variable(stmt.identifier));
            break;
        case STMT_ASSIGN:
            set_variable(stmt.identifier, stmt.value);
            break;
        case STMT_ADD: {
            Variable val = get_variable(stmt.identifier);
            int result = val.num + stmt.operand2;
            set_variable(stmt.identifier, result);          
            break;
        }
        case STMT_SUBTRACT: {
            Variable val = get_variable(stmt.identifier);
            int result = val.num - stmt.operand2;
            set_variable(stmt.identifier, result);            
            break;
        }
        case STMT_MULTIPLY: {
            int result = stmt.operand1 * stmt.operand2;
            set_variable(stmt.identifier, result);            
            break;
        }
        case STMT_ACCUMULATE: {
            Variable var = get_variable(stmt.identifier);
            if (var.type == INT_TYPE) {
                int result = var.num + stmt.operand1;
                set_variable(stmt.identifier, result);
                
            } else if (var.type == FLOAT_TYPE) {
                float result = var.fnum + stmt.operand1;
                set_variable_float(get_variable_index(stmt.identifier), result);
                
            } else {
                fprintf(stderr, "Error: Tipo de variable no soportado para la acumulación.\n");
            }
            break;
        }
        default:            
            fprintf(stderr, "Error: Tipo de declaración no soportado.\n");
            break;
    }
}

void set_variable(char *name, int value) {
    int index = get_variable_index(name);
    if (index != -1) {       
        set_variable_int(index, value);
    } else {
        fprintf(stderr, "Error: Variable '%s' no definida.\n", name);
        exit(1);
    }
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
    return 0;
}

void yyerror(const char *s) {
    printf("Error de sintaxis: %s\n", s);
    fprintf(stderr, "Error de sintaxis: %s\n", s);
}