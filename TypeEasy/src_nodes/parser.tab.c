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

    #include "semantics.c"
    #include "symtab.c"
    #include "ast.h"
    #include "ast.c"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    extern FILE *yyin;
    extern FILE *yyout;
    extern int lineno;
    extern int yylex();

    // for declarations
    void add_to_names(list_t *entry);
    list_t **names;
    int nc = 0;
    AST_Node **elsifs;
    // for the initializations of arrays
    void add_to_vals(Value val);
    Value *vals;
    int vc = 0;

    int elseif_count = 0;

    void yyerror();

#line 99 "parser.tab.c"

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
  YYSYMBOL_CHAR = 3,                       /* CHAR  */
  YYSYMBOL_INT = 4,                        /* INT  */
  YYSYMBOL_FLOAT = 5,                      /* FLOAT  */
  YYSYMBOL_DOUBLE = 6,                     /* DOUBLE  */
  YYSYMBOL_IF = 7,                         /* IF  */
  YYSYMBOL_ELSE = 8,                       /* ELSE  */
  YYSYMBOL_FOR = 9,                        /* FOR  */
  YYSYMBOL_CONTINUE = 10,                  /* CONTINUE  */
  YYSYMBOL_BREAK = 11,                     /* BREAK  */
  YYSYMBOL_VOID = 12,                      /* VOID  */
  YYSYMBOL_RETURN = 13,                    /* RETURN  */
  YYSYMBOL_ADDOP = 14,                     /* ADDOP  */
  YYSYMBOL_MULOP = 15,                     /* MULOP  */
  YYSYMBOL_DIVOP = 16,                     /* DIVOP  */
  YYSYMBOL_INCR = 17,                      /* INCR  */
  YYSYMBOL_OROP = 18,                      /* OROP  */
  YYSYMBOL_ANDOP = 19,                     /* ANDOP  */
  YYSYMBOL_NOTOP = 20,                     /* NOTOP  */
  YYSYMBOL_EQUOP = 21,                     /* EQUOP  */
  YYSYMBOL_RELOP = 22,                     /* RELOP  */
  YYSYMBOL_LPAREN = 23,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 24,                    /* RPAREN  */
  YYSYMBOL_LBRACK = 25,                    /* LBRACK  */
  YYSYMBOL_RBRACK = 26,                    /* RBRACK  */
  YYSYMBOL_LBRACE = 27,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 28,                    /* RBRACE  */
  YYSYMBOL_SEMI = 29,                      /* SEMI  */
  YYSYMBOL_DOT = 30,                       /* DOT  */
  YYSYMBOL_COMMA = 31,                     /* COMMA  */
  YYSYMBOL_ASSIGN = 32,                    /* ASSIGN  */
  YYSYMBOL_REFER = 33,                     /* REFER  */
  YYSYMBOL_ID = 34,                        /* ID  */
  YYSYMBOL_CONST = 35,                     /* CONST  */
  YYSYMBOL_ICONST = 36,                    /* ICONST  */
  YYSYMBOL_WHILE = 37,                     /* WHILE  */
  YYSYMBOL_PRINT = 38,                     /* PRINT  */
  YYSYMBOL_FCONST = 39,                    /* FCONST  */
  YYSYMBOL_CCONST = 40,                    /* CCONST  */
  YYSYMBOL_STRING = 41,                    /* STRING  */
  YYSYMBOL_YYACCEPT = 42,                  /* $accept  */
  YYSYMBOL_program = 43,                   /* program  */
  YYSYMBOL_declarations = 44,              /* declarations  */
  YYSYMBOL_declaration = 45,               /* declaration  */
  YYSYMBOL_type = 46,                      /* type  */
  YYSYMBOL_names = 47,                     /* names  */
  YYSYMBOL_variable = 48,                  /* variable  */
  YYSYMBOL_pointer = 49,                   /* pointer  */
  YYSYMBOL_array = 50,                     /* array  */
  YYSYMBOL_init = 51,                      /* init  */
  YYSYMBOL_var_init = 52,                  /* var_init  */
  YYSYMBOL_array_init = 53,                /* array_init  */
  YYSYMBOL_values = 54,                    /* values  */
  YYSYMBOL_statements = 55,                /* statements  */
  YYSYMBOL_statement = 56,                 /* statement  */
  YYSYMBOL_if_statement = 57,              /* if_statement  */
  YYSYMBOL_else_if = 58,                   /* else_if  */
  YYSYMBOL_optional_else = 59,             /* optional_else  */
  YYSYMBOL_for_statement = 60,             /* for_statement  */
  YYSYMBOL_while_statement = 61,           /* while_statement  */
  YYSYMBOL_tail = 62,                      /* tail  */
  YYSYMBOL_expression = 63,                /* expression  */
  YYSYMBOL_sign = 64,                      /* sign  */
  YYSYMBOL_constant = 65,                  /* constant  */
  YYSYMBOL_assigment = 66,                 /* assigment  */
  YYSYMBOL_var_ref = 67,                   /* var_ref  */
  YYSYMBOL_function_call = 68,             /* function_call  */
  YYSYMBOL_call_params = 69,               /* call_params  */
  YYSYMBOL_call_param = 70,                /* call_param  */
  YYSYMBOL_functions_optional = 71,        /* functions_optional  */
  YYSYMBOL_functions = 72,                 /* functions  */
  YYSYMBOL_function = 73,                  /* function  */
  YYSYMBOL_function_head = 74,             /* function_head  */
  YYSYMBOL_return_type = 75,               /* return_type  */
  YYSYMBOL_parameters_optional = 76,       /* parameters_optional  */
  YYSYMBOL_parameters = 77,                /* parameters  */
  YYSYMBOL_parameter = 78,                 /* parameter  */
  YYSYMBOL_function_tail = 79,             /* function_tail  */
  YYSYMBOL_declarations_optional = 80,     /* declarations_optional  */
  YYSYMBOL_statements_optional = 81,       /* statements_optional  */
  YYSYMBOL_return_optional = 82            /* return_optional  */
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
#define YYFINAL  10
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   325

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  41
/* YYNRULES -- Number of rules.  */
#define YYNRULES  101
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  199

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   296


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
      35,    36,    37,    38,    39,    40,    41
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    83,    83,    85,    85,    87,    89,    89,    89,    89,
      89,    91,    91,    91,    91,    93,    94,    95,    98,    98,
     100,   100,   102,   102,   104,   106,   108,   108,   111,   111,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   132,   133,   137,   138,   141,
     141,   143,   158,   163,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   182,   182,
     184,   184,   184,   186,   188,   188,   190,   192,   192,   192,
     194,   194,   197,   197,   199,   199,   201,   203,   205,   205,
     207,   207,   209,   209,   211,   213,   215,   215,   217,   217,
     219,   219
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
  "\"end of file\"", "error", "\"invalid token\"", "CHAR", "INT", "FLOAT",
  "DOUBLE", "IF", "ELSE", "FOR", "CONTINUE", "BREAK", "VOID", "RETURN",
  "ADDOP", "MULOP", "DIVOP", "INCR", "OROP", "ANDOP", "NOTOP", "EQUOP",
  "RELOP", "LPAREN", "RPAREN", "LBRACK", "RBRACK", "LBRACE", "RBRACE",
  "SEMI", "DOT", "COMMA", "ASSIGN", "REFER", "ID", "CONST", "ICONST",
  "WHILE", "PRINT", "FCONST", "CCONST", "STRING", "$accept", "program",
  "declarations", "declaration", "type", "names", "variable", "pointer",
  "array", "init", "var_init", "array_init", "values", "statements",
  "statement", "if_statement", "else_if", "optional_else", "for_statement",
  "while_statement", "tail", "expression", "sign", "constant", "assigment",
  "var_ref", "function_call", "call_params", "call_param",
  "functions_optional", "functions", "function", "function_head",
  "return_type", "parameters_optional", "parameters", "parameter",
  "function_tail", "declarations_optional", "statements_optional",
  "return_optional", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-127)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-80)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     121,  -127,  -127,  -127,  -127,  -127,    15,    42,  -127,     6,
    -127,    20,    27,   -10,    29,  -127,    34,    21,     5,    47,
      49,  -127,  -127,    58,    74,  -127,  -127,  -127,  -127,    45,
      46,    66,     1,    -2,  -127,  -127,  -127,  -127,   180,     8,
    -127,  -127,    72,    61,  -127,    80,   158,   180,    85,   180,
     279,  -127,  -127,    89,  -127,  -127,   180,  -127,    57,    12,
    -127,     6,  -127,    94,   180,   180,    65,   229,    57,  -127,
    -127,   102,  -127,  -127,  -127,   284,    90,   104,   211,   180,
     240,   108,   113,   118,   122,   126,   127,   121,   284,  -127,
    -127,  -127,  -127,   130,  -127,  -127,  -127,   284,   251,  -127,
     180,   180,   180,   180,   180,   180,   180,   131,  -127,   180,
    -127,   180,  -127,   220,   131,   116,   124,   137,   141,   142,
     145,   161,  -127,   121,  -127,   133,   143,    57,  -127,   303,
     290,   290,   160,  -127,    87,   101,   152,   172,   190,   284,
    -127,  -127,  -127,  -127,  -127,  -127,  -127,  -127,   168,  -127,
     121,  -127,   164,    71,  -127,   106,     4,   176,  -127,   154,
     121,   152,   121,  -127,    57,  -127,   170,  -127,     7,  -127,
     179,   152,   185,    21,   177,   171,  -127,  -127,   180,   184,
     186,   180,   187,  -127,  -127,   121,   262,   180,   131,   202,
    -127,  -127,   131,   273,  -127,  -127,  -127,   131,  -127
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     7,     6,     8,     9,    10,     0,     0,     4,     0,
       1,     0,     0,     0,     0,    19,     0,     0,    15,     0,
       0,     3,    74,     0,     0,    29,    30,    31,    32,     0,
       0,     0,    15,     0,    13,    14,    22,    23,    69,     0,
      34,    35,     0,    15,    75,     0,    69,    69,    17,    69,
       0,    18,    16,     0,    28,    33,    69,    36,     0,    17,
       5,     0,    68,     0,    69,    69,    15,     0,     0,    65,
      67,     0,    38,    37,    78,    81,     0,    77,     0,    69,
       0,     0,     0,     0,     0,     0,     0,    83,    73,    70,
      71,    72,    24,     0,    11,    12,    58,    61,     0,    57,
      69,    69,    69,    69,    69,    69,    69,     0,    66,    69,
      76,    69,    21,     0,     0,     0,     0,     0,     0,     0,
       0,    88,     2,    82,    85,     0,     0,     0,    64,    54,
      55,    56,    59,    60,    62,    63,     0,    50,     0,    80,
      20,    52,    39,    40,    41,    42,    43,    44,    89,    84,
      97,    86,     0,     0,    27,     0,     0,    50,    46,     0,
      96,    99,    91,    25,     0,    53,     0,    49,     0,    45,
       0,    98,   101,     0,     0,    90,    93,    26,    69,     0,
       0,    69,     0,    94,    87,     0,     0,    69,     0,     0,
      95,    92,     0,     0,    51,   100,    48,     0,    47
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -127,  -127,    78,    -4,   -85,  -127,    -5,   119,   217,   191,
    -127,  -127,  -127,  -126,   -19,  -127,  -127,   100,  -127,  -127,
     -94,   -40,  -127,   -60,   183,    -7,    -6,  -127,  -127,  -127,
    -127,   140,  -127,  -127,  -127,  -127,    75,  -127,  -127,  -127,
    -127
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     6,     7,     8,     9,    33,    22,    23,    48,    35,
      36,    37,   153,    24,    25,    26,   157,   158,    27,    28,
     167,    67,    68,    92,    29,    69,    70,    76,    77,   122,
     123,   124,   125,   126,   174,   175,   176,   151,   161,   172,
     182
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      30,    31,   121,    21,    34,    54,    75,    78,   108,    80,
     155,   166,    44,   137,   179,    10,    88,    30,    31,    40,
     141,    15,    45,    15,    97,    98,    47,    60,    46,    61,
      47,   136,    30,    58,   136,   171,    15,    79,   121,   113,
      32,    17,    43,    38,    93,     1,     2,     3,     4,    11,
      39,    12,    13,    14,     5,    43,    94,    15,    41,    16,
     129,   130,   131,   132,   133,   134,   135,   154,    42,   138,
      49,   139,    50,    51,    55,    17,    18,   173,    56,    19,
      20,    11,    99,    12,    13,    14,    47,    53,    46,    15,
      47,    16,    52,    89,   194,    57,    90,    91,   196,   163,
     173,    72,   164,   198,   177,   103,   104,    17,    18,    73,
      79,    19,    20,    11,   110,    12,    13,    14,    87,   103,
     104,    15,   105,    16,     1,     2,     3,     4,    96,    30,
      31,   109,   115,     5,   165,   111,    54,   116,   186,    17,
      18,   189,   117,    19,    20,   142,   118,   193,    30,    31,
     119,   120,    54,   143,    30,    31,    21,   127,   136,    11,
     150,    12,    13,    14,    30,    31,   144,    15,   183,    16,
     145,   146,    62,    15,   147,    63,    15,   152,    64,   104,
     156,    65,   -79,    51,   168,    17,    18,   162,   170,    19,
      20,    17,    66,   178,    62,    15,   180,    63,   181,    74,
      64,   184,   185,    65,   100,   101,   102,   187,   103,   104,
     188,   105,   106,    17,    66,   190,   100,   101,   102,   159,
     103,   104,    71,   105,   106,   100,   101,   102,   160,   103,
     104,   195,   105,   106,   100,   101,   102,   112,   103,   104,
     148,   105,   106,   100,   101,   102,   140,   103,   104,    59,
     105,   106,    95,   107,   100,   101,   102,   169,   103,   104,
     191,   105,   106,   149,   114,   100,   101,   102,     0,   103,
     104,     0,   105,   106,     0,   128,   100,   101,   102,     0,
     103,   104,     0,   105,   106,     0,   192,   100,   101,   102,
       0,   103,   104,     0,   105,   106,     0,   197,   100,   101,
     102,     0,   103,   104,   100,   105,   106,     0,   103,   104,
       0,   105,   106,    81,    82,    83,     0,     0,    84,    85,
      86,   103,   104,     0,   105,   106
};

