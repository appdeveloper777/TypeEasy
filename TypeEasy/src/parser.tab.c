/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "parser.y"
   

    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    #include <locale.h>
    #include <time.h>

    ASTNode *root;
    extern int yylineno;
    extern FILE *yyin;
    void yyerror(const char *s);
    ClassNode *last_class = NULL;
    int yylex();
    void generate_code(const char* code);
    void clean_generated_code();


#line 90 "parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_INT = 3,                        /* INT  */
  YYSYMBOL_STRING = 4,                     /* STRING  */
  YYSYMBOL_FLOAT = 5,                      /* FLOAT  */
  YYSYMBOL_FLOAT_LITERAL = 6,              /* FLOAT_LITERAL  */
  YYSYMBOL_LAYER = 7,                      /* LAYER  */
  YYSYMBOL_LSBRACKET = 8,                  /* LSBRACKET  */
  YYSYMBOL_RSBRACKET = 9,                  /* RSBRACKET  */
  YYSYMBOL_DATASET = 10,                   /* DATASET  */
  YYSYMBOL_MODEL = 11,                     /* MODEL  */
  YYSYMBOL_TRAIN = 12,                     /* TRAIN  */
  YYSYMBOL_PREDICT = 13,                   /* PREDICT  */
  YYSYMBOL_FROM = 14,                      /* FROM  */
  YYSYMBOL_PLOT = 15,                      /* PLOT  */
  YYSYMBOL_ARROW = 16,                     /* ARROW  */
  YYSYMBOL_IN = 17,                        /* IN  */
  YYSYMBOL_LAMBDA = 18,                    /* LAMBDA  */
  YYSYMBOL_VAR = 19,                       /* VAR  */
  YYSYMBOL_ASSIGN = 20,                    /* ASSIGN  */
  YYSYMBOL_PRINT = 21,                     /* PRINT  */
  YYSYMBOL_FOR = 22,                       /* FOR  */
  YYSYMBOL_LPAREN = 23,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 24,                    /* RPAREN  */
  YYSYMBOL_SEMICOLON = 25,                 /* SEMICOLON  */
  YYSYMBOL_CONCAT = 26,                    /* CONCAT  */
  YYSYMBOL_PLUS = 27,                      /* PLUS  */
  YYSYMBOL_MINUS = 28,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 29,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 30,                    /* DIVIDE  */
  YYSYMBOL_LBRACKET = 31,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 32,                  /* RBRACKET  */
  YYSYMBOL_CLASS = 33,                     /* CLASS  */
  YYSYMBOL_CONSTRUCTOR = 34,               /* CONSTRUCTOR  */
  YYSYMBOL_THIS = 35,                      /* THIS  */
  YYSYMBOL_NEW = 36,                       /* NEW  */
  YYSYMBOL_LET = 37,                       /* LET  */
  YYSYMBOL_COLON = 38,                     /* COLON  */
  YYSYMBOL_COMMA = 39,                     /* COMMA  */
  YYSYMBOL_DOT = 40,                       /* DOT  */
  YYSYMBOL_RETURN = 41,                    /* RETURN  */
  YYSYMBOL_IDENTIFIER = 42,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 43,            /* STRING_LITERAL  */
  YYSYMBOL_NUMBER = 44,                    /* NUMBER  */
  YYSYMBOL_GT = 45,                        /* GT  */
  YYSYMBOL_LT = 46,                        /* LT  */
  YYSYMBOL_EQ = 47,                        /* EQ  */
  YYSYMBOL_GT_EQ = 48,                     /* GT_EQ  */
  YYSYMBOL_LT_EQ = 49,                     /* LT_EQ  */
  YYSYMBOL_DIFF = 50,                      /* DIFF  */
  YYSYMBOL_YYACCEPT = 51,                  /* $accept  */
  YYSYMBOL_program = 52,                   /* program  */
  YYSYMBOL_class_decl = 53,                /* class_decl  */
  YYSYMBOL_54_1 = 54,                      /* $@1  */
  YYSYMBOL_class_member = 55,              /* class_member  */
  YYSYMBOL_class_body = 56,                /* class_body  */
  YYSYMBOL_train_options = 57,             /* train_options  */
  YYSYMBOL_layer_list = 58,                /* layer_list  */
  YYSYMBOL_layer_decl = 59,                /* layer_decl  */
  YYSYMBOL_attribute_decl = 60,            /* attribute_decl  */
  YYSYMBOL_constructor_decl = 61,          /* constructor_decl  */
  YYSYMBOL_parameter_decl = 62,            /* parameter_decl  */
  YYSYMBOL_parameter_list = 63,            /* parameter_list  */
  YYSYMBOL_method_decl = 64,               /* method_decl  */
  YYSYMBOL_expression = 65,                /* expression  */
  YYSYMBOL_var_decl = 66,                  /* var_decl  */
  YYSYMBOL_statement = 67,                 /* statement  */
  YYSYMBOL_statement_list = 68,            /* statement_list  */
  YYSYMBOL_expression_list = 69,           /* expression_list  */
  YYSYMBOL_expr_list = 70,                 /* expr_list  */
  YYSYMBOL_lambda = 71,                    /* lambda  */
  YYSYMBOL_object_expression = 72,         /* object_expression  */
  YYSYMBOL_object_list = 73,               /* object_list  */
  YYSYMBOL_lambda_expression = 74          /* lambda_expression  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  53
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   962

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  51
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  24
/* YYNRULES -- Number of rules.  */
#define YYNRULES  110
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  339

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   305


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    64,    64,    65,    66,    67,    71,    71,    76,    77,
      78,    82,    83,    86,    87,    88,    89,    93,    94,    98,
     102,   103,   104,   105,   106,   107,   111,   112,   113,   114,
     115,   119,   120,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   160,   161,   162,   163,   164,   165,   166,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,   185,   186,   189,   190,   191,   192,
     193,   194,   195,   196,   197,   198,   199,   200,   201,   214,
     215,   219,   220,   224,   225,   234,   235,   246,   258,   263,
     268
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "INT", "STRING",
  "FLOAT", "FLOAT_LITERAL", "LAYER", "LSBRACKET", "RSBRACKET", "DATASET",
  "MODEL", "TRAIN", "PREDICT", "FROM", "PLOT", "ARROW", "IN", "LAMBDA",
  "VAR", "ASSIGN", "PRINT", "FOR", "LPAREN", "RPAREN", "SEMICOLON",
  "CONCAT", "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "LBRACKET", "RBRACKET",
  "CLASS", "CONSTRUCTOR", "THIS", "NEW", "LET", "COLON", "COMMA", "DOT",
  "RETURN", "IDENTIFIER", "STRING_LITERAL", "NUMBER", "GT", "LT", "EQ",
  "GT_EQ", "LT_EQ", "DIFF", "$accept", "program", "class_decl", "$@1",
  "class_member", "class_body", "train_options", "layer_list",
  "layer_decl", "attribute_decl", "constructor_decl", "parameter_decl",
  "parameter_list", "method_decl", "expression", "var_decl", "statement",
  "statement_list", "expression_list", "expr_list", "lambda",
  "object_expression", "object_list", "lambda_expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-234)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-53)

