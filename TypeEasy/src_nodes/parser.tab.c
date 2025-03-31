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
  YYSYMBOL_FLOAT = 3,                      /* FLOAT  */
  YYSYMBOL_VAR = 4,                        /* VAR  */
  YYSYMBOL_ASSIGN = 5,                     /* ASSIGN  */
  YYSYMBOL_PRINT = 6,                      /* PRINT  */
  YYSYMBOL_FOR = 7,                        /* FOR  */
  YYSYMBOL_LPAREN = 8,                     /* LPAREN  */
  YYSYMBOL_RPAREN = 9,                     /* RPAREN  */
  YYSYMBOL_SEMICOLON = 10,                 /* SEMICOLON  */
  YYSYMBOL_PLUS = 11,                      /* PLUS  */
  YYSYMBOL_MINUS = 12,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 13,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 14,                    /* DIVIDE  */
  YYSYMBOL_STRING = 15,                    /* STRING  */
  YYSYMBOL_INT = 16,                       /* INT  */
  YYSYMBOL_LBRACKET = 17,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 18,                  /* RBRACKET  */
  YYSYMBOL_CLASS = 19,                     /* CLASS  */
  YYSYMBOL_CONSTRUCTOR = 20,               /* CONSTRUCTOR  */
  YYSYMBOL_THIS = 21,                      /* THIS  */
  YYSYMBOL_NEW = 22,                       /* NEW  */
  YYSYMBOL_LET = 23,                       /* LET  */
  YYSYMBOL_COLON = 24,                     /* COLON  */
  YYSYMBOL_COMMA = 25,                     /* COMMA  */
  YYSYMBOL_DOT = 26,                       /* DOT  */
  YYSYMBOL_IDENTIFIER = 27,                /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 28,            /* STRING_LITERAL  */
  YYSYMBOL_NUMBER = 29,                    /* NUMBER  */
  YYSYMBOL_YYACCEPT = 30,                  /* $accept  */
  YYSYMBOL_program = 31,                   /* program  */
  YYSYMBOL_class_decl = 32,                /* class_decl  */
  YYSYMBOL_33_1 = 33,                      /* $@1  */
  YYSYMBOL_class_body = 34,                /* class_body  */
  YYSYMBOL_attribute_decl = 35,            /* attribute_decl  */
  YYSYMBOL_constructor_decl = 36,          /* constructor_decl  */
  YYSYMBOL_method_decl = 37,               /* method_decl  */
  YYSYMBOL_var_decl = 38,                  /* var_decl  */
  YYSYMBOL_statement = 39,                 /* statement  */
  YYSYMBOL_statement_list = 40,            /* statement_list  */
  YYSYMBOL_expression = 41                 /* expression  */
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
typedef yytype_int8 yy_state_t;

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
<<<<<<< HEAD
#define YYFINAL  30
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   212
=======
#define YYFINAL  29
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   203
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  30
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  12
/* YYNRULES -- Number of rules.  */
<<<<<<< HEAD
#define YYNRULES  45
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  123
=======
#define YYNRULES  44
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  120
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   284


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
      25,    26,    27,    28,    29
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
<<<<<<< HEAD
       0,    38,    38,    39,    40,    41,    47,    46,    59,    60,
      61,    62,    63,    64,    65,    69,    80,    98,   105,   110,
     121,   122,   151,   152,   153,   154,   155,   156,   157,   165,
     175,   176,   178,   179,   193,   194,   208,   214,   215,   216,
     218,   219,   220,   221,   222,   223
=======
       0,    35,    35,    36,    37,    38,    44,    43,    56,    57,
      58,    59,    60,    61,    62,    66,    77,    95,   102,   107,
     118,   119,   120,   121,   122,   123,   124,   125,   133,   143,
     144,   146,   147,   161,   162,   176,   182,   183,   184,   186,
     187,   188,   189,   190,   191
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
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
  "\"end of file\"", "error", "\"invalid token\"", "FLOAT", "VAR",
  "ASSIGN", "PRINT", "FOR", "LPAREN", "RPAREN", "SEMICOLON", "PLUS",
  "MINUS", "MULTIPLY", "DIVIDE", "STRING", "INT", "LBRACKET", "RBRACKET",
  "CLASS", "CONSTRUCTOR", "THIS", "NEW", "LET", "COLON", "COMMA", "DOT",
  "IDENTIFIER", "STRING_LITERAL", "NUMBER", "$accept", "program",
  "class_decl", "$@1", "class_body", "attribute_decl", "constructor_decl",
  "method_decl", "var_decl", "statement", "statement_list", "expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

