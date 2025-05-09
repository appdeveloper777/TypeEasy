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

    ASTNode *root;
    extern int yylineno;
    FILE *yyin;
    void yyerror(const char *s);
    ClassNode *last_class = NULL;
    int yylex();
    void generate_code(const char* code);
    void clean_generated_code();


#line 89 "parser.tab.c"

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
  YYSYMBOL_LAYER = 6,                      /* LAYER  */
  YYSYMBOL_LSBRACKET = 7,                  /* LSBRACKET  */
  YYSYMBOL_RSBRACKET = 8,                  /* RSBRACKET  */
  YYSYMBOL_DATASET = 9,                    /* DATASET  */
  YYSYMBOL_MODEL = 10,                     /* MODEL  */
  YYSYMBOL_TRAIN = 11,                     /* TRAIN  */
  YYSYMBOL_PREDICT = 12,                   /* PREDICT  */
  YYSYMBOL_FROM = 13,                      /* FROM  */
  YYSYMBOL_PLOT = 14,                      /* PLOT  */
  YYSYMBOL_ARROW = 15,                     /* ARROW  */
  YYSYMBOL_IN = 16,                        /* IN  */
  YYSYMBOL_LAMBDA = 17,                    /* LAMBDA  */
  YYSYMBOL_VAR = 18,                       /* VAR  */
  YYSYMBOL_ASSIGN = 19,                    /* ASSIGN  */
  YYSYMBOL_PRINT = 20,                     /* PRINT  */
  YYSYMBOL_FOR = 21,                       /* FOR  */
  YYSYMBOL_LPAREN = 22,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 23,                    /* RPAREN  */
  YYSYMBOL_SEMICOLON = 24,                 /* SEMICOLON  */
  YYSYMBOL_CONCAT = 25,                    /* CONCAT  */
  YYSYMBOL_PLUS = 26,                      /* PLUS  */
  YYSYMBOL_MINUS = 27,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 28,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 29,                    /* DIVIDE  */
  YYSYMBOL_LBRACKET = 30,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 31,                  /* RBRACKET  */
  YYSYMBOL_CLASS = 32,                     /* CLASS  */
  YYSYMBOL_CONSTRUCTOR = 33,               /* CONSTRUCTOR  */
  YYSYMBOL_THIS = 34,                      /* THIS  */
  YYSYMBOL_NEW = 35,                       /* NEW  */
  YYSYMBOL_LET = 36,                       /* LET  */
  YYSYMBOL_COLON = 37,                     /* COLON  */
  YYSYMBOL_COMMA = 38,                     /* COMMA  */
  YYSYMBOL_DOT = 39,                       /* DOT  */
  YYSYMBOL_RETURN = 40,                    /* RETURN  */
  YYSYMBOL_IDENTIFIER = 41,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 42,            /* STRING_LITERAL  */
  YYSYMBOL_NUMBER = 43,                    /* NUMBER  */
  YYSYMBOL_GT = 44,                        /* GT  */
  YYSYMBOL_LT = 45,                        /* LT  */
  YYSYMBOL_EQ = 46,                        /* EQ  */
  YYSYMBOL_YYACCEPT = 47,                  /* $accept  */
  YYSYMBOL_program = 48,                   /* program  */
  YYSYMBOL_class_decl = 49,                /* class_decl  */
  YYSYMBOL_50_1 = 50,                      /* $@1  */
  YYSYMBOL_class_member = 51,              /* class_member  */
  YYSYMBOL_class_body = 52,                /* class_body  */
  YYSYMBOL_train_options = 53,             /* train_options  */
  YYSYMBOL_layer_list = 54,                /* layer_list  */
  YYSYMBOL_layer_decl = 55,                /* layer_decl  */
  YYSYMBOL_attribute_decl = 56,            /* attribute_decl  */
  YYSYMBOL_constructor_decl = 57,          /* constructor_decl  */
  YYSYMBOL_parameter_decl = 58,            /* parameter_decl  */
  YYSYMBOL_parameter_list = 59,            /* parameter_list  */
  YYSYMBOL_method_decl = 60,               /* method_decl  */
  YYSYMBOL_expression = 61,                /* expression  */
  YYSYMBOL_var_decl = 62,                  /* var_decl  */
  YYSYMBOL_statement = 63,                 /* statement  */
  YYSYMBOL_statement_list = 64,            /* statement_list  */
  YYSYMBOL_expression_list = 65,           /* expression_list  */
  YYSYMBOL_expr_list = 66,                 /* expr_list  */
  YYSYMBOL_lambda = 67,                    /* lambda  */
  YYSYMBOL_object_expression = 68,         /* object_expression  */
  YYSYMBOL_object_list = 69,               /* object_list  */
  YYSYMBOL_lambda_expression = 70          /* lambda_expression  */
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
#define YYFINAL  52
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   751

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  47
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  24
/* YYNRULES -- Number of rules.  */
#define YYNRULES  103
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  323

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   301


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
      45,    46
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    63,    63,    64,    65,    66,    70,    70,    75,    76,
      77,    81,    82,    85,    86,    87,    88,    92,    93,    97,
     101,   102,   103,   104,   105,   106,   110,   111,   112,   113,
     114,   118,   119,   124,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   156,   157,   158,
     159,   160,   161,   162,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   197,   198,   202,   203,   207,   208,   217,   218,
     229,   241,   246,   251
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
  "FLOAT", "LAYER", "LSBRACKET", "RSBRACKET", "DATASET", "MODEL", "TRAIN",
  "PREDICT", "FROM", "PLOT", "ARROW", "IN", "LAMBDA", "VAR", "ASSIGN",
  "PRINT", "FOR", "LPAREN", "RPAREN", "SEMICOLON", "CONCAT", "PLUS",
  "MINUS", "MULTIPLY", "DIVIDE", "LBRACKET", "RBRACKET", "CLASS",
  "CONSTRUCTOR", "THIS", "NEW", "LET", "COLON", "COMMA", "DOT", "RETURN",
  "IDENTIFIER", "STRING_LITERAL", "NUMBER", "GT", "LT", "EQ", "$accept",
  "program", "class_decl", "$@1", "class_member", "class_body",
  "train_options", "layer_list", "layer_decl", "attribute_decl",
  "constructor_decl", "parameter_decl", "parameter_list", "method_decl",
  "expression", "var_decl", "statement", "statement_list",
  "expression_list", "expr_list", "lambda", "object_expression",
  "object_list", "lambda_expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-140)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-48)

