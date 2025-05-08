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

    ASTNode *root;
    extern int yylineno;
    FILE *yyin;
    void yyerror(const char *s);
    ClassNode *last_class = NULL;
    int yylex();
    void generate_code(const char* code);
    void clean_generated_code();


#line 87 "parser.tab.c"

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
  YYSYMBOL_var_decl = 61,                  /* var_decl  */
  YYSYMBOL_statement = 62,                 /* statement  */
  YYSYMBOL_statement_list = 63,            /* statement_list  */
  YYSYMBOL_expression_list = 64,           /* expression_list  */
  YYSYMBOL_expression = 65,                /* expression  */
  YYSYMBOL_expr_list = 66,                 /* expr_list  */
  YYSYMBOL_lambda = 67,                    /* lambda  */
  YYSYMBOL_arg_list = 68,                  /* arg_list  */
  YYSYMBOL_more_args = 69                  /* more_args  */
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
#define YYFINAL  55
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   731

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  47
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  23
/* YYNRULES -- Number of rules.  */
#define YYNRULES  103
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  299

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
static const yytype_int16 yyrline[] =
{
       0,    61,    61,    62,    63,    64,    69,    69,    82,    83,
      84,    86,    88,    90,    94,   101,   105,   112,   119,   128,
     139,   155,   157,   159,   161,   163,   165,   170,   171,   172,
     173,   175,   180,   193,   206,   211,   224,   238,   250,   257,
     271,   278,   281,   287,   288,   318,   325,   333,   338,   339,
     340,   341,   342,   343,   344,   352,   362,   363,   365,   366,
     378,   393,   410,   417,   425,   439,   443,   446,   473,   480,
     487,   500,   501,   510,   511,   518,   528,   530,   545,   550,
     555,   568,   576,   582,   589,   596,   597,   598,   599,   607,
     611,   612,   613,   614,   615,   627,   641,   642,   650,   655,
     664,   665,   671,   672
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
  "var_decl", "statement", "statement_list", "expression_list",
  "expression", "expr_list", "lambda", "arg_list", "more_args", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-205)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-87)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     179,   -24,    -1,    -6,     5,    14,    39,    64,   106,   108,
      69,   110,   122,   422,    94,    -2,   105,   109,   542,   -12,
     113,  -205,  -205,  -205,   132,   542,   145,   146,   144,   156,
     140,   134,   135,   542,   154,   556,    15,  -205,   224,  -205,
     138,   152,   159,   542,   158,   542,   165,   153,   155,   163,
    -205,  -205,   177,   567,   157,  -205,  -205,  -205,   542,   222,
     581,   542,   151,   166,   189,   169,   172,    -4,   443,   605,
     173,    42,   176,   202,  -205,  -205,   192,    38,   200,   619,
     443,     7,   184,   443,   542,   185,   209,   191,  -205,   542,
     542,   542,   542,   196,   303,   342,    65,   381,  -205,   215,
     650,   656,   203,   216,   206,    27,  -205,   211,   212,   219,
     542,   234,   664,   221,   239,   250,   225,  -205,   542,   244,
     249,   233,   240,   670,  -205,   542,   243,    16,   260,    54,
     261,   443,   443,   443,   443,   267,  -205,   542,   442,  -205,
    -205,  -205,  -205,   247,  -205,   269,  -205,  -205,   254,   272,
    -205,   443,   257,  -205,   115,  -205,   542,   275,    59,   678,
     279,  -205,   282,   264,  -205,   443,   271,  -205,   290,  -205,
     133,   295,   466,   480,   684,   296,    25,   298,   281,   287,
     309,   284,   310,    76,   542,  -205,   315,    61,  -205,  -205,
    -205,  -205,  -205,  -205,   504,   316,   320,  -205,   542,  -205,
    -205,  -205,    68,   630,  -205,    10,    71,   321,  -205,  -205,
     327,   328,   319,   340,   337,  -205,   322,  -205,   422,   692,
       6,     1,   137,   343,    88,   133,   518,  -205,   133,  -205,
      -7,  -205,  -205,  -205,  -205,  -205,   331,   318,   349,   353,
    -205,   542,   336,   341,   350,   346,  -205,    91,   360,    98,
     372,   374,  -205,   375,   376,  -205,   387,   388,  -205,  -205,
     382,   129,  -205,  -205,  -205,     9,   385,    18,   422,   386,
    -205,  -205,  -205,  -205,   542,   397,  -205,   392,  -205,  -205,
    -205,  -205,   422,   393,  -205,   266,   422,   443,  -205,   422,
     305,    26,  -205,   344,   383,  -205,  -205,  -205,  -205
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
      57,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    57,     0,     0,     0,     0,     0,     0,
       0,     5,    43,     4,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    72,     0,     6,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    85,
      87,    86,     0,     0,     0,     1,     3,     2,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    73,     0,
      85,     0,     0,     0,    56,    71,     0,     0,     0,     0,
      96,     0,     0,    93,     0,     0,     0,     0,    42,     0,
       0,     0,     0,     0,    70,     0,     0,     0,    44,    87,
       0,     0,     0,     0,     0,     0,    15,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    11,     0,     0,
       0,     0,    85,     0,    76,     0,     0,     0,    83,     0,
      80,    89,    90,    91,    92,     0,    52,     0,     0,    37,
      48,    35,    50,     0,    62,     0,    67,    16,     0,     0,
      66,    74,     0,    36,    80,    53,     0,     0,     0,     0,
       0,    59,     0,     0,    34,    97,     0,    88,     0,    94,
     102,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     7,     0,     0,    13,     8,
       9,    10,    39,    47,     0,    80,     0,    84,     0,   101,
      95,    78,     0,     0,    81,    85,     0,     0,    38,    45,
       0,     0,     0,     0,     0,    63,     0,    54,    57,     0,
      27,    27,     0,    94,     0,    73,     0,    77,   102,    79,
      85,    98,    82,    75,    46,    68,     0,     0,     0,     0,
      40,     0,     0,     0,     0,     0,    30,     0,     0,     0,
       0,     0,    60,     0,    78,   103,     0,     0,    14,    69,
      77,     0,    22,    24,    26,     0,     0,     0,    57,     0,
      18,    19,    61,    41,     0,     0,    64,     0,    21,    23,
      25,    28,    57,     0,    31,     0,    57,    99,    17,    57,
       0,     0,    32,     0,     0,    20,    29,    33,    55
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -205,  -205,   409,  -205,  -205,  -205,  -205,  -205,   307,  -205,
    -205,   168,   217,  -205,  -205,     0,  -204,   -31,   -17,  -205,
    -205,  -205,   213
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    20,    21,    76,   188,   158,   214,   105,   106,   189,
     190,   246,   247,   191,    22,    37,    38,   202,    68,    81,
     207,   171,   199
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      23,    52,    67,    25,   242,   243,   244,    53,    59,   242,
     243,   244,   278,   279,   280,   124,   256,    24,    71,   109,
      57,   242,   243,   244,   248,   231,    80,    54,    83,   278,
     279,   280,    87,   104,   110,    27,    95,    40,    75,   167,
      26,    97,   245,   100,   101,   125,    28,   245,   210,    87,
     281,    72,   112,   127,   110,    29,    73,   118,   146,   283,
     119,    43,   123,   110,   285,   114,    44,   296,    89,    90,
      91,    92,   131,   132,   133,   134,    45,   169,   290,    46,
      30,    93,   293,   221,   137,   294,    31,   138,    47,    48,
     185,   229,   186,   151,   232,    49,    50,    51,   222,   218,
     187,   159,    89,    90,    91,    92,   110,   176,   165,   110,
      34,   253,   170,    55,   266,    93,     1,     2,     3,     4,
     174,   269,     5,     6,     7,     8,   110,     9,    32,   267,
      33,    10,    35,    11,    12,    39,   267,   172,   182,   183,
     250,   251,   206,    13,    36,    14,    41,    15,    16,    17,
      42,    58,   277,    18,    19,    89,    90,    91,    92,    89,
      90,    91,    92,   224,    60,    61,    62,   219,    93,    63,
      64,   198,    93,    69,    78,    65,    66,   225,    79,    77,
      82,   228,     1,     2,     3,     4,    83,    84,     5,     6,
       7,     8,    85,     9,   102,   104,    86,    10,    96,    11,
      12,    88,    87,    89,    90,    91,    92,   107,   103,    13,
     108,    14,   113,    15,    16,    17,    93,   115,   240,    18,
      19,   116,   117,   120,   261,   126,   128,     1,     2,     3,
       4,   129,   130,     5,     6,     7,     8,   135,     9,   140,
     144,   143,    10,   150,    11,    12,    98,   145,    89,    90,
      91,    92,   148,   149,    13,    74,   152,   287,    15,    16,
      17,    93,   154,   155,    18,    19,   156,   160,   157,     1,
       2,     3,     4,   161,   162,     5,     6,     7,     8,   163,
       9,   166,   168,   172,    10,    75,    11,    12,   177,   173,
      75,   178,   179,    75,    75,   180,    13,   292,   181,   184,
      15,    16,    17,   193,   194,   195,    18,    19,     1,     2,
       3,     4,   196,   197,     5,     6,     7,     8,   200,     9,
     209,   211,   216,    10,   212,    11,    12,   -86,   213,   -86,
     -86,   -86,   -86,   215,   217,    13,   295,   220,   226,    15,
      16,    17,   -86,   227,   233,    18,    19,     1,     2,     3,
       4,   234,   235,     5,     6,     7,     8,   236,     9,   237,
     238,   258,    10,   239,    11,    12,   136,   252,    89,    90,
      91,    92,   257,   259,    13,   297,   260,   262,    15,    16,
      17,    93,   263,   265,    18,    19,     1,     2,     3,     4,
     268,   264,     5,     6,     7,     8,   270,     9,   271,   272,
     273,    10,   274,    11,    12,   139,   276,    89,    90,    91,
      92,   275,   147,    13,   298,   282,   286,    15,    16,    17,
      93,   288,   289,    18,    19,     1,     2,     3,     4,    56,
     291,     5,     6,     7,     8,   284,     9,     0,   249,     0,
      10,   255,    11,    12,     0,     0,     0,     0,     0,    43,
       0,     0,    13,     0,    44,     0,    15,    16,    17,     0,
       0,     0,    18,    19,    45,   175,     0,    46,     0,    89,
      90,    91,    92,    43,     0,     0,    47,    48,    44,     0,
       0,     0,    93,    49,    50,    51,     0,    43,    45,   201,
       0,    46,    44,     0,     0,     0,     0,     0,     0,     0,
      47,    48,   203,   204,     0,    46,     0,    49,    50,    51,
       0,    43,     0,     0,    47,    48,    44,     0,     0,     0,
       0,   205,    50,    51,     0,    43,    45,   223,     0,    46,
      44,     0,     0,     0,     0,     0,     0,     0,    47,    48,
      45,   254,     0,    46,     0,    49,    50,    51,     0,    43,
       0,     0,    47,    48,    44,     0,     0,     0,     0,    49,
      50,    51,     0,    43,    45,     0,     0,    46,    44,     0,
       0,     0,     0,     0,    43,     0,    47,    48,    45,    44,
       0,    46,     0,    49,    50,    51,     0,     0,    43,    45,
      47,    48,    46,    44,     0,     0,     0,    70,    50,    51,
       0,    47,    48,    45,     0,     0,    46,     0,    49,    50,
      94,     0,    43,     0,     0,    47,    48,   111,     0,     0,
       0,     0,    49,    99,    51,     0,    43,    45,     0,     0,
      46,    44,     0,     0,     0,     0,     0,    43,     0,    47,
      48,    45,    44,     0,    46,     0,    49,    50,    51,     0,
       0,     0,    45,    47,   121,    46,     0,     0,     0,     0,
     122,    50,    51,     0,    47,    48,     0,     0,     0,     0,
       0,   230,    50,    51,   141,     0,    89,    90,    91,    92,
     142,     0,    89,    90,    91,    92,     0,     0,   153,    93,
      89,    90,    91,    92,   164,    93,    89,    90,    91,    92,
       0,     0,   192,    93,    89,    90,    91,    92,   208,    93,
      89,    90,    91,    92,     0,     0,   241,    93,    89,    90,
      91,    92,     0,    93,     0,     0,     0,     0,     0,     0,
       0,    93
};

