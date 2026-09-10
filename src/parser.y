%{   
    #include <stdio.h>
    #include <stdlib.h>
    #include <locale.h>
    #include <time.h>
    #include "te_vm.h"
    #include "ast.h"
    #include "te_csv.h"
    #include "te_parse.h"

    /* Parser PURO (api.pure full) + scanner reentrante: sin globales. El estado
     * de la pasada vive en TeParseCtx (ctx, %parse-param) y el scanner es
     * `scanner` (%parse-param / %lex-param). yyerror recibe ambos. */
%}

%code requires {
    #include "te_parse.h"
    #ifndef YY_TYPEDEF_YY_SCANNER_T
    #define YY_TYPEDEF_YY_SCANNER_T
    typedef void *yyscan_t;
    #endif
}
%code {
    #include "lex.yy.h"
    static void yyerror(yyscan_t scanner, TeParseCtx *ctx, const char *s);
}

%define api.pure full
%lex-param   { yyscan_t scanner }
%parse-param { yyscan_t scanner } { TeParseCtx *ctx }

%union {
    long long ival;
    char *sval;
    ASTNode *node;
    ParameterNode *pnode;
}

%token <sval> INT STRING FLOAT FLOAT_LITERAL LAYER LSBRACKET RSBRACKET CACHE
%token DATASET MODEL TRAIN PREDICT FROM PLOT ARROW IN LAMBDA CONCAT JSON XML HTTPGET HTTPPOST HTTPPUT HTTPDELETE HTTPPATCH WEBSOCKET
%token ON_OPEN ON_MESSAGE ON_CLOSE
%token AS
%token       VAR ASSIGN PRINT PRINTLN FOR FOREACH LPAREN RPAREN SEMICOLON CONCAT FPRINT FPRINTLN 
%token       PLUS MINUS MULTIPLY DIVIDE LBRACKET RBRACKET
%token       CLASS CONSTRUCTOR THIS NEW LET COLON COMMA DOT RETURN
/* Fase 2: tokens MYSQL/POSTGRES/SQLSERVER and ORM_QUERY removed -
 * those names lex as IDENTIFIER and dispatch through the runtime
 * registry (te_builtins.c). */
%token       EXTENDS
%token       VOID DYNAMIC
%token       PUBLIC PRIVATE PROTECTED
%token       NULLTOK QMARK
%token       TRUETOK FALSETOK
%token <sval> BOOLTYPE DATETIMETYPE UUIDTYPE DECIMALTYPE DECIMAL_LITERAL
%token       TRY CATCH FINALLY THROW
%token       PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN INCREMENT DECREMENT
%token       WHILE BREAK CONTINUE
%token       AND OR NOT QDOT QQ
%token       PERCENT SHL SHR BIT_AND BIT_OR BIT_XOR BIT_NOT
%token       FN
%token       ASYNC AWAIT
%token <sval> IDENTIFIER STRING_LITERAL CONST
%token <sval> STRING_INTERP
%token <ival> NUMBER
%token IF ELSE
%token AGENT LISTENER BRIDGE STATE MATCH CASE
%token NODE ENDPOINT
%token AUTH
%token <sval> DECORATOR
%type <sval> method_name
%type <sval> method_return_type
%type <sval> type_name
%type <sval> field_type
%type <sval> member_name
%type <ival> access_mod
%type <node>  expression_list var_decl constructor_decl return_stmt arg_list more_args lambda_expression httpget_method_decl lambda_value
%type <sval> lambda_param_list
%type <pnode> parameter_decl parameter_list
%type <node> list_literal
%type <node> for_c_init for_c_update
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
%type <node> class_member
%type <node> if_statement
%type <node> match_statement case_clause case_list
%type <node> statement expression program statement_list class_decl class_body
%type <node> attribute_decl method_decl
%type <node> agent_decl agent_body listener_decl bridge_decl
%type <node> object_literal key_value_list key_value_pair
%type <node> node_decl
%type <node> state_decl
%type <node> endpoint_decl auth_endpoint_decl guard_endpoint_decl endpoint_methods endpoint_method
%type <node> ws_body ws_clauses ws_clause
%type <ival> cache_decorator
%right ARROW
%right QMARK
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
%right AWAIT ASYNC

%%

