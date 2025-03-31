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
    void generate_code(const char* code);
    void clean_generated_code();

%}

%union {
    int ival;
    char *sval;
    ASTNode *node;
}

%token FLOAT VAR ASSIGN PRINT FOR LPAREN RPAREN SEMICOLON 
%token PLUS MINUS MULTIPLY DIVIDE STRING INT LBRACKET RBRACKET
%token CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT

%token <sval> IDENTIFIER STRING_LITERAL 
%token <ival> NUMBER
%type <sval> method_name
%type <node> expression_list var_decl

%type <node> statement expression program statement_list class_decl class_body 
%type <node> attribute_decl method_decl constructor_decl parameter_list

%%

program:
    program statement { $$ = create_ast_node("STATEMENT_LIST", $2, $1); root = $$; }
  | program class_decl { $$ = create_ast_node("STATEMENT_LIST", $2, $1); root = $$; }
  | statement { root = $1; }
  | class_decl { root = $1; };



class_decl:
    CLASS IDENTIFIER
    {
        //printf(" [DEBUG] Se detectó una clase: %s\n", $2);
        last_class = create_class($2);  
        add_class(last_class);
    }
    LBRACKET class_body RBRACKET
    {
       // printf(" [DEBUG] Fin de la definición de clase %s\n", $2);
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
        //printf("[DEBUG] Se detectó un constructor en la clase %s\n", last_class->name);
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
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }   
    | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  // ✅ correctamente marcado
        $$ = create_ast_node("ASSIGN_ATTR", access, $5);  // ✅ left = acceso, right = valor
    }
    
    
    ;

statement:
var_decl    
    | STRING STRING expression SEMICOLON { 

        printf(" [DEBUG] Declaración de variable s: ¿");

       
        char buffer[2048];
          sprintf(buffer,
              "#include \"easyspark/dataframe.hpp\"\n"
              "#include <chrono>\n"
              "#include <iostream>\n"
              "int main() {\n"
              "    auto start = std::chrono::high_resolution_clock::now();\n"
              "    DataFrame df = read(\"dataset2.csv\");\n"
              "    auto filtered = df.filter([](const Row& row) {\n"
              "        auto it = row.find(\"adult\");\n"
              "        return it != row.end() && it->second == \"FALSE\";\n"
              "    });\n"
              "    std::cout << \"Filas que cumplen el filtro: \" << filtered.rows.size() << std::endl;\n"
              "    filtered.select({\"title\"}).show();\n"
              "    auto end = std::chrono::high_resolution_clock::now();\n"
              "    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);\n"
              "    std::cout << \"Duración: \" << duration.count() << \" ms\" << std::endl;\n"
              "    return 0;\n"
              "}\n"
          );
          generate_code(buffer);
        

    }
    | STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON { $$ = create_var_decl_node($2, create_string_node($4)); }
    | INT IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_var_decl_node($2, create_int_node($4->value)); }
    | FLOAT IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_var_decl_node($2, create_float_node($4->value)); }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, $2), $4); }
    | IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, $1), $3); }
    | PRINT LPAREN expression RPAREN SEMICOLON { $$ = create_ast_node("PRINT", $3, NULL); }
    | PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON {
       // printf(" [DEBUG] Reconocido PRINT con acceso a atributo: %s.%s\n", $3, $5);
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, $3);
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, $5);
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
        $$ = create_ast_node("PRINT", access, NULL);
    }
    
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
            printf("[DEBUG] Creación de objeto: %s\n", $2);
        }
    }
    
    ;
    statement_list:
    statement_list statement { $$ = create_ast_node("STATEMENT_LIST", $2, $1); }
  | statement                { $$ = $1; };





    ;

expression_list:
    expression { $$ = $1; }
    | expression_list COMMA expression { $$ = add_statement($1, $3); }
    ;

expression:
 IDENTIFIER DOT IDENTIFIER {
   // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);
    $$ = create_ast_node("ACCESS_ATTR",
                        create_ast_leaf("ID", 0, NULL, $1),
                        create_ast_leaf("ID", 0, NULL, $3));
} 
| IDENTIFIER { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
    | NUMBER { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }  
    | STRING_LITERAL { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
   
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

void clean_generated_code() {
    printf("sAPEEEEEEEEE");
    FILE* out = fopen("generated.cpp", "w"); // limpia completamente
    if (out != NULL) {
        fprintf(out, "");        
        fclose(out);
    }
}



void generate_code(const char* code) {
    
    FILE* out = fopen("generated.cpp", "w"); // modo 'a' agrega contenido
    if (out != NULL) {
        fprintf(out, "%s\n", code);
        fclose(out);
    }
}



void yyerror(const char *s) {
    extern char *yytext;
    fprintf(stderr, "Error: %s en la línea %d (token: '%s')\n", s, yylineno, yytext);
}


int main(int argc, char *argv[]) {

    

    // Flags para el modo de ejecución
int interpret_mode = 0;
    if (argc < 2) {
        printf("Uso: %s <archivo.te> [--interpret]\n", argv[0]);
        return 1;
    }

    // Verificar si se pasó el flag --interpret
    if (argc == 3 && strcmp(argv[2], "--run") == 0) {
        interpret_mode = 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo %s\n", argv[1]);
        return 1;
    }

    yyin = file;
    int parse_result = yyparse();
    fclose(file);

    if (parse_result != 0) {
        printf(" Error al parsear el archivo.\n");
        return 1;
    }
    

    if(interpret_mode){

        int compile_status = system("g++ generated.cpp easyspark/dataframe.cpp -o typeeasy_output");
        if (compile_status != 0) {
            printf("Error al compilar el programa generado.\n");
            return 1;
        }
    
        int run_status = system("typeeasy_output.exe");
        if (run_status != 0) {
            printf("Error al ejecutar el programa generado.\n");
            return 1;
        }

    }   
 

    //if (interpret_mode) {
        //  Ejecutar directamente el AST
        if (root) {
            interpret_ast(root);
            free_ast(root);
        }
   // } else {
        // Compilar y ejecutar el código generado
       
   // }

    return 0;
}
