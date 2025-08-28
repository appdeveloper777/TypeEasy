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
    FLOAT_LITERAL = 261,           /* FLOAT_LITERAL  */
    LAYER = 262,                   /* LAYER  */
    LSBRACKET = 263,               /* LSBRACKET  */
    RSBRACKET = 264,               /* RSBRACKET  */
    DATASET = 265,                 /* DATASET  */
    MODEL = 266,                   /* MODEL  */
    TRAIN = 267,                   /* TRAIN  */
    PREDICT = 268,                 /* PREDICT  */
    FROM = 269,                    /* FROM  */
    PLOT = 270,                    /* PLOT  */
    ARROW = 271,                   /* ARROW  */
    IN = 272,                      /* IN  */
    LAMBDA = 273,                  /* LAMBDA  */
    VAR = 274,                     /* VAR  */
    ASSIGN = 275,                  /* ASSIGN  */
    PRINT = 276,                   /* PRINT  */
    FOR = 277,                     /* FOR  */
    LPAREN = 278,                  /* LPAREN  */
    RPAREN = 279,                  /* RPAREN  */
    SEMICOLON = 280,               /* SEMICOLON  */
    CONCAT = 281,                  /* CONCAT  */
    PLUS = 282,                    /* PLUS  */
    MINUS = 283,                   /* MINUS  */
    MULTIPLY = 284,                /* MULTIPLY  */
    DIVIDE = 285,                  /* DIVIDE  */
    LBRACKET = 286,                /* LBRACKET  */
    RBRACKET = 287,                /* RBRACKET  */
    CLASS = 288,                   /* CLASS  */
    CONSTRUCTOR = 289,             /* CONSTRUCTOR  */
    THIS = 290,                    /* THIS  */
    NEW = 291,                     /* NEW  */
    LET = 292,                     /* LET  */
    COLON = 293,                   /* COLON  */
    COMMA = 294,                   /* COMMA  */
    DOT = 295,                     /* DOT  */
    RETURN = 296,                  /* RETURN  */
    IDENTIFIER = 297,              /* IDENTIFIER  */
    STRING_LITERAL = 298,          /* STRING_LITERAL  */
    NUMBER = 299,                  /* NUMBER  */
    GT = 300,                      /* GT  */
    LT = 301,                      /* LT  */
    EQ = 302,                      /* EQ  */
    GT_EQ = 303,                   /* GT_EQ  */
    LT_EQ = 304,                   /* LT_EQ  */
    DIFF = 305                     /* DIFF  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 20 "parser.y"

    int ival;
    char *sval;
    ASTNode *node;
    ParameterNode *pnode;

#line 121 "parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_PARSER_TAB_H_INCLUDED  */
