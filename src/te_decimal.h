/* te_decimal.h — Tipo `decimal`: aritmética decimal EXACTA (dinero).
 *
 * Representación en el intérprete: nodo AST de tipo "DECIMAL" con el texto canónico
 * en str_value; Variable con vtype VAL_STRING y tag type "DECIMAL" (misma técnica que
 * BOOL sobre VAL_INT: ningún sitio que no conozca el tipo corrompe el valor — a lo
 * sumo lo trata como texto). Los sitios que SÍ lo conocen (aritmética, comparación,
 * JSON, binding SQL, is_string_type) usan estas primitivas.
 *
 * Interno: mantisa __int128 + escala (0..TE_DEC_MAX_SCALE). Reglas:
 *   + -  : escala = max(sa, sb)                         1.10 + 2.005 = 3.105
 *   *    : escala = sa + sb                             1.10 * 2.0   = 2.200
 *   /    : cociente exacto si termina en <= 18 decimales; si no, 18 decimales
 *          redondeados half-even; luego se recortan ceros finales SIN bajar de
 *          max(sa, sb)                                  10.00 / 4 = 2.50, 1/3 = 0.333333333333333333
 *   %    : resto con escala max(sa, sb)
 *   round(d, n): half-away-from-zero (convención contable) a n decimales.
 * Overflow (> 38 dígitos) o división por cero -> te_dec_* devuelve 0 e imprime error,
 * igual que la división por cero de int/float.
 */
#ifndef TE_DECIMAL_H
#define TE_DECIMAL_H

#include "ast.h"

#define TE_DEC_MAX_SCALE 18
#define TE_DEC_TEXT_MAX  64   /* 38 dígitos + signo + punto + NUL */

typedef struct TeDec { __int128 m; int scale; } TeDec;

int  te_dec_parse(const char *s, TeDec *out);            /* "12.50", "-3", "1e2" NO; 1 ok / 0 texto inválido */
void te_dec_format(const TeDec *d, char *buf, size_t cap); /* texto canónico ("-0.50" -> "-0.50", "0" -> "0") */
int  te_dec_binop(NodeKind k, const TeDec *a, const TeDec *b, TeDec *out); /* ADD SUB MUL DIV MOD; comparaciones -> m=0/1 scale 0 */
void te_dec_neg(TeDec *d);
int  te_dec_cmp(const TeDec *a, const TeDec *b);         /* -1 / 0 / 1 exacto */
void te_dec_round(const TeDec *d, int n, TeDec *out);    /* half-away-from-zero a n decimales (n >= 0) */
double te_dec_to_double(const TeDec *d);

/* ---- integración con el walker ---- */
/* 1 si el subárbol contiene un operando decimal (literal DECIMAL, variable/atributo
 * DECIMAL, o llamada decimal(...)): entonces la expresión se evalúa exacta. */
int  te_dec_expr_has_decimal(ASTNode *node);
/* Evalúa exacto `node` (aritmética/negación/paréntesis sobre decimales, ints, floats
 * literales y variables). 1 ok (out = texto canónico) / 0 no evaluable -> caller cae al
 * camino double. */
int  te_dec_eval(ASTNode *node, char *out, size_t cap);
/* Igual, pero devuelve double (comparaciones exactas -> 0/1; aritmética -> strtod del texto exacto). */
int  te_dec_eval_double(ASTNode *node, double *out);
/* Variable con tag DECIMAL. */
int  te_var_is_decimal(const Variable *v);
/* Nodo hoja DECIMAL nuevo (str_value copiado). */
ASTNode *te_dec_leaf(const char *text);
/* Para binding de argumentos: si `arg` es un literal/variable/expresión decimal devuelve
 * un leaf DECIMAL nuevo con su valor exacto; si no, NULL (el caller sigue su cadena). */
ASTNode *te_dec_arg_leaf(ASTNode *arg);
/* Builtins: decimal(x) y métodos .round(n) / .to_float() / .to_string() sobre decimales. 1 = manejado. */
int  te_dec_builtin(const char *fn, ASTNode *args);
int  te_dec_method_dispatch(ASTNode *node, ASTNode *objNode, Variable *v);

#endif /* TE_DECIMAL_H */