#define yytable_value_is_error(Yyn) \
  ((Yyn) == YYTABLE_NINF)

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      79,   -32,    -1,     3,     8,    38,    51,    15,    90,    96,
      67,   104,   129,    87,    95,   111,   123,   243,    -9,    11,
    -140,  -140,  -140,   154,   243,   158,   166,   165,   180,   184,
     161,   174,   243,   197,   497,   -29,  -140,   177,   201,   200,
     508,   202,   243,   203,   194,   193,    16,  -140,  -140,   155,
     522,   198,  -140,  -140,  -140,   243,   609,   546,   243,   213,
     199,   237,   207,   211,   700,   -10,   560,    31,   225,   217,
     240,   233,   150,   247,   571,   230,   700,    -3,  -140,    -2,
     231,   484,   243,   232,   252,   243,   234,  -140,   243,   243,
     243,   243,   235,   243,   616,   623,   189,   630,  -140,   257,
     637,   644,   244,   259,   246,    -4,  -140,   251,   260,   269,
     243,   276,   651,   261,   280,   284,   264,  -140,   243,   286,
     287,   271,    37,   658,   288,  -140,   243,  -140,   278,   277,
    -140,    10,   300,   383,   686,   301,   700,   700,   700,   700,
     302,   707,  -140,   243,   407,  -140,  -140,  -140,  -140,   290,
    -140,   310,  -140,  -140,   299,   316,  -140,   700,   305,  -140,
      34,  -140,   243,   317,   172,   665,   319,  -140,   318,   243,
     306,  -140,   383,   700,   307,  -140,   308,  -140,   327,  -140,
      58,   311,   421,   445,   672,   329,    86,   328,   313,   320,
     330,   325,   333,   547,   243,  -140,   340,   103,  -140,  -140,
    -140,  -140,  -140,  -140,   459,   693,   348,   700,   106,   349,
     353,  -140,  -140,   364,   355,  -140,   112,   585,  -140,    56,
     119,   357,  -140,  -140,   358,   361,   350,   367,   366,  -140,
     346,  -140,   362,   679,    60,    57,   206,   369,   137,   311,
     483,  -140,   243,  -140,   243,  -140,  -140,    64,  -140,  -140,
    -140,  -140,  -140,   359,   351,   372,   368,   363,   243,   360,
     370,   371,   365,  -140,   142,   377,   148,   385,   386,  -140,
     389,   392,   396,   700,   401,   398,  -140,  -140,   399,  -140,
     127,   595,  -140,  -140,  -140,    63,   397,   102,   363,   404,
    -140,  -140,  -140,   411,  -140,   243,   412,  -140,  -140,  -140,
     408,  -140,  -140,  -140,  -140,   363,   394,  -140,   186,   363,
    -140,   700,  -140,   363,   226,   118,  -140,   285,   324,  -140,
    -140,  -140,  -140
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       5,    67,     4,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     6,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    46,    48,    47,     0,
       0,     0,     1,     3,     2,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    94,     0,     0,    46,     0,     0,
       0,     0,     0,     0,     0,     0,    96,     0,   101,     0,
       0,     0,     0,     0,     0,     0,     0,    66,     0,     0,
       0,     0,     0,     0,    91,     0,     0,     0,    68,    48,
       0,     0,     0,     0,     0,     0,    14,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    11,     0,     0,
       0,     0,    46,     0,     0,    37,     0,    35,     0,     0,
      54,     0,    44,     0,     0,    41,    50,    51,    52,    53,
       0,    33,    76,     0,     0,    61,    72,    59,    74,     0,
      83,     0,    88,    15,     0,     0,    87,    95,     0,    60,
      41,    77,     0,     0,     0,     0,     0,    80,     0,     0,
       0,    58,     0,    97,     0,   102,     0,    49,     0,    55,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     7,     0,     0,    12,     8,
       9,    10,    63,    71,     0,     0,    41,    94,     0,     0,
       0,    45,    56,     0,     0,    39,     0,     0,    42,    46,
       0,     0,    62,    69,     0,     0,     0,     0,     0,    84,
       0,    78,     0,     0,    26,    26,     0,    55,     0,     0,
       0,   100,     0,    38,     0,    34,    40,    46,    98,    43,
      36,    70,    89,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    29,     0,     0,     0,     0,     0,    81,
       0,     0,    39,   103,     0,     0,    13,    90,    38,    93,
       0,     0,    21,    23,    25,     0,     0,     0,     0,     0,
      17,    18,    82,    34,    65,     0,     0,    85,    64,    92,
       0,    20,    22,    24,    27,     0,     0,    30,     0,     0,
      57,    99,    16,     0,     0,     0,    31,     0,     0,    19,
      28,    32,    79
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -140,  -140,   403,  -140,  -140,  -140,  -140,  -140,   332,  -140,
    -140,   152,   205,  -140,   -16,  -140,     0,  -139,   -28,   405,
    -140,   323,  -140,   208
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    19,    20,    71,   198,   164,   228,   105,   106,   199,
     200,   263,   264,   201,    64,    21,   279,   280,   208,   180,
     221,    78,    79,   214
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      22,    49,   104,    24,    65,   125,   127,    69,    56,    23,
      50,    52,    70,   109,     1,     2,     3,     4,    68,    54,
       5,     6,     7,     8,    76,     9,    81,   152,   110,    10,
      51,    11,    12,   177,    95,   126,   128,    30,    85,    97,
      25,   100,   101,    13,    26,    14,    15,    16,   110,    27,
     112,    17,    18,    85,   131,    86,   182,   192,   123,   169,
     259,   260,   261,   259,   260,   261,   301,   302,   303,   134,
     113,   248,   136,   137,   138,   139,   170,   141,    85,    28,
     265,   212,     1,     2,     3,     4,    85,   274,     5,     6,
       7,     8,    29,     9,   157,    86,   126,    10,   262,    11,
      12,   262,   165,    86,   304,   259,   260,   261,    33,   224,
     173,    13,    31,    14,    15,    16,   186,    76,    32,    17,
      18,   301,   302,   303,   110,   235,    34,   184,    36,   241,
       1,     2,     3,     4,    37,   246,     5,     6,     7,     8,
     236,     9,   249,   306,   110,    10,   193,    11,    12,   308,
     110,    35,    38,   205,   216,   220,   207,   110,   298,   320,
     270,    14,    15,    16,    39,   286,   314,    17,    18,   118,
     317,   289,   119,    55,   318,   110,   238,    57,   233,    87,
     287,    88,    89,    90,    91,    58,   287,    59,   207,     1,
       2,     3,     4,    60,    92,     5,     6,     7,     8,    93,
       9,    81,    62,   195,    10,   196,    11,    12,   143,   267,
     268,   144,   216,   197,    61,    63,    66,   316,    72,    74,
      14,    15,    16,    73,    80,    82,    17,    18,   273,     1,
       2,     3,     4,    83,    84,     5,     6,     7,     8,    96,
       9,   103,   281,   104,    10,   107,    11,    12,   114,   108,
      40,    88,    89,    90,    91,    41,   102,   319,   115,   116,
      14,    15,    16,   117,    92,    42,    17,    18,    43,    93,
     120,   124,   129,   132,   133,   135,   140,    44,    45,   311,
     299,   146,   149,   150,    46,    47,    48,   151,     1,     2,
       3,     4,   154,   156,     5,     6,     7,     8,   158,     9,
     162,   155,   160,    10,   161,    11,    12,   163,   299,   166,
     172,   167,   168,   174,   299,   176,   321,   299,   299,    14,
      15,    16,   178,   182,   183,    17,    18,     1,     2,     3,
       4,   187,   188,     5,     6,     7,     8,   189,     9,   190,
     204,   194,    10,   203,    11,    12,   191,   206,   209,   210,
     211,   225,   213,   223,   229,   322,   226,   231,    14,    15,
      16,   227,   234,   230,    17,    18,     1,     2,     3,     4,
     240,   242,     5,     6,     7,     8,   243,     9,   245,   244,
     250,    10,   251,    11,    12,   252,   254,   256,   253,   255,
      40,   278,   257,   269,   276,    41,   277,    14,    15,    16,
     275,   282,   285,    17,    18,    42,   179,   288,    43,   290,
     291,   283,   284,   292,    40,   293,   295,    44,    45,    41,
     294,   296,    53,   297,    46,    47,    48,   305,    40,    42,
     185,   315,    43,    41,   309,   310,   312,   153,   313,   307,
     266,    44,    45,    42,   215,    77,    43,   271,    46,    47,
      48,   175,    40,     0,     0,    44,    45,    41,     0,     0,
       0,     0,    46,    47,    48,     0,    40,   217,   218,     0,
      43,    41,     0,     0,     0,     0,     0,     0,     0,    44,
      45,    42,   237,     0,    43,     0,   219,    47,    48,     0,
      40,     0,     0,    44,    45,    41,     0,     0,     0,     0,
      46,    47,    48,     0,    40,    42,   272,   130,    43,    41,
      88,    89,    90,    91,     0,    40,     0,    44,    45,    42,
      41,     0,    43,    92,    46,    47,    48,     0,    93,    40,
      42,    44,    45,    43,    41,     0,     0,     0,    67,    47,
      48,     0,    44,    75,    42,     0,     0,    43,     0,    46,
      47,    48,     0,    40,     0,     0,    44,    45,    41,     0,
       0,     0,     0,    46,    47,    94,     0,    40,    42,     0,
     232,    43,   111,    88,    89,    90,    91,     0,    40,     0,
      44,    45,    42,    41,     0,    43,    92,    46,    99,    48,
       0,    93,    40,    42,    44,    45,    43,    41,     0,     0,
       0,    46,    47,    48,     0,    44,   121,    42,     0,     0,
      43,     0,   122,    47,    48,     0,     0,     0,   300,    44,
      45,    88,    89,    90,    91,     0,   247,    47,    48,     0,
       0,     0,     0,    98,    92,    88,    89,    90,    91,    93,
     -47,     0,   -47,   -47,   -47,   -47,     0,   142,    92,    88,
      89,    90,    91,    93,   145,   -47,    88,    89,    90,    91,
     -47,   147,    92,    88,    89,    90,    91,    93,   148,    92,
      88,    89,    90,    91,    93,   159,    92,    88,    89,    90,
      91,    93,   171,    92,    88,    89,    90,    91,    93,   202,
      92,    88,    89,    90,    91,    93,   222,    92,    88,    89,
      90,    91,    93,   258,    92,    88,    89,    90,    91,    93,
       0,    92,    88,    89,    90,    91,    93,     0,    92,    88,
      89,    90,    91,    93,   181,    92,    88,    89,    90,    91,
      93,   239,    92,    88,    89,    90,    91,    93,     0,    92,
       0,     0,     0,     0,    93,     0,    92,     0,     0,     0,
       0,   -48
};