static const yytype_int16 yycheck[] =
{
       0,    18,    33,     4,     3,     4,     5,    19,    25,     3,
       4,     5,     3,     4,     5,     8,    23,    41,    35,    23,
      20,     3,     4,     5,    23,    15,    43,    39,    45,     3,
       4,     5,    39,     6,    38,    41,    53,    39,    38,    23,
      41,    58,    41,    60,    61,    38,    41,    41,    23,    39,
      41,    36,    69,    84,    38,    41,    41,    19,    31,    41,
      22,     7,    79,    38,   268,    23,    12,    41,    26,    27,
      28,    29,    89,    90,    91,    92,    22,    23,   282,    25,
      41,    39,   286,    22,    19,   289,    22,    22,    34,    35,
      31,    23,    33,   110,    23,    41,    42,    43,    37,    23,
      41,   118,    26,    27,    28,    29,    38,   138,   125,    38,
      41,    23,   129,     0,    23,    39,     3,     4,     5,     6,
     137,    23,     9,    10,    11,    12,    38,    14,    22,    38,
      22,    18,    22,    20,    21,    41,    38,    22,    23,   156,
       3,     4,   173,    30,    22,    32,    41,    34,    35,    36,
      41,    19,    23,    40,    41,    26,    27,    28,    29,    26,
      27,    28,    29,   194,    19,    19,    22,   184,    39,    13,
      30,    38,    39,    19,    22,    41,    41,   194,    19,    41,
      22,   198,     3,     4,     5,     6,   203,    22,     9,    10,
      11,    12,    39,    14,    43,     6,    41,    18,    41,    20,
      21,    24,    39,    26,    27,    28,    29,    38,    42,    30,
      38,    32,    39,    34,    35,    36,    39,    41,   218,    40,
      41,    19,    30,    23,   241,    41,    41,     3,     4,     5,
       6,    22,    41,     9,    10,    11,    12,    41,    14,    24,
      24,    38,    18,    24,    20,    21,    24,    41,    26,    27,
      28,    29,    41,    41,    30,    31,    22,   274,    34,    35,
      36,    39,    41,    24,    40,    41,    16,    23,    43,     3,
       4,     5,     6,    24,    41,     9,    10,    11,    12,    39,
      14,    38,    22,    22,    18,   285,    20,    21,    41,    22,
     290,    22,    38,   293,   294,    23,    30,    31,    41,    24,
      34,    35,    36,    24,    22,    41,    40,    41,     3,     4,
       5,     6,    41,    23,     9,    10,    11,    12,    23,    14,
      24,    23,    38,    18,    43,    20,    21,    24,    41,    26,
      27,    28,    29,    24,    24,    30,    31,    22,    22,    34,
      35,    36,    39,    23,    23,    40,    41,     3,     4,     5,
       6,    24,    24,     9,    10,    11,    12,    38,    14,    19,
      23,    43,    18,    41,    20,    21,    24,    24,    26,    27,
      28,    29,    41,    24,    30,    31,    23,    41,    34,    35,
      36,    39,    41,    37,    40,    41,     3,     4,     5,     6,
      30,    41,     9,    10,    11,    12,    24,    14,    24,    24,
      24,    18,    15,    20,    21,    24,    24,    26,    27,    28,
      29,    23,   105,    30,    31,    30,    30,    34,    35,    36,
      39,    24,    30,    40,    41,     3,     4,     5,     6,    20,
      37,     9,    10,    11,    12,   267,    14,    -1,   221,    -1,
      18,   228,    20,    21,    -1,    -1,    -1,    -1,    -1,     7,
      -1,    -1,    30,    -1,    12,    -1,    34,    35,    36,    -1,
      -1,    -1,    40,    41,    22,    23,    -1,    25,    -1,    26,
      27,    28,    29,     7,    -1,    -1,    34,    35,    12,    -1,
      -1,    -1,    39,    41,    42,    43,    -1,     7,    22,    23,
      -1,    25,    12,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      34,    35,    22,    23,    -1,    25,    -1,    41,    42,    43,
      -1,     7,    -1,    -1,    34,    35,    12,    -1,    -1,    -1,
      -1,    41,    42,    43,    -1,     7,    22,    23,    -1,    25,
      12,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    35,
      22,    23,    -1,    25,    -1,    41,    42,    43,    -1,     7,
      -1,    -1,    34,    35,    12,    -1,    -1,    -1,    -1,    41,
      42,    43,    -1,     7,    22,    -1,    -1,    25,    12,    -1,
      -1,    -1,    -1,    -1,     7,    -1,    34,    35,    22,    12,
      -1,    25,    -1,    41,    42,    43,    -1,    -1,     7,    22,
      34,    35,    25,    12,    -1,    -1,    -1,    41,    42,    43,
      -1,    34,    35,    22,    -1,    -1,    25,    -1,    41,    42,
      43,    -1,     7,    -1,    -1,    34,    35,    12,    -1,    -1,
      -1,    -1,    41,    42,    43,    -1,     7,    22,    -1,    -1,
      25,    12,    -1,    -1,    -1,    -1,    -1,     7,    -1,    34,
      35,    22,    12,    -1,    25,    -1,    41,    42,    43,    -1,
      -1,    -1,    22,    34,    35,    25,    -1,    -1,    -1,    -1,
      41,    42,    43,    -1,    34,    35,    -1,    -1,    -1,    -1,
      -1,    41,    42,    43,    24,    -1,    26,    27,    28,    29,
      24,    -1,    26,    27,    28,    29,    -1,    -1,    24,    39,
      26,    27,    28,    29,    24,    39,    26,    27,    28,    29,
      -1,    -1,    24,    39,    26,    27,    28,    29,    24,    39,
      26,    27,    28,    29,    -1,    -1,    24,    39,    26,    27,
      28,    29,    -1,    39,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    39
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     9,    10,    11,    12,    14,
      18,    20,    21,    30,    32,    34,    35,    36,    40,    41,
      48,    49,    61,    62,    41,     4,    41,    41,    41,    41,
      41,    22,    22,    22,    41,    22,    22,    62,    63,    41,
      39,    41,    41,     7,    12,    22,    25,    34,    35,    41,
      42,    43,    65,    19,    39,     0,    49,    62,    19,    65,
      19,    19,    22,    13,    30,    41,    41,    64,    65,    19,
      41,    65,    36,    41,    31,    62,    50,    41,    22,    19,
      65,    66,    22,    65,    22,    39,    41,    39,    24,    26,
      27,    28,    29,    39,    43,    65,    41,    65,    24,    42,
      65,    65,    43,    42,     6,    54,    55,    38,    38,    23,
      38,    12,    65,    39,    23,    41,    19,    30,    19,    22,
      23,    35,    41,    65,     8,    38,    41,    64,    41,    22,
      41,    65,    65,    65,    65,    41,    24,    19,    22,    24,
      24,    24,    24,    38,    24,    41,    31,    55,    41,    41,
      24,    65,    22,    24,    41,    24,    16,    43,    52,    65,
      23,    24,    41,    39,    24,    65,    38,    23,    22,    23,
      65,    68,    22,    22,    65,    23,    64,    41,    22,    38,
      23,    41,    23,    65,    24,    31,    33,    41,    51,    56,
      57,    60,    24,    24,    22,    41,    41,    23,    38,    69,
      23,    23,    64,    22,    23,    41,    64,    67,    24,    24,
      23,    23,    43,    41,    53,    24,    38,    24,    23,    65,
      22,    22,    37,    23,    64,    65,    22,    23,    65,    23,
      41,    15,    23,    23,    24,    24,    38,    19,    23,    41,
      62,    24,     3,     4,     5,    41,    58,    59,    23,    59,
       3,     4,    24,    23,    23,    69,    23,    41,    43,    24,
      23,    65,    41,    41,    41,    37,    23,    38,    30,    23,
      24,    24,    24,    24,    15,    23,    24,    23,     3,     4,
       5,    41,    30,    41,    58,    63,    30,    65,    24,    30,
      63,    37,    31,    63,    63,    31,    41,    31,    31
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    47,    48,    48,    48,    48,    50,    49,    51,    51,
      51,    52,    52,    52,    53,    54,    54,    55,    56,    56,
      57,    58,    58,    58,    58,    58,    58,    59,    59,    59,
      59,    59,    60,    60,    61,    61,    61,    61,    61,    61,
      62,    62,    62,    62,    62,    62,    62,    62,    62,    62,
      62,    62,    62,    62,    62,    62,    62,    62,    62,    62,
      62,    62,    62,    62,    62,    62,    62,    62,    62,    62,
      62,    63,    63,    64,    64,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    66,    66,    67,    67,
      68,    68,    69,    69
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     1,     1,
       1,     0,     0,     2,     3,     1,     2,     8,     4,     4,
       7,     3,     2,     3,     2,     3,     2,     0,     3,     5,
       1,     3,     6,     7,     5,     5,     5,     5,     6,     6,
       8,     9,     3,     1,     4,     6,     7,     6,     5,     5,
       5,     5,     4,     5,     7,    13,     3,     0,     1,     5,
       8,     9,     5,     7,    10,     5,     5,     5,     8,     9,
       3,     2,     1,     1,     3,     6,     3,     6,     5,     6,
       3,     5,     6,     3,     5,     1,     1,     1,     4,     3,
       3,     3,     3,     2,     4,     5,     1,     3,     2,     5,
       0,     2,     0,     3
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
#line 61 "parser.y"
                              { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1411 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 62 "parser.y"
                              { (yyval.node) = (yyvsp[-1].node); /* Ignora definición de clase */ root = (yyval.node); }
#line 1417 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 63 "parser.y"
                              { (yyval.node) = (yyvsp[0].node); root = (yyval.node); }
#line 1423 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 64 "parser.y"
                              { (yyval.node) = NULL; /* Ignora definición de clase */ }
#line 1429 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 69 "parser.y"
                              {
            last_class = create_class((yyvsp[0].sval));
            add_class(last_class);
          }
#line 1438 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 74 "parser.y"
                                  {
                                    /* Fin definición de clase */
                                    (yyval.node) = NULL;  /* No añade nada al AST */
                                 }
#line 1447 "parser.tab.c"
    break;

  case 12: /* class_body: %empty  */
#line 88 "parser.y"
                                           { (yyval.node) = NULL; }
#line 1453 "parser.tab.c"
    break;

  case 13: /* class_body: class_body class_member  */
#line 90 "parser.y"
                                          { (yyval.node) = (yyvsp[-1].node); }
#line 1459 "parser.tab.c"
    break;

  case 14: /* train_options: IDENTIFIER ASSIGN NUMBER  */
#line 95 "parser.y"
    {
        (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival));
    }
#line 1467 "parser.tab.c"
    break;

  case 15: /* layer_list: layer_decl  */
#line 102 "parser.y"
    {
        (yyval.node) = (yyvsp[0].node);
    }
#line 1475 "parser.tab.c"
    break;

  case 16: /* layer_list: layer_list layer_decl  */
#line 106 "parser.y"
    {
        (yyval.node) = append_layer_to_list((yyvsp[-1].node), (yyvsp[0].node));
    }
#line 1483 "parser.tab.c"
    break;

  case 17: /* layer_decl: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 113 "parser.y"
    {
        (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval));
    }
#line 1491 "parser.tab.c"
    break;

  case 18: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 120 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "int");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1503 "parser.tab.c"
    break;

  case 19: /* attribute_decl: IDENTIFIER COLON STRING SEMICOLON  */
#line 129 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "string");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1515 "parser.tab.c"
    break;

  case 20: /* constructor_decl: CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 140 "parser.y"
    {
        //printf("[DEBUG] Se detectó un constructor en la clase %s\n", last_class->name);
        if (last_class) {
            add_constructor_to_class(last_class, (yyvsp[-4].pnode), (yyvsp[-1].node));
           // add_constructor_to_class(last_class, $6);
           //add_method_to_class(last_class, last_class->name /*o "__init__"*/, $6);

        } else {
            printf("Error: No hay clase definida para el constructor.\n");
        }
        (yyval.node) = NULL;
    }
