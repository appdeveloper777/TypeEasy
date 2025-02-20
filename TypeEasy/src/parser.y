%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "variables.h"
    #include <stdbool.h>
    #include <time.h>
    #include "csvparser.h"
    
    #define MAX_LINE_LENGTH 1024
    
    extern int yylex();
    void yyerror(const char *s);
    
    int symbol_table[256];
    FILE *yyin;
    CSVData read_csv(const char *filename);
    %}
    
    %union {
        int num;
        float fval;
        char* sval;
        char* id;
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
    
    %type <num> expression statement assignment
    %type <void> statement_body
    
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
            printf("Asignando %d a %s\n", $4, $2);
          }
        | FLOAT IDENTIFIER ASSIGN expression {
            int index = add_variable($2, FLOAT_TYPE);
            set_variable_float(index, $4);
            printf("Asignando %.2f a %s\n", $4, $2);
          }
        | STRING IDENTIFIER ASSIGN STRING_LITERAL {
            int index = add_variable($2, STRING_TYPE);
            set_variable_string(index, $4);
            printf("Asignando \"%s\" a la variable \"%s\"\n", $4, $2);
        }
        | IDENTIFIER ASSIGN expression {
            Variable var = get_variable($1);
            int index = get_variable_index($1);
            if (var.type != UNDEFINED) {
                if (var.type == INT_TYPE) {
                    set_variable_int(index, $3);
                    printf("Asignando %d a la variable %s\n", $3, $1);
                } else if (var.type == FLOAT_TYPE) {
                    printf("Asignando %.2f a la variable %s\n", (double)$3, $1);
                } else if (var.type == STRING_TYPE) {
                    printf("Asignando \"%s\" a la variable %s\n", (char*)$3, $1);
                }
            } else {
                printf("Error: La variable %s no está declarada\n", $1);
            }
        }
        | FOR LPAREN IDENTIFIER SEMICOLON IDENTIFIER SEMICOLON IDENTIFIER RPAREN LBRACKET statement_body RBRACKET {
            printf("Iniciando ciclo for...\n");
            Variable var_init = get_variable($3);
            Variable var_cond = get_variable($5);
            Variable var_inc = get_variable($7);
            int init_value = var_init.num;
            int condition = var_cond.num;
            int increment = var_inc.num;
            set_variable_int(init_value, condition);
            while (condition != 0) {
                yyparse();
                set_variable_int(condition, increment);
                condition -= increment;
                if (condition == 0) break;
            }
        }
        | PRINT IDENTIFIER {
            printf("Imprimiendo %s: ", $2);
            print_variable(get_variable($2));
        };
    
    assignment:
        IDENTIFIER ASSIGN expression {
            printf("Asignando valor a la variable %s\n", $1);
        };
    
    statement_body:
        statement
        | statement_body statement;
    
    expression:
          NUMBER { $$ = $1; }
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
        | expression PLUS expression { $$ = $1 + $3; }
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
    
    %%
    
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
    