program:
     program statement       { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; ctx->root = $$; }  
    | program class_decl      { $$ = $1; ctx->root = $$; }
    | program agent_decl      { $$ = $1 ? create_ast_node("AGENT_LIST", $1, $2) : $2; ctx->root = $$; }
    | program bridge_decl     { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; ctx->root = $$; }
    | program endpoint_decl   { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; ctx->root = $$; }
    | endpoint_decl           { $$ = $1; ctx->root = $$; }
    | program auth_endpoint_decl { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; ctx->root = $$; }
    | auth_endpoint_decl      { $$ = $1; ctx->root = $$; }
    | program guard_endpoint_decl { $$ = $1 ? create_ast_node("STATEMENT_LIST", $1, $2) : $2; ctx->root = $$; }
    | guard_endpoint_decl     { $$ = $1; ctx->root = $$; }
    | cache_decorator endpoint_decl { if ($2 && $2->extra) ((MethodNode*)$2->extra)->cache_ttl = $1; $$ = $2; ctx->root = $$; }
    | httpget_method_decl     { $$ = $1; ctx->root = $$; }
    | cache_decorator httpget_method_decl { if ($2 && $2->extra) ((MethodNode*)$2->extra)->cache_ttl = $1; $$ = $2; ctx->root = $$; }
    | class_decl              { $$ = $1; ctx->root = $$; }
    | bridge_decl             { $$ = $1; ctx->root = $$; }   
    | statement               { $$ = $1; ctx->root = $$; }
;

cache_decorator:
    CACHE LPAREN NUMBER RPAREN { $$ = $3; }



endpoint_decl:
    ENDPOINT LBRACKET endpoint_methods RBRACKET
        { $$ = create_ast_node("ENDPOINT_DECL", NULL, NULL); }
    ;

/* @auth endpoint { ... } : exige autenticacion en TODOS los metodos del bloque.
 * El mid-rule action arma el flag ANTES de parsear los metodos. */
auth_endpoint_decl:
    AUTH ENDPOINT LBRACKET { ctx->endpoint_auth_all = 1; } endpoint_methods RBRACKET
        { ctx->endpoint_auth_all = 0; $$ = create_ast_node("ENDPOINT_DECL", NULL, NULL); }
    ;

/* v0.0.24 — user-defined guard applied to ALL methods of the block. Two
 * spellings are accepted: `@<name> endpoint { ... }` (consistent with
 * `@auth endpoint`) and `endpoint @<name> { ... }`. Per-method `@<name>` (if
 * present) overrides the block-level guard for that method. */
guard_endpoint_decl:
      DECORATOR ENDPOINT LBRACKET { ctx->endpoint_guard_all = $1; } endpoint_methods RBRACKET
        { if (ctx->endpoint_guard_all) free(ctx->endpoint_guard_all); ctx->endpoint_guard_all = NULL; $$ = create_ast_node("ENDPOINT_DECL", NULL, NULL); }
    | ENDPOINT DECORATOR LBRACKET { ctx->endpoint_guard_all = $2; } endpoint_methods RBRACKET
        { if (ctx->endpoint_guard_all) free(ctx->endpoint_guard_all); ctx->endpoint_guard_all = NULL; $$ = create_ast_node("ENDPOINT_DECL", NULL, NULL); }
    ;

endpoint_methods:
    endpoint_method
        { if (g_vm.global_methods) { g_vm.global_methods->requires_auth = ctx->pending_auth || ctx->endpoint_auth_all; g_vm.global_methods->is_async = ctx->pending_async; g_vm.global_methods->guard_name = ctx->pending_guard ? ctx->pending_guard : (ctx->endpoint_guard_all ? strdup(ctx->endpoint_guard_all) : NULL); } ctx->pending_auth = 0; ctx->pending_async = 0; ctx->pending_guard = NULL; $$ = NULL; }
    | endpoint_methods endpoint_method
        { if (g_vm.global_methods) { g_vm.global_methods->requires_auth = ctx->pending_auth || ctx->endpoint_auth_all; g_vm.global_methods->is_async = ctx->pending_async; g_vm.global_methods->guard_name = ctx->pending_guard ? ctx->pending_guard : (ctx->endpoint_guard_all ? strdup(ctx->endpoint_guard_all) : NULL); } ctx->pending_auth = 0; ctx->pending_async = 0; ctx->pending_guard = NULL; $$ = NULL; }
    | auth_marker
        { $$ = NULL; }
    | endpoint_methods auth_marker
        { $$ = NULL; }
    | decorator_marker
        { $$ = NULL; }
    | endpoint_methods decorator_marker
        { $$ = NULL; }
    ;

auth_marker:
    AUTH { ctx->pending_auth = 1; }
    ;

/* v0.0.24 \u2014 user-defined `@<name>` decorator marker. Captures the guard
 * function name; the enclosing endpoint_methods reduction attaches it to the
 * next endpoint method's MethodNode.guard_name. */
