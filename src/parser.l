%{
    #include "ast.h"
    #include "te_vm.h"
    #include "te_parse.h"
    #include "parser.tab.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <setjmp.h>

    /* Scanner REENTRANTE (flex %option reentrant bison-bridge): ningún estado
     * a nivel de archivo. Todo lo que antes eran globales (pila de imports,
     * lista de importados, último token) vive en el TeParseCtx que parse_file()
     * pasa como yyextra; la posición (línea/archivo) se publica en la VM actual
     * (g_vm.lex_line / g_vm.lex_file_id) para los constructores de ast.c. */

    /* Recoverable flex fatal errors: flex's default YY_FATAL_ERROR prints and
     * calls exit(2). Long-lived hosts (--api, fuzzer) must survive a scanner
     * error on adversarial input, so reuse the runtime recovery point. */
    static void te_lexer_fatal(const char *msg) {
        fprintf(stderr, "[LEXER] fatal scanner error: %s\n", msg);
        if (g_vm.runtime_recovery) longjmp(*g_vm.runtime_recovery, 1);
        exit(2);
    }
    #define YY_FATAL_ERROR(msg) te_lexer_fatal(msg)

    static int te_is_already_imported(TeParseCtx *ctx, const char *path) {
        for (TeImportedFile *c = ctx->imported; c; c = c->next)
            if (strcmp(c->path, path) == 0) return 1;
        return 0;
    }
    static void te_mark_as_imported(TeParseCtx *ctx, const char *path) {
        TeImportedFile *n = (TeImportedFile *)malloc(sizeof(TeImportedFile));
        if (!n) return;
        n->path = strdup(path);
        n->next = ctx->imported;
        ctx->imported = n;
    }

    /* flex emits the scanner as te_raw_yylex(); the yylex() wrapper in the
     * user-code section strips the cosmetic endpoint `async` modifier and
     * demotes soft keywords after '.', keeping both out of the LALR grammar. */
    #define YY_DECL int te_raw_yylex(YYSTYPE *yylval_param, yyscan_t yyscanner)
%}

%option reentrant bison-bridge
%option extra-type="TeParseCtx *"
%option header-file="lex.yy.h"
%option nounistd never-interactive
%option nounput

%x INCL

%%

[ \t\r]+                ;
\n                      { yylineno++; }    

"//".*                  {  }
"/*"([^*]|\*+[^*/])*\*+"/"  { 
    // Count newlines in the comment
    for (char *p = yytext; *p; p++) {
        if (*p == '\n') yylineno++;
    }
}

"import"[ \t]*\"      { BEGIN(INCL); }

<INCL>[^"]+\"     {
    // Extract filename (remove trailing quote)
    char *fname = strdup(yytext);
    // Guard against an empty result: if the import path begins with an
    // embedded NUL byte, strdup() truncates at that NUL and strlen() is 0, so
    // fname[strlen(fname)-1] would underflow to fname[-1] and corrupt the heap
    // (heap-buffer-overflow). Only strip the trailing quote when present.
    size_t _flen = strlen(fname);
    if (_flen > 0) fname[_flen - 1] = '\0';

    // Consume trailing semicolon and whitespace
    int c;
    while((c = input(yyscanner)) != ';' && c != EOF && c != '\n'); 
    
    // Resolve path
    char path[1024];
    if (fname[0] == '/') {
        snprintf(path, sizeof(path), "%s", fname);
    } else {
        // 1) Relative to CWD
        FILE *probe = fopen(fname, "r");
        if (probe) {
            fclose(probe);
            snprintf(path, sizeof(path), "%s", fname);
        } else {
            // 2) /code/ (docker mount of typeeasycode/)
            snprintf(path, sizeof(path), "/code/%s", fname);
            probe = fopen(path, "r");
            if (probe) { fclose(probe); }
            else {
                // 3) /app/
                snprintf(path, sizeof(path), "/app/%s", fname);
            }
        }
    }
    free(fname);

    TeParseCtx *ctx = yyextra;
    if (!te_is_already_imported(ctx, path)) {
        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "[IMPORT] Error: Cannot open import file %s\n", path);
            // Don't exit, just continue (maybe warn)
        } else {
            fprintf(stderr, "[IMPORT] Lexer including: %s\n", path);
            te_mark_as_imported(ctx, path);
            
            if (ctx->include_stack_ptr >= TE_MAX_INCLUDE_DEPTH) {
                fprintf(stderr, "[IMPORT] Error: Includes nested too deeply\n");
                fclose(f);
            } else {
                int sp = ctx->include_stack_ptr;
                ctx->include_stack[sp] = YY_CURRENT_BUFFER;
                ctx->include_file_stack[sp] = f;  // Store FILE* to close later
                ctx->include_line_stack[sp] = yylineno;
                ctx->include_file_id_stack[sp] = g_vm.lex_file_id;
                ctx->include_stack_ptr++;
                g_vm.lex_file_id = te_src_file_register(path);
                yylineno = 1;
                yy_switch_to_buffer(yy_create_buffer(f, YY_BUF_SIZE, yyscanner), yyscanner);
            }
        }
    } else {
        // Already imported, skip silently
    }
    
    BEGIN(INITIAL);
}

