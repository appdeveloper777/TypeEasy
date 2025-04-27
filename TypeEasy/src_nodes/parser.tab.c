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
  YYSYMBOL_VAR = 6,                        /* VAR  */
  YYSYMBOL_ASSIGN = 7,                     /* ASSIGN  */
  YYSYMBOL_PRINT = 8,                      /* PRINT  */
  YYSYMBOL_FOR = 9,                        /* FOR  */
  YYSYMBOL_LPAREN = 10,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 11,                    /* RPAREN  */
  YYSYMBOL_SEMICOLON = 12,                 /* SEMICOLON  */
  YYSYMBOL_CONCAT = 13,                    /* CONCAT  */
  YYSYMBOL_PLUS = 14,                      /* PLUS  */
  YYSYMBOL_MINUS = 15,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 16,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 17,                    /* DIVIDE  */
  YYSYMBOL_LBRACKET = 18,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 19,                  /* RBRACKET  */
  YYSYMBOL_CLASS = 20,                     /* CLASS  */
  YYSYMBOL_CONSTRUCTOR = 21,               /* CONSTRUCTOR  */
  YYSYMBOL_THIS = 22,                      /* THIS  */
  YYSYMBOL_NEW = 23,                       /* NEW  */
  YYSYMBOL_LET = 24,                       /* LET  */
  YYSYMBOL_COLON = 25,                     /* COLON  */
  YYSYMBOL_COMMA = 26,                     /* COMMA  */
  YYSYMBOL_DOT = 27,                       /* DOT  */
  YYSYMBOL_RETURN = 28,                    /* RETURN  */
  YYSYMBOL_IDENTIFIER = 29,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 30,            /* STRING_LITERAL  */
  YYSYMBOL_NUMBER = 31,                    /* NUMBER  */
  YYSYMBOL_YYACCEPT = 32,                  /* $accept  */
  YYSYMBOL_program = 33,                   /* program  */
  YYSYMBOL_class_decl = 34,                /* class_decl  */
  YYSYMBOL_35_1 = 35,                      /* $@1  */
  YYSYMBOL_class_member = 36,              /* class_member  */
  YYSYMBOL_class_body = 37,                /* class_body  */
  YYSYMBOL_attribute_decl = 38,            /* attribute_decl  */
  YYSYMBOL_constructor_decl = 39,          /* constructor_decl  */
  YYSYMBOL_parameter_decl = 40,            /* parameter_decl  */
  YYSYMBOL_parameter_list = 41,            /* parameter_list  */
  YYSYMBOL_method_decl = 42,               /* method_decl  */
  YYSYMBOL_var_decl = 43,                  /* var_decl  */
  YYSYMBOL_statement = 44,                 /* statement  */
  YYSYMBOL_statement_list = 45,            /* statement_list  */
  YYSYMBOL_expression_list = 46,           /* expression_list  */
  YYSYMBOL_expression = 47                 /* expression  */
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
typedef yytype_uint8 yy_state_t;

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
#define YYFINAL  41
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   431

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  32
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  16
/* YYNRULES -- Number of rules.  */
#define YYNRULES  75
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  204

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   286


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
      25,    26,    27,    28,    29,    30,    31
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    46,    46,    47,    48,    49,    54,    54,    67,    68,
      69,    71,    73,    75,    80,    89,   100,   116,   118,   120,
     122,   124,   126,   131,   132,   133,   134,   136,   141,   154,
     167,   172,   184,   191,   205,   207,   213,   214,   244,   251,
     259,   264,   265,   266,   267,   268,   269,   270,   278,   288,
     289,   291,   292,   304,   319,   335,   336,   345,   346,   351,
     356,   361,   374,   382,   388,   395,   402,   403,   404,   405,
     413,   417,   418,   419,   420,   421
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
  "FLOAT", "VAR", "ASSIGN", "PRINT", "FOR", "LPAREN", "RPAREN",
  "SEMICOLON", "CONCAT", "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "LBRACKET",
  "RBRACKET", "CLASS", "CONSTRUCTOR", "THIS", "NEW", "LET", "COLON",
  "COMMA", "DOT", "RETURN", "IDENTIFIER", "STRING_LITERAL", "NUMBER",
  "$accept", "program", "class_decl", "$@1", "class_member", "class_body",
  "attribute_decl", "constructor_decl", "parameter_decl", "parameter_list",
  "method_decl", "var_decl", "statement", "statement_list",
  "expression_list", "expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-58)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
       1,    -7,     9,    -2,    19,    24,    70,   205,    33,    62,
      69,    80,   281,     4,    68,   -58,   -58,   -58,    75,   281,
     117,   128,   138,   292,   103,   -58,    98,   -58,   121,   148,
     152,   281,   150,   140,   142,   145,   -58,   -58,    96,   281,
     156,   -58,   -58,   -58,   281,   334,   303,   281,   281,   154,
     159,   180,   -58,   -58,   172,   108,   182,   307,   404,   281,
     162,   187,   178,   -58,   281,   281,   281,   281,   183,   342,
     170,   348,   -58,   206,   356,   362,   370,   188,   210,   193,
     -58,   281,   220,   231,   203,   217,   376,    -8,   404,   227,
     238,   240,   404,   404,   404,   404,   243,   -58,   281,   225,
     -58,   -58,   -58,   -58,   -58,     5,   -58,   245,   175,   384,
     257,   -58,   253,   241,   -58,   -58,   281,   256,   -58,   229,
     251,   390,   259,     6,   260,   281,   -58,   265,    42,   -58,
     -58,   -58,   -58,   -58,   -58,   255,   266,   404,   -58,   -58,
      53,   -58,    55,   -58,   -58,   267,   -58,   398,    40,    36,
      57,   271,    73,   277,   -58,   -58,   -58,   281,   263,   264,
     268,   270,   -58,    74,   278,   114,   286,   289,   -58,   297,
     306,   328,   -58,   -58,   -58,    46,   301,    54,   205,   309,
     -58,   -58,   -58,   -58,   310,   -58,   -58,   -58,   -58,   205,
     299,   -58,   125,   205,   205,   133,    90,   -58,   160,   197,
     -58,   -58,   -58,   -58
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
      50,     0,     0,     0,     0,     0,     0,    50,     0,     0,
       0,     0,     0,     0,     0,     5,    36,     4,     0,     0,
       0,     0,     0,     0,     0,    56,     0,     6,     0,     0,
       0,     0,     0,     0,     0,    66,    68,    67,     0,     0,
       0,     1,     3,     2,     0,     0,     0,     0,     0,    66,
       0,     0,    49,    55,     0,     0,     0,     0,    74,     0,
       0,     0,     0,    35,     0,     0,     0,     0,     0,     0,
       0,     0,    37,    68,     0,     0,     0,     0,     0,     0,
      11,     0,     0,     0,     0,    66,     0,     0,    57,    64,
       0,    61,    70,    71,    72,    73,     0,    45,     0,     0,
      42,    41,    31,    43,    44,    61,    46,     0,     0,     0,
       0,    52,     0,     0,    30,    69,     0,     0,    75,     0,
       0,     0,     0,     0,     0,     0,     7,     0,     0,    13,
       8,     9,    10,    33,    40,     0,    61,    58,    65,    59,
       0,    62,     0,    32,    38,     0,    47,     0,    23,    23,
       0,    75,     0,     0,    60,    63,    39,     0,     0,     0,
       0,     0,    26,     0,     0,     0,     0,     0,    53,     0,
      59,     0,    18,    20,    22,     0,     0,     0,    50,     0,
      14,    15,    54,    34,     0,    17,    19,    21,    24,    50,
       0,    27,     0,    50,    50,     0,     0,    28,     0,     0,
      16,    25,    29,    48
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -58,   -58,   275,   -58,   -58,   -58,   -58,   -58,   158,   191,
     -58,   -58,     0,    52,   -57,   -11
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    14,    15,    54,   129,   108,   130,   131,   162,   163,
     132,    16,    25,    26,   140,    88
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      17,    38,    87,   115,     1,     2,     3,     4,    45,     5,
       6,    39,    50,    19,    43,   119,   124,   145,   116,     7,
      58,     8,    18,     9,    10,    11,    53,    21,    69,    12,
      13,    40,   116,    71,    23,    74,    75,    76,    20,   158,
     159,   160,   123,   158,   159,   160,    86,   164,    22,   185,
     186,   187,   149,    92,    93,    94,    95,   158,   159,   160,
     166,   167,    27,   142,   154,   161,   155,   150,    41,   161,
     109,     1,     2,     3,     4,   188,     5,     6,   152,   116,
      24,   116,    44,   190,   169,   176,     7,   121,     8,    28,
       9,    10,    11,   185,   186,   187,    12,    13,    29,   116,
     177,     1,     2,     3,     4,   137,     5,     6,    63,    30,
      64,    65,    66,    67,   147,    81,     7,    52,    82,   201,
       9,    10,    11,    68,    46,   179,    12,    13,     1,     2,
       3,     4,    51,     5,     6,    47,     1,     2,     3,     4,
     177,     5,     6,     7,   197,    48,   171,     9,    10,    11,
      55,     7,   200,    12,    13,     9,    10,    11,    56,    57,
      59,    12,    13,     1,     2,     3,     4,    60,     5,     6,
      78,    61,    62,    64,    65,    66,    67,    98,     7,   202,
      99,    77,     9,    10,    11,    70,    68,    79,    12,    13,
      80,    89,    53,    83,   126,    53,   127,    90,    53,    53,
       1,     2,     3,     4,   128,     5,     6,    91,     1,     2,
       3,     4,    96,     5,     6,     7,   203,   105,   101,     9,
      10,    11,   106,     7,   107,    12,    13,     9,    10,    11,
     192,   110,   112,    12,    13,    31,   122,   117,    32,    31,
     139,   195,    32,   111,   113,   198,   199,    33,    34,   118,
     119,    33,    34,   120,    35,    36,    37,   125,    35,    36,
      37,    31,   141,   135,    32,    31,   151,   138,    32,   134,
     136,   144,   146,    33,    34,   148,   153,    33,    34,   156,
      35,    36,    37,   168,    35,    36,    37,    31,   170,    42,
      32,    31,   172,   173,    32,   175,   178,   174,   180,    33,
      34,   181,    31,    33,    34,    32,    35,    36,    37,   182,
      35,    36,    37,    31,    33,    34,    32,    31,   183,   189,
      32,    49,    36,    37,   196,    33,    34,   193,   194,    33,
      84,     0,    35,    73,    37,   191,    85,    36,    37,   184,
     165,     0,    64,    65,    66,    67,    72,     0,    64,    65,
      66,    67,     0,     0,    97,    68,    64,    65,    66,    67,
     100,    68,    64,    65,    66,    67,     0,     0,   102,    68,
      64,    65,    66,    67,   103,    68,    64,    65,    66,    67,
       0,     0,   104,    68,    64,    65,    66,    67,   114,    68,
      64,    65,    66,    67,     0,     0,   133,    68,    64,    65,
      66,    67,   143,    68,    64,    65,    66,    67,     0,     0,
     157,    68,    64,    65,    66,    67,     0,    68,    64,    65,
      66,    67,     0,     0,     0,    68,     0,     0,     0,     0,
       0,    68
};