static const yytype_int16 yycheck[] =
{
       0,    17,     6,     4,    32,     8,     8,    36,    24,    41,
      19,     0,    41,    23,     3,     4,     5,     6,    34,    19,
       9,    10,    11,    12,    40,    14,    42,    31,    38,    18,
      39,    20,    21,    23,    50,    38,    38,    22,    22,    55,
      41,    57,    58,    32,    41,    34,    35,    36,    38,    41,
      66,    40,    41,    22,    82,    39,    22,    23,    74,    22,
       3,     4,     5,     3,     4,     5,     3,     4,     5,    85,
      39,    15,    88,    89,    90,    91,    39,    93,    22,    41,
      23,    23,     3,     4,     5,     6,    22,    23,     9,    10,
      11,    12,    41,    14,   110,    39,    38,    18,    41,    20,
      21,    41,   118,    39,    41,     3,     4,     5,    41,    23,
     126,    32,    22,    34,    35,    36,   144,   133,    22,    40,
      41,     3,     4,     5,    38,    22,    22,   143,    41,    23,
       3,     4,     5,     6,    39,    23,     9,    10,    11,    12,
      37,    14,    23,    41,    38,    18,   162,    20,    21,   288,
      38,    22,    41,   169,   182,   183,   172,    38,    31,    41,
      23,    34,    35,    36,    41,    23,   305,    40,    41,    19,
     309,    23,    22,    19,   313,    38,   204,    19,   194,    24,
      38,    26,    27,    28,    29,    19,    38,    22,   204,     3,
       4,     5,     6,    13,    39,     9,    10,    11,    12,    44,
      14,   217,    41,    31,    18,    33,    20,    21,    19,     3,
       4,    22,   240,    41,    30,    41,    19,    31,    41,    19,
      34,    35,    36,    22,    22,    22,    40,    41,   244,     3,
       4,     5,     6,    39,    41,     9,    10,    11,    12,    41,
      14,    42,   258,     6,    18,    38,    20,    21,    23,    38,
       7,    26,    27,    28,    29,    12,    43,    31,    41,    19,
      34,    35,    36,    30,    39,    22,    40,    41,    25,    44,
      23,    41,    41,    41,    22,    41,    41,    34,    35,   295,
     280,    24,    38,    24,    41,    42,    43,    41,     3,     4,
       5,     6,    41,    24,     9,    10,    11,    12,    22,    14,
      16,    41,    41,    18,    24,    20,    21,    43,   308,    23,
      22,    24,    41,    35,   314,    38,    31,   317,   318,    34,
      35,    36,    22,    22,    22,    40,    41,     3,     4,     5,
       6,    41,    22,     9,    10,    11,    12,    38,    14,    23,
      22,    24,    18,    24,    20,    21,    41,    41,    41,    41,
      23,    23,    41,    24,    24,    31,    43,    24,    34,    35,
      36,    41,    22,    38,    40,    41,     3,     4,     5,     6,
      22,    22,     9,    10,    11,    12,    23,    14,    23,    15,
      23,    18,    24,    20,    21,    24,    19,    41,    38,    23,
       7,    23,    30,    24,    43,    12,    24,    34,    35,    36,
      41,    41,    37,    40,    41,    22,    23,    30,    25,    24,
      24,    41,    41,    24,     7,    23,    15,    34,    35,    12,
      24,    23,    19,    24,    41,    42,    43,    30,     7,    22,
      23,    37,    25,    12,    30,    24,    24,   105,    30,   287,
     235,    34,    35,    22,    23,    40,    25,   239,    41,    42,
      43,   128,     7,    -1,    -1,    34,    35,    12,    -1,    -1,
      -1,    -1,    41,    42,    43,    -1,     7,    22,    23,    -1,
      25,    12,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,
      35,    22,    23,    -1,    25,    -1,    41,    42,    43,    -1,
       7,    -1,    -1,    34,    35,    12,    -1,    -1,    -1,    -1,
      41,    42,    43,    -1,     7,    22,    23,    23,    25,    12,
      26,    27,    28,    29,    -1,     7,    -1,    34,    35,    22,
      12,    -1,    25,    39,    41,    42,    43,    -1,    44,     7,
      22,    34,    35,    25,    12,    -1,    -1,    -1,    41,    42,
      43,    -1,    34,    35,    22,    -1,    -1,    25,    -1,    41,
      42,    43,    -1,     7,    -1,    -1,    34,    35,    12,    -1,
      -1,    -1,    -1,    41,    42,    43,    -1,     7,    22,    -1,
      23,    25,    12,    26,    27,    28,    29,    -1,     7,    -1,
      34,    35,    22,    12,    -1,    25,    39,    41,    42,    43,
      -1,    44,     7,    22,    34,    35,    25,    12,    -1,    -1,
      -1,    41,    42,    43,    -1,    34,    35,    22,    -1,    -1,
      25,    -1,    41,    42,    43,    -1,    -1,    -1,    23,    34,
      35,    26,    27,    28,    29,    -1,    41,    42,    43,    -1,
      -1,    -1,    -1,    24,    39,    26,    27,    28,    29,    44,
      24,    -1,    26,    27,    28,    29,    -1,    24,    39,    26,
      27,    28,    29,    44,    24,    39,    26,    27,    28,    29,
      44,    24,    39,    26,    27,    28,    29,    44,    24,    39,
      26,    27,    28,    29,    44,    24,    39,    26,    27,    28,
      29,    44,    24,    39,    26,    27,    28,    29,    44,    24,
      39,    26,    27,    28,    29,    44,    24,    39,    26,    27,
      28,    29,    44,    24,    39,    26,    27,    28,    29,    44,
      -1,    39,    26,    27,    28,    29,    44,    -1,    39,    26,
      27,    28,    29,    44,    38,    39,    26,    27,    28,    29,
      44,    38,    39,    26,    27,    28,    29,    44,    -1,    39,
      -1,    -1,    -1,    -1,    44,    -1,    39,    -1,    -1,    -1,
      -1,    44
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     9,    10,    11,    12,    14,
      18,    20,    21,    32,    34,    35,    36,    40,    41,    48,
      49,    62,    63,    41,     4,    41,    41,    41,    41,    41,
      22,    22,    22,    41,    22,    22,    41,    39,    41,    41,
       7,    12,    22,    25,    34,    35,    41,    42,    43,    61,
      19,    39,     0,    49,    63,    19,    61,    19,    19,    22,
      13,    30,    41,    41,    61,    65,    19,    41,    61,    36,
      41,    50,    41,    22,    19,    35,    61,    66,    68,    69,
      22,    61,    22,    39,    41,    22,    39,    24,    26,    27,
      28,    29,    39,    44,    43,    61,    41,    61,    24,    42,
      61,    61,    43,    42,     6,    54,    55,    38,    38,    23,
      38,    12,    61,    39,    23,    41,    19,    30,    19,    22,
      23,    35,    41,    61,    41,     8,    38,     8,    38,    41,
      23,    65,    41,    22,    61,    41,    61,    61,    61,    61,
      41,    61,    24,    19,    22,    24,    24,    24,    24,    38,
      24,    41,    31,    55,    41,    41,    24,    61,    22,    24,
      41,    24,    16,    43,    52,    61,    23,    24,    41,    22,
      39,    24,    22,    61,    35,    68,    38,    23,    22,    23,
      66,    38,    22,    22,    61,    23,    65,    41,    22,    38,
      23,    41,    23,    61,    24,    31,    33,    41,    51,    56,
      57,    60,    24,    24,    22,    61,    41,    61,    65,    41,
      41,    23,    23,    41,    70,    23,    65,    22,    23,    41,
      65,    67,    24,    24,    23,    23,    43,    41,    53,    24,
      38,    24,    23,    61,    22,    22,    37,    23,    65,    38,
      22,    23,    22,    23,    15,    23,    23,    41,    15,    23,
      23,    24,    24,    38,    19,    23,    41,    30,    24,     3,
       4,     5,    41,    58,    59,    23,    59,     3,     4,    24,
      23,    70,    23,    61,    23,    41,    43,    24,    23,    63,
      64,    61,    41,    41,    41,    37,    23,    38,    30,    23,
      24,    24,    24,    23,    24,    15,    23,    24,    31,    63,
      23,     3,     4,     5,    41,    30,    41,    58,    64,    30,
      24,    61,    24,    30,    64,    37,    31,    64,    64,    31,
      41,    31,    31
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    47,    48,    48,    48,    48,    50,    49,    51,    51,
      51,    52,    52,    53,    54,    54,    55,    56,    56,    57,
      58,    58,    58,    58,    58,    58,    59,    59,    59,    59,
      59,    60,    60,    61,    61,    61,    61,    61,    61,    61,
      61,    61,    61,    61,    61,    61,    61,    61,    61,    61,
      61,    61,    61,    61,    61,    61,    61,    62,    62,    62,
      62,    62,    62,    62,    63,    63,    63,    63,    63,    63,
      63,    63,    63,    63,    63,    63,    63,    63,    63,    63,
      63,    63,    63,    63,    63,    63,    63,    63,    63,    63,
      63,    63,    64,    64,    65,    65,    66,    66,    67,    67,
      68,    69,    69,    70
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     1,     1,
       1,     0,     2,     3,     1,     2,     8,     4,     4,     7,
       3,     2,     3,     2,     3,     2,     0,     3,     5,     1,
       3,     6,     7,     3,     6,     3,     6,     3,     6,     5,
       6,     3,     5,     6,     3,     5,     1,     1,     1,     4,
       3,     3,     3,     3,     3,     4,     5,    10,     5,     5,
       5,     5,     6,     6,    10,     9,     3,     1,     4,     6,
       7,     6,     5,     5,     5,     5,     4,     5,     7,    13,
       5,     8,     9,     5,     7,    10,     5,     5,     5,     8,
       9,     3,     2,     1,     1,     3,     1,     3,     2,     5,
       5,     1,     3,     3
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
#line 63 "parser.y"
                                { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[-1].node), (yyvsp[0].node)); root = (yyval.node); }