#line 1532 "parser.tab.c"
    break;

  case 21: /* parameter_decl: IDENTIFIER COLON INT  */
#line 156 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1538 "parser.tab.c"
    break;

  case 22: /* parameter_decl: INT IDENTIFIER  */
#line 158 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1544 "parser.tab.c"
    break;

  case 23: /* parameter_decl: IDENTIFIER COLON STRING  */
#line 160 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1550 "parser.tab.c"
    break;

  case 24: /* parameter_decl: STRING IDENTIFIER  */
#line 162 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1556 "parser.tab.c"
    break;

  case 25: /* parameter_decl: IDENTIFIER COLON FLOAT  */
#line 164 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1562 "parser.tab.c"
    break;

  case 26: /* parameter_decl: FLOAT IDENTIFIER  */
#line 166 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1568 "parser.tab.c"
    break;

  case 27: /* parameter_list: %empty  */
#line 170 "parser.y"
                 { (yyval.pnode) = NULL; }
#line 1574 "parser.tab.c"
    break;

  case 28: /* parameter_list: IDENTIFIER COLON IDENTIFIER  */
#line 171 "parser.y"
                                  { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1580 "parser.tab.c"
    break;

  case 29: /* parameter_list: parameter_list COMMA IDENTIFIER COLON IDENTIFIER  */
#line 172 "parser.y"
                                                       { (yyval.pnode) = add_parameter((yyvsp[-4].pnode), (yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1586 "parser.tab.c"
    break;

  case 30: /* parameter_list: parameter_decl  */
#line 174 "parser.y"
    { (yyval.pnode) = (yyvsp[0].pnode);}
#line 1592 "parser.tab.c"
    break;

  case 31: /* parameter_list: parameter_list COMMA parameter_decl  */
#line 176 "parser.y"
    { (yyval.pnode) = add_parameter((yyvsp[-2].pnode), (yyvsp[0].pnode)->name, (yyvsp[0].pnode)->type); }
#line 1598 "parser.tab.c"
    break;

  case 32: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 181 "parser.y"
    {
        /* Añade el método $1 al último last_class declarado */
                if (!last_class) {
                        printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-5].sval));
                    } else {
                        /* params = NULL, body = $5 */
                        add_method_to_class(last_class, (yyvsp[-5].sval), NULL, (yyvsp[-1].node));
                    }
                    (yyval.node) = NULL;
    }
#line 1613 "parser.tab.c"
    break;

  case 33: /* method_decl: IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 194 "parser.y"
    {
        if (!last_class) {
            printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-6].sval));
        } else {
            /* params = $3, body = $6 */
            add_method_to_class(last_class, (yyvsp[-6].sval), (yyvsp[-4].pnode), (yyvsp[-1].node));
        }
        (yyval.node) = NULL;
    }
