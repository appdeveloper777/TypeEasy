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
    ParameterNode *pnode;
}

%token <sval> INT STRING FLOAT LAYER
%token DATASET MODEL TRAIN PREDICT FROM
%token       VAR ASSIGN PRINT FOR LPAREN RPAREN SEMICOLON CONCAT
%token       PLUS MINUS MULTIPLY DIVIDE LBRACKET RBRACKET
%token       CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT RETURN
%token <sval> IDENTIFIER STRING_LITERAL
%token <ival> NUMBER

%type <sval> method_name
%type <node>  expression_list var_decl constructor_decl return_stmt
%type <pnode> parameter_decl parameter_list

%type <node> dataset_decl
%type <node> model_decl
%type <node> layer_list
%type <node> layer_decl
%type <node> train_stmt
%type <node> train_options
%type <node> predict_stmt


%define parse.trace
%type <node> class_member


%type <node> statement expression program statement_list class_decl class_body
%type <node> attribute_decl method_decl

%%

program:
      program statement       { $$ = create_ast_node("STATEMENT_LIST", $2, $1); root = $$; }
    | program class_decl      { $$ = $1; /* Ignora definición de clase */ root = $$; }
    | statement               { $$ = $1; root = $$; }
    | class_decl              { $$ = NULL; /* Ignora definición de clase */ }



class_decl:
      CLASS IDENTIFIER        {
            last_class = create_class($2);
            add_class(last_class);
          }
          LBRACKET class_body RBRACKET
                                  {
                                    /* Fin definición de clase */
                                    $$ = NULL;  /* No añade nada al AST */
                                 }
    ;

/* Un miembro de clase puede ser un atributo, constructor o método */
class_member:
    attribute_decl
  | constructor_decl
  | method_decl
  ;
class_body:
  /* cero miembros */
| /* vacío */                             { $$ = NULL; }
  /* uno o más miembros: no agregamos nunca nodos C al AST */
| class_body class_member                 { $$ = $1; }
;

train_options:
    IDENTIFIER ASSIGN NUMBER
    {
        $$ = create_train_option_node($1, $3);
    }
;

layer_list:
    layer_decl
    {
        $$ = $1;
    }
    | layer_list layer_decl
    {
        $$ = append_layer_to_list($1, $2);
    }
;

layer_decl:
    LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON
    {
        $$ = create_layer_node($2, $4, $6);
    }
;

attribute_decl:
    IDENTIFIER COLON INT SEMICOLON
    {
        if (last_class) {
            add_attribute_to_class(last_class, $1, "int");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", $1);
        }
    } |

    IDENTIFIER COLON STRING SEMICOLON
    {
        if (last_class) {
            add_attribute_to_class(last_class, $1, "string");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", $1);
        }
    }
    ;

constructor_decl:
    CONSTRUCTOR LPAREN parameter_list  RPAREN LBRACKET statement_list  RBRACKET
    {
        //printf("[DEBUG] Se detectó un constructor en la clase %s\n", last_class->name);
        if (last_class) {
            add_constructor_to_class(last_class, $3, $6);
           // add_constructor_to_class(last_class, $6);
           //add_method_to_class(last_class, last_class->name /*o "__init__"*/, $6);

        } else {
            printf("Error: No hay clase definida para el constructor.\n");
        }
        $$ = NULL;
    }
    ;

    parameter_decl:
         IDENTIFIER COLON INT
             { $$ = create_parameter_node($1, $3); }
       | INT IDENTIFIER
             { $$ = create_parameter_node($2, $1); }
       | IDENTIFIER COLON STRING
             { $$ = create_parameter_node($1, $3); }
       | STRING IDENTIFIER
             { $$ = create_parameter_node($2, $1); }
       | IDENTIFIER COLON FLOAT
             { $$ = create_parameter_node($1, $3); }
       | FLOAT IDENTIFIER
             { $$ = create_parameter_node($2, $1); }
    ;

    parameter_list:
    /* vacío */ { $$ = NULL; }
    | IDENTIFIER COLON IDENTIFIER { $$ = create_parameter_node($1, $3); }
    | parameter_list COMMA IDENTIFIER COLON IDENTIFIER { $$ = add_parameter($1, $3, $5); }
    | parameter_decl
    { $$ = $1;}
