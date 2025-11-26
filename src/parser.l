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
    if (fname[0] == '/') snprintf(path, sizeof(path), "%s", fname);
    else snprintf(path, sizeof(path), "/app/%s", fname);
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
"orm_query"              { return ORM_QUERY; }

"var"                   { return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { return LET; }
"print"                 { return PRINT; }
"println"               { return PRINTLN; }
"fprint"                { return FPRINT; }
"fprintln"              { return FPRINTLN; }
"const"                 { return CONST; }

"HttpGet"               { return HTTPGET; }
"HTTPGET"               { return HTTPGET; }
"HttpPost"              { return HTTPPOST; }
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
"concat"                { return CONCAT; }
"in"                    { return IN; }

"int"                   { return INT; }
"float"                 { return FLOAT; }
"string"                { return STRING; }

">"                     { return GT; }  
"<"                     { return LT; } 
"<="                    { return GT_EQ; }
">="                    { return LT_EQ; }
"=="                    { return EQ; }
"!="                    { return DIFF; }
"=>"                    { return ARROW; }
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
"if"                    { return IF; }    
"else"                  { return ELSE; }  


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

[0-9]+                  {
    yylval.ival = atoi(yytext);
    return NUMBER;
}
[0-9]+\.[0-9]+          {
                            yylval.sval = strdup(yytext);
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
