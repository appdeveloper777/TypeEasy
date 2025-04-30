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
  YYSYMBOL_DATASET = 7,                    /* DATASET  */
  YYSYMBOL_MODEL = 8,                      /* MODEL  */
  YYSYMBOL_TRAIN = 9,                      /* TRAIN  */
  YYSYMBOL_PREDICT = 10,                   /* PREDICT  */
  YYSYMBOL_FROM = 11,                      /* FROM  */
  YYSYMBOL_PLOT = 12,                      /* PLOT  */
  YYSYMBOL_VAR = 13,                       /* VAR  */
  YYSYMBOL_ASSIGN = 14,                    /* ASSIGN  */
  YYSYMBOL_PRINT = 15,                     /* PRINT  */
  YYSYMBOL_FOR = 16,                       /* FOR  */
  YYSYMBOL_LPAREN = 17,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 18,                    /* RPAREN  */
  YYSYMBOL_SEMICOLON = 19,                 /* SEMICOLON  */
  YYSYMBOL_CONCAT = 20,                    /* CONCAT  */
  YYSYMBOL_PLUS = 21,                      /* PLUS  */
  YYSYMBOL_MINUS = 22,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 23,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 24,                    /* DIVIDE  */
  YYSYMBOL_LBRACKET = 25,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 26,                  /* RBRACKET  */
  YYSYMBOL_CLASS = 27,                     /* CLASS  */
  YYSYMBOL_CONSTRUCTOR = 28,               /* CONSTRUCTOR  */
  YYSYMBOL_THIS = 29,                      /* THIS  */
  YYSYMBOL_NEW = 30,                       /* NEW  */
  YYSYMBOL_LET = 31,                       /* LET  */
  YYSYMBOL_COLON = 32,                     /* COLON  */
  YYSYMBOL_COMMA = 33,                     /* COMMA  */
  YYSYMBOL_DOT = 34,                       /* DOT  */
  YYSYMBOL_RETURN = 35,                    /* RETURN  */
  YYSYMBOL_IDENTIFIER = 36,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 37,            /* STRING_LITERAL  */
  YYSYMBOL_NUMBER = 38,                    /* NUMBER  */
  YYSYMBOL_YYACCEPT = 39,                  /* $accept  */
  YYSYMBOL_program = 40,                   /* program  */
  YYSYMBOL_class_decl = 41,                /* class_decl  */
  YYSYMBOL_42_1 = 42,                      /* $@1  */
  YYSYMBOL_class_member = 43,              /* class_member  */
  YYSYMBOL_class_body = 44,                /* class_body  */
  YYSYMBOL_train_options = 45,             /* train_options  */
  YYSYMBOL_layer_list = 46,                /* layer_list  */
  YYSYMBOL_layer_decl = 47,                /* layer_decl  */
  YYSYMBOL_attribute_decl = 48,            /* attribute_decl  */
  YYSYMBOL_constructor_decl = 49,          /* constructor_decl  */
  YYSYMBOL_parameter_decl = 50,            /* parameter_decl  */
  YYSYMBOL_parameter_list = 51,            /* parameter_list  */
  YYSYMBOL_method_decl = 52,               /* method_decl  */
  YYSYMBOL_var_decl = 53,                  /* var_decl  */
  YYSYMBOL_statement = 54,                 /* statement  */
  YYSYMBOL_statement_list = 55,            /* statement_list  */
  YYSYMBOL_expression_list = 56,           /* expression_list  */
  YYSYMBOL_expression = 57                 /* expression  */
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
#define YYFINAL  54
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   566

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  39
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  19
/* YYNRULES -- Number of rules.  */
#define YYNRULES  91
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  270

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   293


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
      35,    36,    37,    38
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    55,    55,    56,    57,    58,    63,    63,    76,    77,
      78,    80,    82,    84,    88,    95,    99,   106,   113,   122,
     133,   149,   151,   153,   155,   157,   159,   164,   165,   166,
     167,   169,   174,   187,   200,   205,   218,   232,   244,   251,
     265,   267,   273,   274,   304,   311,   319,   324,   325,   326,
     327,   328,   329,   330,   338,   348,   349,   351,   352,   364,
     379,   396,   403,   411,   425,   429,   432,   459,   466,   473,
     486,   487,   496,   497,   505,   520,   525,   530,   543,   551,
     557,   564,   571,   572,   573,   574,   582,   586,   587,   588,
     589,   590
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
  "FLOAT", "LAYER", "DATASET", "MODEL", "TRAIN", "PREDICT", "FROM", "PLOT",
  "VAR", "ASSIGN", "PRINT", "FOR", "LPAREN", "RPAREN", "SEMICOLON",
  "CONCAT", "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "LBRACKET", "RBRACKET",
  "CLASS", "CONSTRUCTOR", "THIS", "NEW", "LET", "COLON", "COMMA", "DOT",
  "RETURN", "IDENTIFIER", "STRING_LITERAL", "NUMBER", "$accept", "program",
  "class_decl", "$@1", "class_member", "class_body", "train_options",
  "layer_list", "layer_decl", "attribute_decl", "constructor_decl",
  "parameter_decl", "parameter_list", "method_decl", "var_decl",
  "statement", "statement_list", "expression_list", "expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-209)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-84)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     111,   -33,     3,    -2,    17,    19,    37,    42,    50,    69,
      55,    87,   108,   331,    94,     2,    96,   103,   412,    -5,
      72,  -209,  -209,  -209,   131,   412,   157,   162,   147,   156,
     152,   144,   145,   412,   172,   416,   154,  -209,   153,  -209,
     166,   170,   192,   194,   412,   195,   181,   180,   187,  -209,
    -209,    71,   441,   189,  -209,  -209,  -209,   412,   151,   445,
     412,   184,   197,   231,   208,   213,    -1,   532,   456,   216,
      40,   233,  -209,  -209,   226,    14,   238,   470,   224,   532,
     412,   227,   245,   241,  -209,   412,   412,   412,   412,   250,
     186,   221,    31,   261,  -209,   255,   295,   490,   248,   268,
     254,     4,  -209,   260,   272,   278,   412,   274,   496,   275,
     296,   282,  -209,   412,   303,   305,   289,   308,   504,   298,
      23,   313,   333,   332,   532,   532,   532,   532,   337,  -209,
     412,   335,  -209,  -209,  -209,  -209,   321,  -209,   342,  -209,
    -209,   330,   351,  -209,   532,   334,  -209,    48,  -209,   355,
     322,   510,   360,  -209,   363,   341,  -209,   345,  -209,   365,
    -209,   358,   372,   518,   366,    65,   368,   346,   357,   379,
     367,   380,   412,  -209,   374,   112,  -209,  -209,  -209,  -209,
    -209,  -209,   387,   386,   388,  -209,  -209,   110,  -209,   115,
    -209,  -209,   393,   394,   381,   406,   397,  -209,   391,  -209,
     524,     8,     1,   150,   409,   116,   401,  -209,  -209,  -209,
    -209,  -209,   398,   402,   424,   417,   412,   408,   411,   420,
     425,  -209,   117,   434,   119,   444,   448,  -209,   449,   450,
     442,  -209,  -209,   453,    88,  -209,  -209,  -209,    11,   439,
      18,   331,   459,  -209,  -209,  -209,  -209,   469,  -209,   464,
    -209,  -209,  -209,  -209,   331,   463,  -209,   188,   331,  -209,
     331,   223,    21,  -209,   263,   297,  -209,  -209,  -209,  -209
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
      56,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    56,     0,     0,     0,     0,     0,     0,
       0,     5,    42,     4,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    71,     0,     6,
       0,     0,     0,     0,     0,     0,     0,     0,    82,    84,
      83,     0,     0,     0,     1,     3,     2,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    72,     0,    82,
       0,     0,    55,    70,     0,     0,     0,     0,     0,    90,
       0,     0,     0,     0,    41,     0,     0,     0,     0,     0,
      69,     0,     0,     0,    43,    84,     0,     0,     0,     0,
       0,     0,    15,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    11,     0,     0,     0,     0,    82,     0,     0,
       0,    80,     0,    77,    86,    87,    88,    89,     0,    51,
       0,     0,    37,    47,    35,    49,     0,    61,     0,    66,
      16,     0,     0,    65,    73,     0,    36,    77,    52,     0,
       0,     0,     0,    58,     0,     0,    34,     0,    85,     0,
      91,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     7,     0,     0,    13,     8,     9,    10,
      39,    46,     0,    77,     0,    81,    75,     0,    78,     0,
      38,    44,     0,     0,     0,     0,     0,    62,     0,    53,
       0,    27,    27,     0,    91,     0,     0,    74,    76,    79,
      45,    67,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    30,     0,     0,     0,     0,     0,    59,     0,    75,
       0,    14,    68,    74,     0,    22,    24,    26,     0,     0,
       0,    56,     0,    18,    19,    60,    40,     0,    63,     0,
      21,    23,    25,    28,    56,     0,    31,     0,    56,    17,
      56,     0,     0,    32,     0,     0,    20,    29,    33,    54
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -209,  -209,   471,  -209,  -209,  -209,  -209,  -209,   395,  -209,
    -209,   257,   299,  -209,  -209,     0,  -208,   -31,   -17
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    20,    21,    74,   176,   150,   196,   101,   102,   177,
     178,   221,   222,   179,    22,    37,    38,   187,    67
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      23,    51,    66,    24,   217,   218,   219,    25,    58,    52,
     100,   217,   218,   219,   250,   251,   252,   105,    70,   223,
      56,   217,   218,   219,   250,   251,   252,    79,   113,    53,
     139,   114,   106,   257,    27,    91,    40,   220,    73,    26,
      93,   158,    96,    97,   220,   130,   261,   253,   131,   120,
     264,   108,   265,    28,   255,    29,   106,   267,   110,    31,
     118,    85,    86,    87,    88,   161,   171,    32,   124,   125,
     126,   127,    54,    30,    89,     1,     2,     3,     4,     5,
       6,     7,     8,   192,     9,    10,    33,    11,    12,   144,
      84,    34,    85,    86,    87,    88,   151,    13,   106,    14,
     165,    15,    16,    17,    35,    89,   249,    18,    19,    85,
      86,    87,    88,   163,     1,     2,     3,     4,     5,     6,
       7,     8,    89,     9,    10,    36,    11,    12,   208,   202,
      39,   189,    41,   209,   228,   239,    13,   242,    14,    42,
      15,    16,    17,   106,   203,    57,    18,    19,   106,   106,
     240,   205,   240,   225,   226,   200,     1,     2,     3,     4,
       5,     6,     7,     8,    61,     9,    10,    62,    11,    12,
      94,    59,    85,    86,    87,    88,    60,    63,    13,    72,
      64,    65,    15,    16,    17,    89,    68,    76,    18,    19,
      71,     1,     2,     3,     4,     5,     6,     7,     8,   234,
       9,    10,    75,    11,    12,   -83,    77,   -83,   -83,   -83,
     -83,    78,    80,    13,   263,    81,    82,    15,    16,    17,
     -83,    83,    98,    18,    19,    92,     1,     2,     3,     4,
       5,     6,     7,     8,    99,     9,    10,   100,    11,    12,
     129,   103,    85,    86,    87,    88,   104,   111,    13,   266,
     109,   112,    15,    16,    17,    89,   115,    73,    18,    19,
     119,    73,   122,   121,    73,    73,     1,     2,     3,     4,
       5,     6,     7,     8,   133,     9,    10,   123,    11,    12,
     132,   136,    85,    86,    87,    88,   128,   137,    13,   268,
     138,   145,    15,    16,    17,    89,   141,   143,    18,    19,
       1,     2,     3,     4,     5,     6,     7,     8,   142,     9,
      10,   147,    11,    12,   134,   148,    85,    86,    87,    88,
     149,   152,    13,   269,   153,   154,    15,    16,    17,    89,
     159,   157,    18,    19,     1,     2,     3,     4,     5,     6,
       7,     8,   155,     9,    10,    43,    11,    12,   173,   161,
     174,   160,    44,   164,   162,    45,    13,   166,   175,   167,
      15,    16,    17,   168,    46,    47,    18,    19,    43,   169,
     170,    48,    49,    50,   172,    44,   186,   183,    45,   181,
     182,   184,    43,   185,   194,   191,   193,    46,    47,    44,
     188,   201,    45,   195,    48,    49,    50,    43,   197,   199,
     198,    46,    47,   206,    44,   204,   207,    45,    48,    49,
      50,    43,   210,   211,   212,   214,    46,    47,    44,   229,
     213,    45,    43,    48,    49,    50,    43,   215,   227,    44,
      46,    47,    45,    44,   230,   233,    45,    48,    49,    50,
     231,    46,    47,   232,   235,    46,    47,   236,    48,    49,
      50,    43,    69,    49,    50,    43,   237,   238,    44,   241,
     247,    45,    44,   243,   254,    45,   107,   244,   245,   246,
      46,    47,   248,    44,    46,    47,    45,    48,    49,    90,
      43,    48,    95,    50,   258,    46,    47,    44,   259,   260,
      45,    55,    48,    49,    50,   262,   140,   256,     0,    46,
     116,   224,     0,     0,     0,     0,   117,    49,    50,   135,
       0,    85,    86,    87,    88,   146,     0,    85,    86,    87,
      88,     0,     0,   156,    89,    85,    86,    87,    88,   180,
      89,    85,    86,    87,    88,     0,     0,   190,    89,    85,
      86,    87,    88,   216,    89,    85,    86,    87,    88,     0,
       0,     0,    89,    85,    86,    87,    88,     0,    89,     0,
       0,     0,     0,     0,     0,     0,    89
};