| parameter_list COMMA  parameter_decl
    { $$ = add_parameter($1, $3->name, $3->type); }
    ;

method_decl:
    IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET
    {
        /* Añade el método $1 al último last_class declarado */
                if (!last_class) {
                        printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1);
                    } else {
                        /* params = NULL, body = $5 */
                        add_method_to_class(last_class, $1, NULL, $5);
                    }
                    $$ = NULL;
    }
    |

    IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        if (!last_class) {
            printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1);
        } else {
            /* params = $3, body = $6 */
            add_method_to_class(last_class, $1, $3, $6);
        }
        $$ = NULL;
    }
    ;

var_decl:
    LET IDENTIFIER ASSIGN expression SEMICOLON
    {
        $$ = create_var_decl_node($2, $4);
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }  |
    STRING IDENTIFIER ASSIGN expression SEMICOLON {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        $$ = create_var_decl_node($2, $4);

    }

    |
    VAR IDENTIFIER ASSIGN expression SEMICOLON {
        printf("IMPRIMIENDO VAR \n");
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        $$ = create_var_decl_node($2, $4);

    }

    |
    INT IDENTIFIER ASSIGN expression SEMICOLON {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        $$ = create_var_decl_node($2, $4);

    }

    | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
        $$ = create_ast_node("ASSIGN_ATTR", access, $5);  //  left = acceso, right = valor
    }

      | THIS DOT IDENTIFIER ASSIGN expression SEMICOLON {
              /* obj será siempre “this” */
              ASTNode *obj    = create_ast_leaf("ID", 0, NULL, "this");
                    /* 2) El nombre del atributo viene en $3 */
                    ASTNode *attr   = create_ast_leaf("ID", 0, NULL, $3);
                    ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
                    /* 3) La expresión es $5 */
                    $$ = create_ast_node("ASSIGN_ATTR", access, $5);
    }


    ;

statement:    
 LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON
        { $$ = create_var_decl_node($2, $4); }
|  RETURN expression SEMICOLON
{
   // printf(" [DEBUG] Reconocido RETURN\n");
  // crea un nodo de retorno
  $$ = create_return_node($2);
} |
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
     /* ——— llamadas a método ——— */
  | IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON
  {

    ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    $$ = create_method_call_node(obj, $3, NULL);
  }

  | IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
  {

    //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    $$ = create_method_call_node(obj, $3, $5);
  }

| THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON
  {
    ASTNode *thisObj = create_ast_leaf("ID", 0, NULL, "this");
    $$ = create_method_call_node(thisObj, $3, NULL);
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

      |  LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON
     {
         ClassNode *cls = find_class($5);
         if (!cls) {
             printf("Error: Clase '%s' no definida.\n", $5);
             $$ = NULL;
         } else {
             $$ = create_var_decl_node(
                 $2,
                 create_object_with_args(cls, NULL)
             );
         }
     }

    /* constructor con argumentos: let x = new Clase(arg1, arg2, …); */
  |  LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
     {
         ClassNode *cls = find_class($5);
         if (!cls) {
             printf("Error: Clase '%s' no definida.\n", $5);
             $$ = NULL;
         } else {
             $$ = create_var_decl_node(
                 $2,
                 create_object_with_args(cls, $7)
             );
         }
     }


     |

     DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON
     { printf(" [DEBUG] Reconocido DATASET\n");
        $$ = create_dataset_node($2,$4);
     }

     |

     PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");
        $$ = create_predict_node($3,$5); 
    }

    |

    VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");

        ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
        $$ = create_method_call_node(obj, "predict", NULL);


        $$ = create_predict_node($6,$8); 
    }