#define yytable_value_is_error(Yyn) \
  ((Yyn) == YYTABLE_NINF)

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     138,     4,    12,    76,    91,   102,   110,    64,    68,   133,
     112,   141,   149,   146,   144,   156,   158,   496,    37,    94,
    -234,  -234,  -234,   185,   496,   186,   187,   176,   194,   178,
     168,   169,   496,   192,   535,    53,  -234,   171,   193,   200,
    -234,   539,   198,   496,   199,   188,   183,    -6,  -234,  -234,
     702,   578,   184,  -234,  -234,  -234,   496,   716,   582,   496,
     190,   189,   228,   201,   203,   898,   -15,   621,    49,   646,
     202,   229,   215,    33,   227,   363,   208,   898,    -7,  -234,
      11,   210,   660,   496,   211,   231,   496,   213,  -234,   496,
     496,   496,   496,   214,   496,   496,   496,   496,   496,   496,
     730,   744,   142,   758,  -234,   234,   772,   786,   221,   239,
     223,    -4,  -234,   224,   230,   249,   496,   252,   800,   238,
     257,   267,   242,  -234,   496,   259,   264,   247,   250,    62,
     814,   268,  -234,   496,  -234,   258,   254,  -234,    45,   274,
     367,   870,   276,   898,   898,   898,   898,   277,   912,   912,
     912,   912,   912,   912,  -234,   496,   406,  -234,  -234,  -234,
    -234,   262,  -234,   282,  -234,  -234,   273,   290,  -234,   898,
     278,  -234,   153,  -234,   496,   281,    80,   828,   297,  -234,
     284,   292,   496,   287,  -234,   367,   898,   289,  -234,   293,
    -234,   302,  -234,    87,   294,   410,   449,   842,   307,    95,
     313,   295,   298,   319,   315,   320,   674,   496,  -234,   323,
     100,  -234,  -234,  -234,  -234,  -234,  -234,   310,   453,   884,
     332,   898,   101,   337,   338,  -234,  -234,   347,   340,  -234,
     108,   625,  -234,    70,   122,   342,  -234,  -234,   345,   349,
     333,   358,   355,  -234,   343,  -234,   353,   856,     7,     2,
     179,   369,   370,   131,   294,   492,  -234,   496,  -234,   496,
    -234,  -234,    97,  -234,  -234,  -234,  -234,  -234,   350,   352,
     372,   376,   346,   496,   359,   362,   366,   375,  -234,   139,
     384,   157,   392,   395,  -234,  -234,   396,   398,   399,   898,
     409,   402,  -234,  -234,   403,  -234,    26,   688,  -234,  -234,
    -234,    10,   400,    18,   346,   404,  -234,  -234,  -234,   412,
    -234,   496,   413,  -234,  -234,  -234,   408,  -234,  -234,  -234,
    -234,   346,   389,  -234,   182,   346,  -234,   898,  -234,   346,
     226,    61,  -234,   266,   306,  -234,  -234,  -234,  -234
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       5,    73,     4,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     6,     0,     0,     0,
      53,     0,     0,     0,     0,     0,     0,    51,    54,    52,
       0,     0,     0,     1,     3,     2,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   101,     0,     0,    51,     0,
       0,     0,     0,     0,     0,     0,     0,   103,     0,   108,
       0,     0,     0,     0,     0,     0,     0,     0,    72,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      97,     0,     0,     0,    74,    54,     0,     0,     0,     0,
       0,     0,    14,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    11,     0,     0,     0,     0,     0,    51,
       0,     0,    42,     0,    40,     0,     0,    60,     0,    49,
       0,     0,    46,    56,    57,    58,    59,     0,    33,    34,
      35,    36,    37,    38,    82,     0,     0,    67,    78,    65,
      80,     0,    89,     0,    94,    15,     0,     0,    93,   102,
       0,    66,    46,    83,     0,     0,     0,     0,     0,    86,
       0,     0,     0,     0,    64,     0,   104,     0,   109,     0,
      55,     0,    61,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     7,     0,
       0,    12,     8,     9,    10,    69,    77,     0,     0,     0,
      46,   101,     0,     0,     0,    50,    62,     0,     0,    44,
       0,     0,    47,    51,     0,     0,    68,    75,     0,     0,
       0,     0,     0,    90,     0,    84,     0,     0,    26,    26,
       0,     0,    61,     0,     0,     0,   107,     0,    43,     0,
      39,    45,    51,   105,    48,    41,    76,    95,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    29,     0,
       0,     0,     0,     0,    98,    87,     0,     0,    44,   110,
       0,     0,    13,    96,    43,   100,     0,     0,    21,    23,
      25,     0,     0,     0,     0,     0,    17,    18,    88,    39,
      71,     0,     0,    91,    70,    99,     0,    20,    22,    24,
      27,     0,     0,    30,     0,     0,    63,   106,    16,     0,
       0,     0,    31,     0,     0,    19,    28,    32,    85
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -234,  -234,   421,  -234,  -234,  -234,  -234,  -234,   336,  -234,
    -234,   140,   195,  -234,   -16,  -234,     0,  -233,   -28,   415,
    -234,   316,  -234,   204
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    19,    20,    72,   211,   176,   242,   111,   112,   212,
     213,   278,   279,   214,    65,    21,   295,   296,   222,   193,
     235,    79,    80,   228
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      22,    50,   132,   110,    66,   274,   275,   276,    57,   115,
     274,   275,   276,   317,   318,   319,    24,    86,    69,    55,
     134,   274,   275,   276,   116,    77,   280,    82,   164,     1,
       2,     3,   133,     4,    87,   101,     5,     6,     7,     8,
     103,     9,   106,   107,   277,    10,    23,    11,    12,   277,
     135,   118,   320,   124,    25,   138,   125,    51,   314,   130,
     322,    14,    15,    16,   317,   318,   319,    17,    18,   190,
     141,   324,    86,   143,   144,   145,   146,    52,   148,   149,
     150,   151,   152,   153,   116,   182,   263,    30,   330,   119,
      70,    31,   333,    86,    53,    71,   334,     1,     2,     3,
     169,     4,   183,   336,     5,     6,     7,     8,   177,     9,
      87,   226,   208,    10,   209,    11,    12,   186,    26,   238,
      86,   290,   210,   249,    77,   256,   133,    13,   199,    14,
      15,    16,   261,    27,   116,    17,    18,    87,   250,   197,
     116,     1,     2,     3,    28,     4,   264,   116,     5,     6,
       7,     8,    29,     9,    33,   286,    32,    10,   206,    11,
      12,   116,   155,   302,    34,   156,   219,   230,   234,   221,
     116,    13,    35,    14,    15,    16,   195,   205,   303,    17,
      18,   305,   282,   283,    37,     1,     2,     3,    36,     4,
     253,   247,     5,     6,     7,     8,   303,     9,    38,    60,
      39,    10,   221,    11,    12,    56,    58,    59,    61,    62,
      63,    64,    67,    73,   332,    82,    74,    14,    15,    16,
      75,    81,    83,    17,    18,    85,   102,   230,    84,     1,
       2,     3,   109,     4,   108,   110,     5,     6,     7,     8,
     113,     9,   114,   289,   121,    10,   123,    11,    12,   122,
     131,   126,   136,   139,   140,   142,   147,   297,   335,   158,
     161,    14,    15,    16,   162,   163,   166,    17,    18,     1,
       2,     3,   167,     4,   168,   170,     5,     6,     7,     8,
     172,     9,   173,   178,   174,    10,   175,    11,    12,   179,
     180,   185,   181,   189,   187,   327,   315,   191,   337,   195,
     196,    14,    15,    16,   200,   201,   207,    17,    18,     1,
       2,     3,   202,     4,   203,   218,     5,     6,     7,     8,
     204,     9,   216,   217,   315,    10,   225,    11,    12,   220,
     315,   223,   237,   315,   315,   224,   227,   239,   338,   240,
     241,    14,    15,    16,   243,   245,   248,    17,    18,     1,
       2,     3,   251,     4,   244,   255,     5,     6,     7,     8,
     257,     9,   258,   259,   260,    10,   265,    11,    12,    40,
     266,    41,   268,    40,   267,    41,    42,   127,   269,   270,
      42,    14,    15,    16,   272,   271,    43,    17,    18,    44,
      43,   192,   291,    44,   284,   285,   292,   293,    45,   128,
     294,   298,    45,    46,   299,   129,    48,    49,   300,    47,
      48,    49,    40,   301,    41,   304,    40,   306,    41,    42,
     307,   308,   309,    42,   310,   311,   312,   331,   313,    43,
     198,   321,    44,    43,   229,   325,    44,   326,   328,   329,
      54,    45,    46,   323,   281,    45,    46,   165,    47,    48,
      49,   188,    47,    48,    49,    40,    78,    41,   287,    40,
       0,    41,    42,     0,     0,     0,    42,     0,     0,     0,
       0,     0,   231,   232,     0,    44,    43,   252,     0,    44,
       0,     0,     0,     0,    45,    46,     0,     0,    45,    46,
       0,   233,    48,    49,     0,    47,    48,    49,    40,     0,
      41,     0,    40,     0,    41,    42,     0,     0,     0,    42,
       0,     0,     0,     0,     0,    43,   288,     0,    44,    43,
       0,     0,    44,     0,     0,     0,     0,    45,    46,     0,
       0,    45,    46,     0,    47,    48,    49,     0,    47,    48,
      49,    40,     0,    41,     0,    40,     0,    41,    42,     0,
       0,     0,    42,     0,     0,     0,     0,     0,    43,     0,
       0,    44,    43,     0,     0,    44,     0,     0,     0,     0,
      45,    46,     0,     0,    45,    76,     0,    68,    48,    49,
       0,    47,    48,    49,    40,     0,    41,     0,    40,     0,
      41,    42,     0,     0,     0,    42,     0,     0,     0,     0,
       0,    43,     0,     0,    44,    43,     0,     0,    44,     0,
       0,     0,     0,    45,    46,     0,     0,    45,    46,     0,
      47,    48,   100,     0,    47,   105,    49,    40,     0,    41,
       0,    40,     0,    41,   117,     0,     0,     0,    42,     0,
       0,     0,     0,     0,    43,     0,     0,    44,    43,     0,
       0,    44,     0,     0,     0,     0,    45,    46,     0,     0,
      45,    46,     0,    47,    48,    49,     0,   262,    48,    49,
     120,     0,     0,    89,    90,    91,    92,     0,     0,     0,
       0,     0,     0,     0,   137,     0,    93,    89,    90,    91,
      92,    94,    95,    96,    97,    98,    99,     0,   246,     0,
      93,    89,    90,    91,    92,    94,    95,    96,    97,    98,
      99,     0,   316,     0,    93,    89,    90,    91,    92,    94,
      95,    96,    97,    98,    99,     0,     0,    88,    93,    89,
      90,    91,    92,    94,    95,    96,    97,    98,    99,     0,
       0,   104,    93,    89,    90,    91,    92,    94,    95,    96,
      97,    98,    99,     0,     0,   -52,    93,   -52,   -52,   -52,
     -52,    94,    95,    96,    97,    98,    99,     0,     0,   154,
     -52,    89,    90,    91,    92,   -52,   -52,   -52,   -52,   -52,
     -52,     0,     0,   157,    93,    89,    90,    91,    92,    94,
      95,    96,    97,    98,    99,     0,     0,   159,    93,    89,
      90,    91,    92,    94,    95,    96,    97,    98,    99,     0,
       0,   160,    93,    89,    90,    91,    92,    94,    95,    96,
      97,    98,    99,     0,     0,   171,    93,    89,    90,    91,
      92,    94,    95,    96,    97,    98,    99,     0,     0,   184,
      93,    89,    90,    91,    92,    94,    95,    96,    97,    98,
      99,     0,     0,   215,    93,    89,    90,    91,    92,    94,
      95,    96,    97,    98,    99,     0,     0,   236,    93,    89,
      90,    91,    92,    94,    95,    96,    97,    98,    99,     0,
       0,   273,    93,    89,    90,    91,    92,    94,    95,    96,
      97,    98,    99,     0,     0,     0,    93,    89,    90,    91,
      92,    94,    95,    96,    97,    98,    99,     0,     0,   194,
      93,    89,    90,    91,    92,    94,    95,    96,    97,    98,
      99,     0,     0,   254,    93,    89,    90,    91,    92,    94,
      95,    96,    97,    98,    99,     0,     0,     0,    93,    89,
      90,    91,    92,    94,    95,    96,    97,    98,    99,     0,
       0,     0,    93,     0,     0,     0,     0,   -53,   -53,   -53,
     -53,   -53,   -53
};

