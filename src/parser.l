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
    fname[strlen(fname)-1] = '\0';

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
"layer"      { yylval.sval = strdup(yytext); return LAYER; }
"plot"                   { return PLOT; }       

"mysql_connect"          { return MYSQL_CONNECT; }
"mysql_close"            { return MYSQL_CLOSE; }
"mysql_query"            { return MYSQL_QUERY; }
"postgres_connect"       { return POSTGRES_CONNECT; }
"postgres_close"         { return POSTGRES_CLOSE; }
"postgres_query"         { return POSTGRES_QUERY; }
"sqlserver_connect"      { return SQLSERVER_CONNECT; }
"sqlserver_close"        { return SQLSERVER_CLOSE; }
"sqlserver_query"        { return SQLSERVER_QUERY; }
"orm_query"              { return ORM_QUERY; }

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
"const"                 { return CONST; }
"fn"                    { return FN; }

"HttpGet"               { return HTTPGET; }
"HTTPGET"               { return HTTPGET; }
"HttpPost"              { return HTTPPOST; }
"HttpPut"               { return HTTPPUT; }
"HttpDelete"            { return HTTPDELETE; }
"HttpPatch"             { return HTTPPATCH; }
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
"@Cache"                { return CACHE; }

"for"                   { return FOR; }
"foreach"               { return FOREACH; }
"concat"                { return CONCAT; }
"in"                    { return IN; }

"int"                   { return INT; }
"float"                 { return FLOAT; }
"string"                { return STRING; }
"void"                  { return VOID; }
"dynamic"               { return DYNAMIC; }
"null"                  { return NULLTOK; }
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
"["                     { return LSBRACKET;}
"]"                     { return RSBRACKET;}
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


[A-Za-z_][A-Za-z0-9_]*  {
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