#line 1428 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 64 "parser.y"
                                  { (yyval.node) = (yyvsp[-1].node); /* Ignora definición de clase */ root = (yyval.node); }
#line 1434 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 65 "parser.y"
                                  { (yyval.node) = (yyvsp[0].node); root = (yyval.node); }
#line 1440 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 66 "parser.y"
                                  { (yyval.node) = NULL; /* Ignora definición de clase */ }
#line 1446 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 70 "parser.y"
                         { last_class = create_class((yyvsp[0].sval)); add_class(last_class); }
#line 1452 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 71 "parser.y"
                                     { (yyval.node) = NULL; }
#line 1458 "parser.tab.c"
    break;

  case 11: /* class_body: %empty  */
#line 81 "parser.y"
        { (yyval.node) = NULL; }
#line 1464 "parser.tab.c"
    break;

  case 12: /* class_body: class_body class_member  */
#line 82 "parser.y"
                                  { (yyval.node) = (yyvsp[-1].node); }
#line 1470 "parser.tab.c"
    break;

  case 13: /* train_options: IDENTIFIER ASSIGN NUMBER  */
#line 85 "parser.y"
                                        { (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival)); }
#line 1476 "parser.tab.c"
    break;

  case 14: /* layer_list: layer_decl  */
#line 86 "parser.y"
                                                { (yyval.node) = (yyvsp[0].node); }