static const yytype_int16 yycheck[] =
{
       7,     7,    87,     7,     9,    24,    46,    47,    68,    49,
     136,     7,    17,   107,     7,     0,    56,    24,    24,    29,
     114,    15,    17,    15,    64,    65,    25,    29,    23,    31,
      25,    27,    39,    32,    27,   161,    15,    25,   123,    79,
      34,    33,    34,    23,    32,     3,     4,     5,     6,     7,
      23,     9,    10,    11,    12,    34,    61,    15,    29,    17,
     100,   101,   102,   103,   104,   105,   106,   127,    34,   109,
      23,   111,    23,    15,    29,    33,    34,   162,    32,    37,
      38,     7,    17,     9,    10,    11,    25,    13,    23,    15,
      25,    17,    34,    36,   188,    29,    39,    40,   192,    28,
     185,    29,    31,   197,   164,    18,    19,    33,    34,    29,
      25,    37,    38,     7,    24,     9,    10,    11,    29,    18,
      19,    15,    21,    17,     3,     4,     5,     6,    34,   136,
     136,    29,    24,    12,    28,    31,   155,    24,   178,    33,
      34,   181,    24,    37,    38,    29,    24,   187,   155,   155,
      24,    24,   171,    29,   161,   161,   160,    27,    27,     7,
      27,     9,    10,    11,   171,   171,    29,    15,   173,    17,
      29,    29,    14,    15,    29,    17,    15,    34,    20,    19,
       8,    23,    24,    15,     8,    33,    34,    23,    34,    37,
      38,    33,    34,    23,    14,    15,    17,    17,    13,    41,
      20,    24,    31,    23,    14,    15,    16,    23,    18,    19,
      24,    21,    22,    33,    34,    28,    14,    15,    16,    29,
      18,    19,    39,    21,    22,    14,    15,    16,   150,    18,
      19,    29,    21,    22,    14,    15,    16,    26,    18,    19,
     121,    21,    22,    14,    15,    16,    26,    18,    19,    32,
      21,    22,    61,    24,    14,    15,    16,   157,    18,    19,
     185,    21,    22,   123,    24,    14,    15,    16,    -1,    18,
      19,    -1,    21,    22,    -1,    24,    14,    15,    16,    -1,
      18,    19,    -1,    21,    22,    -1,    24,    14,    15,    16,
      -1,    18,    19,    -1,    21,    22,    -1,    24,    14,    15,
      16,    -1,    18,    19,    14,    21,    22,    -1,    18,    19,
      -1,    21,    22,    34,    35,    36,    -1,    -1,    39,    40,
      41,    18,    19,    -1,    21,    22
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,    12,    43,    44,    45,    46,
       0,     7,     9,    10,    11,    15,    17,    33,    34,    37,
      38,    45,    48,    49,    55,    56,    57,    60,    61,    66,
      67,    68,    34,    47,    48,    51,    52,    53,    23,    23,
      29,    29,    34,    34,    48,    17,    23,    25,    50,    23,
      23,    15,    34,    13,    56,    29,    32,    29,    32,    50,
      29,    31,    14,    17,    20,    23,    34,    63,    64,    67,
      68,    66,    29,    29,    41,    63,    69,    70,    63,    25,
      63,    34,    35,    36,    39,    40,    41,    29,    63,    36,
      39,    40,    65,    32,    48,    51,    34,    63,    63,    17,
      14,    15,    16,    18,    19,    21,    22,    24,    65,    29,
      24,    31,    26,    63,    24,    24,    24,    24,    24,    24,
      24,    46,    71,    72,    73,    74,    75,    27,    24,    63,
      63,    63,    63,    63,    63,    63,    27,    62,    63,    63,
      26,    62,    29,    29,    29,    29,    29,    29,    49,    73,
      27,    79,    34,    54,    65,    55,     8,    58,    59,    29,
      44,    80,    23,    28,    31,    28,     7,    62,     8,    59,
      34,    55,    81,    46,    76,    77,    78,    65,    23,     7,
      17,    13,    82,    48,    24,    31,    63,    23,    24,    63,
      28,    78,    24,    63,    62,    29,    62,    24,    62
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    42,    43,    44,    44,    45,    46,    46,    46,    46,
      46,    47,    47,    47,    47,    48,    48,    48,    49,    49,
      50,    50,    51,    51,    52,    53,    54,    54,    55,    55,
      56,    56,    56,    56,    56,    56,    56,    56,    56,    56,
      56,    56,    56,    56,    56,    57,    57,    58,    58,    59,
      59,    60,    61,    62,    63,    63,    63,    63,    63,    63,
      63,    63,    63,    63,    63,    63,    63,    63,    64,    64,
      65,    65,    65,    66,    67,    67,    68,    69,    69,    69,
      70,    70,    71,    71,    72,    72,    73,    74,    75,    75,
      76,    76,    77,    77,    78,    79,    80,    80,    81,    81,
      82,    82
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     5,     2,     1,     3,     1,     1,     1,     1,
       1,     3,     3,     1,     1,     1,     2,     2,     2,     1,
       4,     3,     1,     1,     3,     6,     3,     1,     2,     1,
       1,     1,     1,     2,     2,     2,     2,     3,     3,     5,
       5,     5,     5,     5,     5,     7,     6,     7,     6,     2,
       0,    10,     5,     3,     3,     3,     3,     2,     2,     3,
       3,     2,     3,     3,     3,     1,     2,     1,     1,     0,
       1,     1,     1,     3,     1,     2,     4,     1,     1,     0,
       3,     1,     1,     0,     2,     1,     2,     5,     1,     2,
       1,     0,     3,     1,     2,     5,     1,     0,     1,     0,
       3,     0
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
  case 30: /* statement: if_statement  */
#line 114 "parser.y"
                 { (yyval.node) = (yyvsp[0].node); }
#line 1330 "parser.tab.c"
    break;

  case 31: /* statement: for_statement  */
#line 115 "parser.y"
                    { (yyval.node) = (yyvsp[0].node); }
#line 1336 "parser.tab.c"
    break;

  case 32: /* statement: while_statement  */
#line 116 "parser.y"
                      { (yyval.node) = (yyvsp[0].node); }
#line 1342 "parser.tab.c"
    break;

  case 33: /* statement: assigment SEMI  */
#line 117 "parser.y"
                     { (yyval.node) = (yyvsp[-1].node); }
#line 1348 "parser.tab.c"
    break;

  case 34: /* statement: CONTINUE SEMI  */
#line 118 "parser.y"
                    { (yyval.node) = new_ast_simple_node(0); }
#line 1354 "parser.tab.c"
    break;

  case 35: /* statement: BREAK SEMI  */
#line 119 "parser.y"
                 { (yyval.node) = new_ast_simple_node(1); }
#line 1360 "parser.tab.c"
    break;

  case 36: /* statement: function_call SEMI  */
#line 120 "parser.y"
                         { (yyval.node) = (yyvsp[-1].node); }
#line 1366 "parser.tab.c"
    break;

  case 37: /* statement: ID INCR SEMI  */
#line 121 "parser.y"
                   { (yyval.node) = new_ast_incr_node((yyvsp[-2].symtab_item), 0, 0); }
#line 1372 "parser.tab.c"
    break;

  case 38: /* statement: INCR ID SEMI  */
#line 122 "parser.y"
                   { (yyval.node) = new_ast_incr_node((yyvsp[-2].int_val), 1, 0); }
#line 1378 "parser.tab.c"
    break;

  case 39: /* statement: PRINT LPAREN ID RPAREN SEMI  */
#line 123 "parser.y"
                                  { printf("%s\n",(yyvsp[-2].symtab_item)); }
#line 1384 "parser.tab.c"
    break;

  case 40: /* statement: PRINT LPAREN CONST RPAREN SEMI  */
#line 124 "parser.y"
                                     { printf("%s\n",(yyvsp[-2].int_val)); }
#line 1390 "parser.tab.c"
    break;

  case 41: /* statement: PRINT LPAREN ICONST RPAREN SEMI  */
#line 125 "parser.y"
                                      { printf("%s\n",(yyvsp[-2].int_val)); }
#line 1396 "parser.tab.c"
    break;

  case 42: /* statement: PRINT LPAREN FCONST RPAREN SEMI  */
#line 126 "parser.y"
                                      { printf("%s\n",(yyvsp[-2].double_val)); }
#line 1402 "parser.tab.c"
    break;

  case 43: /* statement: PRINT LPAREN CCONST RPAREN SEMI  */
#line 127 "parser.y"
                                      { printf("%s\n",(yyvsp[-2].char_val)); }
#line 1408 "parser.tab.c"
    break;

  case 44: /* statement: PRINT LPAREN STRING RPAREN SEMI  */
#line 128 "parser.y"
                                      { printf("%s\n",(yyvsp[-2].str_val)); }
#line 1414 "parser.tab.c"
    break;

  case 51: /* for_statement: FOR LPAREN assigment SEMI expression SEMI ID INCR RPAREN tail  */
#line 144 "parser.y"
{
    // create increment node
    AST_Node *incr_node;
    if ((yyvsp[-2].int_val) == INC) { /* increment */
        incr_node = new_ast_incr_node((yyvsp[-3].symtab_item), 0, 0);
    } else {
        incr_node = new_ast_incr_node((yyvsp[-3].symtab_item), 1, 0);
    }

    // Create the 'for' node
    (yyval.node) = new_ast_for_node((yyvsp[-7].node), (yyvsp[-5].node), incr_node, (yyvsp[0].node));
    set_loop_counter((yyval.node));
}
#line 1432 "parser.tab.c"
    break;

  case 52: /* while_statement: WHILE LPAREN expression RPAREN tail  */
#line 159 "parser.y"
{
    //$$ = new_ast_while_node($3, $4);
}
#line 1440 "parser.tab.c"
    break;

  case 53: /* tail: LBRACE statements RBRACE  */
#line 163 "parser.y"
                               { (yyval.node) = (yyvsp[-1].node); }
#line 1446 "parser.tab.c"
    break;


#line 1450 "parser.tab.c"

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

#line 221 "parser.y"


void add_to_names(list_t *entry) {
    if (!entry) {
        fprintf(stderr, "Error: Null entry passed to add_to_names\n");
        return;
    }
    
    if (nc == 0) {
        nc = 1;
        names = (list_t **) malloc(sizeof(list_t *));
        if (!names) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit(1);
        }
        names[0] = entry;
    } else {
        list_t **temp = (list_t **) realloc(names, (nc + 1) * sizeof(list_t *));
        if (!temp) {
            fprintf(stderr, "Error: Memory reallocation failed\n");
            free(names);
            exit(1);
        }
        names = temp;
        names[nc] = entry;
        nc++;
    }
}

void add_to_vals(Value val){
    if (vc == 0) {
        vc = 1;
        vals = (Value *) malloc(1 * sizeof(Value));
        vals[0] = val;
    } else {
        vc++;
        vals = (Value *) realloc(vals, vc * sizeof(Value));
        vals[vc - 1] = val;
    }
}

void add_elseif(AST_Node *elsif){
    if (elseif_count == 0) {
        elseif_count = 1;
        elsifs = (AST_Node **) malloc(1 * sizeof(AST_Node));
        elsifs[0] = elsif;
    } else {
        elseif_count++;
        elsifs = (AST_Node **) realloc(elsifs, elseif_count * sizeof(AST_Node));
        elsifs[elseif_count - 1] = elsif;
    }
}

void yyerror () {
    fprintf(stderr, "Syntax error at line %d\n", lineno);
    exit(1);
}

int main (int argc, char *argv[]) {
    // initialize symbol table
    init_hash_table();

    // parsing
    int flag;
    yyin = fopen(argv[1], "r");
    flag = yyparse();
    fclose(yyin);
    
    printf("Parsing finished!\n");
    
    return flag;
}