#line 1627 "parser.tab.c"
    break;

  case 34: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
#line 207 "parser.y"
    {
        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }
#line 1636 "parser.tab.c"
    break;

  case 35: /* var_decl: STRING IDENTIFIER ASSIGN expression SEMICOLON  */
#line 211 "parser.y"
                                                  {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1652 "parser.tab.c"
    break;

  case 36: /* var_decl: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 224 "parser.y"
                                               {
        printf("IMPRIMIENDO VAR \n");
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1669 "parser.tab.c"
    break;

  case 37: /* var_decl: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 238 "parser.y"
                                               {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1685 "parser.tab.c"
    break;

  case 38: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 250 "parser.y"
                                                            {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
        (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));  //  left = acceso, right = valor
    }
#line 1696 "parser.tab.c"
    break;

  case 39: /* var_decl: THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 257 "parser.y"
                                                        {
              /* obj será siempre “this” */
              ASTNode *obj    = create_ast_leaf("ID", 0, NULL, "this");
                    /* 2) El nombre del atributo viene en $3 */
                    ASTNode *attr   = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
                    ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
                    /* 3) La expresión es $5 */
                    (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));
    }
#line 1710 "parser.tab.c"
    break;

  case 40: /* statement: FOR LPAREN LET IDENTIFIER IN expression RPAREN statement  */