decorator_marker:
    DECORATOR { if (ctx->pending_guard) free(ctx->pending_guard); ctx->pending_guard = $1; }
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
        m->next = g_vm.global_methods;
        g_vm.global_methods = m;
        $$ = NULL;
    }
    | LSBRACKET HTTPPOST LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("POST");
        m->cache_ttl = 0; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPPUT LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("PUT");
        m->cache_ttl = 0; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPDELETE LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("DELETE");
        m->cache_ttl = 0; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | LSBRACKET HTTPPATCH LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($7); m->body = $12; m->params = $9;
        m->route_path = strdup($4); m->http_method = strdup("PATCH");
        m->cache_ttl = 0; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | LSBRACKET WEBSOCKET { ctx->ws_is_lifecycle = 0; ctx->ws_on_open = NULL; ctx->ws_on_message = NULL; ctx->ws_on_close = NULL; ctx->ws_msg_param = NULL; } LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET ws_body RBRACKET
    {
        MethodNode *m = (MethodNode*)calloc(1, sizeof(MethodNode));
        m->name = strdup($8); m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("WS");
        m->cache_ttl = 0;
        if (ctx->ws_is_lifecycle) {
            m->ws_lifecycle = 1;
            m->body         = ctx->ws_on_message;  /* per-message handler */
            m->ws_on_open   = ctx->ws_on_open;
            m->ws_on_close  = ctx->ws_on_close;
            m->ws_msg_param = ctx->ws_msg_param; ctx->ws_msg_param = NULL;   /* ownership -> MethodNode */
        } else {
            m->body = $13;                       /* legacy single-shot handler */
        }
        m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
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
        m->next = g_vm.global_methods;
        g_vm.global_methods = m;
        $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPOST LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("POST");
        m->cache_ttl = $1; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPUT LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("PUT");
        m->cache_ttl = $1; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPDELETE LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("DELETE");
        m->cache_ttl = $1; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    | cache_decorator LSBRACKET HTTPPATCH LPAREN STRING_LITERAL RPAREN RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
        MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
        m->name = strdup($8); m->body = $13; m->params = $10;
        m->route_path = strdup($5); m->http_method = strdup("PATCH");
        m->cache_ttl = $1; m->next = g_vm.global_methods; g_vm.global_methods = m; $$ = NULL;
    }
    ;

/* WebSocket handler body: either a flat statement_list (legacy single-shot,
 * runs once on connect) or one-or-more lifecycle clauses (on_open /
 * on_message / on_close). The two are disambiguated purely by lookahead:
 * lifecycle clauses always start with a dedicated keyword token, which can
 * never begin a statement. */
ws_body:
      statement_list   { ctx->ws_is_lifecycle = 0; $$ = $1; }
    | ws_clauses       { ctx->ws_is_lifecycle = 1; $$ = NULL; }
    ;

ws_clauses:
      ws_clause                 { $$ = NULL; }
    | ws_clauses ws_clause      { $$ = NULL; }
    ;

ws_clause:
      ON_OPEN LBRACKET statement_list RBRACKET
        { ctx->ws_on_open = $3; $$ = NULL; }
    | ON_OPEN LPAREN RPAREN LBRACKET statement_list RBRACKET
        { ctx->ws_on_open = $5; $$ = NULL; }
    | ON_MESSAGE LPAREN RPAREN LBRACKET statement_list RBRACKET
        { ctx->ws_on_message = $5; $$ = NULL; }
    | ON_MESSAGE LPAREN IDENTIFIER RPAREN LBRACKET statement_list RBRACKET
        { ctx->ws_on_message = $6; ctx->ws_msg_param = strdup($3); $$ = NULL; }
    | ON_CLOSE LBRACKET statement_list RBRACKET
        { ctx->ws_on_close = $3; $$ = NULL; }
    | ON_CLOSE LPAREN RPAREN LBRACKET statement_list RBRACKET
        { ctx->ws_on_close = $5; $$ = NULL; }
    ;

httpget_method_decl:
     LSBRACKET HTTPGET RSBRACKET IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET
    {
            MethodNode *m = (MethodNode*)malloc(sizeof(MethodNode));
            m->name = strdup($4);
            m->body = $9;
            m->params = $6;
            m->guard_name = NULL;
            m->next = g_vm.global_methods;
            g_vm.global_methods = m;            
            $$ = NULL;           
     }
    ;