static const yytype_int16 yycheck[] =
{
       0,    12,    59,    11,     3,     4,     5,     6,    19,     8,
       9,     7,    23,     4,    14,    10,    11,    11,    26,    18,
      31,    20,    29,    22,    23,    24,    26,    29,    39,    28,
      29,    27,    26,    44,    10,    46,    47,    48,    29,     3,
       4,     5,    99,     3,     4,     5,    57,    11,    29,     3,
       4,     5,    10,    64,    65,    66,    67,     3,     4,     5,
       3,     4,    29,   120,    11,    29,    11,    25,     0,    29,
      81,     3,     4,     5,     6,    29,     8,     9,   135,    26,
      10,    26,     7,    29,    11,    11,    18,    98,    20,    27,
      22,    23,    24,     3,     4,     5,    28,    29,    29,    26,
      26,     3,     4,     5,     6,   116,     8,     9,    12,    29,
      14,    15,    16,    17,   125,     7,    18,    19,    10,    29,
      22,    23,    24,    27,     7,    11,    28,    29,     3,     4,
       5,     6,    29,     8,     9,     7,     3,     4,     5,     6,
      26,     8,     9,    18,    19,     7,   157,    22,    23,    24,
      29,    18,    19,    28,    29,    22,    23,    24,    10,     7,
      10,    28,    29,     3,     4,     5,     6,    27,     8,     9,
      11,    29,    27,    14,    15,    16,    17,     7,    18,    19,
      10,    27,    22,    23,    24,    29,    27,     7,    28,    29,
      18,    29,   192,    11,    19,   195,    21,    10,   198,   199,
       3,     4,     5,     6,    29,     8,     9,    29,     3,     4,
       5,     6,    29,     8,     9,    18,    19,    29,    12,    22,
      23,    24,    12,    18,    31,    28,    29,    22,    23,    24,
     178,    11,    29,    28,    29,    10,    11,    10,    13,    10,
      11,   189,    13,    12,    27,   193,   194,    22,    23,    11,
      10,    22,    23,    10,    29,    30,    31,    12,    29,    30,
      31,    10,    11,    10,    13,    10,    11,    11,    13,    12,
      29,    12,    12,    22,    23,    10,    10,    22,    23,    12,
      29,    30,    31,    12,    29,    30,    31,    10,    11,    14,
      13,    10,    29,    29,    13,    25,    18,    29,    12,    22,
      23,    12,    10,    22,    23,    13,    29,    30,    31,    12,
      29,    30,    31,    10,    22,    23,    13,    10,    12,    18,
      13,    29,    30,    31,    25,    22,    23,    18,    18,    22,
      23,    -1,    29,    30,    31,   177,    29,    30,    31,    11,
     149,    -1,    14,    15,    16,    17,    12,    -1,    14,    15,
      16,    17,    -1,    -1,    12,    27,    14,    15,    16,    17,
      12,    27,    14,    15,    16,    17,    -1,    -1,    12,    27,
      14,    15,    16,    17,    12,    27,    14,    15,    16,    17,
      -1,    -1,    12,    27,    14,    15,    16,    17,    12,    27,
      14,    15,    16,    17,    -1,    -1,    12,    27,    14,    15,
      16,    17,    12,    27,    14,    15,    16,    17,    -1,    -1,
      12,    27,    14,    15,    16,    17,    -1,    27,    14,    15,
      16,    17,    -1,    -1,    -1,    27,    -1,    -1,    -1,    -1,
      -1,    27
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     8,     9,    18,    20,    22,
      23,    24,    28,    29,    33,    34,    43,    44,    29,     4,
      29,    29,    29,    10,    10,    44,    45,    29,    27,    29,
      29,    10,    13,    22,    23,    29,    30,    31,    47,     7,
      27,     0,    34,    44,     7,    47,     7,     7,     7,    29,
      47,    29,    19,    44,    35,    29,    10,     7,    47,    10,
      27,    29,    27,    12,    14,    15,    16,    17,    27,    47,
      29,    47,    12,    30,    47,    47,    47,    27,    11,     7,
      18,     7,    10,    11,    23,    29,    47,    46,    47,    29,
      10,    29,    47,    47,    47,    47,    29,    12,     7,    10,
      12,    12,    12,    12,    12,    29,    12,    31,    37,    47,
      11,    12,    29,    27,    12,    11,    26,    10,    11,    10,
      10,    47,    11,    46,    11,    12,    19,    21,    29,    36,
      38,    39,    42,    12,    12,    10,    29,    47,    11,    11,
      46,    11,    46,    12,    12,    11,    12,    47,    10,    10,
      25,    11,    46,    10,    11,    11,    12,    12,     3,     4,
       5,    29,    40,    41,    11,    41,     3,     4,    12,    11,
      11,    47,    29,    29,    29,    25,    11,    26,    18,    11,
      12,    12,    12,    12,    11,     3,     4,     5,    29,    18,
      29,    40,    45,    18,    18,    45,    25,    19,    45,    45,
      19,    29,    19,    19
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    32,    33,    33,    33,    33,    35,    34,    36,    36,
      36,    37,    37,    37,    38,    38,    39,    40,    40,    40,
      40,    40,    40,    41,    41,    41,    41,    41,    42,    42,
      43,    43,    43,    43,    44,    44,    44,    44,    44,    44,
      44,    44,    44,    44,    44,    44,    44,    44,    44,    44,
      44,    44,    44,    44,    44,    45,    45,    46,    46,    47,
      47,    47,    47,    47,    47,    47,    47,    47,    47,    47,
      47,    47,    47,    47,    47,    47
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     1,     1,
       1,     0,     0,     2,     4,     4,     7,     3,     2,     3,
       2,     3,     2,     0,     3,     5,     1,     3,     6,     7,
       5,     5,     6,     6,     9,     3,     1,     4,     6,     7,
       6,     5,     5,     5,     5,     4,     5,     7,    13,     3,
       0,     1,     5,     8,     9,     2,     1,     1,     3,     5,
       6,     3,     5,     6,     3,     5,     1,     1,     1,     4,
       3,     3,     3,     3,     2,     4
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
#line 46 "parser.y"
                              { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1286 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 47 "parser.y"
                              { (yyval.node) = (yyvsp[-1].node); /* Ignora definición de clase */ root = (yyval.node); }
#line 1292 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 48 "parser.y"
                              { (yyval.node) = (yyvsp[0].node); root = (yyval.node); }
#line 1298 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 49 "parser.y"
                              { (yyval.node) = NULL; /* Ignora definición de clase */ }
#line 1304 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 54 "parser.y"
                              {
            last_class = create_class((yyvsp[0].sval));
            add_class(last_class);
          }
#line 1313 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 59 "parser.y"
                                  {
                                    /* Fin definición de clase */
                                    (yyval.node) = NULL;  /* No añade nada al AST */
                                 }
#line 1322 "parser.tab.c"
    break;

  case 12: /* class_body: %empty  */
#line 73 "parser.y"
                                           { (yyval.node) = NULL; }
#line 1328 "parser.tab.c"
    break;

  case 13: /* class_body: class_body class_member  */
#line 75 "parser.y"
                                          { (yyval.node) = (yyvsp[-1].node); }
#line 1334 "parser.tab.c"
    break;

  case 14: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 81 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "int");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1346 "parser.tab.c"
    break;

  case 15: /* attribute_decl: IDENTIFIER COLON STRING SEMICOLON  */
#line 90 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "string");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1358 "parser.tab.c"
    break;

  case 16: /* constructor_decl: CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 101 "parser.y"
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
#line 1375 "parser.tab.c"
    break;

  case 17: /* parameter_decl: IDENTIFIER COLON INT  */
#line 117 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1381 "parser.tab.c"
    break;

  case 18: /* parameter_decl: INT IDENTIFIER  */
#line 119 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1387 "parser.tab.c"
    break;

  case 19: /* parameter_decl: IDENTIFIER COLON STRING  */
#line 121 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1393 "parser.tab.c"
    break;

  case 20: /* parameter_decl: STRING IDENTIFIER  */
#line 123 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1399 "parser.tab.c"
    break;

  case 21: /* parameter_decl: IDENTIFIER COLON FLOAT  */
#line 125 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1405 "parser.tab.c"
    break;

  case 22: /* parameter_decl: FLOAT IDENTIFIER  */
#line 127 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1411 "parser.tab.c"
    break;

  case 23: /* parameter_list: %empty  */
#line 131 "parser.y"
                 { (yyval.pnode) = NULL; }
#line 1417 "parser.tab.c"
    break;

  case 24: /* parameter_list: IDENTIFIER COLON IDENTIFIER  */
#line 132 "parser.y"
                                  { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1423 "parser.tab.c"
    break;

  case 25: /* parameter_list: parameter_list COMMA IDENTIFIER COLON IDENTIFIER  */
#line 133 "parser.y"
                                                       { (yyval.pnode) = add_parameter((yyvsp[-4].pnode), (yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1429 "parser.tab.c"
    break;

  case 26: /* parameter_list: parameter_decl  */
#line 135 "parser.y"
    { (yyval.pnode) = (yyvsp[0].pnode);}
#line 1435 "parser.tab.c"
    break;

  case 27: /* parameter_list: parameter_list COMMA parameter_decl  */
#line 137 "parser.y"
    { (yyval.pnode) = add_parameter((yyvsp[-2].pnode), (yyvsp[0].pnode)->name, (yyvsp[0].pnode)->type); }
#line 1441 "parser.tab.c"
    break;

  case 28: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 142 "parser.y"
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
#line 1456 "parser.tab.c"
    break;

  case 29: /* method_decl: IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 155 "parser.y"
    {
        if (!last_class) {
            printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-6].sval));
        } else {
            /* params = $3, body = $6 */
            add_method_to_class(last_class, (yyvsp[-6].sval), (yyvsp[-4].pnode), (yyvsp[-1].node));
        }
        (yyval.node) = NULL;
    }
#line 1470 "parser.tab.c"
    break;

  case 30: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
#line 168 "parser.y"
    {
        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }
#line 1479 "parser.tab.c"
    break;

  case 31: /* var_decl: STRING IDENTIFIER ASSIGN expression SEMICOLON  */
#line 172 "parser.y"
                                                  {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1495 "parser.tab.c"
    break;

  case 32: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 184 "parser.y"
                                                            {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
        (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));  //  left = acceso, right = valor
    }
#line 1506 "parser.tab.c"
    break;

  case 33: /* var_decl: THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 191 "parser.y"
                                                        {
              /* obj será siempre “this” */
              ASTNode *obj    = create_ast_leaf("ID", 0, NULL, "this");
                    /* 2) El nombre del atributo viene en $3 */
                    ASTNode *attr   = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
                    ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
                    /* 3) La expresión es $5 */
                    (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));
    }
#line 1520 "parser.tab.c"
    break;

  case 34: /* statement: LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 206 "parser.y"
        { (yyval.node) = create_var_decl_node((yyvsp[-7].sval), (yyvsp[-5].sval)); }
#line 1526 "parser.tab.c"
    break;

  case 35: /* statement: RETURN expression SEMICOLON  */
#line 208 "parser.y"
{
   // printf(" [DEBUG] Reconocido RETURN\n");
  // crea un nodo de retorno
  (yyval.node) = create_return_node((yyvsp[-1].node));
}
#line 1536 "parser.tab.c"
    break;

  case 37: /* statement: STRING STRING expression SEMICOLON  */
#line 214 "parser.y"
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
#line 1570 "parser.tab.c"
    break;

  case 38: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 245 "parser.y"
  {

    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL);
  }
