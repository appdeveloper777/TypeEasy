%{
    #include "ast.h"
    #include "parser.tab.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

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

"var"                   { return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"extends"               { return EXTENDS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { return LET; }
"print"                 { return PRINT; }
"println"               { return PRINTLN; }
"fprint"                { return FPRINT; }
"fprintln"              { return FPRINTLN; }
"const"                 { yylval.sval = strdup(yytext); return CONST; }
"fn"                    { return FN; }

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

"for"                   { return FOR; }
"foreach"               { return FOREACH; }
"concat"                { return CONCAT; }
"in"                    { return IN; }

"int"                   { yylval.sval = strdup(yytext); return INT; }
"float"                 { yylval.sval = strdup(yytext); return FLOAT; }
"string"                { yylval.sval = strdup(yytext); return STRING; }
"void"                  { return VOID; }
"dynamic"               { return DYNAMIC; }
"public"                { return PUBLIC; }
"private"               { return PRIVATE; }
"protected"             { return PROTECTED; }
"null"                  { return NULLTOK; }
"true"                  { return TRUETOK; }
"false"                 { return FALSETOK; }
"bool"                  { yylval.sval = strdup(yytext); return BOOLTYPE; }
"datetime"              { yylval.sval = strdup(yytext); return DATETIMETYPE; }
"uuid"                  { yylval.sval = strdup(yytext); return UUIDTYPE; }
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
