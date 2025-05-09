%{   

    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    #include <locale.h>

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

%token <sval> INT STRING FLOAT LAYER LSBRACKET RSBRACKET
%token DATASET MODEL TRAIN PREDICT FROM PLOT ARROW IN LAMBDA
%token       VAR ASSIGN PRINT FOR LPAREN RPAREN SEMICOLON CONCAT
%token       PLUS MINUS MULTIPLY DIVIDE LBRACKET RBRACKET
%token       CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT RETURN
%token <sval> IDENTIFIER STRING_LITERAL
%token <ival> NUMBER

%type <sval> method_name
%type <node>  expression_list var_decl constructor_decl return_stmt arg_list more_args lambda_expression
%type <pnode> parameter_decl parameter_list list_literal
%type <node> object_expression object_list

%type <node> dataset_decl
%type <node> model_decl
%type <node> layer_list
%type <node> layer_decl
%type <node> train_stmt
%type <node> train_options
%type <node> predict_stmt
%type <node> lambda
%type <node> expr_list


%define parse.trace
%type <node> class_member


%type <node> statement expression program statement_list class_decl class_body
%type <node> attribute_decl method_decl

%right ARROW
%nonassoc GT LT EQ /* tus operadores relacionales */

%%

program:
        program statement       { $$ = create_ast_node("STATEMENT_LIST", $1, $2); root = $$; }
        | program class_decl      { $$ = $1; /* Ignora definición de clase */ root = $$; }
        | statement               { $$ = $1; root = $$; }
        | class_decl              { $$ = NULL; /* Ignora definición de clase */ }


class_decl:
        CLASS IDENTIFIER { last_class = create_class($2); add_class(last_class); } 
        LBRACKET class_body RBRACKET { $$ = NULL; }
;

class_member:
        attribute_decl
        | constructor_decl
        | method_decl
        ;

class_body:
        { $$ = NULL; }
        | class_body class_member { $$ = $1; }
        ;

train_options: IDENTIFIER ASSIGN NUMBER { $$ = create_train_option_node($1, $3); };
layer_list: layer_decl                          { $$ = $1; };
layer_list: layer_list layer_decl               { $$ = append_layer_to_list($1, $2); };
layer_decl: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON { $$ = create_layer_node($2, $4, $6); };


attribute_decl:
    IDENTIFIER COLON INT SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "int"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", $1); } }
  | IDENTIFIER COLON STRING SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "string"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", $1); } }
  ;

constructor_decl:
    CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  { if (last_class) { add_constructor_to_class(last_class, $3, $6); } else { printf("Error: No hay clase definida para el constructor.\n"); } $$ = NULL; }
  ;

parameter_decl:
    IDENTIFIER COLON INT        { $$ = create_parameter_node($1, $3); }
  | INT IDENTIFIER             { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON STRING    { $$ = create_parameter_node($1, $3); }
  | STRING IDENTIFIER          { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON FLOAT     { $$ = create_parameter_node($1, $3); }
  | FLOAT IDENTIFIER           { $$ = create_parameter_node($2, $1); }
  ;

parameter_list:
    /* vacío */                                    { $$ = NULL; }
  | IDENTIFIER COLON IDENTIFIER                    { $$ = create_parameter_node($1, $3); }
  | parameter_list COMMA IDENTIFIER COLON IDENTIFIER { $$ = add_parameter($1, $3, $5); }
  | parameter_decl                                 { $$ = $1; }
  | parameter_list COMMA parameter_decl            { $$ = add_parameter($1, $3->name, $3->type); }
  ;

method_decl:
    IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1); } else { add_method_to_class(last_class, $1, NULL, $5); } $$ = NULL; }
  | IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1); } else { add_method_to_class(last_class, $1, $3, $6); } $$ = NULL; }
  ;

expression:

expression GT expression
      {
        $$ = create_ast_node("GT", $1, $3);
      }
    | expression LT expression
      {
        $$ = create_ast_node("LT", $1, $3);
      }    
| IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN  { printf(" [DEBUG] PERU Reconocido FUNCTION_CALL: %s(%s)\n", $1, $3); ASTNode *listExpr = create_identifier_node($1); ASTNode *filterCall = create_list_function_call_node(listExpr, $3, $5); $$ = create_var_decl_node($1, filterCall); }
| LSBRACKET object_list RSBRACKET                       { /* printf(" [DEBUG] Reconocido OBJECT_LIST\n"); */ $$ = $2; }
| expression DOT IDENTIFIER LPAREN lambda RPAREN        {  if (strcmp($3, "filter")==0) { $$ = create_list_function_call_node($1, $3, $5); } else { $$ = create_method_call_node($1, $3, NULL); } free($3); }
| LSBRACKET expr_list RSBRACKET                         { /* printf(" [DEBUG] Reconocido EXPR_LIST\n"); */ $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); $$ = create_list_node($2); }
| PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN  { ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); $$ = create_method_call_node(obj, $3, NULL); }
| IDENTIFIER DOT IDENTIFIER LPAREN RPAREN           { ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1); $$ = create_method_call_node(obj, $3, NULL); }
| IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  { ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1); $$ = create_method_call_node(obj, $3, $5); }
| IDENTIFIER DOT IDENTIFIER                        { ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1); ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3); $$ = create_ast_node("ACCESS_ATTR", obj, attr); }
| expression DOT IDENTIFIER LPAREN RPAREN          { printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", $1, $3); $$ = create_method_call_node($1, $3, NULL); }
| expression DOT IDENTIFIER LPAREN expression_list RPAREN  { $$ = create_method_call_node($1, $3, $5); }
| THIS DOT IDENTIFIER                              { $$ = create_ast_node("ACCESS_ATTR", create_ast_leaf("ID", 0, NULL, "this"), create_ast_leaf("ID", 0, NULL, $3)); }
| THIS DOT IDENTIFIER LPAREN RPAREN                { $$ = create_method_call_node(create_ast_leaf("ID", 0, NULL, "this"), $3, NULL); }
| IDENTIFIER                                      { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
| NUMBER                                          { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }
| STRING_LITERAL                                  { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
| CONCAT LPAREN expression_list RPAREN            { printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", $3); $$ = create_function_call_node("concat", $3); }
| expression PLUS expression                      { $$ = create_ast_node("ADD", $1, $3); }
| expression MINUS expression                     { $$ = create_ast_node("SUB", $1, $3); }
| expression MULTIPLY expression                  { $$ = create_ast_node("MUL", $1, $3); }
| expression DIVIDE expression                    { $$ = create_ast_node("DIV", $1, $3); }
| LPAREN expression RPAREN                        { $$ = $2; }
| NEW IDENTIFIER LPAREN RPAREN                    { ClassNode *cls = find_class($2); if (!cls) { printf("Error: Clase '%s' no definida.\n", $2); $$ = NULL; } else { $$ = (ASTNode *)create_object_with_args(cls, $2); } }
| NEW IDENTIFIER LPAREN expr_list RPAREN { ClassNode *cls = find_class($2); if (!cls) { fprintf(stderr, "Error: clase '%s' no encontrada\n", $2); exit(1); } $$ = create_object_with_args(cls, $4); free($2); }

;


var_decl:
    LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN SEMICOLON  { /*printf(" [DEBUG] Reconocido FILTER_CALL: let %s = %s(...)\n", $2, $4);*/ ASTNode *listExpr = $6; ASTNode *lambda = $8; ASTNode *filterCall = create_list_function_call_node(listExpr, $4, lambda); filterCall->type = strdup("FILTER_CALL"); $$ = create_var_decl_node($2, filterCall); }
  | LET IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | STRING IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON  { printf("IMPRIMIENDO VAR \n"); $$ = create_var_decl_node($2, $4); }
  | INT IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  | THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  
  ;

statement:
    FOR LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  { $$ = create_for_in_node($4, $6, $9); }
  | LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4); $$ = create_var_decl_node($2, $4); }
  | RETURN expression SEMICOLON  { $$ = create_return_node($2); }
  | var_decl
  | STRING STRING expression SEMICOLON                          { printf(" [DEBUG] Declaración de variable s: ¿"); char buffer[2048]; sprintf(buffer,"#include \"easyspark/dataframe.hpp\"\n..."); generate_code(buffer); }
  | IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); $$ = create_method_call_node(obj, $3, NULL); }
  | IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); $$ = create_method_call_node(obj, $3, $5); }
  | THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON                 { ASTNode *thisObj = create_ast_leaf("ID",0,NULL,"this"); $$ = create_method_call_node(thisObj, $3, NULL); }
  | STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON           { $$ = create_var_decl_node($2, create_string_node($4)); }
  | INT IDENTIFIER ASSIGN expression SEMICOLON                  { $$ = create_var_decl_node($2, create_int_node($4->value)); }
  | FLOAT IDENTIFIER ASSIGN expression SEMICOLON                { $$ = create_var_decl_node($2, create_float_node($4->value)); }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON                  { $$ = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, $2), $4); }
  | IDENTIFIER ASSIGN expression SEMICOLON                      { $$ = create_ast_node("ASSIGN", create_ast_leaf("VAR",0,NULL,$1), $3); }
  | PRINT LPAREN expression RPAREN SEMICOLON                    { $$ = create_ast_node("PRINT", $3, NULL); }
  | PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON     { ASTNode *obj = create_ast_leaf("ID",0,NULL,$3); ASTNode *attr = create_ast_leaf("ID",0,NULL,$5); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("PRINT", access, NULL); }
  | FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  { $$ = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,$3), create_ast_leaf("NUMBER",$5,NULL,NULL), $7, $9, $12); }
  | NEW IDENTIFIER LPAREN RPAREN SEMICOLON                          { ClassNode *cls = find_class($2); if (!cls) { printf("Error: Clase '%s' no definida.\n", $2); $$ = NULL; } else { $$ = (ASTNode *)create_object_with_args(cls, NULL); printf("[DEBUG] Creación de objeto: %s\n", $2); } }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON    { ClassNode *cls = find_class($5); if (!cls) { printf("Error: Clase '%s' no definida.\n", $5); $$ = NULL; } else { $$ = create_var_decl_node($2, create_object_with_args(cls, NULL)); } }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  { ClassNode *cls = find_class($5); if (!cls) { printf("Error: Clase '%s' no definida.\n", $5); $$ = NULL; } else { $$ = create_var_decl_node($2, create_object_with_args(cls, $7)); } }
  | DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON                { printf(" [DEBUG] Reconocido DATASET\n"); $$ = create_dataset_node($2, $4); }
  | PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON     { printf(" [DEBUG] Reconocido PREDICT\n"); $$ = create_predict_node($3, $5); }
  | VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON { printf(" [DEBUG] Reconocido PREDICT\n"); ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); $$ = create_method_call_node(obj, "predict", NULL); $$ = create_predict_node($6, $8); }
  | DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON            { $$ = create_dataset_node($2, $4); }
  | PLOT LPAREN expression_list RPAREN SEMICOLON                { $$ = create_ast_node("PLOT", $3, NULL); }
  | MODEL IDENTIFIER LBRACKET layer_list RBRACKET               { printf("[DEBUG] Reconocido MODEL\n"); printf("[DEBUG] Nombre del modelo: %s\n", $2); ASTNode *layer = $4; int capa_index = 0; while (layer) { if (strcmp(layer->type, "LAYER") == 0) { printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n", capa_index++, layer->id, layer->value, layer->str_value); } layer = layer->right; } ASTNode *modelNode = create_model_node($2, $4); printf("[DEBUG] Nodo de modelo creado: type=%s, id=%s\n", modelNode->type, modelNode->id); }
  | LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  { $$ = create_layer_node($2, $4, $6); }
  | TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON  { $$ = create_train_node($3, $5, $7); }
  | IDENTIFIER ASSIGN NUMBER                                    { $$ = create_train_option_node($1, $3); }
  | LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON {
    ClassNode* cls = find_class($7);
        if (!cls) {
            printf("Clase '%s' no encontrada.\n", $7);
            $$ = NULL;
        } else {
            ASTNode* list = from_csv_to_list($5, cls);
            $$ = create_var_decl_node($2, list);
        }
    }
  ;

statement_list:
    statement_list statement  { $$ = create_ast_node("STATEMENT_LIST", $1, $2); }
  | statement                { $$ = $1; }
  ;

expression_list:
    expression                                { $$ = $1; }
  | expression_list COMMA expression          { $$ = add_statement($1, $3); }
  ;

expr_list:
    expression               { $$ = $1; }
  | expr_list COMMA expression { $$ = append_to_list($1, $3); }
  ;

list_literal:
    LSBRACKET expr_list RSBRACKET  {        
        $$ = create_list_node($2); }
  ;

lambda:
    IDENTIFIER ARROW                { printf(" [DEBUG] Reconocido LAMBDA\n"); }
  | LPAREN IDENTIFIER RPAREN ARROW expression  { $$ = create_lambda_node($2, $5); free($2); }
  ;

arg_list:
    expression
  | arg_list COMMA expression  {  $$ = append_argument_raw($1, $3); }
  | /* vacío */                { $$ = NULL; }
  
  ;

  object_expression
  : NEW IDENTIFIER LPAREN expression_list RPAREN
  {
        ClassNode *cls = find_class($2);
        if (!cls) {
            printf("Error: Clase '%s' no encontrada.\n", $2);
            $$ = NULL;
        } else {
            $$ = create_object_with_args(cls, $4);
        }
  }
;
object_list
    : object_expression
      {
        /*printf(" [DEBUG] Reconocido OBJECT_EXPRESSION\n");*/
         $$ = create_list_node($1); 
    } 
    | object_list COMMA object_expression
      { $$ = append_to_list($1, $3); }
;

lambda_expression
    : IDENTIFIER ARROW expression
      {
        $$ = create_lambda_node($1, $3); // crea un nodo tipo LAMBDA
      }
;

more_args:
    /* vacío */               { $$ = NULL; }
  | COMMA expression more_args  { $$ = add_argument($2, $3); }
  ;

%%

void clean_generated_code() {    
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

