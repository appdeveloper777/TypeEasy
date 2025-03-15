%{
    #include "ast.h"
    #include "parser.tab.h"    
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
%}

%%

[ \t\r\n]+              ;
"var"                   { return VAR; }
"string"                 { return STRING; }
\"(\\.|[^\"])*\"         { 
                            yylval.sval = strdup(yytext + 1); 
                            yylval.sval[strlen(yylval.sval) - 1] = '\0'; 
                            return STRING_LITERAL; 
                        }
"int"                   { return INT; }
"float"                   { return FLOAT; }
"print"                 { return PRINT; }
"for"                   { return FOR; }
"("                     { return LPAREN; }
")"                     { return RPAREN; }
"{"                     { return LBRACKET; }
"}"                     { return RBRACKET; }
"="                     { return ASSIGN; }
";"                     { return SEMICOLON; }
"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return MULTIPLY; }
"/"                     { return DIVIDE; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.sval = strdup(yytext); return IDENTIFIER; }
[0-9]+                  { yylval.ival = atoi(yytext); return NUMBER; }
\"[^\"]*\"              { 
                            yylval.sval = strdup(yytext + 1); 
                            yylval.sval[strlen(yylval.sval) - 1] = '\0'; 
                            return STRING_LITERAL; 
                        }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.sval = strdup(yytext); return IDENTIFIER; }

%%

int yywrap() { 
    return 1; 
}