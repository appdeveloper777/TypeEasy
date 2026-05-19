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

%token <sval> INT STRING FLOAT FLOAT_LITERAL LAYER LSBRACKET RSBRACKET CACHE
%token DATASET MODEL TRAIN PREDICT FROM PLOT ARROW IN LAMBDA CONCAT JSON XML HTTPGET HTTPPOST HTTPPUT HTTPDELETE HTTPPATCH
%token AS
%token       VAR ASSIGN PRINT PRINTLN FOR FOREACH LPAREN RPAREN SEMICOLON CONCAT FPRINT FPRINTLN 
%token       PLUS MINUS MULTIPLY DIVIDE LBRACKET RBRACKET
%token       CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT RETURN
/* Fase 2: tokens MYSQL/POSTGRES/SQLSERVER and ORM_QUERY removed -
 * those names lex as IDENTIFIER and dispatch through the runtime
 * registry (te_builtins.c). */
%token       EXTENDS
%token       VOID DYNAMIC
%token       NULLTOK QMARK
%token       TRY CATCH FINALLY THROW
%token       PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN INCREMENT DECREMENT
%token       WHILE BREAK CONTINUE
%token       AND OR NOT QDOT QQ
%token       PERCENT SHL SHR BIT_AND BIT_OR BIT_XOR BIT_NOT
%token       FN
%token <sval> IDENTIFIER STRING_LITERAL CONST
%token <sval> STRING_INTERP
%token <ival> NUMBER
%token IF ELSE
%token AGENT LISTENER BRIDGE STATE MATCH CASE
%token NODE ENDPOINT
%type <sval> method_name
%type <sval> method_return_type
%type <node>  expression_list var_decl constructor_decl return_stmt arg_list more_args lambda_expression httpget_method_decl lambda_value
%type <sval> lambda_param_list
%type <pnode> parameter_decl parameter_list
%type <node> list_literal
%type <node> object_expression object_list
%type <node> dataset_decl
%type <node> model_decl func_call_expr
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
%type <node> endpoint_decl endpoint_methods endpoint_method
%type <ival> cache_decorator
%right ARROW
%left QQ
%left OR
%left AND
%right NOT
%nonassoc GT LT EQ GT_EQ LT_EQ, DIFF
%nonassoc IN
%left BIT_OR
%left BIT_XOR
%left BIT_AND
%left SHL SHR
%left PLUS MINUS
%left MULTIPLY DIVIDE PERCENT
%right UMINUS BIT_NOT

%%

program:
     program statement       { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; root = $$; }  
    | program class_decl      { $$ = $1; root = $$; }
    | program agent_decl      { $$ = $1 ? create_ast_node("AGENT_LIST", $1, $2) : $2; root = $$; }
    | program bridge_decl     { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; root = $$; }
    | program endpoint_decl   { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; root = $$; }
    | endpoint_decl           { $$ = $1; root = $$; }
    | cache_decorator endpoint_decl { if ($2 && $2->extra) ((MethodNode*)$2->extra)->cache_ttl = $1; $$ = $2; root = $$; }
    | httpget_method_decl     { $$ = $1; root = $$; }
    | cache_decorator httpget_method_decl { if ($2 && $2->extra) ((MethodNode*)$2->extra)->cache_ttl = $1; $$ = $2; root = $$; }
    | class_decl              { $$ = $1; root = $$; }
    | bridge_decl             { $$ = $1; root = $$; }   
    | statement               { $$ = $1; root = $$; }
;

cache_decorator:
    CACHE LPAREN NUMBER RPAREN { $$ = $3; }



endpoint_decl:
    ENDPOINT LBRACKET endpoint_methods RBRACKET
        { $$ = create_ast_node("ENDPOINT_DECL", NULL, NULL); }
    ;

endpoint_methods:
    endpoint_method
        { $$ = NULL; }
    | endpoint_methods endpoint_method
        { $$ = NULL; }
    ;