#line 1580 "parser.tab.c"
    break;

  case 39: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 252 "parser.y"
  {

    //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-6].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-4].sval), (yyvsp[-2].node));
  }
#line 1591 "parser.tab.c"
    break;

  case 40: /* statement: THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 260 "parser.y"
  {
    ASTNode *thisObj = create_ast_leaf("ID", 0, NULL, "this");
    (yyval.node) = create_method_call_node(thisObj, (yyvsp[-3].sval), NULL);
  }
#line 1600 "parser.tab.c"
    break;

  case 41: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 264 "parser.y"
                                                        { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1606 "parser.tab.c"
    break;

  case 42: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 265 "parser.y"
                                                 { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1612 "parser.tab.c"
    break;

  case 43: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 266 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1618 "parser.tab.c"
    break;

  case 44: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 267 "parser.y"
                                                 { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1624 "parser.tab.c"
    break;

  case 45: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 268 "parser.y"
                                             { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1630 "parser.tab.c"
    break;

  case 46: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 269 "parser.y"
                                               { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1636 "parser.tab.c"
    break;

  case 47: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 270 "parser.y"
                                                              {
       // printf(" [DEBUG] Reconocido PRINT con acceso a atributo: %s.%s\n", $3, $5);
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
        (yyval.node) = create_ast_node("PRINT", access, NULL);
    }
#line 1648 "parser.tab.c"
    break;

  case 48: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 279 "parser.y"
    {
        (yyval.node) = create_ast_node_for("FOR",
            create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-10].sval)),
            create_ast_leaf("NUMBER", (yyvsp[-8].ival), NULL, NULL),
            (yyvsp[-6].node),
            (yyvsp[-4].node),
            (yyvsp[-1].node)
        );
    }
#line 1662 "parser.tab.c"
    break;

  case 52: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 293 "parser.y"
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
#line 1677 "parser.tab.c"
    break;

  case 53: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 305 "parser.y"
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
#line 1694 "parser.tab.c"
    break;

  case 54: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 320 "parser.y"
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
#line 1711 "parser.tab.c"
    break;

  case 55: /* statement_list: statement_list statement  */