static const yytype_int16 yycheck[] =
{
       0,    17,     9,     7,    32,     3,     4,     5,    24,    24,
       3,     4,     5,     3,     4,     5,     4,    23,    34,    19,
       9,     3,     4,     5,    39,    41,    24,    43,    32,     3,
       4,     5,    39,     7,    40,    51,    10,    11,    12,    13,
      56,    15,    58,    59,    42,    19,    42,    21,    22,    42,
      39,    67,    42,    20,    42,    83,    23,    20,    32,    75,
      42,    35,    36,    37,     3,     4,     5,    41,    42,    24,
      86,   304,    23,    89,    90,    91,    92,    40,    94,    95,
      96,    97,    98,    99,    39,    23,    16,    23,   321,    40,
      37,    23,   325,    23,     0,    42,   329,     3,     4,     5,
     116,     7,    40,    42,    10,    11,    12,    13,   124,    15,
      40,    24,    32,    19,    34,    21,    22,   133,    42,    24,
      23,    24,    42,    23,   140,    24,    39,    33,   156,    35,
      36,    37,    24,    42,    39,    41,    42,    40,    38,   155,
      39,     3,     4,     5,    42,     7,    24,    39,    10,    11,
      12,    13,    42,    15,    42,    24,    23,    19,   174,    21,
      22,    39,    20,    24,    23,    23,   182,   195,   196,   185,
      39,    33,    23,    35,    36,    37,    23,    24,    39,    41,
      42,    24,     3,     4,    40,     3,     4,     5,    42,     7,
     218,   207,    10,    11,    12,    13,    39,    15,    42,    23,
      42,    19,   218,    21,    22,    20,    20,    20,    14,    31,
      42,    42,    20,    42,    32,   231,    23,    35,    36,    37,
      20,    23,    23,    41,    42,    42,    42,   255,    40,     3,
       4,     5,    43,     7,    44,     7,    10,    11,    12,    13,
      39,    15,    39,   259,    42,    19,    31,    21,    22,    20,
      42,    24,    42,    42,    23,    42,    42,   273,    32,    25,
      39,    35,    36,    37,    25,    42,    42,    41,    42,     3,
       4,     5,    42,     7,    25,    23,    10,    11,    12,    13,
      42,    15,    25,    24,    17,    19,    44,    21,    22,    25,
      43,    23,    42,    39,    36,   311,   296,    23,    32,    23,
      23,    35,    36,    37,    42,    23,    25,    41,    42,     3,
       4,     5,    39,     7,    24,    23,    10,    11,    12,    13,
      42,    15,    25,    39,   324,    19,    24,    21,    22,    42,
     330,    42,    25,   333,   334,    42,    42,    24,    32,    44,
      42,    35,    36,    37,    25,    25,    23,    41,    42,     3,
       4,     5,    42,     7,    39,    23,    10,    11,    12,    13,
      23,    15,    24,    16,    24,    19,    24,    21,    22,     6,
      25,     8,    39,     6,    25,     8,    13,    14,    20,    24,
      13,    35,    36,    37,    31,    42,    23,    41,    42,    26,
      23,    24,    42,    26,    25,    25,    44,    25,    35,    36,
      24,    42,    35,    36,    42,    42,    43,    44,    42,    42,
      43,    44,     6,    38,     8,    31,     6,    25,     8,    13,
      25,    25,    24,    13,    25,    16,    24,    38,    25,    23,
      24,    31,    26,    23,    24,    31,    26,    25,    25,    31,
      19,    35,    36,   303,   249,    35,    36,   111,    42,    43,
      44,   135,    42,    43,    44,     6,    41,     8,   254,     6,
      -1,     8,    13,    -1,    -1,    -1,    13,    -1,    -1,    -1,
      -1,    -1,    23,    24,    -1,    26,    23,    24,    -1,    26,
      -1,    -1,    -1,    -1,    35,    36,    -1,    -1,    35,    36,
      -1,    42,    43,    44,    -1,    42,    43,    44,     6,    -1,
       8,    -1,     6,    -1,     8,    13,    -1,    -1,    -1,    13,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    26,    23,
      -1,    -1,    26,    -1,    -1,    -1,    -1,    35,    36,    -1,
      -1,    35,    36,    -1,    42,    43,    44,    -1,    42,    43,
      44,     6,    -1,     8,    -1,     6,    -1,     8,    13,    -1,
      -1,    -1,    13,    -1,    -1,    -1,    -1,    -1,    23,    -1,
      -1,    26,    23,    -1,    -1,    26,    -1,    -1,    -1,    -1,
      35,    36,    -1,    -1,    35,    36,    -1,    42,    43,    44,
      -1,    42,    43,    44,     6,    -1,     8,    -1,     6,    -1,
       8,    13,    -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    26,    23,    -1,    -1,    26,    -1,
      -1,    -1,    -1,    35,    36,    -1,    -1,    35,    36,    -1,
      42,    43,    44,    -1,    42,    43,    44,     6,    -1,     8,
      -1,     6,    -1,     8,    13,    -1,    -1,    -1,    13,    -1,
      -1,    -1,    -1,    -1,    23,    -1,    -1,    26,    23,    -1,
      -1,    26,    -1,    -1,    -1,    -1,    35,    36,    -1,    -1,
      35,    36,    -1,    42,    43,    44,    -1,    42,    43,    44,
      24,    -1,    -1,    27,    28,    29,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    24,    -1,    40,    27,    28,    29,
      30,    45,    46,    47,    48,    49,    50,    -1,    24,    -1,
      40,    27,    28,    29,    30,    45,    46,    47,    48,    49,
      50,    -1,    24,    -1,    40,    27,    28,    29,    30,    45,
      46,    47,    48,    49,    50,    -1,    -1,    25,    40,    27,
      28,    29,    30,    45,    46,    47,    48,    49,    50,    -1,
      -1,    25,    40,    27,    28,    29,    30,    45,    46,    47,
      48,    49,    50,    -1,    -1,    25,    40,    27,    28,    29,
      30,    45,    46,    47,    48,    49,    50,    -1,    -1,    25,
      40,    27,    28,    29,    30,    45,    46,    47,    48,    49,
      50,    -1,    -1,    25,    40,    27,    28,    29,    30,    45,
      46,    47,    48,    49,    50,    -1,    -1,    25,    40,    27,
      28,    29,    30,    45,    46,    47,    48,    49,    50,    -1,
      -1,    25,    40,    27,    28,    29,    30,    45,    46,    47,
      48,    49,    50,    -1,    -1,    25,    40,    27,    28,    29,
      30,    45,    46,    47,    48,    49,    50,    -1,    -1,    25,
      40,    27,    28,    29,    30,    45,    46,    47,    48,    49,
      50,    -1,    -1,    25,    40,    27,    28,    29,    30,    45,
      46,    47,    48,    49,    50,    -1,    -1,    25,    40,    27,
      28,    29,    30,    45,    46,    47,    48,    49,    50,    -1,
      -1,    25,    40,    27,    28,    29,    30,    45,    46,    47,
      48,    49,    50,    -1,    -1,    -1,    40,    27,    28,    29,
      30,    45,    46,    47,    48,    49,    50,    -1,    -1,    39,
      40,    27,    28,    29,    30,    45,    46,    47,    48,    49,
      50,    -1,    -1,    39,    40,    27,    28,    29,    30,    45,
      46,    47,    48,    49,    50,    -1,    -1,    -1,    40,    27,
      28,    29,    30,    45,    46,    47,    48,    49,    50,    -1,
      -1,    -1,    40,    -1,    -1,    -1,    -1,    45,    46,    47,
      48,    49,    50
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     7,    10,    11,    12,    13,    15,
      19,    21,    22,    33,    35,    36,    37,    41,    42,    52,
      53,    66,    67,    42,     4,    42,    42,    42,    42,    42,
      23,    23,    23,    42,    23,    23,    42,    40,    42,    42,
       6,     8,    13,    23,    26,    35,    36,    42,    43,    44,
      65,    20,    40,     0,    53,    67,    20,    65,    20,    20,
      23,    14,    31,    42,    42,    65,    69,    20,    42,    65,
      37,    42,    54,    42,    23,    20,    36,    65,    70,    72,
      73,    23,    65,    23,    40,    42,    23,    40,    25,    27,
      28,    29,    30,    40,    45,    46,    47,    48,    49,    50,
      44,    65,    42,    65,    25,    43,    65,    65,    44,    43,
       7,    58,    59,    39,    39,    24,    39,    13,    65,    40,
      24,    42,    20,    31,    20,    23,    24,    14,    36,    42,
      65,    42,     9,    39,     9,    39,    42,    24,    69,    42,
      23,    65,    42,    65,    65,    65,    65,    42,    65,    65,
      65,    65,    65,    65,    25,    20,    23,    25,    25,    25,
      25,    39,    25,    42,    32,    59,    42,    42,    25,    65,
      23,    25,    42,    25,    17,    44,    56,    65,    24,    25,
      43,    42,    23,    40,    25,    23,    65,    36,    72,    39,
      24,    23,    24,    70,    39,    23,    23,    65,    24,    69,
      42,    23,    39,    24,    42,    24,    65,    25,    32,    34,
      42,    55,    60,    61,    64,    25,    25,    39,    23,    65,
      42,    65,    69,    42,    42,    24,    24,    42,    74,    24,
      69,    23,    24,    42,    69,    71,    25,    25,    24,    24,
      44,    42,    57,    25,    39,    25,    24,    65,    23,    23,
      38,    42,    24,    69,    39,    23,    24,    23,    24,    16,
      24,    24,    42,    16,    24,    24,    25,    25,    39,    20,
      24,    42,    31,    25,     3,     4,     5,    42,    62,    63,
      24,    63,     3,     4,    25,    25,    24,    74,    24,    65,
      24,    42,    44,    25,    24,    67,    68,    65,    42,    42,
      42,    38,    24,    39,    31,    24,    25,    25,    25,    24,
      25,    16,    24,    25,    32,    67,    24,     3,     4,     5,
      42,    31,    42,    62,    68,    31,    25,    65,    25,    31,
      68,    38,    32,    68,    68,    32,    42,    32,    32
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    51,    52,    52,    52,    52,    54,    53,    55,    55,
      55,    56,    56,    57,    58,    58,    59,    60,    60,    61,
      62,    62,    62,    62,    62,    62,    63,    63,    63,    63,
      63,    64,    64,    65,    65,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    65,    65,    65,    65,
      65,    65,    65,    66,    66,    66,    66,    66,    66,    66,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    67,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    67,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    68,
      68,    69,    69,    70,    70,    71,    71,    72,    73,    73,
      74
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     1,     1,
       1,     0,     2,     3,     1,     2,     8,     4,     4,     7,
       3,     2,     3,     2,     3,     2,     0,     3,     5,     1,
       3,     6,     7,     3,     3,     3,     3,     3,     3,     6,
       3,     6,     3,     6,     5,     6,     3,     5,     6,     3,
       5,     1,     1,     1,     1,     4,     3,     3,     3,     3,
       3,     4,     5,    10,     5,     5,     5,     5,     6,     6,
      10,     9,     3,     1,     4,     6,     7,     6,     5,     5,
       5,     5,     4,     5,     7,    13,     5,     8,     9,     5,
       7,    10,     5,     5,     5,     8,     9,     3,     8,     2,
       1,     1,     3,     1,     3,     2,     5,     5,     1,     3,
       3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: program statement  */
#line 64 "parser.y"
                                { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[-1].node), (yyvsp[0].node)); root = (yyval.node); }