endpoint_method:
    LSBRACKET HTTPGET LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7);
        m->body = $12;
        m->params = $9;
        m->route_path = strdup($4);
        m->http_method = strdup("GET");
        m->cache_ttl = 0;  // No cache by default
        m->next = global_methods;
        global_methods = m;
        $$ = NULL;
    }
    | LSBRACKET HTTPPOST LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("POST");
        m->cache_ttl = 0; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPPUT LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("PUT");
        m->cache_ttl = 0; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPDELETE LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("DELETE");
        m->cache_ttl = 0; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPPATCH LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("PATCH");
        m->cache_ttl = 0; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPGET LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8);
        m->body = $13;
        m->params = $10;
        m->route_path = strdup($5);
        m->http_method = strdup("GET");
        m->cache_ttl = $1;
        m->next = global_methods;
        global_methods = m;
        $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPOST LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("POST");
        m->cache_ttl = $1; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPUT LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("PUT");
        m->cache_ttl = $1; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPDELETE LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("DELETE");
        m->cache_ttl = $1; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPATCH LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("PATCH");
        m->cache_ttl = $1; m->next = global_methods; global_methods = m; $$ = NULL;
    }
    ;

httpget_method_decl:
     LSBRACKET HTTPGET RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
            MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
            m->name = strdup($4);
            m->body = $9;
            m->params = $6;
            m->next = global_methods;
            global_methods = m;            
            $$ = NULL;           
     }
    ;

class_decl:
        CLASS IDENTIFIER { last_class = create_class($2); add_class(last_class); } 
        LBRACKET class_body RBRACKET { $$ = NULL; }
    |   CLASS IDENTIFIER EXTENDS IDENTIFIER {
            last_class = create_class($2);
            add_class(last_class);
            inherit_from(last_class, $4);
        }
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
    | BRIDGE IDENTIFIER ASSIGN NEW expression SEMICOLON
        { $$ = create_bridge_node($2, $5); }
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
    | STRING_LITERAL COLON expression
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
  | IDENTIFIER COLON FLOAT SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "float"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", $1); } }
  | IDENTIFIER COLON INT QMARK SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "int?"); } }
  | IDENTIFIER COLON STRING QMARK SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "string?"); } }
  | IDENTIFIER COLON FLOAT QMARK SEMICOLON  { if (last_class) { add_attribute_to_class(last_class, $1, "float?"); } }
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

method_return_type:
    INT      { $$ = strdup("int"); }
  | STRING   { $$ = strdup("string"); }
  | FLOAT    { $$ = strdup("float"); }
  | VOID     { $$ = strdup("void"); }
  | DYNAMIC  { $$ = strdup("dynamic"); }
  | INT QMARK      { $$ = strdup("int?"); }
  | STRING QMARK   { $$ = strdup("string?"); }
  | FLOAT QMARK    { $$ = strdup("float?"); }
  | DYNAMIC QMARK  { $$ = strdup("dynamic?"); }
  | IDENTIFIER QMARK { char *t = malloc(strlen($1)+2); sprintf(t,"%s?",$1); free($1); $$ = t; }
  ;

