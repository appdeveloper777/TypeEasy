%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   
#include "ast.h"
#include "parser.tab.h"
%}

%%
"var"         { return VAR; }
"float"       { return FLOAT; }
"string"      { return STRING; }
"for"         { return FOR; }
"print"       { return PRINT; }
[0-9]+\.[0-9]+  { yylval.fval = atof(yytext); return FLOAT_LITERAL; }

[0-9]+          { yylval.ival = atoi(yytext); return NUMBER; }

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.id = strdup(yytext);
    return IDENTIFIER;
}
"\""[^\"]*"\"" {
    yylval.sval = strdup(yytext + 1);
    yylval.sval[strlen(yylval.sval) - 1] = '\0';
    return STRING_LITERAL;
}
"="           { return ASSIGN; }
"("           { return LPAREN; }
")"           { return RPAREN; }
"{"           { return LBRACKET; }
"}"           { return RBRACKET; }
";"           { return SEMICOLON; }
"+"           { return PLUS; }
"-"           { return MINUS; }
"*"           { return MUL; }
"/"           { return DIV; }
">"           { return GREATERTHAN; }

[ \t\n]+      { /* no hacemos nada, sólo los ignoramos */ }
%%
int yywrap(void) {
    return 1;
}