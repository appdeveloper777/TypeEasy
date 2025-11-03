%{   
    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    #include <locale.h>
    #include <time.h>

    ASTNode *root;
    extern int yylineno;
    extern FILE *yyin;
    void yyerror(const char *s);
    ClassNode *last_class = NULL;
    int yylex();
    void generate_code(const char* code);
    void clean_generated_code();
    int g_debug_mode = 0;
    ASTNode* parse_file(FILE* file);
%}

%union {
    int ival;
    char *sval;
    ASTNode *node;
    ParameterNode *pnode;
}

%token <sval> INT STRING FLOAT FLOAT_LITERAL LAYER LSBRACKET RSBRACKET
%token DATASET MODEL TRAIN PREDICT FROM PLOT ARROW IN LAMBDA
%token       VAR ASSIGN PRINT PRINTLN FOR LPAREN RPAREN SEMICOLON CONCAT
%token       PLUS MINUS MULTIPLY DIVIDE LBRACKET RBRACKET
%token       CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT RETURN
%token <sval> IDENTIFIER STRING_LITERAL CONST
%token <ival> NUMBER
%token IF ELSE
%token AGENT LISTENER BRIDGE STATE MATCH CASE
%token NODE
%type <sval> method_name
%type <node>  expression_list var_decl constructor_decl return_stmt arg_list more_args lambda_expression
%type <pnode> parameter_decl parameter_list
%type <node> list_literal
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
%type <node> if_statement
%type <node> match_statement case_clause case_list
%type <node> statement expression program statement_list class_decl class_body
%type <node> attribute_decl method_decl
%type <node> agent_decl agent_body listener_decl bridge_decl
%type <node> object_literal key_value_list key_value_pair
%type <node> node_decl
%type <node> state_decl
%right ARROW
%nonassoc GT LT EQ GT_EQ LT_EQ, DIFF

%%

program:
        program statement       { $$ = create_ast_node("STATEMENT_LIST", $1, $2); root = $$; }
        | program class_decl      { $$ = $1; root = $$; }
        | program agent_decl      { $$ = create_ast_node("AGENT_LIST", $1, $2); root = $$; }
        | program bridge_decl     { $$ = $1; root = $$; }
        | statement               { $$ = $1; root = $$; }
        | class_decl              { $$ = NULL; }
        | agent_decl              { $$ = $1; root = $$; }
        | bridge_decl             { $$ = NULL; }
;

class_decl:
        CLASS IDENTIFIER { last_class = create_class($2); add_class(last_class); } 
        LBRACKET class_body RBRACKET { $$ = NULL; }
;

agent_decl:
    AGENT IDENTIFIER LBRACKET agent_body RBRACKET
        { $$ = create_agent_node($2, $4); }
    ;

agent_body:
    /* NADA - Regla vacía para permitir {} */
        { $$ = NULL; }
    | listener_decl
        { $$ = $1; }
    | agent_body listener_decl
        { $$ = create_ast_node("LISTENER_LIST", $1, $2); }
    ;

listener_decl:
    LISTENER expression LBRACKET statement_list RBRACKET
        { $$ = create_listener_node($2, $4); }
    ;

bridge_decl:
    BRIDGE IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_bridge_node($2, $4); }
    ;

state_decl:
    STATE IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_state_decl_node($2, $4); }
    ;

node_decl:
    NODE IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_var_decl_node($2, $4); }
    ;

object_literal:
    LBRACKET RBRACKET   { $$ = create_object_literal_node(NULL); } /* Objeto vacío {} */
    | LBRACKET key_value_list RBRACKET
        { $$ = create_object_literal_node($2); }
    ;

key_value_list:
    key_value_pair
        { $$ = $1; }
    | key_value_list COMMA key_value_pair
        { $$ = append_kv_pair($1, $3); }
    ;

key_value_pair:
    IDENTIFIER COLON expression
        { $$ = create_kv_pair_node($1, $3); }
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
 | list_literal   