#line 272 "parser.y"
        {
          /* $4 = nombre de variable, $6 = expresión que evalúa la lista,
             $8 = cuerpo (puede ser bloque o simple statement) */
          (yyval.node) = create_for_in_node((yyvsp[-4].sval), (yyvsp[-2].node), (yyvsp[0].node));
        }
#line 1720 "parser.tab.c"
    break;

  case 41: /* statement: LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 279 "parser.y"
        { printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", (yyvsp[-7].sval), (yyvsp[-5].sval));
            (yyval.node) = create_var_decl_node((yyvsp[-7].sval), (yyvsp[-5].sval)); }
#line 1727 "parser.tab.c"
    break;

  case 42: /* statement: RETURN expression SEMICOLON  */
#line 282 "parser.y"
{
   // printf(" [DEBUG] Reconocido RETURN\n");
  // crea un nodo de retorno
  (yyval.node) = create_return_node((yyvsp[-1].node));
}
#line 1737 "parser.tab.c"
    break;

  case 44: /* statement: STRING STRING expression SEMICOLON  */
#line 288 "parser.y"
                                         {

        printf(" [DEBUG] Declaración de variable s: ¿");


        char buffer[2048];
          sprintf(buffer,
              "#include \"easyspark/dataframe.hpp\"\n"
              "#include <chrono>\n"
              "#include <iostream>\n"
              "int main() {\n"
              "    auto start = std::chrono::high_resolution_clock::now();\n"
              "    DataFrame df = read(\"dataset2.csv\");\n"
              "    auto filtered = df.filter([](const Row& row) {\n"
              "        auto it = row.find(\"adult\");\n"
              "        return it != row.end() && it->second == \"FALSE\";\n"
              "    });\n"
              "    std::cout << \"Filas que cumplen el filtro: \" << filtered.rows.size() << std::endl;\n"
              "    filtered.select({\"title\"}).show();\n"
              "    auto end = std::chrono::high_resolution_clock::now();\n"
              "    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);\n"
              "    std::cout << \"Duración: \" << duration.count() << \" ms\" << std::endl;\n"
              "    return 0;\n"
              "}\n"
          );
          generate_code(buffer);


    }