#line 1481 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 65 "parser.y"
                                  { (yyval.node) = (yyvsp[-1].node); /* Ignora definición de clase */ root = (yyval.node); }
#line 1487 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 66 "parser.y"
                                  { (yyval.node) = (yyvsp[0].node); root = (yyval.node); }
#line 1493 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 67 "parser.y"
                                  { (yyval.node) = NULL; /* Ignora definición de clase */ }
#line 1499 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 71 "parser.y"
                         { last_class = create_class((yyvsp[0].sval)); add_class(last_class); }
#line 1505 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 72 "parser.y"
                                     { (yyval.node) = NULL; }
#line 1511 "parser.tab.c"
    break;

  case 11: /* class_body: %empty  */
#line 82 "parser.y"
        { (yyval.node) = NULL; }
#line 1517 "parser.tab.c"
    break;

  case 12: /* class_body: class_body class_member  */
#line 83 "parser.y"
                                  { (yyval.node) = (yyvsp[-1].node); }
#line 1523 "parser.tab.c"
    break;

  case 13: /* train_options: IDENTIFIER ASSIGN NUMBER  */
#line 86 "parser.y"
                                        { (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival)); }
#line 1529 "parser.tab.c"
    break;

  case 14: /* layer_list: layer_decl  */
#line 87 "parser.y"
                                                { (yyval.node) = (yyvsp[0].node); }