class_decl:
        CLASS IDENTIFIER { ctx->last_class = create_class($2); add_class(ctx->last_class); } 
        LBRACKET class_body RBRACKET { $$ = NULL; }
    |   CLASS IDENTIFIER EXTENDS IDENTIFIER {
            ctx->last_class = create_class($2);
            add_class(ctx->last_class);
            inherit_from(ctx->last_class, $4);
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

/* member_name: a class field/method name. Accepts a plain IDENTIFIER or any
 * of the "soft" reserved keywords (state, model, node, match, ...). class_body
 * only ever contains class_member, so these keywords are unambiguous here and
 * introduce no LALR conflict. Member ACCESS (`obj.state`) is handled in the
 * lexer's yylex() wrapper via DOT-context demotion. */
member_name:
    IDENTIFIER { $$ = $1; }
  | STATE      { $$ = strdup("state"); }
  | MODEL      { $$ = strdup("model"); }
  | NODE       { $$ = strdup("node"); }
  | MATCH      { $$ = strdup("match"); }
  | FROM       { $$ = strdup("from"); }
  | AS         { $$ = strdup("as"); }
  | JSON       { $$ = strdup("json"); }
  | CONCAT     { $$ = strdup("concat"); }
  | AGENT      { $$ = strdup("agent"); }
  | CASE       { $$ = strdup("case"); }
  ;

attribute_decl:
    member_name COLON INT SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "int"); } else { fprintf(stderr, "Error: no class defined for attribute '%s'.\n", $1); } }
  | member_name COLON STRING SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "string"); } else { fprintf(stderr, "Error: no class defined for attribute '%s'.\n", $1); } }
  | member_name COLON FLOAT SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "float"); } else { fprintf(stderr, "Error: no class defined for attribute '%s'.\n", $1); } }
  | member_name COLON BOOLTYPE SEMICOLON     { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "bool"); } }
  | member_name COLON DECIMALTYPE SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "decimal"); } }
  | member_name COLON DATETIMETYPE SEMICOLON { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "datetime"); } }
  | member_name COLON UUIDTYPE SEMICOLON     { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "uuid"); } }
  | member_name COLON INT QMARK SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "int?"); } }
  | member_name COLON STRING QMARK SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "string?"); } }
  | member_name COLON FLOAT QMARK SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "float?"); } }
  | member_name COLON BOOLTYPE QMARK SEMICOLON     { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "bool?"); } }
  | member_name COLON DECIMALTYPE QMARK SEMICOLON  { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "decimal?"); } }
  | member_name COLON DATETIMETYPE QMARK SEMICOLON { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "datetime?"); } }
  | member_name COLON UUIDTYPE QMARK SEMICOLON     { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $1, "uuid?"); } }
  /* C#/C-style fields: [public|private|protected] type name [= default] ; */
  | field_type IDENTIFIER SEMICOLON
      { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $2, $1); } free($1); }
  | field_type IDENTIFIER ASSIGN expression SEMICOLON
      { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $2, $1); set_last_attr_default(ctx->last_class, $4); } free($1); }
  | access_mod field_type IDENTIFIER SEMICOLON
      { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $3, $2); set_last_attr_access(ctx->last_class, $1); } free($2); }
  | access_mod field_type IDENTIFIER ASSIGN expression SEMICOLON
      { if (ctx->last_class) { add_attribute_to_class(ctx->last_class, $3, $2); set_last_attr_access(ctx->last_class, $1); set_last_attr_default(ctx->last_class, $5); } free($2); }
  ;

field_type:
    INT          { $$ = $1; }
  | STRING       { $$ = $1; }
  | FLOAT        { $$ = $1; }
  | BOOLTYPE     { $$ = $1; }
  | DECIMALTYPE  { $$ = $1; }
  | DATETIMETYPE { $$ = $1; }
  | UUIDTYPE     { $$ = $1; }
  ;

access_mod:
    PUBLIC    { $$ = 0; }
  | PRIVATE   { $$ = 1; }
  | PROTECTED { $$ = 2; }
  ;


constructor_decl:
    CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  { if (ctx->last_class) { add_constructor_to_class(ctx->last_class, $3, $6); } else { fprintf(stderr, "Error: no class defined for the constructor.\n"); } $$ = NULL; }
  ;