#line 1482 "parser.tab.c"
    break;

  case 15: /* layer_list: layer_list layer_decl  */
#line 87 "parser.y"
                                                { (yyval.node) = append_layer_to_list((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1488 "parser.tab.c"
    break;

  case 16: /* layer_decl: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 88 "parser.y"
                                                                             { (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval)); }
#line 1494 "parser.tab.c"
    break;

  case 17: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 92 "parser.y"
                                    { if (last_class) { add_attribute_to_class(last_class, (yyvsp[-3].sval), "int"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval)); } }
#line 1500 "parser.tab.c"
    break;

  case 18: /* attribute_decl: IDENTIFIER COLON STRING SEMICOLON  */
#line 93 "parser.y"
                                       { if (last_class) { add_attribute_to_class(last_class, (yyvsp[-3].sval), "string"); } else { printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval)); } }
#line 1506 "parser.tab.c"
    break;

  case 19: /* constructor_decl: CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 97 "parser.y"
                                                                               { if (last_class) { add_constructor_to_class(last_class, (yyvsp[-4].pnode), (yyvsp[-1].node)); } else { printf("Error: No hay clase definida para el constructor.\n"); } (yyval.node) = NULL; }
#line 1512 "parser.tab.c"
    break;

  case 20: /* parameter_decl: IDENTIFIER COLON INT  */
#line 101 "parser.y"
                                { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1518 "parser.tab.c"
    break;

  case 21: /* parameter_decl: INT IDENTIFIER  */
#line 102 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1524 "parser.tab.c"
    break;

  case 22: /* parameter_decl: IDENTIFIER COLON STRING  */
#line 103 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1530 "parser.tab.c"
    break;

  case 23: /* parameter_decl: STRING IDENTIFIER  */
#line 104 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1536 "parser.tab.c"
    break;

  case 24: /* parameter_decl: IDENTIFIER COLON FLOAT  */
#line 105 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1542 "parser.tab.c"
    break;

  case 25: /* parameter_decl: FLOAT IDENTIFIER  */
#line 106 "parser.y"
                               { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1548 "parser.tab.c"
    break;

  case 26: /* parameter_list: %empty  */
#line 110 "parser.y"
                                                    { (yyval.pnode) = NULL; }
#line 1554 "parser.tab.c"
    break;

  case 27: /* parameter_list: IDENTIFIER COLON IDENTIFIER  */
#line 111 "parser.y"
                                                   { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1560 "parser.tab.c"
    break;

  case 28: /* parameter_list: parameter_list COMMA IDENTIFIER COLON IDENTIFIER  */
#line 112 "parser.y"
                                                     { (yyval.pnode) = add_parameter((yyvsp[-4].pnode), (yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1566 "parser.tab.c"
    break;

  case 29: /* parameter_list: parameter_decl  */
#line 113 "parser.y"
                                                   { (yyval.pnode) = (yyvsp[0].pnode); }
#line 1572 "parser.tab.c"
    break;

  case 30: /* parameter_list: parameter_list COMMA parameter_decl  */
#line 114 "parser.y"
                                                   { (yyval.pnode) = add_parameter((yyvsp[-2].pnode), (yyvsp[0].pnode)->name, (yyvsp[0].pnode)->type); }
#line 1578 "parser.tab.c"
    break;

  case 31: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 118 "parser.y"
                                                               { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-5].sval)); } else { add_method_to_class(last_class, (yyvsp[-5].sval), NULL, (yyvsp[-1].node)); } (yyval.node) = NULL; }
#line 1584 "parser.tab.c"
    break;

  case 32: /* method_decl: IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 119 "parser.y"
                                                                              { if (!last_class) { printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-6].sval)); } else { add_method_to_class(last_class, (yyvsp[-6].sval), (yyvsp[-4].pnode), (yyvsp[-1].node)); } (yyval.node) = NULL; }
#line 1590 "parser.tab.c"
    break;

  case 33: /* expression: expression GT expression  */
#line 125 "parser.y"
      {
        (yyval.node) = create_ast_node("GT", (yyvsp[-2].node), (yyvsp[0].node));
      }
#line 1598 "parser.tab.c"
    break;

  case 34: /* expression: IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN  */
#line 128 "parser.y"
                                                               { printf(" [DEBUG] PERU Reconocido FUNCTION_CALL: %s(%s)\n", (yyvsp[-5].sval), (yyvsp[-3].node)); ASTNode *listExpr = create_identifier_node((yyvsp[-5].sval)); ASTNode *filterCall = create_list_function_call_node(listExpr, (yyvsp[-3].node), (yyvsp[-1].node)); (yyval.node) = create_var_decl_node((yyvsp[-5].sval), filterCall); }
#line 1604 "parser.tab.c"
    break;

  case 35: /* expression: LSBRACKET object_list RSBRACKET  */
#line 129 "parser.y"
                                                        { /* printf(" [DEBUG] Reconocido OBJECT_LIST\n"); */ (yyval.node) = (yyvsp[-1].node); }
#line 1610 "parser.tab.c"
    break;

  case 36: /* expression: expression DOT IDENTIFIER LPAREN lambda RPAREN  */
#line 130 "parser.y"
                                                        {  if (strcmp((yyvsp[-3].sval), "filter")==0) { (yyval.node) = create_list_function_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node)); } else { (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), NULL); } free((yyvsp[-3].sval)); }