method_decl:
    IDENTIFIER LPAREN RPAREN COLON method_return_type LBRACKET statement_list RBRACKET  { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1); } else { add_method_to_class(last_class, $1, NULL, $7, $5); } $$ = NULL; }
  | IDENTIFIER LPAREN parameter_list RPAREN COLON method_return_type LBRACKET statement_list RBRACKET  { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", $1); } else { add_method_to_class(last_class, $1, $3, $8, $6); } $$ = NULL; }
  ;

expression:
  func_call_expr
 | list_literal   
 | lambda_value
|expression GT expression    { $$ = create_ast_node("GT", $1, $3); }
  | expression LT expression      { $$ = create_ast_node("LT", $1, $3); }
  | expression EQ expression      { $$ = create_ast_node("EQ", $1, $3); }
  | expression GT_EQ expression   { $$ = create_ast_node("GT_EQ", $1, $3); }
  | expression LT_EQ expression   { $$ = create_ast_node("LT_EQ", $1, $3); }
  | expression DIFF expression   { $$ = create_ast_node("DIFF", $1, $3); }
  | expression AND expression    { $$ = create_ast_node("AND", $1, $3); }
  | expression OR expression     { $$ = create_ast_node("OR", $1, $3); }
  | NOT expression               { $$ = create_ast_node("NOT", $2, NULL); }
  | expression QQ expression     { $$ = create_ast_node("NULL_COALESCE", $1, $3); }
  | expression QDOT IDENTIFIER LPAREN RPAREN              { ASTNode *call = create_method_call_node($1, $3, NULL); call->value = 1; /* null-safe flag */ $$ = call; }
  | expression QDOT IDENTIFIER LPAREN expression_list RPAREN { ASTNode *call = create_method_call_node($1, $3, $5); call->value = 1; $$ = call; }
  | expression QDOT IDENTIFIER   { ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3); ASTNode *n = create_ast_node("ACCESS_ATTR", $1, attr); n->value = 1; $$ = n; }
  | expression LSBRACKET expression RSBRACKET       { $$ = create_access_node($1, $3); }
  | object_literal      { $$ = $1; }
  | expression DOT IDENTIFIER LPAREN RPAREN     {         $$ = create_method_call_node($1, $3, NULL); }
  | expression DOT IDENTIFIER LPAREN expression_list RPAREN       { $$ = create_method_call_node($1, $3, $5); }
  | expression DOT IDENTIFIER LPAREN lambda RPAREN       {
         /* v0.0.11: pasar el lambda como argumento a CUALQUIER método higher-order
          * (map, filter, where, select, sumBy, orderBy, groupBy, etc.). Antes solo
          * 'filter' funcionaba; el lambda se descartaba para todo lo demás. */
         if (strcmp($3, "filter")==0) { $$ = create_list_function_call_node($1, $3, $5); }
         else { $$ = create_method_call_node($1, $3, $5); }
         free($3); }
  | expression DOT IDENTIFIER
      { ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3); 
        $$ = create_ast_node("ACCESS_ATTR", $1, attr); }
  | THIS DOT IDENTIFIER       { $$ = create_ast_node("ACCESS_ATTR", create_ast_leaf("ID", 0, NULL, "this"), create_ast_leaf("ID", 0, NULL, $3)); }
  | THIS DOT IDENTIFIER LPAREN RPAREN       { $$ = create_method_call_node(create_ast_leaf("ID", 0, NULL, "this"), $3, NULL); }
  | IDENTIFIER       { $$ = create_ast_leaf("IDENTIFIER", 0, NULL, $1); }
  | NUMBER       { $$ = create_ast_leaf("NUMBER", $1, NULL, NULL); }
  | FLOAT_LITERAL       { $$ = create_ast_leaf("FLOAT", 0, $1, NULL); }
  | STRING_LITERAL       { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
  | STRING_INTERP        { $$ = create_ast_leaf("STRING_INTERP", 0, $1, NULL); }
  | NULLTOK       { $$ = create_ast_leaf("NULL", 0, NULL, NULL); }
  | CONCAT LPAREN expression_list RPAREN       { $$ = create_function_call_node("concat", $3); } /* ARREGLADO: printf eliminado */
  | expression PLUS expression       { $$ = create_ast_node("ADD", $1, $3); }
  | expression MINUS expression       { $$ = create_ast_node("SUB", $1, $3); }
  | expression MULTIPLY expression       { $$ = create_ast_node("MUL", $1, $3); }
  | expression DIVIDE expression       { $$ = create_ast_node("DIV", $1, $3); }
  | expression PERCENT expression      { $$ = create_ast_node("MOD", $1, $3); }
  | expression BIT_AND expression      { $$ = create_ast_node("BIT_AND", $1, $3); }
  | expression BIT_OR  expression      { $$ = create_ast_node("BIT_OR",  $1, $3); }
  | expression BIT_XOR expression      { $$ = create_ast_node("BIT_XOR", $1, $3); }
  | expression SHL     expression      { $$ = create_ast_node("SHL", $1, $3); }
  | expression SHR     expression      { $$ = create_ast_node("SHR", $1, $3); }
  | expression IN      expression      { $$ = create_ast_node("IN",  $1, $3); }
  | MINUS expression %prec UMINUS      { $$ = create_ast_node("NEG", $2, NULL); }
  | BIT_NOT expression                 { $$ = create_ast_node("BIT_NOT", $2, NULL); }
  | LPAREN expression RPAREN       { $$ = $2; }
  | NEW IDENTIFIER LPAREN RPAREN 
      { /* Fase 2: NEW Foo() — class instantiation, or builtin call if no class. */
        ClassNode *cls = find_class($2);
        if (cls) { $$ = (ASTNode *)create_object_with_args(cls, NULL); }
        else     { $$ = create_call_node($2, NULL); } }
  | NEW IDENTIFIER LPAREN expression_list RPAREN 
      { /* Fase 2: NEW Foo(args) — class instantiation, or builtin call if no class.
         * Uses expression_list (chained via ->right) so the constructor invocation
         * walker can iterate args via arg->right. expr_list (used for list literals)
         * chains via ->next now, which is incompatible with the constructor walker. */
        ClassNode *cls = find_class($2);
        if (cls) { $$ = create_object_with_args(cls, $4); free($2); }
        else     { $$ = create_call_node($2, $4); } }
 | JSON LPAREN IDENTIFIER RPAREN  {       $$ = create_call_node_return_json($3, create_ast_leaf("json", 0, NULL, $3));}
| XML LPAREN IDENTIFIER RPAREN  {      $$ = create_call_node_return_xml($3, create_ast_leaf("xml", 0, NULL, $3));}
;

var_decl:
    LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
      { ASTNode *args = $6; ASTNode *first = args; ASTNode *second = args ? args->right : NULL; ASTNode *third = second ? second->right : NULL;
        ASTNode *call;
        if (second && !third && second->type && strcmp(second->type, "LAMBDA") == 0 && (strcmp($4, "filter") == 0 || strcmp($4, "map") == 0)) {
          args->right = NULL;
          call = create_list_function_call_node(first, $4, second);
        } else {
          call = create_call_node($4, args);
        }
        ASTNode* d = create_var_decl_node($2, call); d->value = 1; /* let = immutable */ $$ = d; }
  | VAR IDENTIFIER ASSIGN IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
      { ASTNode *args = $6; ASTNode *first = args; ASTNode *second = args ? args->right : NULL; ASTNode *third = second ? second->right : NULL;
        ASTNode *call;
        if (second && !third && second->type && strcmp(second->type, "LAMBDA") == 0 && (strcmp($4, "filter") == 0 || strcmp($4, "map") == 0)) {
          args->right = NULL;
          call = create_list_function_call_node(first, $4, second);
        } else {
          call = create_call_node($4, args);
        }
        $$ = create_var_decl_node($2, call); }



  | LET IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* d = create_var_decl_node($2, $4); d->value = 1; /* let = immutable */ $$ = d; }
  | STRING IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("STRING"); $$ = decl; }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | CONST INT IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($3, $5); decl->value = 1; /* Marcar como const */ decl->str_value = strdup("INT"); $$ = decl; }
  | CONST IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($2, $4); decl->value = 1; /* Marcar como const */ $$ = decl; }
  | INT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("INT"); $$ = decl; }
  | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  | THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