#line 1535 "parser.tab.c"
    break;

  case 15: /* layer_list: layer_list layer_decl  */
#line 88 "parser.y"
                                                { (yyval.node) = append_layer_to_list((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1541 "parser.tab.c"
    break;

  case 16: /* layer_decl: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 89 "parser.y"
                                                                             { (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval)); }
#line 1547 "parser.tab.c"
    break;

  case 17: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 93 "parser.y"
                                    { if (last_class) { add_attribute_to_class(last_class, (yyvsp[-3].sval), "int"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval)); } }
#line 1553 "parser.tab.c"
    break;

  case 18: /* attribute_decl: IDENTIFIER COLON STRING SEMICOLON  */
#line 94 "parser.y"
                                       { if (last_class) { add_attribute_to_class(last_class, (yyvsp[-3].sval), "string"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval)); } }
#line 1559 "parser.tab.c"
    break;

  case 19: /* constructor_decl: CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 98 "parser.y"
                                                                               { if (last_class) { add_constructor_to_class(last_class, (yyvsp[-4].pnode), (yyvsp[-1].node)); } else { printf("Error: No hay clase definida para el constructor.\n"); } (yyval.node) = NULL; }
#line 1565 "parser.tab.c"
    break;

  case 20: /* parameter_decl: IDENTIFIER COLON INT  */
#line 102 "parser.y"
                                { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1571 "parser.tab.c"
    break;

  case 21: /* parameter_decl: INT IDENTIFIER  */
#line 103 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1577 "parser.tab.c"
    break;

  case 22: /* parameter_decl: IDENTIFIER COLON STRING  */
#line 104 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1583 "parser.tab.c"
    break;

  case 23: /* parameter_decl: STRING IDENTIFIER  */
#line 105 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1589 "parser.tab.c"
    break;

  case 24: /* parameter_decl: IDENTIFIER COLON FLOAT  */
#line 106 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1595 "parser.tab.c"
    break;

  case 25: /* parameter_decl: FLOAT IDENTIFIER  */
#line 107 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1601 "parser.tab.c"
    break;

  case 26: /* parameter_list: %empty  */
#line 111 "parser.y"
                                                    { (yyval.pnode) = NULL; }
#line 1607 "parser.tab.c"
    break;

  case 27: /* parameter_list: IDENTIFIER COLON IDENTIFIER  */
#line 112 "parser.y"
                                                   { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1613 "parser.tab.c"
    break;

  case 28: /* parameter_list: parameter_list COMMA IDENTIFIER COLON IDENTIFIER  */
#line 113 "parser.y"
                                                     { (yyval.pnode) = add_parameter((yyvsp[-4].pnode), (yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1619 "parser.tab.c"
    break;

  case 29: /* parameter_list: parameter_decl  */
#line 114 "parser.y"
                                                   { (yyval.pnode) = (yyvsp[0].pnode); }
#line 1625 "parser.tab.c"
    break;

  case 30: /* parameter_list: parameter_list COMMA parameter_decl  */
#line 115 "parser.y"
                                                   { (yyval.pnode) = add_parameter((yyvsp[-2].pnode), (yyvsp[0].pnode)->name, (yyvsp[0].pnode)->type); }
#line 1631 "parser.tab.c"
    break;

  case 31: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 119 "parser.y"
                                                               { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-5].sval)); } else { add_method_to_class(last_class, (yyvsp[-5].sval), NULL, (yyvsp[-1].node)); } (yyval.node) = NULL; }
#line 1637 "parser.tab.c"
    break;

  case 32: /* method_decl: IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 120 "parser.y"
                                                                              { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-6].sval)); } else { add_method_to_class(last_class, (yyvsp[-6].sval), (yyvsp[-4].pnode), (yyvsp[-1].node)); } (yyval.node) = NULL; }
#line 1643 "parser.tab.c"
    break;

  case 33: /* expression: expression GT expression  */
#line 125 "parser.y"
                                { (yyval.node) = create_ast_node("GT", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1649 "parser.tab.c"
    break;

  case 34: /* expression: expression LT expression  */
#line 126 "parser.y"
                                { (yyval.node) = create_ast_node("LT", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1655 "parser.tab.c"
    break;

  case 35: /* expression: expression EQ expression  */
#line 127 "parser.y"
                                { (yyval.node) = create_ast_node("EQ", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1661 "parser.tab.c"
    break;

  case 36: /* expression: expression GT_EQ expression  */
#line 128 "parser.y"
                                 { (yyval.node) = create_ast_node("GT_EQ", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1667 "parser.tab.c"
    break;

  case 37: /* expression: expression LT_EQ expression  */
#line 129 "parser.y"
                                 { (yyval.node) = create_ast_node("LT_EQ", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1673 "parser.tab.c"
    break;

  case 38: /* expression: expression DIFF expression  */
#line 130 "parser.y"
                                { (yyval.node) = create_ast_node("DIFF", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1679 "parser.tab.c"
    break;

  case 39: /* expression: IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN  */
#line 131 "parser.y"
                                                               { printf(" [DEBUG] PERU Reconocido FUNCTION_CALL: %s\n", (yyvsp[-5].sval)); ASTNode *listExpr = create_identifier_node((yyvsp[-5].sval)); ASTNode *filterCall = create_list_function_call_node(listExpr, "filter", (yyvsp[-1].node)); (yyval.node) = create_var_decl_node((yyvsp[-5].sval), filterCall); }
#line 1685 "parser.tab.c"
    break;

  case 40: /* expression: LSBRACKET object_list RSBRACKET  */
#line 132 "parser.y"
                                                        { /* printf(" [DEBUG] Reconocido OBJECT_LIST\n"); */ (yyval.node) = (yyvsp[-1].node); }
#line 1691 "parser.tab.c"
    break;

  case 41: /* expression: expression DOT IDENTIFIER LPAREN lambda RPAREN  */
#line 133 "parser.y"
                                                        {  if (strcmp((yyvsp[-3].sval), "filter")==0) { (yyval.node) = create_list_function_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node)); } else { (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), NULL); } free((yyvsp[-3].sval)); }
#line 1697 "parser.tab.c"
    break;

  case 42: /* expression: LSBRACKET expr_list RSBRACKET  */
#line 134 "parser.y"
                                                        { /* printf(" [DEBUG] Reconocido EXPR_LIST\n"); */ (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-2].sval)); (yyval.node) = create_list_node((yyvsp[-1].node)); }
#line 1703 "parser.tab.c"
    break;

  case 43: /* expression: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN  */
#line 135 "parser.y"
                                                     { ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL); }
#line 1709 "parser.tab.c"
    break;

  case 44: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN  */
#line 136 "parser.y"
                                                    { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-2].sval), NULL); }
#line 1715 "parser.tab.c"
    break;

  case 45: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 137 "parser.y"
                                                           { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1721 "parser.tab.c"
    break;

  case 46: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 138 "parser.y"
                                                   { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval)); ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)); (yyval.node) = create_ast_node("ACCESS_ATTR", obj, attr); }
#line 1727 "parser.tab.c"
    break;

  case 47: /* expression: expression DOT IDENTIFIER LPAREN RPAREN  */
#line 139 "parser.y"
                                                   { printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", (yyvsp[-4].node), (yyvsp[-2].sval)); (yyval.node) = create_method_call_node((yyvsp[-4].node), (yyvsp[-2].sval), NULL); }
#line 1733 "parser.tab.c"
    break;

  case 48: /* expression: expression DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 140 "parser.y"
                                                           { (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1739 "parser.tab.c"
    break;

  case 49: /* expression: THIS DOT IDENTIFIER  */
#line 141 "parser.y"
                                                   { (yyval.node) = create_ast_node("ACCESS_ATTR", create_ast_leaf("ID", 0, NULL, "this"), create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval))); }
#line 1745 "parser.tab.c"
    break;

  case 50: /* expression: THIS DOT IDENTIFIER LPAREN RPAREN  */
#line 142 "parser.y"
                                                   { (yyval.node) = create_method_call_node(create_ast_leaf("ID", 0, NULL, "this"), (yyvsp[-2].sval), NULL); }
#line 1751 "parser.tab.c"
    break;

  case 51: /* expression: IDENTIFIER  */
#line 143 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[0].sval)); }
#line 1757 "parser.tab.c"
    break;

  case 52: /* expression: NUMBER  */
#line 144 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 1763 "parser.tab.c"
    break;

  case 53: /* expression: FLOAT_LITERAL  */
#line 145 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("FLOAT", 0, (yyvsp[0].sval), NULL); }
#line 1769 "parser.tab.c"
    break;

  case 54: /* expression: STRING_LITERAL  */
#line 146 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 1775 "parser.tab.c"
    break;

  case 55: /* expression: CONCAT LPAREN expression_list RPAREN  */
#line 147 "parser.y"
                                                  { printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", (yyvsp[-1].node)); (yyval.node) = create_function_call_node("concat", (yyvsp[-1].node)); }
#line 1781 "parser.tab.c"
    break;

  case 56: /* expression: expression PLUS expression  */
#line 148 "parser.y"
                                                  { (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1787 "parser.tab.c"
    break;

  case 57: /* expression: expression MINUS expression  */
#line 149 "parser.y"
                                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1793 "parser.tab.c"
    break;

  case 58: /* expression: expression MULTIPLY expression  */
#line 150 "parser.y"
                                                  { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1799 "parser.tab.c"
    break;

  case 59: /* expression: expression DIVIDE expression  */
#line 151 "parser.y"
                                                  { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1805 "parser.tab.c"
    break;

  case 60: /* expression: LPAREN expression RPAREN  */
#line 152 "parser.y"
                                                  { (yyval.node) = (yyvsp[-1].node); }
#line 1811 "parser.tab.c"
    break;

  case 61: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 153 "parser.y"
                                                  { ClassNode *cls = find_class((yyvsp[-2].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval)); (yyval.node) = NULL; } else { (yyval.node) = (ASTNode *)create_object_with_args(cls, NULL); } }
#line 1817 "parser.tab.c"
    break;

  case 62: /* expression: NEW IDENTIFIER LPAREN expr_list RPAREN  */
#line 154 "parser.y"
                                         { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { fprintf(stderr, "Error: clase '%s' no encontrada\n", (yyvsp[-3].sval)); exit(1); } (yyval.node) = create_object_with_args(cls, (yyvsp[-1].node)); free((yyvsp[-3].sval)); }
#line 1823 "parser.tab.c"
    break;

  case 63: /* var_decl: LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN SEMICOLON  */
#line 160 "parser.y"
                                                                                                 { /*printf(" [DEBUG] Reconocido FILTER_CALL: let %s = %s(...)\n", $2, $4);*/ ASTNode *listExpr = (yyvsp[-4].node); ASTNode *lambda = (yyvsp[-2].node); ASTNode *filterCall = create_list_function_call_node(listExpr, (yyvsp[-6].sval), lambda); filterCall->type = strdup("FILTER_CALL"); (yyval.node) = create_var_decl_node((yyvsp[-8].sval), filterCall); }
#line 1829 "parser.tab.c"
    break;

  case 64: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
#line 161 "parser.y"
                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1835 "parser.tab.c"
    break;

  case 65: /* var_decl: STRING IDENTIFIER ASSIGN expression SEMICOLON  */
#line 162 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1841 "parser.tab.c"
    break;

  case 66: /* var_decl: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 163 "parser.y"
                                                { printf("IMPRIMIENDO VAR \n"); (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1847 "parser.tab.c"
    break;

  case 67: /* var_decl: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 164 "parser.y"
                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1853 "parser.tab.c"
    break;

  case 68: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 165 "parser.y"
                                                           { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-5].sval)); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-3].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node)); }
#line 1859 "parser.tab.c"
    break;

  case 69: /* var_decl: THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 166 "parser.y"
                                                     { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-3].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node)); }
#line 1865 "parser.tab.c"
    break;

  case 70: /* statement: FOR LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  */
#line 171 "parser.y"
                                                                                     { (yyval.node) = create_for_in_node((yyvsp[-6].sval), (yyvsp[-4].node), (yyvsp[-1].node)); }
#line 1871 "parser.tab.c"
    break;

  case 71: /* statement: LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 172 "parser.y"
                                                                             { printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", (yyvsp[-7].sval), (yyvsp[-5].sval)); ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-5].sval)); (yyval.node) = create_var_decl_node((yyvsp[-7].sval), obj); }
#line 1877 "parser.tab.c"
    break;

  case 72: /* statement: RETURN expression SEMICOLON  */
#line 173 "parser.y"
                                 { (yyval.node) = create_return_node((yyvsp[-1].node)); }
#line 1883 "parser.tab.c"
    break;

  case 74: /* statement: STRING STRING expression SEMICOLON  */
#line 175 "parser.y"
                                                                { printf(" [DEBUG] Declaración de variable s: ¿"); char buffer[2048]; sprintf(buffer,"#include \"easyspark/dataframe.hpp\"\n..."); generate_code(buffer); }
#line 1889 "parser.tab.c"
    break;

  case 75: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 176 "parser.y"
                                                       { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-5].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL); }
#line 1895 "parser.tab.c"
    break;

  case 76: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 177 "parser.y"
                                                                       { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-6].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-4].sval), (yyvsp[-2].node)); }
