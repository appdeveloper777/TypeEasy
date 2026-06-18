%{
    #include "ast.h"
    #include "parser.tab.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <setjmp.h>

    /* Recoverable flex fatal errors.
     *
     * flex's default YY_FATAL_ERROR prints to stderr and calls exit(2). That is
     * acceptable for the one-shot CLI, but it is wrong for any long-lived host
     * of the parser: the --api server (which parses request bodies in a loop)
     * and the libFuzzer harness both must survive a scanner-internal error
     * (e.g. "input buffer overflow" on a pathological single mega-token, or
     * "end of buffer missed") triggered by adversarial input. A raw exit(2)
     * there tears down the worker / aborts the whole fuzz run (libFuzzer reports
     * "fuzz target exited", exit 77) instead of treating the input as a parse
     * failure.
     *
     * te_lexer_fatal() reuses the same recovery point as te_runtime_fatal()
     * (g_runtime_recovery, owned by ast.c): when a caller installed one it
     * longjmp's back so the host can recover; otherwise it keeps the original
     * print-and-exit behaviour, so CLI semantics are unchanged. */
    extern jmp_buf *g_runtime_recovery;
    static void te_lexer_fatal(const char *msg) {
        fprintf(stderr, "[LEXER] fatal scanner error: %s\n", msg);
        if (g_runtime_recovery) longjmp(*g_runtime_recovery, 1);
        exit(2);
    }
    #define YY_FATAL_ERROR(msg) te_lexer_fatal(msg)

    // Include stack for imports
    #define MAX_INCLUDE_DEPTH 10
    YY_BUFFER_STATE include_stack[MAX_INCLUDE_DEPTH];
    FILE* include_file_stack[MAX_INCLUDE_DEPTH];  // Track FILE* to close them
    int include_stack_ptr = 0;

    // Circular import tracking
    typedef struct ImportedFile {
        char *path;
        struct ImportedFile *next;
    } ImportedFile;
    static ImportedFile *imported_files_list = NULL;

    int is_already_imported(const char *path) {
        ImportedFile *current = imported_files_list;
        while (current) {
            if (strcmp(current->path, path) == 0) return 1;
            current = current->next;
        }
        return 0;
    }

    void mark_as_imported(const char *path) {
        ImportedFile *new_import = (ImportedFile*)malloc(sizeof(ImportedFile));
        new_import->path = strdup(path);
        new_import->next = imported_files_list;
        imported_files_list = new_import;
    }

    /* `async` is an OPTIONAL, cosmetic modifier on an endpoint handler
     * (C#/.NET-style: `[HttpGet("/x")] async foo() {...}`). It carries NO
     * runtime effect — cooperative request interleaving is already transparent,
     * so `async foo()` and `foo()` behave identically. Rather than add the
     * modifier to the (conflict-sensitive) LALR grammar, the scanner strips it:
     * flex emits its scanner as te_raw_yylex(), and the yylex() wrapper in the
     * user-code section below drops an ASYNC token ONLY when it immediately
     * follows ']' (RSBRACKET) — which in valid TypeEasy can only be an endpoint
     * handler declaration. Everywhere else (e.g. `async fn() => ...`) ASYNC
     * passes through unchanged. */
    #define YY_DECL int te_raw_yylex(void)

    /* Last token returned by yylex(); used only to detect the `]`-then-`async`
     * endpoint-modifier sequence. Reset between top-level parses in
     * te_lexer_full_reset(). */
    static int g_prev_lex_tok = 0;

    /* Set to 1 by the yylex() wrapper when it strips an endpoint `async`
     * modifier; the parser (parser.y) reads it in the endpoint_methods
     * reduction to set MethodNode.is_async, then clears it. Defined non-static
     * in parser.y so both translation units share it. */
    extern int g_pending_async;

    /* Line of the keyword that STARTS a variable declaration (let/var/const or
     * a leading type like int/float/string). bison reduces a var_decl rule only
     * AFTER scanning its whole right-hand side, so for a multi-line declaration
     * (e.g. `let xs = [ ...\n... ];`) yylineno already points at the LAST line
     * (the closing `];`). The debugger matches breakpoints by node->line, so a
     * breakpoint placed on the VISUAL first line of the statement would never
     * fire. The scanner stamps this with yylineno the moment it sees the
     * declaration's leading keyword, and create_var_decl_node() (ast.c) uses it
     * so the node is attributed to the statement's first line. Defined in
     * ast.c. */
    extern int g_decl_stmt_line;
%}

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
    while((c = input()) != ';' && c != EOF && c != '\n'); 
    
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

    if (!is_already_imported(path)) {
        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "[IMPORT] Error: Cannot open import file %s\n", path);
            // Don't exit, just continue (maybe warn)
        } else {
            fprintf(stderr, "[IMPORT] Lexer including: %s\n", path);
            mark_as_imported(path);
            
            if (include_stack_ptr >= MAX_INCLUDE_DEPTH) {
                fprintf(stderr, "[IMPORT] Error: Includes nested too deeply\n");
                fclose(f);
            } else {
                include_stack[include_stack_ptr] = YY_CURRENT_BUFFER;
                include_file_stack[include_stack_ptr] = f;  // Store FILE* to close later
                include_stack_ptr++;
                yy_switch_to_buffer(yy_create_buffer(f, YY_BUF_SIZE));
            }
        }
    } else {
        // Already imported, skip silently
    }
    
    BEGIN(INITIAL);
}