<<<<<<< HEAD
#define YYPACT_NINF (-94)
=======
#define YYPACT_NINF (-76)
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
<<<<<<< HEAD
      54,   -20,   -17,    13,    14,   -10,    -2,   151,     1,     3,
       7,    -3,    29,   -94,   -94,   -94,    32,    33,    71,    12,
     142,    42,    44,   -94,    79,   -94,    46,    45,   142,    28,
     -94,   -94,   -94,   142,   142,   142,    36,   -18,   -94,   -94,
      53,    63,    48,    77,    31,   142,   -94,   -94,    55,    66,
     142,   149,    75,   169,   174,   198,    76,    78,    93,   142,
     142,   142,   142,    80,    81,   -94,    94,   179,    -9,    97,
     184,   -94,   142,   -94,   -94,   103,   106,   -94,   198,   198,
     198,   198,   108,   -94,   -94,   -94,   111,    -5,   -14,   -94,
     -94,   -94,   -94,   -94,   189,   -94,   110,   142,   117,   118,
     100,   -94,   -94,   -94,   -94,   -94,   -94,   194,   114,   121,
     133,   142,   130,   151,   -94,   128,   -94,   107,   132,   -94,
     151,   129,   -94
=======
      44,   -23,   -22,     1,     6,   -12,    -8,   138,     3,     9,
      13,    -2,    22,   -76,   -76,   -76,    41,    48,    61,    31,
      57,    59,   -76,    69,   -76,    19,    60,   129,    47,   -76,
     -76,   -76,   129,   129,   129,    66,    68,   -76,   -76,    43,
      65,    71,   129,   -76,   -76,    78,    99,   129,    77,    67,
     108,   114,   136,   175,   107,   102,   120,   129,   129,   129,
     129,   106,   126,   156,   -10,   130,   161,   125,   -76,   129,
     -76,   -76,   150,   153,   -76,   175,   175,   175,   175,   154,
     -76,   -76,   155,    -6,    -7,   -76,   -76,   -76,   -76,   -76,
     -76,   166,   -76,   180,   129,   182,   183,   177,   -76,   -76,
     -76,   -76,   -76,   -76,   171,   178,   179,   184,   129,   181,
     138,   -76,    93,   -76,    94,   185,   -76,   138,   116,   -76
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
<<<<<<< HEAD
      31,     0,     0,     0,     0,     0,     0,    31,     0,     0,
       0,     0,     0,     5,    20,     4,     0,     0,     0,     0,
       0,     0,     0,    35,     0,     6,     0,     0,     0,     0,
       1,     3,     2,     0,     0,     0,     0,    37,    39,    38,
       0,     0,    37,     0,     0,     0,    30,    34,     0,     0,
       0,     0,     0,     0,     0,    44,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    21,     0,     0,     8,     0,
       0,    26,     0,    24,    25,     0,    36,    27,    40,    41,
      42,    43,     0,    36,    22,    23,     0,     0,     0,     9,
      11,    13,    33,    18,     0,    45,     0,     0,     0,     0,
       0,     7,    10,    12,    14,    19,    28,     0,     0,     0,
       0,     0,     0,    31,    15,     0,    16,     0,     0,    17,
      31,     0,    29
=======
      30,     0,     0,     0,     0,     0,     0,    30,     0,     0,
       0,     0,     0,     5,    20,     4,     0,     0,     0,     0,
       0,     0,    34,     0,     6,     0,     0,     0,     0,     1,
       3,     2,     0,     0,     0,     0,    36,    38,    37,     0,
       0,     0,     0,    29,    33,     0,     0,     0,    36,     0,
       0,     0,     0,    43,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     8,     0,     0,     0,    25,     0,
      23,    24,     0,    35,    26,    39,    40,    41,    42,     0,
      21,    22,     0,     0,     0,     9,    11,    13,    32,    18,
      35,     0,    44,     0,     0,     0,     0,     0,     7,    10,
      12,    14,    19,    27,     0,     0,     0,     0,     0,     0,
      30,    15,     0,    16,     0,     0,    17,    30,     0,    28
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
<<<<<<< HEAD
     -94,   -94,   116,   -94,   -94,    65,    84,    87,   -94,     0,
     -93,   -19