#line 1901 "parser.tab.c"
    break;

  case 77: /* statement: THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 178 "parser.y"
                                                                { ASTNode *thisObj = create_ast_leaf("ID",0,NULL,"this"); (yyval.node) = create_method_call_node(thisObj, (yyvsp[-3].sval), NULL); }
#line 1907 "parser.tab.c"
    break;

  case 78: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 179 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1913 "parser.tab.c"
    break;

  case 79: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 180 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1919 "parser.tab.c"
    break;

  case 80: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 181 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1925 "parser.tab.c"
    break;

  case 81: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 182 "parser.y"
                                                                { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1931 "parser.tab.c"
    break;

  case 82: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 183 "parser.y"
                                                                { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR",0,NULL,(yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1937 "parser.tab.c"
    break;

  case 83: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 184 "parser.y"
                                                                { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1943 "parser.tab.c"
    break;

  case 84: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 185 "parser.y"
                                                                { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-4].sval)); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-2].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("PRINT", access, NULL); }
#line 1949 "parser.tab.c"
    break;

  case 85: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 186 "parser.y"
                                                                                                                           { 
    printf("[DEBUG] Reconocido FOR de %s\n", (yyvsp[-10].sval));
    (yyval.node) = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,(yyvsp[-10].sval)), create_ast_leaf("NUMBER",(yyvsp[-8].ival),NULL,NULL), (yyvsp[-6].node), (yyvsp[-4].node), (yyvsp[-1].node)); }
#line 1957 "parser.tab.c"
    break;

  case 86: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 189 "parser.y"
                                                                    { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval)); (yyval.node) = NULL; } else { (yyval.node) = (ASTNode *)create_object_with_args(cls, NULL); printf("[DEBUG] Creación de objeto: %s\n", (yyvsp[-3].sval)); } }
#line 1963 "parser.tab.c"
    break;

  case 87: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 190 "parser.y"
                                                                    { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval)); (yyval.node) = NULL; } else { (yyval.node) = create_var_decl_node((yyvsp[-6].sval), create_object_with_args(cls, NULL)); } }
#line 1969 "parser.tab.c"
    break;

  case 88: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 191 "parser.y"
                                                                                  { ClassNode *cls = find_class((yyvsp[-4].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-4].sval)); (yyval.node) = NULL; } else { (yyval.node) = create_var_decl_node((yyvsp[-7].sval), create_object_with_args(cls, (yyvsp[-2].node))); } }
#line 1975 "parser.tab.c"
    break;

  case 89: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 192 "parser.y"
                                                                    { printf(" [DEBUG] Reconocido DATASET\n"); (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval)); }
#line 1981 "parser.tab.c"
    break;

  case 90: /* statement: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 193 "parser.y"
                                                                    { printf(" [DEBUG] Reconocido PREDICT\n"); (yyval.node) = create_predict_node((yyvsp[-4].sval), (yyvsp[-2].sval)); }
#line 1987 "parser.tab.c"
    break;

  case 91: /* statement: VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 194 "parser.y"
                                                                                      { printf(" [DEBUG] Reconocido PREDICT\n"); ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); (yyval.node) = create_method_call_node(obj, "predict", NULL); (yyval.node) = create_predict_node((yyvsp[-4].sval), (yyvsp[-2].sval)); }
#line 1993 "parser.tab.c"
    break;

  case 92: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 195 "parser.y"
                                                                { (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval)); }
#line 1999 "parser.tab.c"
    break;

  case 93: /* statement: PLOT LPAREN expression_list RPAREN SEMICOLON  */
