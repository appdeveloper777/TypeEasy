%{
    #include "ast.h"
    #include "parser.tab.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
%}

%%

[ \t\r]+                ;
\n                      { yylineno++; }    

"//".*                  {  }
"/*"([^*]|\*+[^*/])*\*+"/"  {  }


"dataset"    { yylval.sval = strdup(yytext); return DATASET; }
"model"      { yylval.sval = strdup(yytext); return MODEL; }
"train"      { yylval.sval = strdup(yytext); return TRAIN; }
"predict"    { yylval.sval = strdup(yytext); return PREDICT; }
"from"       { yylval.sval = strdup(yytext); return FROM; }
"layer"      { yylval.sval = strdup(yytext); return LAYER; }
"plot"                   { return PLOT; }       

"mysql_connect"          { printf("LEXER: mysql_connect\n"); fflush(stdout); return MYSQL_CONNECT; }
"mysql_close"            { printf("LEXER: mysql_close\n"); fflush(stdout); return MYSQL_CLOSE; }
"mysql_query"            { printf("LEXER: mysql_query\n"); fflush(stdout); return MYSQL_QUERY; }
"orm_query"              { printf("LEXER: orm_query\n"); fflush(stdout); return ORM_QUERY; }




"var"                   { return VAR; }
"return"                { return RETURN; }
"class"                 { return CLASS; }
"__constructor"         { return CONSTRUCTOR; }
"this"                  { return THIS; }
"new"                   { return NEW; }
"let"                   { return LET; }
"print"                 { return PRINT; }
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


"println"               { return PRINTLN; }
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
                          // Asigna memoria para la nueva cadena procesada.
                          // yytext - 2 es un tamaño seguro (quitando las 2 comillas de los extremos).
                          yylval.sval = (char *) malloc(strlen(yytext) - 1);
                          
                          // Puntero de escritura 'w' para nuestra nueva cadena.
                          char *w = yylval.sval;

                          // Itera sobre el contenido de la cadena en yytext (saltando las comillas de los extremos).
                          for (char *s = yytext + 1; *s != '"' || (*(s-1) == '\\' && *s == '"'); s++) {
                              if (*s == '\\') { // Si encontramos una barra invertida...
                                  s++; // ...avanzamos al siguiente carácter para ver qué es.
                                  switch (*s) {
                                      case 'n': // Secuencia de escape para nueva línea
                                          *w++ = '\n';
                                          break;
                                      case 't': // Secuencia de escape para tabulación
                                          *w++ = '\t';
                                          break;
                                      case '"': // Secuencia de escape para comillas dobles
                                          *w++ = '"';
                                          break;
                                      case '\\': // Secuencia de escape para la propia barra invertida
                                          *w++ = '\\';
                                          break;
                                      default: // Para cualquier otro carácter, simplemente lo copiamos
                                          *w++ = *s;
                                          break;
                                  }
                              } else {
                                  // Si no es una secuencia de escape, solo copia el carácter.
                                  *w++ = *s;
                              }
                          }
                          // Asegúrate de terminar la cadena con el carácter nulo.
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
    return 1;
}