#line 335 "parser.y"
                             { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); }
#line 1717 "parser.tab.c"
    break;

  case 56: /* statement_list: statement  */
#line 336 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1723 "parser.tab.c"
    break;

  case 57: /* expression_list: expression  */
#line 345 "parser.y"
               { (yyval.node) = (yyvsp[0].node); }
#line 1729 "parser.tab.c"
    break;

  case 58: /* expression_list: expression_list COMMA expression  */
#line 346 "parser.y"
                                       { (yyval.node) = add_statement((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1735 "parser.tab.c"
    break;

  case 59: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN  */
#line 351 "parser.y"
                                        {
    /* obj.metodo() sin argumentos */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-2].sval), NULL);
}
#line 1745 "parser.tab.c"
    break;

  case 60: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 356 "parser.y"
                                                          {
    /* obj.metodo(arg1, arg2, ...) */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), (yyvsp[-1].node));
}
#line 1755 "parser.tab.c"
    break;

  case 61: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 361 "parser.y"
                           {

   // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);

   ASTNode *obj  = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval));
        (yyval.node) = create_ast_node("ACCESS_ATTR", obj, attr);

    /*$$ = create_ast_node("ACCESS_ATTR",
                        create_ast_leaf("ID", 0, NULL, $1),
                        create_ast_leaf("ID", 0, NULL, $3));*/
}
#line 1772 "parser.tab.c"
    break;

  case 62: /* expression: expression DOT IDENTIFIER LPAREN RPAREN  */