#line 196 "parser.y"
                                                                { (yyval.node) = create_ast_node("PLOT", (yyvsp[-2].node), NULL); }
#line 2005 "parser.tab.c"
    break;

  case 94: /* statement: MODEL IDENTIFIER LBRACKET layer_list RBRACKET  */
#line 197 "parser.y"
                                                                { printf("[DEBUG] Reconocido MODEL\n"); printf("[DEBUG] Nombre del modelo: %s\n", (yyvsp[-3].sval)); ASTNode *layer = (yyvsp[-1].node); int capa_index = 0; while (layer) { if (strcmp(layer->type, "LAYER") == 0) { printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n", capa_index++, layer->id, layer->value, layer->str_value); } layer = layer->right; } ASTNode *modelNode = create_model_node((yyvsp[-3].sval), (yyvsp[-1].node)); printf("[DEBUG] Nodo de modelo creado: type=%s, id=%s\n", modelNode->type, modelNode->id); }
#line 2011 "parser.tab.c"
    break;

  case 95: /* statement: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 198 "parser.y"
                                                                      { (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval)); }
#line 2017 "parser.tab.c"
    break;

  case 96: /* statement: TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON  */
#line 199 "parser.y"
                                                                                   { (yyval.node) = create_train_node((yyvsp[-6].sval), (yyvsp[-4].sval), (yyvsp[-2].node)); }
#line 2023 "parser.tab.c"
    break;

  case 97: /* statement: IDENTIFIER ASSIGN NUMBER  */
#line 200 "parser.y"
                                                                { (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival)); }
#line 2029 "parser.tab.c"
    break;

  case 98: /* statement: LET IDENTIFIER ASSIGN FROM STRING_LITERAL COMMA IDENTIFIER SEMICOLON  */
#line 201 "parser.y"
                                                                         {
    ClassNode* cls = find_class((yyvsp[-1].sval));
        if (!cls) {
            printf("Clase '%s' no encontrada.\n", (yyvsp[-1].sval));
            (yyval.node) = NULL;
        } else {
            ASTNode* list = from_csv_to_list((yyvsp[-3].sval), cls);
            (yyval.node) = create_var_decl_node((yyvsp[-6].sval), list);
        }
    }
#line 2044 "parser.tab.c"
    break;

  case 99: /* statement_list: statement_list statement  */
#line 214 "parser.y"
                              { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[-1].node), (yyvsp[0].node)); }