static const yytype_int16 yycheck[] =
{
       0,    18,    33,    36,     3,     4,     5,     4,    25,    14,
       6,     3,     4,     5,     3,     4,     5,    18,    35,    18,
      20,     3,     4,     5,     3,     4,     5,    44,    14,    34,
      26,    17,    33,   241,    36,    52,    34,    36,    38,    36,
      57,    18,    59,    60,    36,    14,   254,    36,    17,    80,
     258,    68,   260,    36,    36,    36,    33,    36,    18,    17,
      77,    21,    22,    23,    24,    17,    18,    17,    85,    86,
      87,    88,     0,    36,    34,     3,     4,     5,     6,     7,
       8,     9,    10,    18,    12,    13,    17,    15,    16,   106,
      19,    36,    21,    22,    23,    24,   113,    25,    33,    27,
     131,    29,    30,    31,    17,    34,    18,    35,    36,    21,
      22,    23,    24,   130,     3,     4,     5,     6,     7,     8,
       9,    10,    34,    12,    13,    17,    15,    16,    18,    17,
      36,   162,    36,    18,    18,    18,    25,    18,    27,    36,
      29,    30,    31,    33,    32,    14,    35,    36,    33,    33,
      33,   182,    33,     3,     4,   172,     3,     4,     5,     6,
       7,     8,     9,    10,    17,    12,    13,    11,    15,    16,
      19,    14,    21,    22,    23,    24,    14,    25,    25,    26,
      36,    36,    29,    30,    31,    34,    14,    17,    35,    36,
      36,     3,     4,     5,     6,     7,     8,     9,    10,   216,
      12,    13,    36,    15,    16,    19,    14,    21,    22,    23,
      24,    17,    17,    25,    26,    34,    36,    29,    30,    31,
      34,    34,    38,    35,    36,    36,     3,     4,     5,     6,
       7,     8,     9,    10,    37,    12,    13,     6,    15,    16,
      19,    33,    21,    22,    23,    24,    33,    14,    25,    26,
      34,    25,    29,    30,    31,    34,    18,   257,    35,    36,
      36,   261,    17,    36,   264,   265,     3,     4,     5,     6,
       7,     8,     9,    10,    19,    12,    13,    36,    15,    16,
      19,    33,    21,    22,    23,    24,    36,    19,    25,    26,
      36,    17,    29,    30,    31,    34,    36,    19,    35,    36,
       3,     4,     5,     6,     7,     8,     9,    10,    36,    12,
      13,    36,    15,    16,    19,    19,    21,    22,    23,    24,
      38,    18,    25,    26,    19,    36,    29,    30,    31,    34,
      17,    33,    35,    36,     3,     4,     5,     6,     7,     8,
       9,    10,    34,    12,    13,    10,    15,    16,    26,    17,
      28,    18,    17,    18,    17,    20,    25,    36,    36,    17,
      29,    30,    31,    33,    29,    30,    35,    36,    10,    18,
      36,    36,    37,    38,    19,    17,    18,    36,    20,    19,
      17,    36,    10,    18,    38,    19,    18,    29,    30,    17,
      18,    17,    20,    36,    36,    37,    38,    10,    19,    19,
      33,    29,    30,    17,    17,    18,    18,    20,    36,    37,
      38,    10,    19,    19,    33,    18,    29,    30,    17,    18,
      14,    20,    10,    36,    37,    38,    10,    36,    19,    17,
      29,    30,    20,    17,    36,    18,    20,    36,    37,    38,
      38,    29,    30,    19,    36,    29,    30,    36,    36,    37,
      38,    10,    36,    37,    38,    10,    36,    32,    17,    25,
      18,    20,    17,    19,    25,    20,    10,    19,    19,    19,
      29,    30,    19,    17,    29,    30,    20,    36,    37,    38,
      10,    36,    37,    38,    25,    29,    30,    17,    19,    25,
      20,    20,    36,    37,    38,    32,   101,   240,    -1,    29,
      30,   202,    -1,    -1,    -1,    -1,    36,    37,    38,    19,
      -1,    21,    22,    23,    24,    19,    -1,    21,    22,    23,
      24,    -1,    -1,    19,    34,    21,    22,    23,    24,    19,
      34,    21,    22,    23,    24,    -1,    -1,    19,    34,    21,
      22,    23,    24,    19,    34,    21,    22,    23,    24,    -1,
      -1,    -1,    34,    21,    22,    23,    24,    -1,    34,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    34
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    12,
      13,    15,    16,    25,    27,    29,    30,    31,    35,    36,
      40,    41,    53,    54,    36,     4,    36,    36,    36,    36,
      36,    17,    17,    17,    36,    17,    17,    54,    55,    36,
      34,    36,    36,    10,    17,    20,    29,    30,    36,    37,
      38,    57,    14,    34,     0,    41,    54,    14,    57,    14,
      14,    17,    11,    25,    36,    36,    56,    57,    14,    36,
      57,    36,    26,    54,    42,    36,    17,    14,    17,    57,
      17,    34,    36,    34,    19,    21,    22,    23,    24,    34,
      38,    57,    36,    57,    19,    37,    57,    57,    38,    37,
       6,    46,    47,    33,    33,    18,    33,    10,    57,    34,
      18,    14,    25,    14,    17,    18,    30,    36,    57,    36,
      56,    36,    17,    36,    57,    57,    57,    57,    36,    19,
      14,    17,    19,    19,    19,    19,    33,    19,    36,    26,
      47,    36,    36,    19,    57,    17,    19,    36,    19,    38,
      44,    57,    18,    19,    36,    34,    19,    33,    18,    17,
      18,    17,    17,    57,    18,    56,    36,    17,    33,    18,
      36,    18,    19,    26,    28,    36,    43,    48,    49,    52,
      19,    19,    17,    36,    36,    18,    18,    56,    18,    56,
      19,    19,    18,    18,    38,    36,    45,    19,    33,    19,
      57,    17,    17,    32,    18,    56,    17,    18,    18,    18,
      19,    19,    33,    14,    18,    36,    19,     3,     4,     5,
      36,    50,    51,    18,    51,     3,     4,    19,    18,    18,
      36,    38,    19,    18,    57,    36,    36,    36,    32,    18,
      33,    25,    18,    19,    19,    19,    19,    18,    19,    18,
       3,     4,     5,    36,    25,    36,    50,    55,    25,    19,
      25,    55,    32,    26,    55,    55,    26,    36,    26,    26
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    39,    40,    40,    40,    40,    42,    41,    43,    43,
      43,    44,    44,    44,    45,    46,    46,    47,    48,    48,
      49,    50,    50,    50,    50,    50,    50,    51,    51,    51,
      51,    51,    52,    52,    53,    53,    53,    53,    53,    53,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    54,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    54,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    54,
      55,    55,    56,    56,    57,    57,    57,    57,    57,    57,
      57,    57,    57,    57,    57,    57,    57,    57,    57,    57,
      57,    57
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     1,     1,
       1,     0,     0,     2,     3,     1,     2,     8,     4,     4,
       7,     3,     2,     3,     2,     3,     2,     0,     3,     5,
       1,     3,     6,     7,     5,     5,     5,     5,     6,     6,
       9,     3,     1,     4,     6,     7,     6,     5,     5,     5,
       5,     4,     5,     7,    13,     3,     0,     1,     5,     8,
       9,     5,     7,    10,     5,     5,     5,     8,     9,     3,
       2,     1,     1,     3,     6,     5,     6,     3,     5,     6,
       3,     5,     1,     1,     1,     4,     3,     3,     3,     3,
       2,     4
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
#line 55 "parser.y"
                              { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1348 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 56 "parser.y"
                              { (yyval.node) = (yyvsp[-1].node); /* Ignora definición de clase */ root = (yyval.node); }
#line 1354 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 57 "parser.y"
                              { (yyval.node) = (yyvsp[0].node); root = (yyval.node); }
#line 1360 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 58 "parser.y"
                              { (yyval.node) = NULL; /* Ignora definición de clase */ }
#line 1366 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 63 "parser.y"
                              {
            last_class = create_class((yyvsp[0].sval));
            add_class(last_class);
          }
#line 1375 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 68 "parser.y"
                                  {
                                    /* Fin definición de clase */
                                    (yyval.node) = NULL;  /* No añade nada al AST */
                                 }
#line 1384 "parser.tab.c"
    break;

  case 12: /* class_body: %empty  */
#line 82 "parser.y"
                                           { (yyval.node) = NULL; }
#line 1390 "parser.tab.c"
    break;

  case 13: /* class_body: class_body class_member  */
#line 84 "parser.y"
                                          { (yyval.node) = (yyvsp[-1].node); }
#line 1396 "parser.tab.c"
    break;

  case 14: /* train_options: IDENTIFIER ASSIGN NUMBER  */
#line 89 "parser.y"
    {
        (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival));
    }
#line 1404 "parser.tab.c"
    break;

  case 15: /* layer_list: layer_decl  */
#line 96 "parser.y"
    {
        (yyval.node) = (yyvsp[0].node);
    }
#line 1412 "parser.tab.c"
    break;

  case 16: /* layer_list: layer_list layer_decl  */
#line 100 "parser.y"
    {
        (yyval.node) = append_layer_to_list((yyvsp[-1].node), (yyvsp[0].node));
    }
#line 1420 "parser.tab.c"
    break;

  case 17: /* layer_decl: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 107 "parser.y"
    {
        (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval));
    }
#line 1428 "parser.tab.c"
    break;

  case 18: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 114 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "int");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1440 "parser.tab.c"
    break;

  case 19: /* attribute_decl: IDENTIFIER COLON STRING SEMICOLON  */
#line 123 "parser.y"
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "string");
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
#line 1452 "parser.tab.c"
    break;

  case 20: /* constructor_decl: CONSTRUCTOR LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 134 "parser.y"
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
#line 1469 "parser.tab.c"
    break;

  case 21: /* parameter_decl: IDENTIFIER COLON INT  */