parameter_decl:
    IDENTIFIER COLON INT        { $$ = create_parameter_node($1, $3); }
  | INT IDENTIFIER             { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON STRING    { $$ = create_parameter_node($1, $3); }
  | STRING IDENTIFIER          { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON FLOAT     { $$ = create_parameter_node($1, $3); }
  | FLOAT IDENTIFIER           { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON BOOLTYPE     { $$ = create_parameter_node($1, $3); }
  | IDENTIFIER COLON DECIMALTYPE  { $$ = create_parameter_node($1, $3); }
  | DECIMALTYPE IDENTIFIER        { $$ = create_parameter_node($2, $1); }
  | IDENTIFIER COLON DATETIMETYPE { $$ = create_parameter_node($1, $3); }
  | IDENTIFIER COLON UUIDTYPE     { $$ = create_parameter_node($1, $3); }
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
  | BOOLTYPE     { $$ = strdup("bool"); }
  | DECIMALTYPE  { $$ = strdup("decimal"); }
  | DATETIMETYPE { $$ = strdup("datetime"); }
  | UUIDTYPE     { $$ = strdup("uuid"); }
  | INT QMARK      { $$ = strdup("int?"); }
  | STRING QMARK   { $$ = strdup("string?"); }
  | FLOAT QMARK    { $$ = strdup("float?"); }
  | DYNAMIC QMARK  { $$ = strdup("dynamic?"); }
  | BOOLTYPE QMARK     { $$ = strdup("bool?"); }
  | DECIMALTYPE QMARK  { $$ = strdup("decimal?"); }
  | DATETIMETYPE QMARK { $$ = strdup("datetime?"); }
  | UUIDTYPE QMARK     { $$ = strdup("uuid?"); }
  | IDENTIFIER QMARK { char *t = malloc(strlen($1)+2); sprintf(t,"%s?",$1); free($1); $$ = t; }
  ;

method_decl:
    member_name LPAREN RPAREN COLON method_return_type LBRACKET statement_list RBRACKET  { if (!ctx->last_class) { fprintf(stderr, "Internal error: no active class to add method '%s'.\n", $1); } else { add_method_to_class(ctx->last_class, $1, NULL, $7, $5); } $$ = NULL; }
  | member_name LPAREN parameter_list RPAREN COLON method_return_type LBRACKET statement_list RBRACKET  { if (!ctx->last_class) { fprintf(stderr, "Internal error: no active class to add method '%s'.\n", $1); } else { add_method_to_class(ctx->last_class, $1, $3, $8, $6); } $$ = NULL; }
  ;

expression:
  func_call_expr
 | list_literal   
 | lambda_value
 | AWAIT expression    { $$ = create_function_call_node("await_async", $2); }
 | ASYNC lambda_value  { $$ = create_call_node("go", $2); }
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
  | expression QMARK expression COLON expression %prec QMARK
      { ASTNode *t = create_ast_node("TERNARY", $1, $3); t->extra = $5; $$ = t; }
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
  | DECIMAL_LITERAL     { $$ = create_ast_leaf("DECIMAL", 0, $1, NULL); }
  | STRING_LITERAL       { $$ = create_ast_leaf("STRING", 0, $1, NULL); }
  | STRING_INTERP        { $$ = create_ast_leaf("STRING_INTERP", 0, $1, NULL); }
  | NULLTOK       { $$ = create_ast_leaf("NULL", 0, NULL, NULL); }
  | TRUETOK       { ASTNode *n = create_ast_leaf("BOOL", 1, NULL, NULL); n->line = yyget_lineno(scanner); $$ = n; }
  | FALSETOK      { ASTNode *n = create_ast_leaf("BOOL", 0, NULL, NULL); n->line = yyget_lineno(scanner); $$ = n; }
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
 | JSON LPAREN IDENTIFIER RPAREN  {       $$ = create_call_node("json", create_ast_leaf("IDENTIFIER", 0, NULL, $3)); }
 | JSON LPAREN object_literal RPAREN  {   $$ = create_call_node("json", $3); }
 | JSON LPAREN list_literal RPAREN    {   $$ = create_call_node("json", $3); }
| XML LPAREN IDENTIFIER RPAREN  {      $$ = create_call_node("xml", create_ast_leaf("IDENTIFIER", 0, NULL, $3)); }
 | XML LPAREN object_literal RPAREN   {   $$ = create_call_node("xml", $3); }
 | XML LPAREN list_literal RPAREN     {   $$ = create_call_node("xml", $3); }
;

var_decl:
    LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
      { ASTNode *args = $6; ASTNode *first = args; ASTNode *second = args ? args->next : NULL; ASTNode *third = second ? second->next : NULL; /* gotcha #1: args via ->next */
        ASTNode *call;
        if (second && !third && second->type && strcmp(second->type, "LAMBDA") == 0 && (strcmp($4, "filter") == 0 || strcmp($4, "map") == 0)) {
          args->next = NULL;
          call = create_list_function_call_node(first, $4, second);
        } else {
          call = create_call_node($4, args);
        }
        ASTNode* d = create_var_decl_node($2, call); d->value = 1; /* let = immutable */ $$ = d; }
  | VAR IDENTIFIER ASSIGN IDENTIFIER LPAREN expression_list RPAREN SEMICOLON
      { ASTNode *args = $6; ASTNode *first = args; ASTNode *second = args ? args->next : NULL; ASTNode *third = second ? second->next : NULL; /* gotcha #1: args via ->next */
        ASTNode *call;
        if (second && !third && second->type && strcmp(second->type, "LAMBDA") == 0 && (strcmp($4, "filter") == 0 || strcmp($4, "map") == 0)) {
          args->next = NULL;
          call = create_list_function_call_node(first, $4, second);
        } else {
          call = create_call_node($4, args);
        }
        $$ = create_var_decl_node($2, call); }



  | LET IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* d = create_var_decl_node($2, $4); d->value = 1; /* let = immutable */ $$ = d; }
  | LET IDENTIFIER COLON type_name ASSIGN expression SEMICOLON  { ASTNode* d = create_var_decl_node($2, $6); d->value = 1; if ($4) d->str_value = $4; $$ = d; }
  | VAR IDENTIFIER COLON type_name ASSIGN expression SEMICOLON  { ASTNode* d = create_var_decl_node($2, $6); if ($4) d->str_value = $4; $$ = d; }
  | CONST IDENTIFIER COLON type_name ASSIGN expression SEMICOLON  { ASTNode* d = create_var_decl_node($2, $6); d->value = 1; if ($4) d->str_value = $4; $$ = d; }
  | STRING IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("STRING"); $$ = decl; }
  | BOOLTYPE IDENTIFIER ASSIGN expression SEMICOLON     { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("BOOL"); $$ = decl; }
  | DECIMALTYPE IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("DECIMAL"); $$ = decl; }
  | DATETIMETYPE IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("DATETIME"); $$ = decl; }
  | UUIDTYPE IDENTIFIER ASSIGN expression SEMICOLON     { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("UUID"); $$ = decl; }
  | VAR IDENTIFIER ASSIGN expression SEMICOLON  { $$ = create_var_decl_node($2, $4); }
  | CONST INT IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($3, $5); decl->value = 1; /* Marcar como const */ decl->str_value = strdup("INT"); $$ = decl; }
  | CONST IDENTIFIER ASSIGN expression SEMICOLON { ASTNode* decl = create_var_decl_node($2, $4); decl->value = 1; /* Marcar como const */ $$ = decl; }
  | INT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode* decl = create_var_decl_node($2, $4); decl->str_value = strdup("INT"); $$ = decl; }
  | IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,$1); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  | THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,$3); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); $$ = create_ast_node("ASSIGN_ATTR", access, $5); }
  /* gotcha chaining: las reglas especiales `LET/VAR/CONST IDENTIFIER ASSIGN
   * IDENTIFIER DOT IDENTIFIER SEMICOLON` (acceso a atributo) se ELIMINARON. Forzaban
   * un shift sobre DOT que comprometía al parser a una ruta terminada en ';' tras un
   * solo `.IDENTIFIER`, rompiendo el encadenamiento `xs.a().b()` (error 'near ('').
   * El camino genérico `... ASSIGN expression SEMICOLON` cubre el acceso a atributo
   * vía `expression DOT IDENTIFIER` (ACCESS_ATTR) y permite chaining recursivo. */