#line 374 "parser.y"
                                          {
    printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", (yyvsp[-4].node), (yyvsp[-2].sval));
    // $1 = AST del objeto, $3 = nombre del método
    (yyval.node) = create_method_call_node((yyvsp[-4].node), (yyvsp[-2].sval), NULL);
}
#line 1782 "parser.tab.c"
    break;

  case 63: /* expression: expression DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 383 "parser.y"
    {
        /* objeto en $1, nombre en $3, args en $5 */
        (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node));
    }
#line 1791 "parser.tab.c"
    break;

  case 64: /* expression: THIS DOT IDENTIFIER  */
#line 388 "parser.y"
                        {
            (yyval.node) = create_ast_node("ACCESS_ATTR",
                  create_ast_leaf("ID", 0, NULL, "this"),
                  create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)));
        }
#line 1801 "parser.tab.c"
    break;

  case 65: /* expression: THIS DOT IDENTIFIER LPAREN RPAREN  */
#line 395 "parser.y"
                                          {
           (yyval.node) = create_method_call_node(
                   create_ast_leaf("ID", 0, NULL, "this"),
                   (yyvsp[-2].sval), NULL);
        }
#line 1811 "parser.tab.c"
    break;

  case 66: /* expression: IDENTIFIER  */
#line 402 "parser.y"
             { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL,  (yyvsp[0].sval)); }