#line 1771 "parser.tab.c"
    break;

  case 45: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 319 "parser.y"
  {

    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL);
  }
#line 1781 "parser.tab.c"
    break;

  case 46: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 326 "parser.y"
  {

    //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-6].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-4].sval), (yyvsp[-2].node));
  }
#line 1792 "parser.tab.c"
    break;

  case 47: /* statement: THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 334 "parser.y"
  {
    ASTNode *thisObj = create_ast_leaf("ID", 0, NULL, "this");
    (yyval.node) = create_method_call_node(thisObj, (yyvsp[-3].sval), NULL);
  }
#line 1801 "parser.tab.c"
    break;

  case 48: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 338 "parser.y"
                                                        { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1807 "parser.tab.c"
    break;

  case 49: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 339 "parser.y"
                                                 { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1813 "parser.tab.c"
    break;

  case 50: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 340 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1819 "parser.tab.c"
    break;

  case 51: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 341 "parser.y"
                                                 { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1825 "parser.tab.c"
    break;

  case 52: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 342 "parser.y"
                                             { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1831 "parser.tab.c"
    break;

  case 53: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 343 "parser.y"
                                               { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1837 "parser.tab.c"
    break;

  case 54: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 344 "parser.y"
                                                              {
       // printf(" [DEBUG] Reconocido PRINT con acceso a atributo: %s.%s\n", $3, $5);
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
        (yyval.node) = create_ast_node("PRINT", access, NULL);
    }
#line 1849 "parser.tab.c"
    break;

  case 55: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 353 "parser.y"
    {
        (yyval.node) = create_ast_node_for("FOR",
            create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-10].sval)),
            create_ast_leaf("NUMBER", (yyvsp[-8].ival), NULL, NULL),
            (yyvsp[-6].node),
            (yyvsp[-4].node),
            (yyvsp[-1].node)
        );
    }
#line 1863 "parser.tab.c"
    break;

  case 59: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 367 "parser.y"
    {
        ClassNode *cls = find_class((yyvsp[-3].sval));
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval));
            (yyval.node) = NULL;
        } else {
            (yyval.node) = (ASTNode *)create_object_with_args(cls, NULL);
            printf("[DEBUG] Creación de objeto: %s\n", (yyvsp[-3].sval));
        }
    }
#line 1878 "parser.tab.c"
    break;

  case 60: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 379 "parser.y"
     {
         ClassNode *cls = find_class((yyvsp[-3].sval));
         if (!cls) {
             printf("Error: Clase '%s' no definida.\n", (yyvsp[-3].sval));
             (yyval.node) = NULL;
         } else {
             (yyval.node) = create_var_decl_node(
                 (yyvsp[-6].sval),
                 create_object_with_args(cls, NULL)
             );
         }
     }
#line 1895 "parser.tab.c"
    break;

  case 61: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 394 "parser.y"
     {
         ClassNode *cls = find_class((yyvsp[-4].sval));
         if (!cls) {
             printf("Error: Clase '%s' no definida.\n", (yyvsp[-4].sval));
             (yyval.node) = NULL;
         } else {
             (yyval.node) = create_var_decl_node(
                 (yyvsp[-7].sval),
                 create_object_with_args(cls, (yyvsp[-2].node))
             );
         }
     }
#line 1912 "parser.tab.c"
    break;

  case 62: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 411 "parser.y"
     { printf(" [DEBUG] Reconocido DATASET\n");
        (yyval.node) = create_dataset_node((yyvsp[-3].sval),(yyvsp[-1].sval));
     }
#line 1920 "parser.tab.c"
    break;

  case 63: /* statement: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 418 "parser.y"
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");
        (yyval.node) = create_predict_node((yyvsp[-4].sval),(yyvsp[-2].sval)); 
    }
#line 1929 "parser.tab.c"
    break;

  case 64: /* statement: VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 426 "parser.y"
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");

        ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
        (yyval.node) = create_method_call_node(obj, "predict", NULL);


        (yyval.node) = create_predict_node((yyvsp[-4].sval),(yyvsp[-2].sval)); 
    }
#line 1943 "parser.tab.c"
    break;

  case 65: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 440 "parser.y"
    {
        (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval));
    }
#line 1951 "parser.tab.c"
    break;

  case 66: /* statement: PLOT LPAREN expression_list RPAREN SEMICOLON  */