;

statement:
func_call_expr SEMICOLON { $$ = $1; }


|
        FOR LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  { ASTNode *n = create_for_in_node($4, $6, $9); if ($6 && $6->line > 0) n->line = $6->line; $$ = n; }
    | FOREACH LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  { ASTNode *n = create_for_in_node($4, $6, $9); if ($6 && $6->line > 0) n->line = $6->line; $$ = n; }
    | FOREACH LPAREN VAR IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  { ASTNode *n = create_for_in_node($4, $6, $9); if ($6 && $6->line > 0) n->line = $6->line; $$ = n; }
    | WHILE LPAREN expression RPAREN LBRACKET statement_list RBRACKET  { $$ = create_ast_node("WHILE", $3, $6); }
    | BREAK SEMICOLON     { $$ = create_ast_leaf("BREAK", 0, NULL, NULL); }
    | CONTINUE SEMICOLON  { $$ = create_ast_leaf("CONTINUE", 0, NULL, NULL); }
    | LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("IDENTIFIER",0,NULL,$4); ASTNode *call = create_method_call_node(obj, $6, NULL); ASTNode* d = create_var_decl_node($2, call); d->value = 1; /* let = immutable */ $$ = d; }
    | LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("IDENTIFIER",0,NULL,$4); ASTNode *call = create_method_call_node(obj, $6, $8); ASTNode* d = create_var_decl_node($2, call); d->value = 1; $$ = d; }
    | VAR IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("IDENTIFIER",0,NULL,$4); ASTNode *call = create_method_call_node(obj, $6, NULL); $$ = create_var_decl_node($2, call); }
    | VAR IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  { ASTNode *obj = create_ast_leaf("IDENTIFIER",0,NULL,$4); ASTNode *call = create_method_call_node(obj, $6, $8); $$ = create_var_decl_node($2, call); }
    | RETURN func_call_expr SEMICOLON { $$ = create_return_node($2); }
    | RETURN expression SEMICOLON  { $$ = create_return_node($2); }
    | THROW expression SEMICOLON   { $$ = create_ast_node("THROW", $2, NULL); }
    | TRY LBRACKET statement_list RBRACKET CATCH LPAREN IDENTIFIER RPAREN LBRACKET statement_list RBRACKET
        { ASTNode *n = create_ast_node("TRY_CATCH", $3, $10); n->id = strdup($7); $$ = n; }
    | TRY LBRACKET statement_list RBRACKET CATCH LPAREN IDENTIFIER RPAREN LBRACKET statement_list RBRACKET FINALLY LBRACKET statement_list RBRACKET
        { ASTNode *n = create_ast_node("TRY_CATCH", $3, $10); n->id = strdup($7); n->extra = $14; $$ = n; }
    | TRY LBRACKET statement_list RBRACKET FINALLY LBRACKET statement_list RBRACKET
        { ASTNode *n = create_ast_node("TRY_CATCH", $3, NULL); n->extra = $7; $$ = n; }


   |RETURN XML LPAREN expression RPAREN SEMICOLON { $$ = create_return_node(create_call_node("xml", $4)); }
   |RETURN XML LPAREN RPAREN SEMICOLON { $$ = create_return_node(create_call_node("xml", NULL)); }
   |RETURN JSON LPAREN expression RPAREN SEMICOLON { $$ = create_return_node(create_call_node("json", $4)); }
   |RETURN JSON LPAREN RPAREN SEMICOLON { $$ = create_return_node(create_call_node("json", NULL)); }


  | state_decl
  | node_decl
  | var_decl

   | IDENTIFIER LPAREN expression_list RPAREN SEMICOLON {           $$ = create_method_call_node_alone(NULL, $1, $3);        }
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
  | IDENTIFIER ASSIGN expression SEMICOLON           { $$ = create_ast_node("ASSIGN", create_ast_leaf("IDENTIFIER",0,NULL,$1), $3); }
  | IDENTIFIER PLUS_ASSIGN expression SEMICOLON      { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("ADD", id2, $3)); }
  | IDENTIFIER MINUS_ASSIGN expression SEMICOLON     { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("SUB", id2, $3)); }
  | IDENTIFIER STAR_ASSIGN expression SEMICOLON      { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("MUL", id2, $3)); }
  | IDENTIFIER SLASH_ASSIGN expression SEMICOLON     { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("DIV", id2, $3)); }
  | IDENTIFIER INCREMENT SEMICOLON                   { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); ASTNode *one = create_ast_leaf_number("NUMBER",1,NULL,NULL); $$ = create_ast_node("ASSIGN", id1, create_ast_node("ADD", id2, one)); }
  | IDENTIFIER DECREMENT SEMICOLON                   { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); ASTNode *one = create_ast_leaf_number("NUMBER",1,NULL,NULL); $$ = create_ast_node("ASSIGN", id1, create_ast_node("SUB", id2, one)); }
  | IDENTIFIER LSBRACKET expression RSBRACKET ASSIGN expression SEMICOLON
      { /* Fase 1b: arr[i] = x */
        ASTNode *base = create_ast_leaf("IDENTIFIER", 0, NULL, $1);
        ASTNode *access = create_access_node(base, $3);
        ASTNode *node = create_ast_node("INDEX_ASSIGN", access, $6);
        $$ = node; }
  | PRINTLN LPAREN expression RPAREN SEMICOLON    { $$ = create_ast_node("PRINTLN", $3, NULL); }
  | PRINT LPAREN expression RPAREN SEMICOLON    { $$ = create_ast_node("PRINT", $3, NULL); }
  | PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON    { ASTNode *obj = create_ast_leaf("ID",0,NULL,$3); ASTNode *attr = create_ast_leaf("ID",0,NULL,$5); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("PRINT", access, NULL); }
  
  | FPRINTLN LPAREN expression RPAREN SEMICOLON    { $$ = create_ast_node("FPRINTLN", $3, NULL); }
  | FPRINT LPAREN expression RPAREN SEMICOLON    { $$ = create_ast_node("FPRINT", $3, NULL); }
  | FPRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON    { ASTNode *obj = create_ast_leaf("ID",0,NULL,$3); ASTNode *attr = create_ast_leaf("ID",0,NULL,$5); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("FPRINT", access, NULL); }
  
  | FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET    { $$ = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,$3), create_ast_leaf("NUMBER",$5,NULL,NULL), $7, $9, $12); }
  | NEW IDENTIFIER LPAREN RPAREN SEMICOLON    { /* Fase 2: class or builtin */ ClassNode *cls = find_class($2); if (cls) $$ = (ASTNode *)create_object_with_args(cls, NULL); else $$ = create_call_node($2, NULL); }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON    { /* Fase 2: class or builtin */ ClassNode *cls = find_class($5); ASTNode *rhs = cls ? create_object_with_args(cls, NULL) : create_call_node($5, NULL); ASTNode* d = create_var_decl_node($2, rhs); d->value = 1; $$ = d; }
  | LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON    { /* Fase 2: class or builtin (let r = new sqlserver_query(...)). */ ClassNode *cls = find_class($5); ASTNode *rhs = cls ? create_object_with_args(cls, $7) : create_call_node($5, $7); ASTNode* d = create_var_decl_node($2, rhs); d->value = 1; $$ = d; }
  | DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON    { $$ = create_dataset_node($2, $4); }
  | PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON    { $$ = create_predict_node($3, $5); }
  | VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON    { ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); $$ = create_method_call_node(obj, "predict", NULL); $$ = create_predict_node($6, $8); }
  | PLOT LPAREN expression_list RPAREN SEMICOLON    { $$ = create_ast_node("PLOT", $3, NULL); }
  | MODEL IDENTIFIER LBRACKET layer_list RBRACKET        { ASTNode *layer = $4; (void)layer; ASTNode *modelNode = create_model_node($2, $4); }
  | LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON     { $$ = create_layer_node($2, $4, $6); }
  | TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON    { $$ = create_train_node($3, $5, $7); }
  | IDENTIFIER ASSIGN NUMBER    { $$ = create_train_option_node($1, $3); }
  | LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON 
    { ClassNode* cls = find_class($7);
      if (!cls) { printf("Clase '%s' no encontrada.\n", $7); $$ = NULL; } 
      else { ASTNode* list = from_csv_to_list($5, cls); ASTNode* d = create_var_decl_node($2, list); d->value = 1; /* let = immutable */ $$ = d; } }
  | VAR IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { printf("Clase '%s' no encontrada.\n", $7); $$ = NULL; }
      else { ASTNode* list = from_csv_to_list($5, cls); $$ = create_var_decl_node($2, list); } }
  | LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER AS IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { printf("Clase '%s' no encontrada.\n", $7); $$ = NULL; }
      else if (strcmp($9, "dataframe") != 0) { printf("Modificador desconocido tras 'as': '%s' (esperado 'dataframe').\n", $9); $$ = NULL; }
      else { ASTNode* list = from_csv_to_dataframe($5, cls); ASTNode* d = create_var_decl_node($2, list); d->value = 1; $$ = d; } }
  | VAR IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER AS IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { printf("Clase '%s' no encontrada.\n", $7); $$ = NULL; }
      else if (strcmp($9, "dataframe") != 0) { printf("Modificador desconocido tras 'as': '%s' (esperado 'dataframe').\n", $9); $$ = NULL; }
      else { ASTNode* list = from_csv_to_dataframe($5, cls); $$ = create_var_decl_node($2, list); } }