#line 1817 "parser.tab.c"
    break;

  case 67: /* expression: NUMBER  */
#line 403 "parser.y"
             { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 1823 "parser.tab.c"
    break;

  case 68: /* expression: STRING_LITERAL  */
#line 404 "parser.y"
                     { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 1829 "parser.tab.c"
    break;

  case 69: /* expression: CONCAT LPAREN expression_list RPAREN  */
#line 405 "parser.y"
                                           {
        printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", (yyvsp[-1].node));
        // create_function_call_node(name, args)

       (yyval.node) = create_function_call_node("concat", (yyvsp[-1].node));

    }
#line 1841 "parser.tab.c"
    break;

  case 70: /* expression: expression PLUS expression  */
#line 413 "parser.y"
                                 {
        (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node));
        printf(" [DEBUG] Reconocido ADD: %s + %s\n", (yyvsp[-2].node), (yyvsp[0].node));
    }
#line 1850 "parser.tab.c"
    break;

  case 71: /* expression: expression MINUS expression  */
#line 417 "parser.y"
                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1856 "parser.tab.c"
    break;

  case 72: /* expression: expression MULTIPLY expression  */
#line 418 "parser.y"
                                     { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1862 "parser.tab.c"
    break;

  case 73: /* expression: expression DIVIDE expression  */
#line 419 "parser.y"
                                   { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1868 "parser.tab.c"
    break;

  case 75: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 422 "parser.y"
    {

        ClassNode *cls = find_class((yyvsp[-2].sval));
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval));
            (yyval.node) = NULL;
        } else {
           (yyval.node) = (ASTNode *)create_object_with_args(cls, (yyvsp[-2].sval));
        }
    }
#line 1883 "parser.tab.c"
    break;


#line 1887 "parser.tab.c"

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

#line 434 "parser.y"


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