;

type_name:
    INT          { $$ = strdup("INT"); }
  | FLOAT        { $$ = strdup("FLOAT"); }
  | STRING       { $$ = strdup("STRING"); }
  | BOOLTYPE     { $$ = strdup("BOOL"); }
  | DECIMALTYPE  { $$ = strdup("DECIMAL"); }
  | DATETIMETYPE { $$ = strdup("DATETIME"); }
  | UUIDTYPE     { $$ = strdup("UUID"); }
  | IDENTIFIER   { $$ = NULL; /* custom/unknown type: keep dynamic */ }
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
    /* gotcha chaining: las reglas especializadas `LET/VAR IDENTIFIER ASSIGN
     * IDENTIFIER DOT IDENTIFIER LPAREN ... RPAREN SEMICOLON` se ELIMINARON porque
     * solo admitían UNA llamada antes del ';' (rompían `xs.orderBy(..).take(2)`).
     * El camino genérico `LET IDENTIFIER ASSIGN expression SEMICOLON` + la recursión
     * `expression DOT IDENTIFIER LPAREN expression_list RPAREN` soporta encadenamiento. */
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
  /* Same prefix as the classic form but the 3rd field is an UPDATE statement
   * (i++ / i += 1 / i = i + 1) -> Java/C semantics (2nd field is a CONDITION). */
  | FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON for_c_update RPAREN LBRACKET statement_list RBRACKET { ASTNode *init = create_ast_node("ASSIGN", create_ast_leaf("IDENTIFIER",0,NULL,$3), create_ast_leaf_number("NUMBER",$5,NULL,NULL)); $$ = create_for_c_node(init, $7, $9, $12); }
  /* Java/C-style: for (var i = 0; i < n; i++) { ... }. The UPDATE field is an
   * assignment statement (++ -- += -= *= /= =), which cannot start an expression,
   * so it never collides with the classic for(init; LIMIT; STEP) forms above/below. */
  | FOR LPAREN for_c_init SEMICOLON expression SEMICOLON for_c_update RPAREN LBRACKET statement_list RBRACKET   { $$ = create_for_c_node($3, $5, $7, $10); }
  | FOR LPAREN for_c_init SEMICOLON SEMICOLON for_c_update RPAREN LBRACKET statement_list RBRACKET              { $$ = create_for_c_node($3, NULL, $6, $9); }
  | FOR LPAREN for_c_init SEMICOLON expression SEMICOLON RPAREN LBRACKET statement_list RBRACKET               { $$ = create_for_c_node($3, $5, NULL, $9); }
  | FOR LPAREN SEMICOLON expression SEMICOLON for_c_update RPAREN LBRACKET statement_list RBRACKET             { $$ = create_for_c_node(NULL, $4, $6, $9); }
  /* for(START; STOP; STEP) — sin variable de control, estilo range() de Python.
   * START/STOP/STEP pueden ser literales o expresiones; STOP es límite exclusivo.
   * Se sintetiza un nombre de contador oculto ("__for$N", imposible de tipear por
   * el usuario porque '$' no es un carácter de identificador válido). */
  | FOR LPAREN expression SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET    { static int __fc_semi=0; char __nm[40]; snprintf(__nm,sizeof(__nm),"__for$%d",__fc_semi++); $$ = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,__nm), $3, $5, $7, $10); }
  /* for(START, STOP, STEP) — variante con comas (range() de Python con coma). */
  | FOR LPAREN expression COMMA expression COMMA expression RPAREN LBRACKET statement_list RBRACKET            { static int __fc_comma=0; char __nm[40]; snprintf(__nm,sizeof(__nm),"__for$%d",__fc_comma++); $$ = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,__nm), $3, $5, $7, $10); }
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
      if (!cls) { fprintf(stderr, "class '%s' not found.\n", $7); $$ = NULL; } 
      else { /* v0.0.14: defer load; te_csv_lazy_resolve_all() will autodetect COLUMNAR. */
             ASTNode* placeholder = create_ast_node("LIST", NULL, NULL);
             ASTNode* d = create_var_decl_node($2, placeholder); d->value = 1; /* let = immutable */
             te_csv_lazy_register_df(d, $5, $7, 0);
             $$ = d; } }
  | VAR IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { fprintf(stderr, "class '%s' not found.\n", $7); $$ = NULL; }
      else { ASTNode* placeholder = create_ast_node("LIST", NULL, NULL);
             ASTNode* d = create_var_decl_node($2, placeholder);
             te_csv_lazy_register_df(d, $5, $7, 0);
             $$ = d; } }
  | LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER AS IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { fprintf(stderr, "class '%s' not found.\n", $7); $$ = NULL; }
      else if (strcmp($9, "dataframe") != 0) { fprintf(stderr, "unknown modifier after 'as': '%s' (expected 'dataframe').\n", $9); $$ = NULL; }
      else { /* v1.0.0: defer load to runtime so now_ms() brackets measure I/O. */
             ASTNode* placeholder = create_ast_node("LIST", NULL, NULL);
             ASTNode* d = create_var_decl_node($2, placeholder); d->value = 1;
             te_csv_lazy_register_df(d, $5, $7, 1);
             $$ = d; } }
  | VAR IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER AS IDENTIFIER SEMICOLON
    { ClassNode* cls = find_class($7);
      if (!cls) { fprintf(stderr, "class '%s' not found.\n", $7); $$ = NULL; }
      else if (strcmp($9, "dataframe") != 0) { fprintf(stderr, "unknown modifier after 'as': '%s' (expected 'dataframe').\n", $9); $$ = NULL; }
      else { ASTNode* placeholder = create_ast_node("LIST", NULL, NULL);
             ASTNode* d = create_var_decl_node($2, placeholder);
             te_csv_lazy_register_df(d, $5, $7, 1);
             $$ = d; } }