#line 150 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1475 "parser.tab.c"
    break;

  case 22: /* parameter_decl: INT IDENTIFIER  */
#line 152 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1481 "parser.tab.c"
    break;

  case 23: /* parameter_decl: IDENTIFIER COLON STRING  */
#line 154 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1487 "parser.tab.c"
    break;

  case 24: /* parameter_decl: STRING IDENTIFIER  */
#line 156 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1493 "parser.tab.c"
    break;

  case 25: /* parameter_decl: IDENTIFIER COLON FLOAT  */
#line 158 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1499 "parser.tab.c"
    break;

  case 26: /* parameter_decl: FLOAT IDENTIFIER  */
#line 160 "parser.y"
             { (yyval.pnode) = create_parameter_node((yyvsp[0].sval), (yyvsp[-1].sval)); }
#line 1505 "parser.tab.c"
    break;

  case 27: /* parameter_list: %empty  */
#line 164 "parser.y"
                 { (yyval.pnode) = NULL; }
#line 1511 "parser.tab.c"
    break;

  case 28: /* parameter_list: IDENTIFIER COLON IDENTIFIER  */
#line 165 "parser.y"
                                  { (yyval.pnode) = create_parameter_node((yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1517 "parser.tab.c"
    break;

  case 29: /* parameter_list: parameter_list COMMA IDENTIFIER COLON IDENTIFIER  */
#line 166 "parser.y"
                                                       { (yyval.pnode) = add_parameter((yyvsp[-4].pnode), (yyvsp[-2].sval), (yyvsp[0].sval)); }
#line 1523 "parser.tab.c"
    break;

  case 30: /* parameter_list: parameter_decl  */
#line 168 "parser.y"
    { (yyval.pnode) = (yyvsp[0].pnode);}
#line 1529 "parser.tab.c"
    break;

  case 31: /* parameter_list: parameter_list COMMA parameter_decl  */
#line 170 "parser.y"
    { (yyval.pnode) = add_parameter((yyvsp[-2].pnode), (yyvsp[0].pnode)->name, (yyvsp[0].pnode)->type); }
#line 1535 "parser.tab.c"
    break;

  case 32: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 175 "parser.y"
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
#line 1550 "parser.tab.c"
    break;

  case 33: /* method_decl: IDENTIFIER LPAREN parameter_list RPAREN LBRACKET statement_list RBRACKET  */
#line 188 "parser.y"
    {
        if (!last_class) {
            printf("Error interno: no hay clase activa para añadir método '%s'.\n", (yyvsp[-6].sval));
        } else {
            /* params = $3, body = $6 */
            add_method_to_class(last_class, (yyvsp[-6].sval), (yyvsp[-4].pnode), (yyvsp[-1].node));
        }
        (yyval.node) = NULL;
    }
#line 1564 "parser.tab.c"
    break;

  case 34: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
#line 201 "parser.y"
    {
        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }
#line 1573 "parser.tab.c"
    break;

  case 35: /* var_decl: STRING IDENTIFIER ASSIGN expression SEMICOLON  */
#line 205 "parser.y"
                                                  {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1589 "parser.tab.c"
    break;

  case 36: /* var_decl: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 218 "parser.y"
                                               {
        printf("IMPRIMIENDO VAR \n");
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1606 "parser.tab.c"
    break;

  case 37: /* var_decl: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 232 "parser.y"
                                               {
        //printf(" [DEBUG] Reconocido LET con acceso a atributo: %s.%s\n", $2, $4);
        //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $2);
      //  ASTNode *attr = create_ast_leaf("ID", 0, NULL, $4);
      //  ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
       // $$ = create_ast_node("ASSIGN_ATTR", access, $6);  //  left = acceso, right = valor


        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));

    }
#line 1622 "parser.tab.c"
    break;

  case 38: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 244 "parser.y"
                                                            {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  //  correctamente marcado
        (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));  //  left = acceso, right = valor
    }
#line 1633 "parser.tab.c"
    break;

  case 39: /* var_decl: THIS DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 251 "parser.y"
                                                        {
              /* obj será siempre “this” */
              ASTNode *obj    = create_ast_leaf("ID", 0, NULL, "this");
                    /* 2) El nombre del atributo viene en $3 */
                    ASTNode *attr   = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
                    ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
                    /* 3) La expresión es $5 */
                    (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));
    }
#line 1647 "parser.tab.c"
    break;

  case 40: /* statement: LET IDENTIFIER ASSIGN IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 266 "parser.y"
        { (yyval.node) = create_var_decl_node((yyvsp[-7].sval), (yyvsp[-5].sval)); }
#line 1653 "parser.tab.c"
    break;

  case 41: /* statement: RETURN expression SEMICOLON  */
#line 268 "parser.y"
{
   // printf(" [DEBUG] Reconocido RETURN\n");
  // crea un nodo de retorno
  (yyval.node) = create_return_node((yyvsp[-1].node));
}
#line 1663 "parser.tab.c"
    break;

  case 43: /* statement: STRING STRING expression SEMICOLON  */
#line 274 "parser.y"
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
#line 1697 "parser.tab.c"
    break;

  case 44: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 305 "parser.y"
  {

    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL);
  }
#line 1707 "parser.tab.c"
    break;

  case 45: /* statement: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 312 "parser.y"
  {

    //ASTNode *obj = create_ast_leaf("ID", 0, NULL, $1);
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-6].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-4].sval), (yyvsp[-2].node));
  }
#line 1718 "parser.tab.c"
    break;

  case 46: /* statement: THIS DOT IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 320 "parser.y"
  {
    ASTNode *thisObj = create_ast_leaf("ID", 0, NULL, "this");
    (yyval.node) = create_method_call_node(thisObj, (yyvsp[-3].sval), NULL);
  }
#line 1727 "parser.tab.c"
    break;

  case 47: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 324 "parser.y"
                                                        { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1733 "parser.tab.c"
    break;

  case 48: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 325 "parser.y"
                                                 { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1739 "parser.tab.c"
    break;

  case 49: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 326 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1745 "parser.tab.c"
    break;

  case 50: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 327 "parser.y"
                                                 { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1751 "parser.tab.c"
    break;

  case 51: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 328 "parser.y"
                                             { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1757 "parser.tab.c"
    break;

  case 52: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 329 "parser.y"
                                               { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1763 "parser.tab.c"
    break;

  case 53: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 330 "parser.y"
                                                              {
       // printf(" [DEBUG] Reconocido PRINT con acceso a atributo: %s.%s\n", $3, $5);
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
        (yyval.node) = create_ast_node("PRINT", access, NULL);
    }
#line 1775 "parser.tab.c"
    break;

  case 54: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 339 "parser.y"
    {
        (yyval.node) = create_ast_node_for("FOR",
            create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-10].sval)),
            create_ast_leaf("NUMBER", (yyvsp[-8].ival), NULL, NULL),
            (yyvsp[-6].node),
            (yyvsp[-4].node),
            (yyvsp[-1].node)
        );
    }
#line 1789 "parser.tab.c"
    break;

  case 58: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 353 "parser.y"
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
#line 1804 "parser.tab.c"
    break;

  case 59: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 365 "parser.y"
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
#line 1821 "parser.tab.c"
    break;

  case 60: /* statement: LET IDENTIFIER ASSIGN NEW IDENTIFIER LPAREN expression_list RPAREN SEMICOLON  */
#line 380 "parser.y"
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
#line 1838 "parser.tab.c"
    break;

  case 61: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 397 "parser.y"
     { printf(" [DEBUG] Reconocido DATASET\n");
        (yyval.node) = create_dataset_node((yyvsp[-3].sval),(yyvsp[-1].sval));
     }
#line 1846 "parser.tab.c"
    break;

  case 62: /* statement: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 404 "parser.y"
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");
        (yyval.node) = create_predict_node((yyvsp[-4].sval),(yyvsp[-2].sval)); 
    }
#line 1855 "parser.tab.c"
    break;

  case 63: /* statement: VAR IDENTIFIER ASSIGN PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 412 "parser.y"
     { 
        printf(" [DEBUG] Reconocido PREDICT\n");

        ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
        (yyval.node) = create_method_call_node(obj, "predict", NULL);


        (yyval.node) = create_predict_node((yyvsp[-4].sval),(yyvsp[-2].sval)); 
    }
#line 1869 "parser.tab.c"
    break;

  case 64: /* statement: DATASET IDENTIFIER FROM STRING_LITERAL SEMICOLON  */
#line 426 "parser.y"
    {
        (yyval.node) = create_dataset_node((yyvsp[-3].sval), (yyvsp[-1].sval));
    }
#line 1877 "parser.tab.c"
    break;

  case 65: /* statement: PLOT LPAREN expression_list RPAREN SEMICOLON  */
#line 429 "parser.y"
                                                   { (yyval.node) = create_ast_node("PLOT", (yyvsp[-2].node), NULL); }
#line 1883 "parser.tab.c"
    break;

  case 66: /* statement: MODEL IDENTIFIER LBRACKET layer_list RBRACKET  */
#line 433 "parser.y"
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
#line 1912 "parser.tab.c"
    break;

  case 67: /* statement: LAYER IDENTIFIER LPAREN NUMBER COMMA IDENTIFIER RPAREN SEMICOLON  */
#line 460 "parser.y"
    {
        (yyval.node) = create_layer_node((yyvsp[-6].sval), (yyvsp[-4].ival), (yyvsp[-2].sval));
    }
#line 1920 "parser.tab.c"
    break;

  case 68: /* statement: TRAIN LPAREN IDENTIFIER COMMA IDENTIFIER COMMA train_options RPAREN SEMICOLON  */
#line 467 "parser.y"
    {
        (yyval.node) = create_train_node((yyvsp[-6].sval), (yyvsp[-4].sval), (yyvsp[-2].node));
    }
#line 1928 "parser.tab.c"
    break;

  case 69: /* statement: IDENTIFIER ASSIGN NUMBER  */
#line 474 "parser.y"
    {
        (yyval.node) = create_train_option_node((yyvsp[-2].sval), (yyvsp[0].ival));
    }
#line 1936 "parser.tab.c"
    break;

  case 70: /* statement_list: statement_list statement  */
#line 486 "parser.y"
                             { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); }
