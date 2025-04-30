%{
    #include "ast.h"
    #include "parser.tab.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
%}

%%

[ \t\r\n]+              ;    

"//".*                  {  }
"/*"([^*]|\*+[^*/])*\*+"/"  {  }


"dataset"    { yylval.sval = strdup(yytext); return DATASET; }
"model"      { yylval.sval = strdup(yytext); return MODEL; }
"train"      { yylval.sval = strdup(yytext); return TRAIN; }
"predict"    { yylval.sval = strdup(yytext); return PREDICT; }
"from"       { yylval.sval = strdup(yytext); return FROM; }
"layer"      { yylval.sval = strdup(yytext); return LAYER; }



"var"                   { return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { return LET; }
"print"                 { return PRINT; }
"for"                   { return FOR; }
"concat"                { return CONCAT; }


"int"                   { return INT; }
"float"                 { return FLOAT; }
"string"                { return STRING; }


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


\"(\\.|[^\"])*\"        {
                            yylval.sval = strdup(yytext + 1);
                            yylval.sval[strlen(yylval.sval) - 1] = '\0';
                            return STRING_LITERAL;
                        }


[0-9]+                  {
                            yylval.ival = atoi(yytext);
                            return NUMBER;
                        }


[A-Za-z_][A-Za-z0-9_]*  {
                            yylval.sval = strdup(yytext);
                            return IDENTIFIER;
                        }

%%

int yywrap() {
    return 1;
}