; 

func_call_expr:
    IDENTIFIER LPAREN RPAREN { $$ = create_call_node($1, NULL); }
    | IDENTIFIER LPAREN expression_list RPAREN { $$ = create_call_node($1, $3); }
    /* decimal(x): `decimal` es keyword de tipo, así que la conversión se reduce aquí. */
    | DECIMALTYPE LPAREN expression_list RPAREN { free($1); $$ = create_call_node("decimal", $3); }
    /* Gotcha #2: llamada sobre el resultado de otra llamada — `make(10)(5)`,
     * `make(10)(5)(...)`. Left-recursivo sobre func_call_expr: el callee ya
     * reducido se invoca con los nuevos argumentos. */
    | func_call_expr LPAREN RPAREN { $$ = create_call_on_expr_node($1, NULL); }
    | func_call_expr LPAREN expression_list RPAREN { $$ = create_call_on_expr_node($1, $3); }
    /* Fase 2: NEW IDENTIFIER (...) for non-class names is handled by the
     * `NEW IDENTIFIER LPAREN expression_list RPAREN` rule in `expression`
     * which falls back to create_call_node when find_class returns NULL. */
;

/* Java/C-style for(...) pieces. INIT declares (var/let) or assigns; UPDATE is an
 * assignment statement without the trailing ';'. */
for_c_init:
    VAR IDENTIFIER ASSIGN expression                { $$ = create_var_decl_node($2, $4); }
  | LET IDENTIFIER ASSIGN expression                { ASTNode *d = create_var_decl_node($2, $4); d->value = 1; $$ = d; }
  | INT IDENTIFIER ASSIGN expression                { ASTNode *d = create_var_decl_node($2, $4); d->str_value = strdup("INT"); $$ = d; }