#line 2050 "parser.tab.c"
    break;

  case 100: /* statement_list: statement  */
#line 215 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 2056 "parser.tab.c"
    break;

  case 101: /* expression_list: expression  */
#line 219 "parser.y"
                                              { (yyval.node) = (yyvsp[0].node); }
#line 2062 "parser.tab.c"
    break;

  case 102: /* expression_list: expression_list COMMA expression  */
#line 220 "parser.y"
                                              { (yyval.node) = add_statement((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2068 "parser.tab.c"
    break;

  case 103: /* expr_list: expression  */
#line 224 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 2074 "parser.tab.c"
    break;

  case 104: /* expr_list: expr_list COMMA expression  */
#line 225 "parser.y"
                               { (yyval.node) = append_to_list((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2080 "parser.tab.c"
    break;

  case 105: /* lambda: IDENTIFIER ARROW  */
#line 234 "parser.y"
                                    { printf(" [DEBUG] Reconocido LAMBDA\n"); }
#line 2086 "parser.tab.c"
    break;

  case 106: /* lambda: LPAREN IDENTIFIER RPAREN ARROW expression  */
#line 235 "parser.y"
                                               { (yyval.node) = create_lambda_node((yyvsp[-3].sval), (yyvsp[0].node)); free((yyvsp[-3].sval)); }
#line 2092 "parser.tab.c"
    break;

  case 107: /* object_expression: NEW IDENTIFIER LPAREN expression_list RPAREN  */
#line 247 "parser.y"
  {
        ClassNode *cls = find_class((yyvsp[-3].sval));
        if (!cls) {
            printf("Error: Clase '%s' no encontrada.\n", (yyvsp[-3].sval));
            (yyval.node) = NULL;
        } else {
            (yyval.node) = create_object_with_args(cls, (yyvsp[-1].node));
        }
  }
#line 2106 "parser.tab.c"
    break;

  case 108: /* object_list: object_expression  */
#line 259 "parser.y"
      {
        /*printf(" [DEBUG] Reconocido OBJECT_EXPRESSION\n");*/
         (yyval.node) = create_list_node((yyvsp[0].node)); 
    }
#line 2115 "parser.tab.c"
    break;

  case 109: /* object_list: object_list COMMA object_expression  */
#line 264 "parser.y"
      { (yyval.node) = append_to_list((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2121 "parser.tab.c"
    break;

  case 110: /* lambda_expression: IDENTIFIER ARROW expression  */
#line 269 "parser.y"
      {
        (yyval.node) = create_lambda_node((yyvsp[-2].sval), (yyvsp[0].node)); // crea un nodo tipo LAMBDA
      }
#line 2129 "parser.tab.c"
    break;


#line 2133 "parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 279 "parser.y"


void clean_generated_code() {    
    FILE* out = fopen("generated.cpp", "w"); // limpia completamente
    if (out != NULL) {
        fprintf(out, "");
        fclose(out);
    }
}



void generate_code(const char* code) {
    FILE* out = fopen("generated.cpp", "w"); // modo 'a' agrega contenido
    if (out != NULL) {
        fprintf(out, "%s\n", code);
        fclose(out);
    }
}



void yyerror(const char *s) {
    extern char *yytext;
    printf("Error de sintaxis en linea %d: %s\n", yylineno, s);
    
    if (yytext) {
        printf("Cerca de: '%s'\n", yytext);
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    if (node->type && strcmp(node->type, "FOR") == 0) {
        printf(">>> FOR DETECTADO <<<\n");
    }
    

    if (node->type) {
        printf("Node type: %s", node->type);
    } else {
        printf("Node");
    }

   // if (node->varname)
      //  printf(", varname: %s", node->varname);

    if (node->value)
        printf(", value: %d", node->value);

    if (node->str_value)
        printf(", str: %s", node->str_value);

    if (node->id)
        printf(", id: %s", node->id);

    printf("\n");

    print_ast(node->left, indent + 1);
    print_ast(node->right, indent + 1);
}


int main(int argc, char *argv[]) {

    //yydebug = 1;
  
    clock_t inicio = clock();
    

    // Flags para el modo de ejecución
    int interpret_mode = 0;
    int use_bytecode = 0;
    if (argc < 2) {
        printf("Uso: %s <archivo.te> [--interpret]\n", argv[0]);
        return 1;
    }

    // Verificar si se pasó el flag --interpret
    if (argc == 3 && strcmp(argv[2], "--run") == 0) {
        interpret_mode = 1;
    }

    if (argc == 3 && strcmp(argv[2], "--bytecode") == 0) {
        use_bytecode = 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo %s\n", argv[1]);
        return 1;
    }

    yyin = file;
    int parse_result = yyparse();
    fclose(file);

    printf("[DEBUG] AST construido:\n");
    //print_ast(root, 0);  // <- muestra el árbol

    if (parse_result != 0) {
        printf(" Error al parsear el archivo.\n");
        return 1;
    }


    if(interpret_mode){

        int compile_status = system("g++ generated.cpp easyspark/dataframe.cpp -o typeeasy_output");
        if (compile_status != 0) {
            printf("Error al compilar el programa generado.\n");
            return 1;
        }

        int run_status = system("typeeasy_output.exe");
        if (run_status != 0) {
            printf("Error al ejecutar el programa generado.\n");
            return 1;
        }

    }

    
    if (use_bytecode) {
        printf("[DEBUG] Ejecutando en modo Bytecode\n");
       // compile_to_bytecode(root);
       // run_bytecode();
    } else {
        printf("[DEBUG] Ejecutando con AST normal\n");
        if (root) {
            interpret_ast(root);
            free_ast(root);
        }
    }


    //if (interpret_mode) {
        //  Ejecutar directamente el AST
     //   if (root) {
      //      interpret_ast(root);
      //      free_ast(root);
      //  }
   // } else {
        // Compilar y ejecutar el código generado

   // }

    clock_t fin = clock();
    double tiempo = (double)(fin - inicio) / CLOCKS_PER_SEC;
    printf("Tiempo de ejecución: %.6f segundos\n", tiempo);


    return 0;
}