#line 1942 "parser.tab.c"
    break;

  case 71: /* statement_list: statement  */
#line 487 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1948 "parser.tab.c"
    break;

  case 72: /* expression_list: expression  */
#line 496 "parser.y"
               { (yyval.node) = (yyvsp[0].node); }
#line 1954 "parser.tab.c"
    break;

  case 73: /* expression_list: expression_list COMMA expression  */
#line 497 "parser.y"
                                       { (yyval.node) = add_statement((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1960 "parser.tab.c"
    break;

  case 74: /* expression: PREDICT LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN  */
#line 506 "parser.y"
{

     /* obj.metodo() sin argumentos */
     ASTNode *obj = create_ast_leaf("ID", 0, NULL, "i");
     (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), NULL);


     
   // *obj  = create_ast_leaf("ID", 0, NULL, $1);
   //ASTNode *attr = create_ast_leaf("ID", 0, NULL, $3);
   //$$ = create_ast_node("ACCESS_ATTR", obj, attr);

}
#line 1978 "parser.tab.c"
    break;

  case 75: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN RPAREN  */
#line 520 "parser.y"
                                        {
    /* obj.metodo() sin argumentos */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-2].sval), NULL);
}
#line 1988 "parser.tab.c"
    break;

  case 76: /* expression: IDENTIFIER DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 525 "parser.y"
                                                          {
    /* obj.metodo(arg1, arg2, ...) */
    ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
    (yyval.node) = create_method_call_node(obj, (yyvsp[-3].sval), (yyvsp[-1].node));
}
#line 1998 "parser.tab.c"
    break;

  case 77: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 530 "parser.y"
                           {

   // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);

   ASTNode *obj  = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval));
        (yyval.node) = create_ast_node("ACCESS_ATTR", obj, attr);

    /*$$ = create_ast_node("ACCESS_ATTR",
                        create_ast_leaf("ID", 0, NULL, $1),
                        create_ast_leaf("ID", 0, NULL, $3));*/
}
#line 2015 "parser.tab.c"
    break;

  case 78: /* expression: expression DOT IDENTIFIER LPAREN RPAREN  */
