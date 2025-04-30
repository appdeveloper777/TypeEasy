/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_PARSER_TAB_H_INCLUDED
# define YY_YY_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    INT = 258,                     /* INT  */
    STRING = 259,                  /* STRING  */
    FLOAT = 260,                   /* FLOAT  */
    LAYER = 261,                   /* LAYER  */
    DATASET = 262,                 /* DATASET  */
    MODEL = 263,                   /* MODEL  */
    TRAIN = 264,                   /* TRAIN  */
    PREDICT = 265,                 /* PREDICT  */
    FROM = 266,                    /* FROM  */
    VAR = 267,                     /* VAR  */
    ASSIGN = 268,                  /* ASSIGN  */
    PRINT = 269,                   /* PRINT  */
    FOR = 270,                     /* FOR  */
    LPAREN = 271,                  /* LPAREN  */
    RPAREN = 272,                  /* RPAREN  */
    SEMICOLON = 273,               /* SEMICOLON  */
    CONCAT = 274,                  /* CONCAT  */
    PLUS = 275,                    /* PLUS  */
    MINUS = 276,                   /* MINUS  */
    MULTIPLY = 277,                /* MULTIPLY  */
    DIVIDE = 278,                  /* DIVIDE  */
    LBRACKET = 279,                /* LBRACKET  */
    RBRACKET = 280,                /* RBRACKET  */
    CLASS = 281,                   /* CLASS  */
    CONSTRUCTOR = 282,             /* CONSTRUCTOR  */
    THIS = 283,                    /* THIS  */
    NEW = 284,                     /* NEW  */
    LET = 285,                     /* LET  */
    COLON = 286,                   /* COLON  */
    COMMA = 287,                   /* COMMA  */
    DOT = 288,                     /* DOT  */
    RETURN = 289,                  /* RETURN  */
    IDENTIFIER = 290,              /* IDENTIFIER  */
    STRING_LITERAL = 291,          /* STRING_LITERAL  */
    NUMBER = 292                   /* NUMBER  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 17 "parser.y"

    int ival;
    char *sval;
    ASTNode *node;
    ParameterNode *pnode;

#line 108 "parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_PARSER_TAB_H_INCLUDED  */