=======
     -76,   -76,   186,   -76,   -76,   113,   117,   119,   -76,     0,
     -75,   -26
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
<<<<<<< HEAD
       0,    12,    13,    48,    88,    89,    90,    91,    14,    23,
      24,    40
=======
       0,    12,    13,    45,    84,    85,    86,    87,    14,    22,
      23,    39
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
<<<<<<< HEAD
      15,    43,    28,    99,   101,    20,    86,    16,    57,    51,
      17,    86,    32,    87,    53,    54,    55,    21,    87,   100,
     117,    18,    19,    29,    47,    22,    67,   121,    25,    30,
      26,    70,     1,     2,    27,     3,     4,    33,    34,    41,
      78,    79,    80,    81,     5,     6,     7,    44,     8,    45,
      50,     9,    10,    94,    49,    52,    11,     1,     2,    66,
       3,     4,    58,    56,    59,    60,    61,    62,    63,     5,
       6,     7,    68,     8,    64,    69,     9,    10,   107,    35,
      72,    11,     1,     2,    75,     3,     4,    65,    59,    60,
      61,    62,   115,    36,     5,     6,     7,    46,    37,    38,
      39,     9,    10,    77,    84,    76,    11,    92,    83,    82,
       1,     2,    95,     3,     4,    96,   110,    47,    97,    98,
     106,    47,     5,     6,     7,   119,   108,   109,    31,     9,
      10,   112,     1,     2,    11,     3,     4,   118,   113,    59,
      60,    61,    62,   114,     5,     6,     7,   122,   116,   120,
      35,     9,    10,   102,     1,     2,    11,     3,     4,    71,
      59,    60,    61,    62,    36,     0,     5,     6,     7,    42,
      38,    39,   103,     9,    10,   104,     0,     0,    11,    73,
      59,    60,    61,    62,    74,    59,    60,    61,    62,    85,
      59,    60,    61,    62,    93,    59,    60,    61,    62,   105,
      59,    60,    61,    62,   111,    59,    60,    61,    62,    59,
      60,    61,    62
=======
      15,    49,    96,    27,    16,    17,    51,    52,    53,    18,
      82,    98,    31,    82,    19,    20,    63,    83,    97,    21,
      83,    66,    29,    44,    28,     1,     2,    46,     3,     4,
      24,    75,    76,    77,    78,   114,    25,     5,     6,     7,
      26,     8,   118,    91,     9,    10,    32,     1,     2,    11,
       3,     4,    56,    33,    57,    58,    59,    60,    40,     5,
       6,     7,    41,     8,    42,    47,     9,    10,   104,    34,
      61,    11,     1,     2,    50,     3,     4,    68,    57,    58,
      59,    60,   112,    35,     5,     6,     7,    43,    36,    37,
      38,     9,    10,    54,    55,    64,    11,     1,     2,    62,
       3,     4,   115,    67,    57,    58,    59,    60,    65,     5,
       6,     7,   116,    69,    44,    72,     9,    10,    44,     1,
       2,    11,     3,     4,    70,    57,    58,    59,    60,    73,
      74,     5,     6,     7,   119,    79,    80,    34,     9,    10,
      88,     1,     2,    11,     3,     4,    71,    57,    58,    59,
      60,    35,    90,     5,     6,     7,    48,    37,    38,    92,
       9,    10,    93,    95,    94,    11,    81,    57,    58,    59,
      60,    89,    57,    58,    59,    60,   102,    57,    58,    59,
      60,   108,    57,    58,    59,    60,    57,    58,    59,    60,
     103,   105,   106,   107,   111,   109,   110,    99,    30,   113,
       0,   100,   117,   101
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