"dataset"    { yylval.sval = strdup(yytext); return DATASET; }
"model"      { yylval.sval = strdup(yytext); return MODEL; }
"train"      { yylval.sval = strdup(yytext); return TRAIN; }
"predict"    { yylval.sval = strdup(yytext); return PREDICT; }
"from"       { yylval.sval = strdup(yytext); return FROM; }
"as"         { yylval.sval = strdup(yytext); return AS; }
"layer"      { yylval.sval = strdup(yytext); return LAYER; }
"plot"                   { return PLOT; }       

    /* Fase 2: tokens MYSQL/POSTGRES/SQLSERVER and ORM_QUERY removed; */
    /* those names lex as plain IDENTIFIERs and dispatch through the    */
    /* runtime registry (te_builtins.c). Add new builtins by calling    */
    /* te_builtin_register() in te_register_ast_builtins() in ast.c.    */

"var"                   { g_decl_stmt_line = yylineno; return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"extends"               { return EXTENDS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { g_decl_stmt_line = yylineno; return LET; }
"print"                 { return PRINT; }
"println"               { return PRINTLN; }
"fprint"                { return FPRINT; }
"fprintln"              { return FPRINTLN; }
"const"                 { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return CONST; }
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
"@Cache"                { yylval.sval = strdup(yytext); return CACHE; }
"@auth"                 { return AUTH; }
 /* User-defined decorator `@<name>` (v0.0.24). Placed AFTER @Cache/@auth so
  * those win on equal-length match (flex earliest-rule-wins). Strips the '@'
  * and passes the bare guard-function name to the parser. */
"@"[a-zA-Z_][a-zA-Z0-9_]* { yylval.sval = strdup(yytext + 1); return DECORATOR; }

"for"                   { return FOR; }
"foreach"               { return FOREACH; }
"concat"                { return CONCAT; }
"in"                    { return IN; }

"int"                   { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return INT; }
"float"                 { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return FLOAT; }
"string"                { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return STRING; }
"void"                  { return VOID; }
"dynamic"               { return DYNAMIC; }
"public"                { return PUBLIC; }
"private"               { return PRIVATE; }
"protected"             { return PROTECTED; }
"null"                  { return NULLTOK; }
"true"                  { return TRUETOK; }
"false"                 { return FALSETOK; }
"bool"                  { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return BOOLTYPE; }
"datetime"              { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return DATETIMETYPE; }
"uuid"                  { g_decl_stmt_line = yylineno; yylval.sval = strdup(yytext); return UUIDTYPE; }
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
"["                     { yylval.sval = strdup(yytext); return LSBRACKET;}
"]"                     { yylval.sval = strdup(yytext); return RSBRACKET;}
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
    yylval.sval = (char *) malloc(max_len + 1);
    char *w = yylval.sval;
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
    yylval.sval = (char *) malloc(max_len + 1);
    
    char *w = yylval.sval;
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
    yylval.ival = (int)strtol(buf, NULL, 16);
    return NUMBER;
}
0[bB][01][01_]*           {
    /* Binary literal: 0b1010, 0b1010_1100 */
    char buf[64]; int j = 0;
    for (char *p = yytext + 2; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval.ival = (int)strtol(buf, NULL, 2);
    return NUMBER;
}
[0-9][0-9_]*              {
    /* Decimal literal con separadores opcionales: 1_000_000 */
    char buf[64]; int j = 0;
    for (char *p = yytext; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval.ival = atoi(buf);
    return NUMBER;
}
[0-9][0-9_]*\.[0-9][0-9_]*  {
    /* Float con underscores opcional */
    char buf[64]; int j = 0;
    for (char *p = yytext; *p && j < 63; p++) if (*p != '_') buf[j++] = *p;
    buf[j] = '\0';
    yylval.sval = strdup(buf);
    return FLOAT_LITERAL;
}


[A-Za-z_\x80-\xFF][A-Za-z0-9_\x80-\xFF]*  {
    yylval.sval = strdup(yytext);
    return IDENTIFIER;
}

%%

int yywrap() {
    if (include_stack_ptr > 0) {
        // Close the current file before switching back
        FILE* current_file = include_file_stack[include_stack_ptr - 1];
        if (current_file) {
            fclose(current_file);
        }
        
        yy_delete_buffer(YY_CURRENT_BUFFER);
        yy_switch_to_buffer(include_stack[--include_stack_ptr]);
        return 0;
    }
    return 1;
}

/* yylex(): thin wrapper around the flex-generated scanner (te_raw_yylex, see
 * the YY_DECL define near the top of this file). Its sole job is to make the
 * `async` keyword an OPTIONAL, cosmetic modifier on endpoint handlers:
 *
 *     [HttpGet("/ping")] async ping() { ... }   // C#/.NET-style
 *     [HttpGet("/ping")]       ping() { ... }   // plain — identical behaviour
 *
 * When ASYNC arrives immediately after ']' (RSBRACKET) it can only be an
 * endpoint handler's modifier (an array index / list literal `]` is never
 * followed by `async` in valid source), so we swallow the token but RECORD it
 * in g_pending_async so the parser can flag the handler's MethodNode.is_async
 * (which gates cooperative request overlap on await — C#/.NET semantics). In
 * every other position — notably the `async fn() => ...` task-spawn
 * expression — ASYNC is returned unchanged. This keeps the modifier out of the
 * conflict-sensitive LALR grammar entirely, so single-user behaviour and all
 * existing syntax are byte-for-byte unchanged. */
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

int yylex(void) {
    int t = te_raw_yylex();
    if (t == ASYNC && g_prev_lex_tok == RSBRACKET) {
        /* endpoint `async` modifier: drop the token but remember it so the
         * parser marks this handler async; then fetch the handler name. */
        g_pending_async = 1;
        g_prev_lex_tok = t;
        t = te_raw_yylex();
    }
    /* Member-access demotion: `obj.state`, `repo.model()`. When a soft keyword
     * immediately follows '.', it is a member name, not a keyword — rewrite it
     * to IDENTIFIER so the grammar's `expression DOT IDENTIFIER` rules apply. */
    if (g_prev_lex_tok == DOT) {
        const char *sp = te_soft_keyword_spelling(t);
        if (sp) {
            yylval.sval = strdup(sp);
            t = IDENTIFIER;
        }
    }
    g_prev_lex_tok = t;
    return t;
}

/* === Bloque B: full lexer state reset between top-level parse_file() calls.
 * Called from parse_file() in parser.y. Lives here in parser.l because the
 * YY_CURRENT_BUFFER macro and the BEGIN() / start condition machinery are
 * only visible inside the flex-generated lex.yy.c translation unit.
 *
 * Behavior:
 *   1) Drain any leftover include-stack buffers from a previously aborted
 *      parse (e.g., a syntax error inside an "import"-included file). Without
 *      this, the next parse_file() inherits stale buffers and reports bogus
 *      "syntax error in line 1" on a perfectly valid file.
 *   2) Close any FILE* still owned by the include-stack array (mirrors the
 *      cleanup that yywrap() would have done on a clean EOF).
 *   3) yyrestart(file) installs a fresh top-level buffer for `file`, resets
 *      the scanner DFA and clears any pending lookahead.
 *   4) BEGIN(INITIAL) resets the start condition (e.g., we may have aborted
 *      while inside <INCL>).
 */
void te_lexer_full_reset(FILE* file) {
    /* (2) close FILE*s tracked by the manual include stack */
    while (include_stack_ptr > 0) {
        FILE *cur = include_file_stack[--include_stack_ptr];
        if (cur) fclose(cur);
        include_file_stack[include_stack_ptr] = NULL;
    }
    /* (1) drain flex's own buffer stack */
    while (YY_CURRENT_BUFFER) {
        yypop_buffer_state();
    }
    /* (3) fresh buffer for the new file */
    yyrestart(file);
    /* (4) back to the default start condition */
    BEGIN(INITIAL);

    /* (4b) reset the yylex() previous-token tracker so a stale RSBRACKET from
     * an aborted prior parse can't swallow a leading `async` of the next file. */
    g_prev_lex_tok = 0;
    /* and clear any pending endpoint-async flag from an aborted prior parse. */
    g_pending_async = 0;

    /* (5) Bloque C: free the per-parse import dedup list.
     * imported_files_list is a global linked list used by <INCL> to skip
     * circular imports inside ONE parse. Across top-level parse_file() calls
     * (e.g. one per file in apis/), it must be cleared because:
     *   a) Two endpoint files importing the same module would silently
     *      skip the second import and lose its symbols.
     *   b) It accumulates forever otherwise (memory leak grows with the
     *      number of files loaded in the api server's lifetime). */
    while (imported_files_list) {
        ImportedFile *next = imported_files_list->next;
        if (imported_files_list->path) free(imported_files_list->path);
        free(imported_files_list);
        imported_files_list = next;
    }
}