|
    DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON
    {
        $$ = create_dataset_node($2, $4);
    }
|
   MODEL IDENTIFIER LBRACKET layer_list RBRACKET
{
    printf("[DEBUG] Reconocido MODEL\n");
    printf("[DEBUG] Nombre del modelo: %s\n", $2);

    // Mostrar lista de capas
    ASTNode* layer = $4;
    int capa_index = 0;
    while (layer) {
        if (strcmp(layer->type, "LAYER") == 0) {
            printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n",
                   capa_index++,
                   layer->id,
                   layer->value,
                   layer->str_value);
        }
        layer = layer->right;
    }

    ASTNode* modelNode = create_model_node($2, $4);
    printf("[DEBUG] Nodo de modelo creado: type=%s, id=%s\n", modelNode->type, modelNode->id);

    $$ = create_var_decl_node($2, modelNode);
    printf("[DEBUG] VAR_DECL creado para modelo '%s'\n", $2);
}

|
    LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON
    {
        $$ = create_layer_node($2, $4, $6);
    }
|


    TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON
    {
        $$ = create_train_node($3, $5, $7);
    }

|

    IDENTIFIER ASSIGN NUMBER
    {
        $$ = create_train_option_node($1, $3);
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

PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN
{

     /* obj.metodo() sin argumentos */
     ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
     $$ = create_method_call_node(obj, $3, NULL);


     
   // *obj  = create_ast_leaf("ID", 0, NULL, $1);
   //ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
   //$$ = create_ast_node("ACCESS_ATTR", obj, attr);

}
|
IDENTIFIER DOT IDENTIFIER LPAREN RPAREN {
    /* obj.metodo() sin argumentos */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    $$ = create_method_call_node(obj, $3, NULL);
}
| IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN {
    /* obj.metodo(arg1, arg2, ...) */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    $$ = create_method_call_node(obj, $3, $5);
} |
 IDENTIFIER DOT IDENTIFIER {

   // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);

   ASTNode *obj  = create_ast_leaf("ID", 0, NULL, $1);
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
        $$ = create_ast_node("ACCESS_ATTR", obj, attr);

    /*$$ = create_ast_node("ACCESS_ATTR",
                        create_ast_leaf("ID", 0, NULL, $1),
                        create_ast_leaf("ID", 0, NULL, $3));*/
}

| expression DOT IDENTIFIER LPAREN RPAREN {
    printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", $1, $3);
    // $1 = AST del objeto, $3 = nombre del método
    $$ = create_method_call_node($1, $3, NULL);
}

|

expression DOT IDENTIFIER LPAREN expression_list RPAREN
    {
        /* objeto en $1, nombre en $3, args en $5 */
        $$ = create_method_call_node($1, $3, $5);
    }

  | THIS DOT IDENTIFIER {
            $$ = create_ast_node("ACCESS_ATTR",
                  create_ast_leaf("ID", 0, NULL, "this"),
                  create_ast_leaf("ID", 0, NULL, $3));
        }

        /* this.metodo() */
      | THIS DOT IDENTIFIER LPAREN RPAREN {
           $$ = create_method_call_node(
                   create_ast_leaf("ID", 0, NULL, "this"),
                   $3, NULL);
        }


| IDENTIFIER { $$ = create_ast_leaf("IDENTIFIER", 0, NULL,  $1); }
    | NUMBER { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }
    | STRING_LITERAL { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
    | CONCAT LPAREN expression_list RPAREN {
        printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", $3);
        // create_function_call_node(name, args)

       $$ = create_function_call_node("concat", $3);

    }

    | expression PLUS expression {
        $$ = create_ast_node("ADD", $1, $3);
        //printf(" [DEBUG] Reconocido ADD: %s + %s\n", $1, $3);
    }
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

    //yydebug = 1;




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