#line 443 "parser.y"
                                                   { (yyval.node) = create_ast_node("PLOT", (yyvsp[-2].node), NULL); }
#line 1957 "parser.tab.c"
    break;

  case 67: /* statement: MODEL IDENTIFIER LBRACKET layer_list RBRACKET  */
#line 447 "parser.y"
{
    printf("[DEBUG] Reconocido MODEL\n");
    printf("[DEBUG] Nombre del modelo: %s\n", (yyvsp[-3].sval));

    // Mostrar lista de capas
    ASTNode* layer = (yyvsp[-1].node);
    int capa_index = 0;
    while (layer) {
        if (strcmp(layer->type, "LAYER") == 0) {
            printf("[DEBUG] Capa #%d: tipo=%s, unidades=%d, activación=%s\n",
                   capa_index++,
                   layer->id,
                   layer->value,
                   layer->str_value);
        }
        layer = layer->right;
    }

    ASTNode* modelNode = create_model_node((yyvsp[-3].sval), (yyvsp[-1].node));
    printf("[DEBUG] Nodo de modelo creado: type=%s, id=%s\n", modelNode->type, modelNode->id);

    //$$ = create_var_decl_node($2, modelNode);
    //printf("[DEBUG] VAR_DECL creado para modelo '%s'\n", $2);
}
#line 1986 "parser.tab.c"
    break;

  case 68: /* statement: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 474 "parser.y"
    {
        (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval));
    }
#line 1994 "parser.tab.c"
    break;

  case 69: /* statement: TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON  */
#line 481 "parser.y"
    {
        (yyval.node) = create_train_node((yyvsp[-6].sval), (yyvsp[-4].sval), (yyvsp[-2].node));
    }
#line 2002 "parser.tab.c"
    break;

  case 70: /* statement: IDENTIFIER ASSIGN NUMBER  */
#line 488 "parser.y"
    {
        (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival));
    }
#line 2010 "parser.tab.c"
    break;

  case 71: /* statement_list: statement_list statement  */
#line 500 "parser.y"
                             { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); }
#line 2016 "parser.tab.c"
    break;

  case 72: /* statement_list: statement  */
#line 501 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 2022 "parser.tab.c"
    break;

  case 73: /* expression_list: expression  */
#line 510 "parser.y"
               { (yyval.node) = (yyvsp[0].node); }
#line 2028 "parser.tab.c"
    break;

  case 74: /* expression_list: expression_list COMMA expression  */
#line 511 "parser.y"
                                       { (yyval.node) = add_statement((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2034 "parser.tab.c"
    break;

  case 75: /* expression: expression DOT IDENTIFIER LPAREN lambda RPAREN  */
#line 518 "parser.y"
                                                           {
                printf("[DEBUG] Reconocido llamada a lambda: %s\n", (yyvsp[-3].sval));
                if (strcmp((yyvsp[-3].sval), "filter")==0) {
                    (yyval.node) = create_list_function_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node));
                  } else {
                    (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), NULL);
                  }
                  free((yyvsp[-3].sval));
            }
#line 2048 "parser.tab.c"
    break;

  case 76: /* expression: LSBRACKET expr_list RSBRACKET  */
#line 528 "parser.y"
                                           { (yyval.node) = create_list_node((yyvsp[-1].node)); }
#line 2054 "parser.tab.c"
    break;

  case 77: /* expression: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN  */
#line 531 "parser.y"
            {

                /* obj.metodo() sin argumentos */
                ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
                (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL);


                
            // *obj  = create_ast_leaf("ID", 0, NULL, $1);
            //ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
            //$$ = create_ast_node("ACCESS_ATTR", obj, attr);

            }
#line 2072 "parser.tab.c"
    break;

  case 78: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN  */
#line 545 "parser.y"
                                                    {
                /* obj.metodo() sin argumentos */
                ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
                (yyval.node) = create_method_call_node(obj, (yyvsp[-2].sval), NULL);
            }
#line 2082 "parser.tab.c"
    break;

  case 79: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 550 "parser.y"
                                                                      {
                /* obj.metodo(arg1, arg2, ...) */
                ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
                (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), (yyvsp[-1].node));
            }
#line 2092 "parser.tab.c"
    break;

  case 80: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 555 "parser.y"
                                      {

            // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);

            ASTNode *obj  = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
                    ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval));
                    (yyval.node) = create_ast_node("ACCESS_ATTR", obj, attr);

                /*$$ = create_ast_node("ACCESS_ATTR",
                                    create_ast_leaf("ID", 0, NULL, $1),
                                    create_ast_leaf("ID", 0, NULL, $3));*/
            }
#line 2109 "parser.tab.c"
    break;

  case 81: /* expression: expression DOT IDENTIFIER LPAREN RPAREN  */
