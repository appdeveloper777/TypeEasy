/* te_types.h — Única fuente de verdad para los TAGS DE TIPO del runtime.
 *
 * ASTNode.type y Variable.type son cadenas ("STRING", "INT", ...) y el
 * intérprete las compara con strcmp en ~2000 sitios. Este header nombra cada
 * tag una sola vez: un typo (TE_T_STRIN) es error de compilación en vez de un
 * strcmp que nunca matchea. NodeKind (ast.h) es la caché enum de estos tags
 * para los nodos de expresión; nk_from_str() es el mapeo tag -> NodeKind.
 *
 * Regla: NUNCA escribir el literal en código; usar la constante. El script
 * scripts/audit_core_debt.sh cuenta literales residuales (baseline 0).
 *
 * TE_T_*  : tags de runtime (mayúsculas) — ASTNode.type / Variable.type.
 * TE_DT_* : nombres de tipo DECLARADOS en fuente .te (minúsculas) —
 *           ParameterNode.type, ClassNode.attributes[i].type, MethodNode.return_type.
 * TE_SYM_*: identificadores reservados del runtime (slot de retorno, receptor, ctor).
 */
#ifndef TE_TYPES_H
#define TE_TYPES_H

/* ---- valores / hojas ---------------------------------------------------- */
#define TE_T_STRING          "STRING"
#define TE_T_STRING_LITERAL  "STRING_LITERAL"
#define TE_T_STRING_INTERP   "STRING_INTERP"
#define TE_T_IDENTIFIER      "IDENTIFIER"
#define TE_T_ID              "ID"
#define TE_T_INT             "INT"
#define TE_T_NUMBER          "NUMBER"
#define TE_T_FLOAT           "FLOAT"
#define TE_T_FLOAT_LITERAL   "FLOAT_LITERAL"
#define TE_T_DECIMAL         "DECIMAL"
#define TE_T_BOOL            "BOOL"
#define TE_T_NULL            "NULL"
#define TE_T_NULLTOK         "NULLTOK"
#define TE_T_LIST            "LIST"
#define TE_T_MAP             "MAP"
#define TE_T_OBJECT          "OBJECT"
#define TE_T_OBJECT_LITERAL  "OBJECT_LITERAL"
#define TE_T_KV_PAIR         "KV_PAIR"
#define TE_T_LAMBDA          "LAMBDA"
#define TE_T_LAZY_ITER       "LAZY_ITER"
#define TE_T_LAZY_OP         "LAZY_OP"
#define TE_T_DATETIME        "DATETIME"
#define TE_T_UUID            "UUID"
#define TE_T_DB_RAW          "DB_RAW"
#define TE_T_DATAFRAME       "DATAFRAME"
#define TE_T_CSV_LOAD        "CSV_LOAD"
#define TE_T_THIS            "THIS"
#define TE_T_ARGS            "ARGS"

/* ---- operadores --------------------------------------------------------- */
#define TE_T_ADD             "ADD"
#define TE_T_SUB             "SUB"
#define TE_T_MUL             "MUL"
#define TE_T_DIV             "DIV"
#define TE_T_MOD             "MOD"
#define TE_T_NEG             "NEG"
#define TE_T_GT              "GT"
#define TE_T_LT              "LT"
#define TE_T_GT_EQ           "GT_EQ"
#define TE_T_LT_EQ           "LT_EQ"
#define TE_T_EQ              "EQ"
#define TE_T_DIFF            "DIFF"
#define TE_T_AND             "AND"
#define TE_T_OR              "OR"
#define TE_T_NOT             "NOT"
#define TE_T_IN              "IN"
#define TE_T_BIT_AND         "BIT_AND"
#define TE_T_BIT_OR          "BIT_OR"
#define TE_T_BIT_XOR         "BIT_XOR"
#define TE_T_BIT_NOT         "BIT_NOT"
#define TE_T_SHL             "SHL"
#define TE_T_SHR             "SHR"
#define TE_T_TERNARY         "TERNARY"
#define TE_T_NULL_COALESCE   "NULL_COALESCE"