; 

func_call_expr:
    IDENTIFIER LPAREN RPAREN { $$ = create_call_node($1, NULL); }
    | IDENTIFIER LPAREN expression_list RPAREN { $$ = create_call_node($1, $3); }
    /* Fase 2: NEW IDENTIFIER (...) for non-class names is handled by the
     * `NEW IDENTIFIER LPAREN expression_list RPAREN` rule in `expression`
     * which falls back to create_call_node when find_class returns NULL. */
;

if_statement:
    IF LPAREN expression RPAREN LBRACKET statement_list RBRACKET
    { $$ = create_if_node($3, $6, NULL); }
    | IF LPAREN expression RPAREN LBRACKET statement_list RBRACKET 
      ELSE LBRACKET statement_list RBRACKET
    { $$ = create_if_node($3, $6, $10); }
    | IF LPAREN expression RPAREN LBRACKET statement_list RBRACKET 
      ELSE if_statement
    { $$ = create_if_node($3, $6, $9); }
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
        {             
            $$ = create_case_node($2, $4); 
        }
    ;

statement_list:
    statement_list statement  { $$ = create_ast_node("STATEMENT_LIST", $1, $2); }
  | statement                { $$ = create_ast_node("STATEMENT_LIST", $1, NULL); $$->next = NULL; }
  ;