#line 1616 "parser.tab.c"
    break;

  case 37: /* expression: LSBRACKET expr_list RSBRACKET  */
#line 131 "parser.y"
                                                        { /* printf(" [DEBUG] Reconocido EXPR_LIST\n"); */ (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-2].sval)); (yyval.node) = create_list_node((yyvsp[-1].node)); }
#line 1622 "parser.tab.c"
    break;

  case 38: /* expression: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN  */
#line 132 "parser.y"
                                                     { ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL); }
#line 1628 "parser.tab.c"
    break;

  case 39: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN  */
#line 133 "parser.y"
                                                    { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-2].sval), NULL); }
#line 1634 "parser.tab.c"
    break;

  case 40: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 134 "parser.y"
                                                           { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1640 "parser.tab.c"
    break;

  case 41: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 135 "parser.y"
                                                   { ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval)); ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)); (yyval.node) = create_ast_node("ACCESS_ATTR", obj, attr); }
#line 1646 "parser.tab.c"
    break;

  case 42: /* expression: expression DOT IDENTIFIER LPAREN RPAREN  */
#line 136 "parser.y"
                                                   { printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", (yyvsp[-4].node), (yyvsp[-2].sval)); (yyval.node) = create_method_call_node((yyvsp[-4].node), (yyvsp[-2].sval), NULL); }
#line 1652 "parser.tab.c"
    break;

  case 43: /* expression: expression DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 137 "parser.y"
                                                           { (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1658 "parser.tab.c"
    break;

  case 44: /* expression: THIS DOT IDENTIFIER  */
#line 138 "parser.y"
                                                   { (yyval.node) = create_ast_node("ACCESS_ATTR", create_ast_leaf("ID", 0, NULL, "this"), create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval))); }
#line 1664 "parser.tab.c"
    break;

  case 45: /* expression: THIS DOT IDENTIFIER LPAREN RPAREN  */
#line 139 "parser.y"
                                                   { (yyval.node) = create_method_call_node(create_ast_leaf("ID", 0, NULL, "this"), (yyvsp[-2].sval), NULL); }
#line 1670 "parser.tab.c"
    break;

  case 46: /* expression: IDENTIFIER  */
#line 140 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[0].sval)); }
#line 1676 "parser.tab.c"
    break;

  case 47: /* expression: NUMBER  */
#line 141 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 1682 "parser.tab.c"
    break;

  case 48: /* expression: STRING_LITERAL  */