|expression GT expression    { $$ = create_ast_node("GT", $1, $3); }
  | expression LT expression      { $$ = create_ast_node("LT", $1, $3); }
  | expression EQ expression      { $$ = create_ast_node("EQ", $1, $3); }
  | expression GT_EQ expression   { $$ = create_ast_node("GT_EQ", $1, $3); }
  | expression LT_EQ expression   { $$ = create_ast_node("LT_EQ", $1, $3); }
  | expression DIFF expression   { $$ = create_ast_node("DIFF", $1, $3); }
  | expression LSBRACKET expression RSBRACKET
      { $$ = create_access_node($1, $3); }
  | object_literal
      { $$ = $1; }
  | expression DOT IDENTIFIER LPAREN RPAREN
      { /* ARREGLADO: El printf aquí causaba warnings. $1 es un ASTNode*, no un string. 
        printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", $1->id, $3); */
        $$ = create_method_call_node($1, $3, NULL); }
  | expression DOT IDENTIFIER LPAREN expression_list RPAREN
      { $$ = create_method_call_node($1, $3, $5); }
  | expression DOT IDENTIFIER LPAREN lambda RPAREN
      {  if (strcmp($3, "filter")==0) { $$ = create_list_function_call_node($1, $3, $5); } 
         else { $$ = create_method_call_node($1, $3, NULL); } free($3); }
  | expression DOT IDENTIFIER
      { ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3); 
        $$ = create_ast_node("ACCESS_ATTR", $1, attr); }
  | THIS DOT IDENTIFIER
      { $$ = create_ast_node("ACCESS_ATTR", create_ast_leaf("ID", 0, NULL, "this"), create_ast_leaf("ID", 0, NULL, $3)); }
  | THIS DOT IDENTIFIER LPAREN RPAREN
      { $$ = create_method_call_node(create_ast_leaf("ID", 0, NULL, "this"), $3, NULL); }
  | IDENTIFIER
      { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
  | NUMBER
      { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }
  | FLOAT_LITERAL
      { $$ = create_ast_leaf("FLOAT", 0, $1, NULL); }
  | STRING_LITERAL
      { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
  | CONCAT LPAREN expression_list RPAREN
      { $$ = create_function_call_node("concat", $3); } /* ARREGLADO: printf eliminado */
  | expression PLUS expression
      { $$ = create_ast_node("ADD", $1, $3); }
  | expression MINUS expression
      { $$ = create_ast_node("SUB", $1, $3); }
  | expression MULTIPLY expression
      { $$ = create_ast_node("MUL", $1, $3); }
  | expression DIVIDE expression
      { $$ = create_ast_node("DIV", $1, $3); }
  | LPAREN expression RPAREN
      { $$ = $2; }
  | NEW IDENTIFIER LPAREN RPAREN
      { ClassNode *cls = find_class($2);
        if (!cls) { printf("Error: Clase '%s' no definida.\n", $2); $$ = NULL; } 
        else { $$ = (ASTNode *)create_object_with_args(cls, NULL); } }
  | NEW IDENTIFIER LPAREN expr_list RPAREN 
      { ClassNode *cls = find_class($2);
        if (!cls) { fprintf(stderr, "Error: clase '%s' no encontrada\n", $2); exit(1); } 
        $$ = create_object_with_args(cls, $4); free($2); }
;

var_decl:
    LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN SEMICOLON
      { ASTNode *listExpr = $6; ASTNode *lambda = $8; 
        ASTNode *filterCall = create_list_function_call_node(listExpr, $4, lambda); 
        filterCall->type = strdup("FILTER_CALL"); 
        $$ = create_var_decl_node($2, filterCall); }
  | LET IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | STRING IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("STRING"); $$ = decl; }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | CONST INT IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($3, $5); decl->value = 1; /* Marcar como const */ decl->str_value = strdup("INT"); $$ = decl; }
  | CONST IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($2, $4); decl->value = 1; /* Marcar como const */ $$ = decl; }
  | INT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("INT"); $$ = decl; }
  | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  | THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
;

