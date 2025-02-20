%{
#include "parser.tab.h"
#include <stdio.h>
#include <stdlib.h>
#include "variables.h"

%}

digit   [0-9]
letter  [a-zA-Z]

%%

[ \t\n]+             ;   // Ignorar espacios en blanco
"var"                { return VAR; }
"float"             { return FLOAT; }
"<"                 { return GREATERTHAN; }
"for"               { return FOR; }  // Reconoce "for" como un token FOR
{digit}+"."{digit}+ { 
    yylval.fval = atof(yytext);  // Convertir el texto en un valor float
    return FLOAT_LITERAL; 
}
"string"             { return STRING; }
"print"              { return PRINT; }
"="                  { return ASSIGN; }
[0-9]+               { yylval.num = atoi(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9]* { yylval.id = strdup(yytext); return IDENTIFIER; }
"+"                  { return PLUS; }
"-"                  { return MINUS; }
"*"                  { return MUL; }
"/"                  { return DIV; }
"("                { return LPAREN; }
")"                { return RPAREN; }
"{"              { return LBRACKET; }  // Define el corchete izquierdo
"}"              { return RBRACKET; }  // Define el corchete derecho
";"        { return SEMICOLON; }
","        { return COMMA; }
\"[^\"]*\"           { 
                        yylval.sval = strdup(yytext + 1);  // Ignorar comillas iniciales
                        yylval.sval[strlen(yylval.sval) - 1] = '\0';  // Eliminar comillas finales
                        return STRING_LITERAL; 
                     }



%%

int yywrap() {
    return 1;
}