;
for_c_update:
    IDENTIFIER INCREMENT      { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("ADD", id2, create_ast_leaf_number("NUMBER",1,NULL,NULL))); }
  | IDENTIFIER DECREMENT      { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("SUB", id2, create_ast_leaf_number("NUMBER",1,NULL,NULL))); }
  | IDENTIFIER PLUS_ASSIGN expression   { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("ADD", id2, $3)); }
  | IDENTIFIER MINUS_ASSIGN expression  { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("SUB", id2, $3)); }
  | IDENTIFIER STAR_ASSIGN expression   { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("MUL", id2, $3)); }
  | IDENTIFIER SLASH_ASSIGN expression  { ASTNode *id1 = create_ast_leaf("IDENTIFIER",0,NULL,$1); ASTNode *id2 = create_ast_leaf("IDENTIFIER",0,NULL,strdup($1)); $$ = create_ast_node("ASSIGN", id1, create_ast_node("DIV", id2, $3)); }
  | IDENTIFIER ASSIGN expression        { $$ = create_ast_node("ASSIGN", create_ast_leaf("IDENTIFIER",0,NULL,$1), $3); }
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
        if (!cls) { fprintf(stderr, "Error: class '%s' not found.\n", $2); $$ = NULL; } 
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

static void yyerror(yyscan_t scanner, TeParseCtx *ctx, const char *s) {
    (void)ctx;
    extern void te_capture_error(int line, const char *msg, const char *near);
    extern const char *te_src_file_name(int id);
    const char *text = yyget_text(scanner);
    int line = yyget_lineno(scanner);
    if (g_vm.quiet_parse_errors) return;
    if (g_vm.capture_errors) {
        te_capture_error(line, s, text);
        return;
    }
    /* Diagnostics go to stderr in an English, editor-jumpable file:line: form. */
    const char *src = g_vm.lex_file_id > 0 ? te_src_file_name(g_vm.lex_file_id)
                      : (g_vm.debug_source_file && g_vm.debug_source_file[0])
                      ? g_vm.debug_source_file : "<stdin>";
    fprintf(stderr, "%s:%d: syntax error: %s\n", src, line, s);
    if (text && text[0]) {
        fprintf(stderr, "%s:%d: near '%s'\n", src, line, text);
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
        printf(", value: %lld", (long long)node->value);
    if (node->str_value)
        printf(", str: %s", node->str_value);
    if (node->id)
        printf(", id: %s", node->id);
    printf("\n");
    print_ast(node->left, indent + 1);
    print_ast(node->right, indent + 1);
}

extern void te_lexer_ctx_cleanup(yyscan_t scanner);   /* parser.l */

static void te_parse_ctx_free(TeParseCtx *ctx) {
    if (ctx->endpoint_guard_all) free(ctx->endpoint_guard_all);
    if (ctx->pending_guard) free(ctx->pending_guard);
    if (ctx->ws_msg_param) free(ctx->ws_msg_param);
}

/* Reentrante: scanner + contexto NUEVOS por llamada, destruidos al salir. Un
 * parse abortado (error de sintaxis o fatal via longjmp) no deja residuos en el
 * siguiente: el scanner huérfano queda anotado en g_vm.parse_scanner y se libera
 * aquí antes de empezar. */
ASTNode* parse_file(FILE* file) {
    if (!file) {
        fprintf(stderr, "[PARSER] ERROR: file is NULL\n");
        return NULL;
    }
    if (g_vm.parse_scanner) {                       /* parse anterior abortado por longjmp */
        yyscan_t stale = (yyscan_t)g_vm.parse_scanner;
        TeParseCtx *sctx = yyget_extra(stale);
        te_lexer_ctx_cleanup(stale);
        yylex_destroy(stale);
        if (sctx) { te_parse_ctx_free(sctx); free(sctx); }
        g_vm.parse_scanner = NULL;
    }

    TeParseCtx *ctx = (TeParseCtx *)calloc(1, sizeof(TeParseCtx));
    if (!ctx) return NULL;
    yyscan_t scanner = NULL;
    if (yylex_init_extra(ctx, &scanner) != 0) { free(ctx); return NULL; }
    yyrestart(file, scanner);               /* crea el buffer (yyset_lineno lo exige) */
    yyset_lineno(1, scanner);
    g_vm.lex_line = 1;
    g_vm.decl_stmt_line = 0;
    g_vm.parse_scanner = scanner;

    int parse_result = yyparse(scanner, ctx);

    te_lexer_ctx_cleanup(scanner);
    yylex_destroy(scanner);
    g_vm.parse_scanner = NULL;
    ASTNode *root = ctx->root;
    te_parse_ctx_free(ctx);
    free(ctx);

    if (parse_result != 0) return NULL;

    /* Empty file / only comments: keep the "always an AST" contract. */
    if (!root) root = create_ast_node("STATEMENT_LIST", NULL, NULL);

    /* v0.0.14: Resolve deferred CSV loads now that the full AST exists.
     * Auto-detects COLUMNAR-safe usage and avoids per-row wrapper allocs
     * for analytics-only loads (sumBy/countWhere/avgBy/.length/...). */
    te_csv_lazy_resolve_all(root);

    return root;
}