"dataset"    { yylval->sval = strdup(yytext); return DATASET; }
"model"      { yylval->sval = strdup(yytext); return MODEL; }
"train"      { yylval->sval = strdup(yytext); return TRAIN; }
"predict"    { yylval->sval = strdup(yytext); return PREDICT; }
"from"       { yylval->sval = strdup(yytext); return FROM; }
"as"         { yylval->sval = strdup(yytext); return AS; }
"layer"      { yylval->sval = strdup(yytext); return LAYER; }
"plot"                   { return PLOT; }       

    /* Fase 2: tokens MYSQL/POSTGRES/SQLSERVER and ORM_QUERY removed; */
    /* those names lex as plain IDENTIFIERs and dispatch through the    */
    /* runtime registry (te_builtins.c). Add new builtins by calling    */
    /* te_builtin_register() in te_register_ast_builtins() in ast.c.    */

"var"                   { g_vm.decl_stmt_line = yylineno; return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"extends"               { return EXTENDS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { g_vm.decl_stmt_line = yylineno; return LET; }
"print"                 { return PRINT; }
"println"               { return PRINTLN; }
"fprint"                { return FPRINT; }
"fprintln"              { return FPRINTLN; }
"const"                 { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return CONST; }
"fn"                    { return FN; }
"async"                 { return ASYNC; }
"await"                 { return AWAIT; }

"HttpGet"               { return HTTPGET; }
"HTTPGET"               { return HTTPGET; }
"HttpPost"              { return HTTPPOST; }
"HttpPut"               { return HTTPPUT; }
"HttpDelete"            { return HTTPDELETE; }
"HttpPatch"             { return HTTPPATCH; }
"WebSocket"             { return WEBSOCKET; }
"WEBSOCKET"             { return WEBSOCKET; }
"on_open"               { return ON_OPEN; }
"on_message"            { return ON_MESSAGE; }
"on_close"              { return ON_CLOSE; }
"xml"                   { return XML; }
"json"                  { return JSON; }

"agent"                 { return AGENT; }
"listener"              { return LISTENER; }
"bridge"                { return BRIDGE; }
"state"                 { return STATE; }
"match"                 { return MATCH; }
"case"                  { return CASE; }
"endpoint"              { return ENDPOINT; }
"node"                  { return NODE; }
"@Cache"                { yylval->sval = strdup(yytext); return CACHE; }
"@auth"                 { return AUTH; }
 /* User-defined decorator `@<name>` (v0.0.24). Placed AFTER @Cache/@auth so
  * those win on equal-length match (flex earliest-rule-wins). Strips the '@'
  * and passes the bare guard-function name to the parser. */
"@"[a-zA-Z_][a-zA-Z0-9_]* { yylval->sval = strdup(yytext + 1); return DECORATOR; }

"for"                   { return FOR; }
"foreach"               { return FOREACH; }
"concat"                { return CONCAT; }
"in"                    { return IN; }

"int"                   { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return INT; }
"float"                 { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return FLOAT; }
"string"                { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return STRING; }
"void"                  { return VOID; }
"dynamic"               { return DYNAMIC; }
"public"                { return PUBLIC; }
"private"               { return PRIVATE; }
"protected"             { return PROTECTED; }
"null"                  { return NULLTOK; }
"true"                  { return TRUETOK; }
"false"                 { return FALSETOK; }
"bool"                  { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return BOOLTYPE; }
"decimal"               { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return DECIMALTYPE; }
"datetime"              { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return DATETIMETYPE; }
"uuid"                  { g_vm.decl_stmt_line = yylineno; yylval->sval = strdup(yytext); return UUIDTYPE; }
"try"                   { return TRY; }
"catch"                 { return CATCH; }
"finally"               { return FINALLY; }
"throw"                 { return THROW; }
"while"                 { return WHILE; }
"break"                 { return BREAK; }
"continue"              { return CONTINUE; }

">"                     { return GT; }  
"<"                     { return LT; } 
"<="                    { return GT_EQ; }
">="                    { return LT_EQ; }
"=="                    { return EQ; }
"!="                    { return DIFF; }
"=>"                    { return ARROW; }
"+="                    { return PLUS_ASSIGN; }
"-="                    { return MINUS_ASSIGN; }
"*="                    { return STAR_ASSIGN; }
"/="                    { return SLASH_ASSIGN; }
"++"                    { return INCREMENT; }
"--"                    { return DECREMENT; }
"&&"                    { return AND; }
"||"                    { return OR; }
"!"                     { return NOT; }
"?."                    { return QDOT; }
"??"                    { return QQ; }
"?"                     { return QMARK; }
"["                     { yylval->sval = strdup(yytext); return LSBRACKET;}
"]"                     { yylval->sval = strdup(yytext); return RSBRACKET;}
"("                     { return LPAREN; }
")"                     { return RPAREN; }
"{"                     { return LBRACKET; }
"}"                     { return RBRACKET; }
":"                     { return COLON; }
"."                     { return DOT; }
","                     { return COMMA; }      
";"                     { return SEMICOLON; }
"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return MULTIPLY; }
"/"                     { return DIVIDE; }
"="                     { return ASSIGN; }
"%"                     { return PERCENT; }
"<<"                    { return SHL; }
">>"                    { return SHR; }
"&"                     { return BIT_AND; }
"|"                     { return BIT_OR; }
"^"                     { return BIT_XOR; }
"~"                     { return BIT_NOT; }
"if"                    { return IF; }    
"else"                  { return ELSE; }  


\$\"(\\.|[^\"])*\"      {
    /* Fase 3a: string interpolation $"text {var}" — emitir STRING_INTERP con contenido crudo */
    int max_len = strlen(yytext);
    yylval->sval = (char *) malloc(max_len + 1);
    char *w = yylval->sval;
    char *s = yytext + 2; /* saltar $ y " */
    while (*s != '\0' && !(*s == '"' && *(s-1) != '\\')) {
        if (*s == '\\') {
            s++;
            /* Dangling backslash at end of token (e.g. embedded NUL byte):
             * stop here so the trailing s++ below cannot read past the buffer. */
            if (*s == '\0') break;
            switch (*s) {
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 't': *w++ = '\t'; break;
                case '"': *w++ = '"'; break;
                case '\\': *w++ = '\\'; break;
                case '{': *w++ = '\1'; break; /* literal { */
                case '}': *w++ = '\2'; break; /* literal } */
                default: *w++ = *s; break;
            }
        } else {
            *w++ = *s;
        }
        s++;
    }
    *w = '\0';
    return STRING_INTERP;
}

\"(\\.|[^\"])*\"      {
    // Calcular longitud máxima necesaria
    int max_len = strlen(yytext);
    yylval->sval = (char *) malloc(max_len + 1);
    
    char *w = yylval->sval;
    char *s = yytext + 1; // Saltar la comilla inicial
    
    while (*s != '\0' && !(*s == '"' && *(s-1) != '\\')) {
        if (*s == '\\') {
            s++; // Avanzar al carácter escapado
            /* Dangling backslash at end of token (e.g. embedded NUL byte):
             * stop here so the trailing s++ below cannot read past the buffer. */
            if (*s == '\0') break;
            switch (*s) {
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 't': *w++ = '\t'; break;
                case '"': *w++ = '"'; break;
                case '\\': *w++ = '\\'; break;
                default: *w++ = *s; break; // Carácter desconocido, copiar tal cual
            }
        } else {
            *w++ = *s;
        }
        s++;
    }
    *w = '\0';
    return STRING_LITERAL;
}