#line 142 "parser.y"
                                                  { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 1688 "parser.tab.c"
    break;

  case 49: /* expression: CONCAT LPAREN expression_list RPAREN  */
#line 143 "parser.y"
                                                  { printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", (yyvsp[-1].node)); (yyval.node) = create_function_call_node("concat", (yyvsp[-1].node)); }
#line 1694 "parser.tab.c"
    break;

  case 50: /* expression: expression PLUS expression  */
#line 144 "parser.y"
                                                  { (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1700 "parser.tab.c"
    break;

  case 51: /* expression: expression MINUS expression  */
#line 145 "parser.y"
                                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1706 "parser.tab.c"
    break;

  case 52: /* expression: expression MULTIPLY expression  */
#line 146 "parser.y"
                                                  { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1712 "parser.tab.c"
    break;

  case 53: /* expression: expression DIVIDE expression  */
#line 147 "parser.y"
                                                  { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1718 "parser.tab.c"
    break;

  case 54: /* expression: LPAREN expression RPAREN  */
#line 148 "parser.y"
                                                  { (yyval.node) = (yyvsp[-1].node); }
#line 1724 "parser.tab.c"
    break;

  case 55: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 149 "parser.y"
                                                  { ClassNode *cls = find_class((yyvsp[-2].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval)); (yyval.node) = NULL; } else { (yyval.node) = (ASTNode *)create_object_with_args(cls, (yyvsp[-2].sval)); } }
#line 1730 "parser.tab.c"
    break;

  case 56: /* expression: NEW IDENTIFIER LPAREN expr_list RPAREN  */
#line 150 "parser.y"
                                         { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { fprintf(stderr, "Error: clase '%s' no encontrada\n", (yyvsp[-3].sval)); exit(1); } (yyval.node) = create_object_with_args(cls, (yyvsp[-1].node)); free((yyvsp[-3].sval)); }
#line 1736 "parser.tab.c"
    break;

  case 57: /* var_decl: LET IDENTIFIER ASSIGN IDENTIFIER LPAREN expression COMMA lambda_expression RPAREN SEMICOLON  */
#line 156 "parser.y"
                                                                                                 { /*printf(" [DEBUG] Reconocido FILTER_CALL: let %s = %s(...)\n", $2, $4);*/ ASTNode *listExpr = (yyvsp[-4].node); ASTNode *lambda = (yyvsp[-2].node); ASTNode *filterCall = create_list_function_call_node(listExpr, (yyvsp[-6].sval), lambda); filterCall->type = strdup("FILTER_CALL"); (yyval.node) = create_var_decl_node((yyvsp[-8].sval), filterCall); }
#line 1742 "parser.tab.c"
    break;

  case 58: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
#line 157 "parser.y"
                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1748 "parser.tab.c"
    break;

  case 59: /* var_decl: STRING IDENTIFIER ASSIGN expression SEMICOLON  */
#line 158 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1754 "parser.tab.c"
    break;

  case 60: /* var_decl: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 159 "parser.y"
                                                { printf("IMPRIMIENDO VAR \n"); (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1760 "parser.tab.c"
    break;

  case 61: /* var_decl: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 160 "parser.y"
                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node)); }
#line 1766 "parser.tab.c"
    break;

  case 62: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 161 "parser.y"
                                                           { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-5].sval)); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-3].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node)); }
#line 1772 "parser.tab.c"
    break;

  case 63: /* var_decl: THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 162 "parser.y"
                                                     { ASTNode *obj = create_ast_leaf("ID",0,NULL,"this"); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-3].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node)); }
#line 1778 "parser.tab.c"
    break;

  case 64: /* statement: FOR LPAREN LET IDENTIFIER IN expression RPAREN LBRACKET statement_list RBRACKET  */
#line 166 "parser.y"
                                                                                     { (yyval.node) = create_for_in_node((yyvsp[-6].sval), (yyvsp[-4].node), (yyvsp[-1].node)); }
#line 1784 "parser.tab.c"
    break;

  case 65: /* statement: LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 167 "parser.y"
                                                                             { printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", (yyvsp[-7].sval), (yyvsp[-5].sval)); (yyval.node) = create_var_decl_node((yyvsp[-7].sval), (yyvsp[-5].sval)); }
#line 1790 "parser.tab.c"
    break;

  case 66: /* statement: RETURN expression SEMICOLON  */
#line 168 "parser.y"
                                 { (yyval.node) = create_return_node((yyvsp[-1].node)); }
#line 1796 "parser.tab.c"
    break;

  case 68: /* statement: STRING STRING expression SEMICOLON  */
#line 170 "parser.y"
                                                                { printf(" [DEBUG] Declaración de variable s: ¿"); char buffer[2048]; sprintf(buffer,"#include \"easyspark/dataframe.hpp\"\n..."); generate_code(buffer); }
#line 1802 "parser.tab.c"
    break;

  case 69: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 171 "parser.y"
                                                       { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-5].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL); }
#line 1808 "parser.tab.c"
    break;

  case 70: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 172 "parser.y"
                                                                       { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-6].sval)); (yyval.node) = create_method_call_node(obj, (yyvsp[-4].sval), (yyvsp[-2].node)); }
#line 1814 "parser.tab.c"
    break;

  case 71: /* statement: THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 173 "parser.y"
                                                                { ASTNode *thisObj = create_ast_leaf("ID",0,NULL,"this"); (yyval.node) = create_method_call_node(thisObj, (yyvsp[-3].sval), NULL); }
#line 1820 "parser.tab.c"
    break;

  case 72: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 174 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1826 "parser.tab.c"
    break;

  case 73: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 175 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1832 "parser.tab.c"
    break;

  case 74: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 176 "parser.y"
                                                                { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1838 "parser.tab.c"
    break;

  case 75: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 177 "parser.y"
                                                                { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1844 "parser.tab.c"
    break;

  case 76: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 178 "parser.y"
                                                                { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR",0,NULL,(yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1850 "parser.tab.c"
    break;

  case 77: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 179 "parser.y"
                                                                { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1856 "parser.tab.c"
    break;

  case 78: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 180 "parser.y"
                                                                { ASTNode *obj = create_ast_leaf("ID",0,NULL,(yyvsp[-4].sval)); ASTNode *attr = create_ast_leaf("ID",0,NULL,(yyvsp[-2].sval)); ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr); (yyval.node) = create_ast_node("PRINT", access, NULL); }
#line 1862 "parser.tab.c"
    break;

  case 79: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 181 "parser.y"
                                                                                                                           { (yyval.node) = create_ast_node_for("FOR", create_ast_leaf("IDENTIFIER",0,NULL,(yyvsp[-10].sval)), create_ast_leaf("NUMBER",(yyvsp[-8].ival),NULL,NULL), (yyvsp[-6].node), (yyvsp[-4].node), (yyvsp[-1].node)); }
#line 1868 "parser.tab.c"
    break;

  case 80: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 182 "parser.y"
                                                                    { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval)); (yyval.node) = NULL; } else { (yyval.node) = (ASTNode *)create_object_with_args(cls, NULL); printf("[DEBUG] Creación de objeto: %s\n", (yyvsp[-3].sval)); } }
#line 1874 "parser.tab.c"
    break;

  case 81: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 183 "parser.y"
                                                                    { ClassNode *cls = find_class((yyvsp[-3].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval)); (yyval.node) = NULL; } else { (yyval.node) = create_var_decl_node((yyvsp[-6].sval), create_object_with_args(cls, NULL)); } }
#line 1880 "parser.tab.c"
    break;

  case 82: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 184 "parser.y"
                                                                                  { ClassNode *cls = find_class((yyvsp[-4].sval)); if (!cls) { printf("Error: Clase '%s' no definida.\n", (yyvsp[-4].sval)); (yyval.node) = NULL; } else { (yyval.node) = create_var_decl_node((yyvsp[-7].sval), create_object_with_args(cls, (yyvsp[-2].node))); } }
#line 1886 "parser.tab.c"
    break;

  case 83: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 185 "parser.y"
                                                                    { printf(" [DEBUG] Reconocido DATASET\n"); (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval)); }
#line 1892 "parser.tab.c"
    break;

  case 84: /* statement: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 186 "parser.y"
                                                                    { printf(" [DEBUG] Reconocido PREDICT\n"); (yyval.node) = create_predict_node((yyvsp[-4].sval), (yyvsp[-2].sval)); }
#line 1898 "parser.tab.c"
    break;

  case 85: /* statement: VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 187 "parser.y"
                                                                                      { printf(" [DEBUG] Reconocido PREDICT\n"); ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i"); (yyval.node) = create_method_call_node(obj, "predict", NULL); (yyval.node) = create_predict_node((yyvsp[-4].sval), (yyvsp[-2].sval)); }
#line 1904 "parser.tab.c"
    break;

  case 86: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 188 "parser.y"
                                                                { (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval)); }
#line 1910 "parser.tab.c"
    break;

  case 87: /* statement: PLOT LPAREN expression_list RPAREN SEMICOLON  */