#line 568 "parser.y"
                                                      {
                printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", (yyvsp[-4].node), (yyvsp[-2].sval));
                // $1 = AST del objeto, $3 = nombre del método
                (yyval.node) = create_method_call_node((yyvsp[-4].node), (yyvsp[-2].sval), NULL);
            }
#line 2119 "parser.tab.c"
    break;

  case 82: /* expression: expression DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 577 "parser.y"
                {
                    /* objeto en $1, nombre en $3, args en $5 */
                    (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node));
                }
#line 2128 "parser.tab.c"
    break;

  case 83: /* expression: THIS DOT IDENTIFIER  */
#line 582 "parser.y"
                                  {
                        (yyval.node) = create_ast_node("ACCESS_ATTR",
                            create_ast_leaf("ID", 0, NULL, "this"),
                            create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)));
                    }
#line 2138 "parser.tab.c"
    break;

  case 84: /* expression: THIS DOT IDENTIFIER LPAREN RPAREN  */
#line 589 "parser.y"
                                                    {
                    (yyval.node) = create_method_call_node(
                            create_ast_leaf("ID", 0, NULL, "this"),
                            (yyvsp[-2].sval), NULL);
                    }
#line 2148 "parser.tab.c"
    break;

  case 85: /* expression: IDENTIFIER  */
#line 596 "parser.y"
                         { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL,  (yyvsp[0].sval)); }
#line 2154 "parser.tab.c"
    break;

  case 86: /* expression: NUMBER  */
#line 597 "parser.y"
                         { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 2160 "parser.tab.c"
    break;

  case 87: /* expression: STRING_LITERAL  */
#line 598 "parser.y"
                                 { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 2166 "parser.tab.c"
    break;

  case 88: /* expression: CONCAT LPAREN expression_list RPAREN  */
#line 599 "parser.y"
                                                       {
                    printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", (yyvsp[-1].node));
                    // create_function_call_node(name, args)

                (yyval.node) = create_function_call_node("concat", (yyvsp[-1].node));

                }
#line 2178 "parser.tab.c"
    break;

  case 89: /* expression: expression PLUS expression  */
#line 607 "parser.y"
                                             {
                    (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node));
                    //printf(" [DEBUG] Reconocido ADD: %s + %s\n", $1, $3);
                }
#line 2187 "parser.tab.c"
    break;

  case 90: /* expression: expression MINUS expression  */
#line 611 "parser.y"
                                              { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2193 "parser.tab.c"
    break;

  case 91: /* expression: expression MULTIPLY expression  */
#line 612 "parser.y"
                                                 { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2199 "parser.tab.c"
    break;

  case 92: /* expression: expression DIVIDE expression  */
#line 613 "parser.y"
                                               { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2205 "parser.tab.c"
    break;

  case 94: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 616 "parser.y"
                {

                    ClassNode *cls = find_class((yyvsp[-2].sval));
                    if (!cls) {
                        printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval));
                        (yyval.node) = NULL;
                    } else {
                    (yyval.node) = (ASTNode *)create_object_with_args(cls, (yyvsp[-2].sval));
                    }
                }
#line 2220 "parser.tab.c"
    break;

  case 95: /* expression: NEW IDENTIFIER LPAREN arg_list RPAREN  */
#line 628 "parser.y"
                {
                  /* 1) Buscamos la definición de la clase */
                  ClassNode *cls = find_class((yyvsp[-3].sval));
                  if (!cls) {
                    fprintf(stderr, "Error: clase '%s' no encontrada\n", (yyvsp[-3].sval));
                    exit(1);
                  }
                  /* 2) Creamos el AST de la instanciación con argumentos */
                  (yyval.node) = create_object_with_args(cls, (yyvsp[-1].node));
                  free((yyvsp[-3].sval));
                }
#line 2236 "parser.tab.c"
    break;

  case 96: /* expr_list: expression  */
#line 641 "parser.y"
                                  { (yyval.node) = (yyvsp[0].node); }
#line 2242 "parser.tab.c"
    break;

  case 97: /* expr_list: expr_list COMMA expression  */
#line 642 "parser.y"
                                   { (yyval.node) = append_to_list((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2248 "parser.tab.c"
    break;

  case 98: /* lambda: IDENTIFIER ARROW  */
#line 650 "parser.y"
                       { 
        printf(" [DEBUG] Reconocido LAMBDA\n");
        //$$ = create_lambda_node($1, $4); 
        //free($1);
    }
#line 2258 "parser.tab.c"
    break;

  case 99: /* lambda: LPAREN IDENTIFIER RPAREN ARROW expression  */
#line 656 "parser.y"
    {
      (yyval.node) = create_lambda_node((yyvsp[-3].sval), (yyvsp[0].node));
      free((yyvsp[-3].sval));
    }
#line 2267 "parser.tab.c"
    break;

  case 100: /* arg_list: %empty  */
#line 664 "parser.y"
        { (yyval.node) = NULL; }
#line 2273 "parser.tab.c"
    break;

  case 101: /* arg_list: expression more_args  */
#line 666 "parser.y"
        { (yyval.node) = add_argument((yyvsp[-1].node), (yyvsp[0].node)); }
#line 2279 "parser.tab.c"
    break;

  case 102: /* more_args: %empty  */
#line 671 "parser.y"
        { (yyval.node) = NULL; }
#line 2285 "parser.tab.c"
    break;

  case 103: /* more_args: COMMA expression more_args  */
#line 673 "parser.y"
        { (yyval.node) = add_argument((yyvsp[-1].node), (yyvsp[0].node)); }
#line 2291 "parser.tab.c"
    break;


#line 2295 "parser.tab.c"

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

#line 678 "parser.y"


void clean_generated_code() {
    printf("sAPEEEEEEEEE");
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