#line 543 "parser.y"
                                          {
    printf(" [DEBUG] Reconocido METHOD_CALL: %s.%s()\n", (yyvsp[-4].node), (yyvsp[-2].sval));
    // $1 = AST del objeto, $3 = nombre del método
    (yyval.node) = create_method_call_node((yyvsp[-4].node), (yyvsp[-2].sval), NULL);
}
#line 2025 "parser.tab.c"
    break;

  case 79: /* expression: expression DOT IDENTIFIER LPAREN expression_list RPAREN  */
#line 552 "parser.y"
    {
        /* objeto en $1, nombre en $3, args en $5 */
        (yyval.node) = create_method_call_node((yyvsp[-5].node), (yyvsp[-3].sval), (yyvsp[-1].node));
    }
#line 2034 "parser.tab.c"
    break;

  case 80: /* expression: THIS DOT IDENTIFIER  */
#line 557 "parser.y"
                        {
            (yyval.node) = create_ast_node("ACCESS_ATTR",
                  create_ast_leaf("ID", 0, NULL, "this"),
                  create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)));
        }
#line 2044 "parser.tab.c"
    break;

  case 81: /* expression: THIS DOT IDENTIFIER LPAREN RPAREN  */
#line 564 "parser.y"
                                          {
           (yyval.node) = create_method_call_node(
                   create_ast_leaf("ID", 0, NULL, "this"),
                   (yyvsp[-2].sval), NULL);
        }