expression_list:
        expression                                { 
        fflush(stdout); $$ = $1; }
    | expression_list COMMA expression          { 
         fflush(stdout); $$ = add_statement($1, $3); }
    | expression_list COMMA lambda_expression   {
         fflush(stdout); $$ = add_statement($1, $3); }
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
    IDENTIFIER ARROW expression                  { $$ = create_lambda_node($1, $3); free($1); }
  | LPAREN IDENTIFIER RPAREN ARROW expression    { $$ = create_lambda_node($2, $5); free($2); }
  ;

/* Fase B: lambda first-class con N parámetros y body expr o block. */
lambda_param_list:
    IDENTIFIER                                    { $$ = $1; }
  | lambda_param_list COMMA IDENTIFIER            {
        size_t la = strlen($1), lb = strlen($3);
        char *r = (char*)malloc(la + 1 + lb + 1);
        memcpy(r, $1, la); r[la] = '\1'; memcpy(r + la + 1, $3, lb); r[la + 1 + lb] = '\0';
        free($1); free($3); $$ = r; }
  ;

lambda_value:
    FN LPAREN RPAREN ARROW expression
        { $$ = create_lambda_multi_node("", $5); }
  | FN LPAREN RPAREN ARROW LBRACKET statement_list RBRACKET
        { $$ = create_lambda_multi_node("", $6); }
  | FN LPAREN lambda_param_list RPAREN ARROW expression
        { $$ = create_lambda_multi_node($3, $6); free($3); }
  | FN LPAREN lambda_param_list RPAREN ARROW LBRACKET statement_list RBRACKET
        { $$ = create_lambda_multi_node($3, $7); free($3); }
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
    extern int g_capture_errors;
    extern void te_capture_error(int line, const char *msg, const char *near);
    if (g_capture_errors) {
        te_capture_error(yylineno, s, yytext);
        return;
    }
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
    //fprintf(stderr, "[PARSER] parse_file() called, file=%p\n", (void*)file);
    
    if (!file) {
        fprintf(stderr, "[PARSER] ERROR: file is NULL\n");
        return NULL;
    }
    
    // Reset parser state
    root = NULL;

    /* === Bloque B: full lexer state reset between parse_file() calls ===
     * The api server (servidor_api) calls parse_file() in a loop over apis/*.te.
     * If a previous parse aborted mid-file (especially mid-include via "import"),
     * flex leaves leftover buffers on the include stack and the scanner state
     * machine in a non-INITIAL start condition. The next parse_file() then sees
     * residual lookahead/buffers and reports bogus "syntax error in line 1" on
     * a perfectly valid file (the cascade bug).
     *
     * te_lexer_full_reset() lives in parser.l where YY_CURRENT_BUFFER, BEGIN()
     * and start conditions are visible. It pops every buffer, closes leftover
     * include FILE*s, calls yyrestart(file) and resets to INITIAL.
     */
    extern void te_lexer_full_reset(FILE* file);
    te_lexer_full_reset(file);
    yyin = file;

    // Reset lexer state
    extern int yylineno;
    yylineno = 1;
    
    // Enable debug output
    extern int yydebug;
    yydebug = 0;  // Disable Bison trace
    
   // fprintf(stderr, "[PARSER] Calling yyparse()...\n");
    int parse_result = yyparse();
    //fprintf(stderr, "[PARSER] yyparse() returned: %d\n", parse_result);
    
    if (parse_result != 0) {
       // fprintf(stderr, "[PARSER] ERROR: yyparse() failed\n");
        return NULL;
    }
    
    if (!root) {
      //  fprintf(stderr, "[PARSER] WARNING: yyparse() succeeded but root is NULL\n");
        // If root is NULL but parsing succeeded (e.g. empty file or just comments), return a dummy node
        // But we fixed the grammar to return nodes for declarations, so this shouldn't happen for valid code.
        // However, let's be safe and return a dummy node if it still happens.
     //   fprintf(stderr, "[PARSER] Returning dummy root node to avoid failure.\n");
        root = create_ast_node("STATEMENT_LIST", NULL, NULL);
    }
    
    return root;
}