static const yytype_int8 yycheck[] =
{
<<<<<<< HEAD
       0,    20,     5,     8,    18,    15,    20,    27,    26,    28,
      27,    20,    12,    27,    33,    34,    35,    27,    27,    24,
     113,     8,     8,    26,    24,    27,    45,   120,    27,     0,
      27,    50,     3,     4,    27,     6,     7,     5,     5,    27,
      59,    60,    61,    62,    15,    16,    17,     5,    19,     5,
       5,    22,    23,    72,     8,    27,    27,     3,     4,    28,
       6,     7,     9,    27,    11,    12,    13,    14,     5,    15,
      16,    17,    17,    19,    26,     9,    22,    23,    97,     8,
       5,    27,     3,     4,     8,     6,     7,    10,    11,    12,
      13,    14,   111,    22,    15,    16,    17,    18,    27,    28,
      29,    22,    23,    10,    10,    27,    27,    10,    27,    29,
       3,     4,     9,     6,     7,     9,    16,   117,    10,     8,
      10,   121,    15,    16,    17,    18,     9,     9,    12,    22,
      23,    17,     3,     4,    27,     6,     7,     9,    17,    11,
      12,    13,    14,    10,    15,    16,    17,    18,    18,    17,
       8,    22,    23,    88,     3,     4,    27,     6,     7,    10,
      11,    12,    13,    14,    22,    -1,    15,    16,    17,    27,
      28,    29,    88,    22,    23,    88,    -1,    -1,    27,    10,
      11,    12,    13,    14,    10,    11,    12,    13,    14,    10,
      11,    12,    13,    14,    10,    11,    12,    13,    14,    10,
      11,    12,    13,    14,    10,    11,    12,    13,    14,    11,
      12,    13,    14
=======
       0,    27,     8,     5,    27,    27,    32,    33,    34,     8,
      20,    18,    12,    20,     8,    27,    42,    27,    24,    27,
      27,    47,     0,    23,    26,     3,     4,     8,     6,     7,
      27,    57,    58,    59,    60,   110,    27,    15,    16,    17,
      27,    19,   117,    69,    22,    23,     5,     3,     4,    27,
       6,     7,     9,     5,    11,    12,    13,    14,    27,    15,
      16,    17,     5,    19,     5,     5,    22,    23,    94,     8,
       5,    27,     3,     4,    27,     6,     7,    10,    11,    12,
      13,    14,   108,    22,    15,    16,    17,    18,    27,    28,
      29,    22,    23,    27,    26,    17,    27,     3,     4,    28,
       6,     7,     9,    26,    11,    12,    13,    14,     9,    15,
      16,    17,    18,     5,   114,     8,    22,    23,   118,     3,
       4,    27,     6,     7,    10,    11,    12,    13,    14,    27,
      10,    15,    16,    17,    18,    29,    10,     8,    22,    23,
      10,     3,     4,    27,     6,     7,    10,    11,    12,    13,
      14,    22,    27,    15,    16,    17,    27,    28,    29,     9,
      22,    23,     9,     8,    10,    27,    10,    11,    12,    13,
      14,    10,    11,    12,    13,    14,    10,    11,    12,    13,
      14,    10,    11,    12,    13,    14,    11,    12,    13,    14,
      10,     9,     9,    16,    10,    17,    17,    84,    12,    18,
      -1,    84,    17,    84
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     6,     7,    15,    16,    17,    19,    22,
      23,    27,    31,    32,    38,    39,    27,    27,     8,     8,
<<<<<<< HEAD
      15,    27,    27,    39,    40,    27,    27,    27,     5,    26,
       0,    32,    39,     5,     5,     8,    22,    27,    28,    29,
      41,    27,    27,    41,     5,     5,    18,    39,    33,     8,
       5,    41,    27,    41,    41,    41,    27,    26,     9,    11,
      12,    13,    14,     5,    26,    10,    28,    41,    17,     9,
      41,    10,     5,    10,    10,     8,    27,    10,    41,    41,
      41,    41,    29,    27,    10,    10,    20,    27,    34,    35,
      36,    37,    10,    10,    41,     9,     9,    10,     8,     8,
      24,    18,    35,    36,    37,    10,    10,    41,     9,     9,
      16,    10,    17,    17,    10,    41,    18,    40,     9,    18,
      17,    40,    18
=======
      27,    27,    39,    40,    27,    27,    27,     5,    26,     0,
      32,    39,     5,     5,     8,    22,    27,    28,    29,    41,
      27,     5,     5,    18,    39,    33,     8,     5,    27,    41,
      27,    41,    41,    41,    27,    26,     9,    11,    12,    13,
      14,     5,    28,    41,    17,     9,    41,    26,    10,     5,
      10,    10,     8,    27,    10,    41,    41,    41,    41,    29,
      10,    10,    20,    27,    34,    35,    36,    37,    10,    10,
      27,    41,     9,     9,    10,     8,     8,    24,    18,    35,
      36,    37,    10,    10,    41,     9,     9,    16,    10,    17,
      17,    10,    41,    18,    40,     9,    18,    17,    40,    18
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    30,    31,    31,    31,    31,    33,    32,    34,    34,
      34,    34,    34,    34,    34,    35,    36,    37,    38,    38,
      39,    39,    39,    39,    39,    39,    39,    39,    39,    39,
<<<<<<< HEAD
      39,    39,    39,    39,    40,    40,    41,    41,    41,    41,
      41,    41,    41,    41,    41,    41
=======
      39,    39,    39,    40,    40,    41,    41,    41,    41,    41,
      41,    41,    41,    41,    41
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     2,     1,     1,     0,     6,     0,     1,
       2,     1,     2,     1,     2,     4,     5,     6,     5,     6,
<<<<<<< HEAD
       1,     4,     5,     5,     5,     5,     4,     5,     7,    13,
       3,     0,     1,     5,     2,     1,     3,     1,     1,     1,
       3,     3,     3,     3,     2,     4
=======
       1,     5,     5,     5,     5,     4,     5,     7,    13,     3,
       0,     1,     5,     2,     1,     3,     1,     1,     1,     3,
       3,     3,     3,     2,     4
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
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
<<<<<<< HEAD
#line 38 "parser.y"
                      { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1201 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 39 "parser.y"
                       { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1207 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 40 "parser.y"
              { root = (yyvsp[0].node); }
#line 1213 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 41 "parser.y"
               { root = (yyvsp[0].node); }
#line 1219 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 47 "parser.y"
=======
#line 35 "parser.y"
                      { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1193 "parser.tab.c"
    break;

  case 3: /* program: program class_decl  */
#line 36 "parser.y"
                       { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); root = (yyval.node); }
#line 1199 "parser.tab.c"
    break;

  case 4: /* program: statement  */
#line 37 "parser.y"
              { root = (yyvsp[0].node); }
#line 1205 "parser.tab.c"
    break;

  case 5: /* program: class_decl  */
#line 38 "parser.y"
               { root = (yyvsp[0].node); }
#line 1211 "parser.tab.c"
    break;

  case 6: /* $@1: %empty  */
#line 44 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
        //printf(" [DEBUG] Se detectó una clase: %s\n", $2);
        last_class = create_class((yyvsp[0].sval));  
        add_class(last_class);
    }
<<<<<<< HEAD
#line 1229 "parser.tab.c"
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 53 "parser.y"
    {
       // printf(" [DEBUG] Fin de la definición de clase %s\n", $2);
    }
#line 1237 "parser.tab.c"
=======
#line 1221 "parser.tab.c"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    break;

  case 7: /* class_decl: CLASS IDENTIFIER $@1 LBRACKET class_body RBRACKET  */
#line 50 "parser.y"
    {
       // printf(" [DEBUG] Fin de la definición de clase %s\n", $2);
    }
#line 1229 "parser.tab.c"
    break;

  case 8: /* class_body: %empty  */
<<<<<<< HEAD
#line 59 "parser.y"
                 { (yyval.node) = NULL; }
#line 1243 "parser.tab.c"
    break;

  case 9: /* class_body: attribute_decl  */
#line 60 "parser.y"
                     { (yyval.node) = (yyvsp[0].node); }
#line 1249 "parser.tab.c"
    break;

  case 10: /* class_body: class_body attribute_decl  */
#line 61 "parser.y"
                                { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1255 "parser.tab.c"
    break;

  case 11: /* class_body: constructor_decl  */
#line 62 "parser.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1261 "parser.tab.c"
    break;

  case 12: /* class_body: class_body constructor_decl  */
#line 63 "parser.y"
                                  { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1267 "parser.tab.c"
    break;

  case 13: /* class_body: method_decl  */
#line 64 "parser.y"
                  { (yyval.node) = (yyvsp[0].node); }
#line 1273 "parser.tab.c"
    break;

  case 14: /* class_body: class_body method_decl  */
#line 65 "parser.y"
                             { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1279 "parser.tab.c"
    break;

  case 15: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 70 "parser.y"
=======
#line 56 "parser.y"
                 { (yyval.node) = NULL; }
#line 1235 "parser.tab.c"
    break;

  case 9: /* class_body: attribute_decl  */
#line 57 "parser.y"
                     { (yyval.node) = (yyvsp[0].node); }
#line 1241 "parser.tab.c"
    break;

  case 10: /* class_body: class_body attribute_decl  */
#line 58 "parser.y"
                                { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1247 "parser.tab.c"
    break;

  case 11: /* class_body: constructor_decl  */
#line 59 "parser.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1253 "parser.tab.c"
    break;

  case 12: /* class_body: class_body constructor_decl  */
#line 60 "parser.y"
                                  { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1259 "parser.tab.c"
    break;

  case 13: /* class_body: method_decl  */
#line 61 "parser.y"
                  { (yyval.node) = (yyvsp[0].node); }
#line 1265 "parser.tab.c"
    break;

  case 14: /* class_body: class_body method_decl  */
#line 62 "parser.y"
                             { (yyval.node) = add_statement((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1271 "parser.tab.c"
    break;

  case 15: /* attribute_decl: IDENTIFIER COLON INT SEMICOLON  */
#line 67 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
        if (last_class) {
            add_attribute_to_class(last_class, (yyvsp[-3].sval), "int");  
        } else {
            printf("Error: No hay clase definida para el atributo '%s'.\n", (yyvsp[-3].sval));
        }
    }
<<<<<<< HEAD
#line 1291 "parser.tab.c"
    break;

  case 16: /* constructor_decl: CONSTRUCTOR LPAREN RPAREN LBRACKET RBRACKET  */
#line 81 "parser.y"
=======
#line 1283 "parser.tab.c"
    break;

  case 16: /* constructor_decl: CONSTRUCTOR LPAREN RPAREN LBRACKET RBRACKET  */
#line 78 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
        //printf("[DEBUG] Se detectó un constructor en la clase %s\n", last_class->name);
        if (last_class) {
            //add_constructor_to_class(last_class, $6);
        } else {
            printf("Error: No hay clase definida para el constructor.\n");
        }
    }
<<<<<<< HEAD
#line 1304 "parser.tab.c"
    break;

  case 17: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 99 "parser.y"
    {
        add_method_to_class(find_class((yyvsp[-5].sval)), strdup((yyvsp[-5].sval)), (yyvsp[-1].node));
    }
#line 1312 "parser.tab.c"
=======
#line 1296 "parser.tab.c"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    break;

  case 17: /* method_decl: IDENTIFIER LPAREN RPAREN LBRACKET statement_list RBRACKET  */
#line 96 "parser.y"
    {
        add_method_to_class(find_class((yyvsp[-5].sval)), strdup((yyvsp[-5].sval)), (yyvsp[-1].node));
    }
#line 1304 "parser.tab.c"
    break;

  case 18: /* var_decl: LET IDENTIFIER ASSIGN expression SEMICOLON  */
<<<<<<< HEAD
#line 106 "parser.y"
=======
#line 103 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
        (yyval.node) = create_var_decl_node((yyvsp[-3].sval), (yyvsp[-1].node));
        //printf(" [DEBUG] Declaración de variable: %s\n", $2);
    }
<<<<<<< HEAD
#line 1321 "parser.tab.c"
    break;

  case 19: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 110 "parser.y"
=======
#line 1313 "parser.tab.c"
    break;

  case 19: /* var_decl: IDENTIFIER DOT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 107 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
                                                            {
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-5].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-3].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);  // ✅ correctamente marcado
        (yyval.node) = create_ast_node("ASSIGN_ATTR", access, (yyvsp[-1].node));  // ✅ left = acceso, right = valor
    }
<<<<<<< HEAD
#line 1332 "parser.tab.c"
    break;

  case 21: /* statement: STRING STRING expression SEMICOLON  */
#line 122 "parser.y"
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
#line 1366 "parser.tab.c"
    break;

  case 22: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 151 "parser.y"
                                                        { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1372 "parser.tab.c"
    break;

  case 23: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 152 "parser.y"
                                                 { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1378 "parser.tab.c"
    break;

  case 24: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 153 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1384 "parser.tab.c"
    break;

  case 25: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 154 "parser.y"
                                                 { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1390 "parser.tab.c"
    break;

  case 26: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 155 "parser.y"
                                             { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1396 "parser.tab.c"
    break;

  case 27: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 156 "parser.y"
                                               { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1402 "parser.tab.c"
    break;

  case 28: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 157 "parser.y"
=======
#line 1324 "parser.tab.c"
    break;

  case 21: /* statement: STRING IDENTIFIER ASSIGN STRING_LITERAL SEMICOLON  */
#line 119 "parser.y"
                                                        { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_string_node((yyvsp[-1].sval))); }
#line 1330 "parser.tab.c"
    break;

  case 22: /* statement: INT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 120 "parser.y"
                                                 { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_int_node((yyvsp[-1].node)->value)); }
#line 1336 "parser.tab.c"
    break;

  case 23: /* statement: FLOAT IDENTIFIER ASSIGN expression SEMICOLON  */
#line 121 "parser.y"
                                                   { (yyval.node) = create_var_decl_node((yyvsp[-3].sval), create_float_node((yyvsp[-1].node)->value)); }
#line 1342 "parser.tab.c"
    break;

  case 24: /* statement: VAR IDENTIFIER ASSIGN expression SEMICOLON  */
#line 122 "parser.y"
                                                 { (yyval.node) = create_ast_node("DECLARE", create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1348 "parser.tab.c"
    break;

  case 25: /* statement: IDENTIFIER ASSIGN expression SEMICOLON  */
#line 123 "parser.y"
                                             { (yyval.node) = create_ast_node("ASSIGN", create_ast_leaf("VAR", 0, NULL, (yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1354 "parser.tab.c"
    break;

  case 26: /* statement: PRINT LPAREN expression RPAREN SEMICOLON  */
#line 124 "parser.y"
                                               { (yyval.node) = create_ast_node("PRINT", (yyvsp[-2].node), NULL); }
#line 1360 "parser.tab.c"
    break;

  case 27: /* statement: PRINT LPAREN IDENTIFIER DOT IDENTIFIER RPAREN SEMICOLON  */
#line 125 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
                                                              {
       // printf(" [DEBUG] Reconocido PRINT con acceso a atributo: %s.%s\n", $3, $5);
        ASTNode *obj = create_ast_leaf("ID", 0, NULL, (yyvsp[-4].sval));
        ASTNode *attr = create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval));
        ASTNode *access = create_ast_node("ACCESS_ATTR", obj, attr);
        (yyval.node) = create_ast_node("PRINT", access, NULL);
    }
<<<<<<< HEAD
#line 1414 "parser.tab.c"
    break;

  case 29: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 166 "parser.y"
=======
#line 1372 "parser.tab.c"
    break;

  case 28: /* statement: FOR LPAREN IDENTIFIER ASSIGN NUMBER SEMICOLON expression SEMICOLON expression RPAREN LBRACKET statement_list RBRACKET  */
#line 134 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
        (yyval.node) = create_ast_node_for("FOR", 
            create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[-10].sval)),  
            create_ast_leaf("NUMBER", (yyvsp[-8].ival), NULL, NULL),   
            (yyvsp[-6].node),  
            (yyvsp[-4].node),  
            (yyvsp[-1].node)  
        );
    }
<<<<<<< HEAD
#line 1428 "parser.tab.c"
    break;

  case 33: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 180 "parser.y"
=======
#line 1386 "parser.tab.c"
    break;

  case 32: /* statement: NEW IDENTIFIER LPAREN RPAREN SEMICOLON  */
#line 148 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
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
<<<<<<< HEAD
#line 1443 "parser.tab.c"
    break;

  case 34: /* statement_list: statement_list statement  */
#line 193 "parser.y"
                             { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); }
#line 1449 "parser.tab.c"
    break;

  case 35: /* statement_list: statement  */
#line 194 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1455 "parser.tab.c"
    break;

  case 36: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 208 "parser.y"
                           {
   // printf(" [DEBUG] Reconocido ACCESS_ATTR: %s.%s\n", $1, $3);
=======
#line 1401 "parser.tab.c"
    break;

  case 33: /* statement_list: statement_list statement  */
#line 161 "parser.y"
                             { (yyval.node) = create_ast_node("STATEMENT_LIST", (yyvsp[0].node), (yyvsp[-1].node)); }
#line 1407 "parser.tab.c"
    break;

  case 34: /* statement_list: statement  */
#line 162 "parser.y"
                             { (yyval.node) = (yyvsp[0].node); }
#line 1413 "parser.tab.c"
    break;

  case 35: /* expression: IDENTIFIER DOT IDENTIFIER  */
#line 176 "parser.y"
                           {
    printf("🎯 Reconocido ACCESS_ATTR: %s.%s\n", (yyvsp[-2].sval), (yyvsp[0].sval));
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    (yyval.node) = create_ast_node("ACCESS_ATTR",
                        create_ast_leaf("ID", 0, NULL, (yyvsp[-2].sval)),
                        create_ast_leaf("ID", 0, NULL, (yyvsp[0].sval)));
}
<<<<<<< HEAD
#line 1466 "parser.tab.c"
    break;

  case 37: /* expression: IDENTIFIER  */
#line 214 "parser.y"
             { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[0].sval)); }
#line 1472 "parser.tab.c"
    break;

  case 38: /* expression: NUMBER  */
#line 215 "parser.y"
             { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 1478 "parser.tab.c"
    break;

  case 39: /* expression: STRING_LITERAL  */
#line 216 "parser.y"
                     { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 1484 "parser.tab.c"
    break;

  case 40: /* expression: expression PLUS expression  */
#line 218 "parser.y"
                                 { (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1490 "parser.tab.c"
    break;

  case 41: /* expression: expression MINUS expression  */
#line 219 "parser.y"
                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1496 "parser.tab.c"
    break;

  case 42: /* expression: expression MULTIPLY expression  */
#line 220 "parser.y"
                                     { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1502 "parser.tab.c"
    break;

  case 43: /* expression: expression DIVIDE expression  */
#line 221 "parser.y"
                                   { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1508 "parser.tab.c"
    break;

  case 45: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 224 "parser.y"
=======
#line 1424 "parser.tab.c"
    break;

  case 36: /* expression: IDENTIFIER  */
#line 182 "parser.y"
             { (yyval.node) = create_ast_leaf("IDENTIFIER", 0, NULL, (yyvsp[0].sval)); }
#line 1430 "parser.tab.c"
    break;

  case 37: /* expression: NUMBER  */
#line 183 "parser.y"
             { (yyval.node) = create_ast_leaf("NUMBER", (yyvsp[0].ival), NULL, NULL); }
#line 1436 "parser.tab.c"
    break;

  case 38: /* expression: STRING_LITERAL  */
#line 184 "parser.y"
                     { (yyval.node) = create_ast_leaf("STRING", 0, (yyvsp[0].sval), NULL); }
#line 1442 "parser.tab.c"
    break;

  case 39: /* expression: expression PLUS expression  */
#line 186 "parser.y"
                                 { (yyval.node) = create_ast_node("ADD", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1448 "parser.tab.c"
    break;

  case 40: /* expression: expression MINUS expression  */
#line 187 "parser.y"
                                  { (yyval.node) = create_ast_node("SUB", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1454 "parser.tab.c"
    break;

  case 41: /* expression: expression MULTIPLY expression  */
#line 188 "parser.y"
                                     { (yyval.node) = create_ast_node("MUL", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1460 "parser.tab.c"
    break;

  case 42: /* expression: expression DIVIDE expression  */
#line 189 "parser.y"
                                   { (yyval.node) = create_ast_node("DIV", (yyvsp[-2].node), (yyvsp[0].node)); }
#line 1466 "parser.tab.c"
    break;

  case 44: /* expression: NEW IDENTIFIER LPAREN RPAREN  */
#line 192 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618
    {
       
        ClassNode *cls = find_class((yyvsp[-2].sval));
        if (!cls) {
            printf("Error: Clase '%s' no definida.\n", (yyvsp[-2].sval));
            (yyval.node) = NULL;
        } else {
           (yyval.node) = (ASTNode *)create_object_with_args(cls, (yyvsp[-2].sval));
        }
    }
<<<<<<< HEAD
#line 1523 "parser.tab.c"
    break;


#line 1527 "parser.tab.c"
=======
#line 1481 "parser.tab.c"
    break;


#line 1485 "parser.tab.c"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618

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

<<<<<<< HEAD
#line 236 "parser.y"


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

=======
#line 204 "parser.y"
>>>>>>> 4e21379b49d2ceae5eedb005601a51e72dcbb618


void yyerror(const char *s) {
    extern char *yytext;
    fprintf(stderr, "Error: %s en la línea %d (token: '%s')\n", s, yylineno, yytext);
}


int main(int argc, char *argv[]) {

    

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