#line 189 "parser.y"
                                                                { (yyval.node) = create_ast_node("PLOT", (yyvsp[-2].node), NULL); }
#line 1916 "parser.tab.c"
    break;

  case 88: /* statement: MODEL IDENTIFIER LBRACKET layer_list RBRACKET  */
#line 190 "parser.y"
                                                                { printf("[DEBUG] Reconocido MODEL\n"); printf("[DEBUG] Nombre del modelo: %s\n", (yyvsp[-3].sval)); ASTNode *layer = (yyvsp[-1].node); int capa_index = 0; while (layer) { if (strcmp(layer->type, "LAYER") == 0) { printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n", capa_index++, layer->id, layer->value, layer->str_value); } layer = layer->right; } ASTNode *modelNode = create_model_node((yyvsp[-3].sval), (yyvsp[-1].node)); printf("[DEBUG] Nodo de modelo creado: type=%s, id=%s\n", modelNode->type, modelNode->id); }
#line 1922 "parser.tab.c"
    break;

  case 89: /* statement: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 191 "parser.y"
                                                                      { (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval)); }
#line 1928 "parser.tab.c"
    break;

  case 90: /* statement: TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON  */
#line 192 "parser.y"
                                                                                   { (yyval.node) = create_train_node((yyvsp[-6].sval), (yyvsp[-4].sval), (yyvsp[-2].node)); }
#line 1934 "parser.tab.c"
    break;

  case 91: /* statement: IDENTIFIER ASSIGN NUMBER  */
#line 193 "parser.y"
                                                                { (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival)); }
#line 1940 "parser.tab.c"
    break;

  case 92: /* statement_list: statement_list statement  */
#line 197 "parser.y"
                              { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[-1].node), (yyvsp[0].node)); }
#line 1946 "parser.tab.c"
    break;

  case 93: /* statement_list: statement  */
#line 198 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1952 "parser.tab.c"
    break;

  case 94: /* expression_list: expression  */
#line 202 "parser.y"
                                              { (yyval.node) = (yyvsp[0].node); }
#line 1958 "parser.tab.c"
    break;

  case 95: /* expression_list: expression_list COMMA expression  */
#line 203 "parser.y"
                                              { (yyval.node) = add_statement((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1964 "parser.tab.c"
    break;

  case 96: /* expr_list: expression  */
#line 207 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1970 "parser.tab.c"
    break;

  case 97: /* expr_list: expr_list COMMA expression  */
#line 208 "parser.y"
                               { (yyval.node) = append_to_list((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1976 "parser.tab.c"
    break;

  case 98: /* lambda: IDENTIFIER ARROW  */
#line 217 "parser.y"
                                    { printf(" [DEBUG] Reconocido LAMBDA\n"); }
#line 1982 "parser.tab.c"
    break;

  case 99: /* lambda: LPAREN IDENTIFIER RPAREN ARROW expression  */
#line 218 "parser.y"
                                               { (yyval.node) = create_lambda_node((yyvsp[-3].sval), (yyvsp[0].node)); free((yyvsp[-3].sval)); }
#line 1988 "parser.tab.c"
    break;

  case 100: /* object_expression: NEW IDENTIFIER LPAREN expression_list RPAREN  */
#line 230 "parser.y"
  {
        ClassNode *cls = find_class((yyvsp[-3].sval));
        if (!cls) {
            printf("Error: Clase '%s' no encontrada.\n", (yyvsp[-3].sval));
            (yyval.node) = NULL;
        } else {
            (yyval.node) = create_object_with_args(cls, (yyvsp[-1].node));
        }
  }
#line 2002 "parser.tab.c"
    break;

  case 101: /* object_list: object_expression  */
#line 242 "parser.y"
      {
        /*printf(" [DEBUG] Reconocido OBJECT_EXPRESSION\n");*/
         (yyval.node) = create_list_node((yyvsp[0].node)); 
    }
#line 2011 "parser.tab.c"
    break;

  case 102: /* object_list: object_list COMMA object_expression  */
#line 247 "parser.y"
      { (yyval.node) = append_to_list((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2017 "parser.tab.c"
    break;

  case 103: /* lambda_expression: IDENTIFIER ARROW expression  */
#line 252 "parser.y"
      {
        (yyval.node) = create_lambda_node((yyvsp[-2].sval), (yyvsp[0].node)); // crea un nodo tipo LAMBDA
      }
#line 2025 "parser.tab.c"
    break;


#line 2029 "parser.tab.c"

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

#line 262 "parser.y"


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
    fprintf(stderr, "Error: %s en la línea %d (token: '%s')\n", s, yylineno, yytext);
}


int main(int argc, char *argv[]) {

    //yydebug = 1;
  

    // Flags para el modo de ejecución
    int interpret_mode = 0;
    if (argc < 2) {
        printf("Uso: %s <archivo.te> [--interpret]\n", argv[0]);
        return 1;
    }

    // Verificar si se pasó el flag --interpret
    if (argc == 3 && strcmp(argv[2], "--run") == 0) {
        interpret_mode = 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo %s\n", argv[1]);
        return 1;
    }

    yyin = file;
    int parse_result = yyparse();
    fclose(file);

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


    //if (interpret_mode) {
        //  Ejecutar directamente el AST
        if (root) {
            interpret_ast(root);
            free_ast(root);
        }
   // } else {
        // Compilar y ejecutar el código generado

   // }

    return 0;
}