statement:
    FOR LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  { $$ = create_for_in_node($4, $6, $9); }
  | LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$4); $$ = create_var_decl_node($2, obj); }
  | RETURN expression SEMICOLON  { $$ = create_return_node($2); }
  | state_decl
  | node_decl
  | var_decl
  | STRING STRING expression SEMICOLON                          { char buffer[2048]; sprintf(buffer,"#include \"easyspark/dataframe.hpp\"\n..."); generate_code(buffer); }
  | IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("IDENTIFIER",0,NULL,$1); $$ = create_method_call_node(obj, $3, NULL); }
  | IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); $$ = create_method_call_node(obj, $3, $5); }
  | THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON                 { ASTNode *thisObj = create_ast_leaf("ID",0,NULL,"this"); $$ = create_method_call_node(thisObj, $3, NULL); }
  | STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON           { $$ = create_var_decl_node($2, create_string_node($4)); }
  | INT IDENTIFIER ASSIGN expression SEMICOLON                  { $$ = create_var_decl_node($2, create_int_node($4->value)); }
  | FLOAT IDENTIFIER ASSIGN expression SEMICOLON                { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("FLOAT"); $$ = decl; }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON                  { $$ = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, $2), $4); }
  | if_statement
  | match_statement
  | IDENTIFIER ASSIGN expression SEMICOLON       
    { $$ = create_ast_node("ASSIGN", create_ast_leaf("IDENTIFIER",0,NULL,$1), $3); }
  | PRINTLN LPAREN expression RPAREN SEMICOLON
    { $$ = create_ast_node("PRINTLN", $3, NULL); }
  | PRINTLN LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON
    { ASTNode *obj = create_ast_leaf("ID",0,NULL,$3); ASTNode *attr = create_ast_leaf("ID",0,NULL,$5); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("PRINTLN", access, NULL); }
  | PRINT LPAREN expression RPAREN SEMICOLON
    { $$ = create_ast_node("PRINT", $3, NULL); }
  | PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON
    { ASTNode *obj = create_ast_leaf("ID",0,NULL,$3); ASTNode *attr = create_ast_leaf("ID",0,NULL,$5); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("PRINT", access, NULL); }
  | FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET
    { $$ = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,$3), create_ast_leaf("NUMBER",$5,NULL,NULL), $7, $9, $12); }
  | NEW IDENTIFIER LPAREN RPAREN SEMICOLON
    { ClassNode *cls = find_class($2); if (!cls) { printf("Error: Clase '%s' no definida.\n", $2); $$ = NULL; } else { $$ = (ASTNode *)create_object_with_args(cls, NULL); } }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON
    { ClassNode *cls = find_class($5); if (!cls) { printf("Error: Clase '%s' no definida.\n", $5); $$ = NULL; } else { $$ = create_var_decl_node($2, create_object_with_args(cls, NULL)); } }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
    { ClassNode *cls = find_class($5); if (!cls) { printf("Error: Clase '%s' no definida.\n", $5); $$ = NULL; } else { $$ = create_var_decl_node($2, create_object_with_args(cls, $7)); } }
  | DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON
    { $$ = create_dataset_node($2, $4); }
  | PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON
    { $$ = create_predict_node($3, $5); }
  | VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON
    { ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); $$ = create_method_call_node(obj, "predict", NULL); $$ = create_predict_node($6, $8); }
  | PLOT LPAREN expression_list RPAREN SEMICOLON
    { $$ = create_ast_node("PLOT", $3, NULL); }
  | MODEL IDENTIFIER LBRACKET layer_list RBRACKET
    { ASTNode *layer = $4; int capa_index = 0;
      while (layer) { if (strcmp(layer->type, "LAYER") == 0) { printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n", capa_index++, layer->id, layer->value, layer->str_value); } layer = layer->right; } 
      ASTNode *modelNode = create_model_node($2, $4); }
  | LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON
    { $$ = create_layer_node($2, $4, $6); }
  | TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON
    { $$ = create_train_node($3, $5, $7); }
  | IDENTIFIER ASSIGN NUMBER
    { $$ = create_train_option_node($1, $3); }
  | LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON 
    { ClassNode* cls = find_class($7);
      if (!cls) { printf("Clase '%s' no encontrada.\n", $7); $$ = NULL; } 
      else { ASTNode* list = from_csv_to_list($5, cls); $$ = create_var_decl_node($2, list); } }
; 

if_statement:
    IF LPAREN expression RPAREN LBRACKET statement_list RBRACKET
    { $$ = create_if_node($3, $6, NULL); }
    | IF LPAREN expression RPAREN LBRACKET statement_list RBRACKET 
      ELSE LBRACKET statement_list RBRACKET
    { $$ = create_if_node($3, $6, $10); }
;

match_statement:
    MATCH LPAREN expression RPAREN LBRACKET case_list RBRACKET
        { $$ = create_match_node($3, $6); }
    ;

case_list:
    /* empty */ { $$ = NULL; }
    | case_list case_clause { $$ = append_case_clause($1, $2); }
    ;

case_clause:
    CASE expression COLON statement_list
        { $$ = create_case_node($2, $4); }
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
    expression               { $$ = $1; $1->next = NULL; }
  | expr_list COMMA expression { $$ = append_to_list_parser($1, $3); }
  ;

list_literal:
    LSBRACKET RSBRACKET            { $$ = create_list_node(NULL); }
    | LSBRACKET expr_list RSBRACKET  { $$ = create_list_node($2); }
  ;

lambda:
    IDENTIFIER ARROW                { /*printf(" [DEBUG] Reconocido LAMBDA\n");*/ }
  | LPAREN IDENTIFIER RPAREN ARROW expression  { $$ = create_lambda_node($2, $5); free($2); }
  ;

arg_list:
    expression
  | arg_list COMMA expression  {  $$ = append_argument_raw($1, $3); }
  | /* vacío */                { $$ = NULL; }
  ;

object_expression: 
    NEW IDENTIFIER LPAREN expression_list RPAREN
    {
        ClassNode *cls = find_class($2);
        if (!cls) { printf("Error: Clase '%s' no encontrada.\n", $2); $$ = NULL; } 
        else { $$ = create_object_with_args(cls, $4); }
    }
;

object_list: 
    object_expression
    { $$ = create_list_node($1); } 
    | object_list COMMA object_expression
    { $$ = append_to_list($1, $3); }
;

lambda_expression:
    IDENTIFIER ARROW expression
    { $$ = create_lambda_node($1, $3); }
;

more_args:
    /* vacío */               { $$ = NULL; }
  | COMMA expression more_args  { $$ = add_argument($2, $3); }
  ;
%%

void clean_generated_code() {    
    FILE* out = fopen("generated.cpp", "w");
    if (out != NULL) {
        fprintf(out, "");
        fclose(out);
    }
}

void generate_code(const char* code) {
    FILE* out = fopen("generated.cpp", "w");
    if (out != NULL) {
        fprintf(out, "%s\n", code);
        fclose(out);
    }
}

void yyerror(const char *s) {
    extern char *yytext;
    printf("Error de sintaxis en linea %d: %s\n", yylineno, s);
    if (yytext) {
        printf("Cerca de: '%s'\n", yytext);
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) printf("  ");
    if (node->type && strcmp(node->type, "FOR") == 0) {
        printf(">>> FOR DETECTADO <<<\n");
    }
    if (node->type) {
        printf("Node type: %s", node->type);
    } else {
        printf("Node");
    }
    if (node->value)
        printf(", value: %d", node->value);
    if (node->str_value)
        printf(", str: %s", node->str_value);
    if (node->id)
        printf(", id: %s", node->id);
    printf("\n");
    print_ast(node->left, indent + 1);
    print_ast(node->right, indent + 1);
}

ASTNode* parse_file(FILE* file) {
    root = NULL;
    yyin = file;
    int parse_result = yyparse();
    if (parse_result != 0) {
        return NULL;
    }
    return root;
}