/* ---- acceso / llamadas -------------------------------------------------- */
#define TE_T_ACCESS_ATTR       "ACCESS_ATTR"
#define TE_T_ACCESS_EXPR       "ACCESS_EXPR"
#define TE_T_ACCESS_INDEX      "ACCESS_INDEX"
#define TE_T_CALL_FUNC         "CALL_FUNC"
#define TE_T_CALL_METHOD       "CALL_METHOD"
#define TE_T_CALL_EXPR         "CALL_EXPR"
#define TE_T_METHOD_CALL_ALONE "METHOD_CALL_ALONE"
#define TE_T_LIST_FUNC_CALL    "LIST_FUNC_CALL"
#define TE_T_FILTER_CALL       "FILTER_CALL"
#define TE_T_RETURN_JSON       "RETURN_JSON"
#define TE_T_RETURN_XML        "RETURN_XML"

/* ---- sentencias --------------------------------------------------------- */
#define TE_T_STATEMENT_LIST  "STATEMENT_LIST"
#define TE_T_EXPRESSION      "EXPRESSION"
#define TE_T_VAR_DECL        "VAR_DECL"
#define TE_T_DECLARE         "DECLARE"
#define TE_T_STATE_DECL      "STATE_DECL"
#define TE_T_ASSIGN          "ASSIGN"
#define TE_T_ASSIGN_ATTR     "ASSIGN_ATTR"
#define TE_T_INDEX_ASSIGN    "INDEX_ASSIGN"
#define TE_T_IF              "IF"
#define TE_T_MATCH           "MATCH"
#define TE_T_CASE            "CASE"
#define TE_T_FOR             "FOR"
#define TE_T_FOR_C           "FOR_C"
#define TE_T_FOR_IN          "FOR_IN"
#define TE_T_FOR_BODY        "FOR_BODY"
#define TE_T_WHILE           "WHILE"
#define TE_T_BREAK           "BREAK"
#define TE_T_CONTINUE        "CONTINUE"
#define TE_T_RETURN          "RETURN"
#define TE_T_THROW           "THROW"
#define TE_T_TRY_CATCH       "TRY_CATCH"
#define TE_T_PRINT           "PRINT"
#define TE_T_PRINTLN         "PRINTLN"
#define TE_T_FPRINT          "FPRINT"
#define TE_T_FPRINTLN        "FPRINTLN"
#define TE_T_ENDPOINT_DECL   "ENDPOINT_DECL"
#define TE_T_BRIDGE_DECL     "BRIDGE_DECL"
#define TE_T_AGENT           "AGENT"
#define TE_T_AGENT_LIST      "AGENT_LIST"
#define TE_T_LISTENER        "LISTENER"
#define TE_T_LISTENER_LIST   "LISTENER_LIST"

/* ---- ML (dataset/model/train/predict/plot) ------------------------------ */
#define TE_T_DATASET         "DATASET"
#define TE_T_MODEL           "MODEL"
#define TE_T_LAYER           "LAYER"
#define TE_T_TRAIN           "TRAIN"
#define TE_T_TRAIN_OPTION    "TRAIN_OPTION"
#define TE_T_PREDICT         "PREDICT"
#define TE_T_PLOT            "PLOT"

/* ---- nombres de tipo declarados en .te (minúsculas) --------------------- */
#define TE_DT_INT            "int"
#define TE_DT_STRING         "string"
#define TE_DT_FLOAT          "float"
#define TE_DT_BOOL           "bool"
#define TE_DT_DECIMAL        "decimal"
#define TE_DT_UUID           "uuid"
#define TE_DT_DATETIME       "datetime"
#define TE_DT_VOID           "void"
#define TE_DT_DYNAMIC        "dynamic"
/* variantes opcionales `T?` */
#define TE_DT_INT_OPT        "int?"
#define TE_DT_STRING_OPT     "string?"
#define TE_DT_FLOAT_OPT      "float?"
#define TE_DT_BOOL_OPT       "bool?"
#define TE_DT_DECIMAL_OPT    "decimal?"
#define TE_DT_UUID_OPT       "uuid?"
#define TE_DT_DATETIME_OPT   "datetime?"

/* ---- identificadores reservados del runtime -------------------------- */
#define TE_SYM_RET           "__ret__"        /* slot del valor de retorno / resultado de builtins */
#define TE_SYM_THIS          "this"           /* receptor del método en curso */
#define TE_SYM_CTOR          "__constructor"  /* nombre interno del constructor de clase */

#endif /* TE_TYPES_H */