#line 2054 "parser.tab.c"
    break;

  case 82: /* expression: IDENTIFIER  */
#line 571 "parser.y"
             { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL,  (yyvsp[0].sval)); }
#line 2060 "parser.tab.c"
    break;

  case 83: /* expression: NUMBER  */
#line 572 "parser.y"
             { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 2066 "parser.tab.c"
    break;

  case 84: /* expression: STRING_LITERAL  */
#line 573 "parser.y"
                     { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 2072 "parser.tab.c"
    break;

  case 85: /* expression: CONCAT LPAREN expression_list RPAREN  */
#line 574 "parser.y"
                                           {
        printf(" [DEBUG] Reconocido FUNCTION_CALL: %s(%s)\n", "concat", (yyvsp[-1].node));
        // create_function_call_node(name, args)

       (yyval.node) = create_function_call_node("concat", (yyvsp[-1].node));

    }
#line 2084 "parser.tab.c"
    break;

  case 86: /* expression: expression PLUS expression  */
#line 582 "parser.y"
                                 {
        (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node));
        //printf(" [DEBUG] Reconocido ADD: %s + %s\n", $1, $3);
    }
#line 2093 "parser.tab.c"
    break;

  case 87: /* expression: expression MINUS expression  */
#line 586 "parser.y"
                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2099 "parser.tab.c"
    break;

  case 88: /* expression: expression MULTIPLY expression  */
#line 587 "parser.y"
                                     { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2105 "parser.tab.c"
    break;

  case 89: /* expression: expression DIVIDE expression  */
#line 588 "parser.y"
                                   { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 2111 "parser.tab.c"
    break;

  case 91: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 591 "parser.y"
    {

        ClassNode *cls = find_class((yyvsp[-2].sval));
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval));
            (yyval.node) = NULL;
        } else {
           (yyval.node) = (ASTNode *)create_object_with_args(cls, (yyvsp[-2].sval));
        }
    }
#line 2126 "parser.tab.c"
    break;


#line 2130 "parser.tab.c"

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

#line 605 "parser.y"


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