0[xX][0-9a-fA-F][0-9a-fA-F_]*  {
    /* Hex literal: 0xFF, 0xDEAD_BEEF */
    char buf[64]; int j = 0;
    for (char *p = yytext + 2; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval->ival = strtoll(buf, NULL, 16);
    return NUMBER;
}
0[bB][01][01_]*           {
    /* Binary literal: 0b1010, 0b1010_1100 */
    char buf[64]; int j = 0;
    for (char *p = yytext + 2; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval->ival = strtoll(buf, NULL, 2);
    return NUMBER;
}
[0-9][0-9_]*              {
    /* Decimal literal con separadores opcionales: 1_000_000 */
    char buf[64]; int j = 0;
    for (char *p = yytext; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval->ival = strtoll(buf, NULL, 10);
    return NUMBER;
}
[0-9][0-9_]*(\.[0-9][0-9_]*)?m  {
    /* Literal decimal exacto: 12.50m / 3m (sufijo m, como C#) */
    char buf[64]; int j = 0;
    for (char *p = yytext; *p && *p != 'm' && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval->sval = strdup(buf);
    return DECIMAL_LITERAL;
}
[0-9][0-9_]*\.[0-9][0-9_]*  {
    /* Float con underscores opcional */
    char buf[64]; int j = 0;
    for (char *p = yytext; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval->sval = strdup(buf);
    return FLOAT_LITERAL;
}


[A-Za-z_\x80-\xFF][A-Za-z0-9_\x80-\xFF]*  {
    yylval->sval = strdup(yytext);
    return IDENTIFIER;
}

%%

/* EOF de un archivo importado: cerrar y volver al buffer que lo incluyó. */
int yywrap(yyscan_t yyscanner) {
    struct yyguts_t *yyg = (struct yyguts_t *)yyscanner;
    TeParseCtx *ctx = yyextra;
    if (ctx->include_stack_ptr > 0) {
        int sp = --ctx->include_stack_ptr;
        if (ctx->include_file_stack[sp]) { fclose(ctx->include_file_stack[sp]); ctx->include_file_stack[sp] = NULL; }
        yy_delete_buffer(YY_CURRENT_BUFFER, yyscanner);
        yy_switch_to_buffer((YY_BUFFER_STATE)ctx->include_stack[sp], yyscanner);
        ctx->include_stack[sp] = NULL;
        yylineno = ctx->include_line_stack[sp];
        g_vm.lex_file_id = ctx->include_file_id_stack[sp];
        return 0;
    }
    return 1;
}

/* Limpieza de un parse ABORTADO (error de sintaxis o fatal dentro de un import):
 * cierra los FILE* y libera los buffers que quedaron en la pila de imports del
 * contexto. Los buffers activos los libera yylex_destroy(). */
void te_lexer_ctx_cleanup(yyscan_t yyscanner) {
    TeParseCtx *ctx = yyget_extra(yyscanner);
    while (ctx->include_stack_ptr > 0) {
        int sp = --ctx->include_stack_ptr;
        if (ctx->include_file_stack[sp]) { fclose(ctx->include_file_stack[sp]); ctx->include_file_stack[sp] = NULL; }
        if (ctx->include_stack[sp]) { yy_delete_buffer((YY_BUFFER_STATE)ctx->include_stack[sp], yyscanner); ctx->include_stack[sp] = NULL; }
    }
    while (ctx->imported) {
        TeImportedFile *n = ctx->imported->next;
        free(ctx->imported->path);
        free(ctx->imported);
        ctx->imported = n;
    }
}

/* Soft-keyword spelling table: maps a reserved keyword TOKEN to its source
 * spelling. Used by the yylex() wrapper to demote a keyword to a plain
 * IDENTIFIER when it appears in member-access position (`obj.state`,
 * `repo.model()`). After a '.' a bare keyword can never be valid in the
 * current grammar, so this rewrite is unambiguous and introduces no conflict.
 * Returns NULL for tokens that are NOT demotable soft keywords. */
static const char *te_soft_keyword_spelling(int t) {
    switch (t) {
        case STATE:   return "state";
        case MODEL:   return "model";
        case NODE:    return "node";
        case MATCH:   return "match";
        case FROM:    return "from";
        case AS:      return "as";
        case JSON:    return "json";
        case CONCAT:  return "concat";
        case AGENT:   return "agent";
        case CASE:    return "case";
        case DATASET: return "dataset";
        case TRAIN:   return "train";
        case PREDICT: return "predict";
        case LISTENER:return "listener";
        case BRIDGE:  return "bridge";
        case ENDPOINT:return "endpoint";
        case LAYER:   return "layer";
        default:      return NULL;
    }
}


/* Wrapper sobre el scanner generado (te_raw_yylex). (1) `]` seguido de `async`:
 * modificador cosmético de endpoint -> se traga el token y se marca
 * ctx->pending_async para MethodNode.is_async. (2) Soft keyword tras '.'
 * (`obj.state`, `repo.model()`) -> IDENTIFIER. (3) Publica la línea actual en
 * la VM para que los constructores de nodos (ast.c) la estampen. */
int yylex(YYSTYPE *yylval_param, yyscan_t yyscanner) {
    TeParseCtx *ctx = yyget_extra(yyscanner);
    int t = te_raw_yylex(yylval_param, yyscanner);
    if (t == ASYNC && ctx->prev_lex_tok == RSBRACKET) {
        ctx->pending_async = 1;
        ctx->prev_lex_tok = t;
        t = te_raw_yylex(yylval_param, yyscanner);
    }
    if (ctx->prev_lex_tok == DOT) {
        const char *sp = te_soft_keyword_spelling(t);
        if (sp) {
            yylval_param->sval = strdup(sp);
            t = IDENTIFIER;
        }
    }
    ctx->prev_lex_tok = t;
    g_vm.lex_line = yyget_lineno(yyscanner);
    return t;
}
