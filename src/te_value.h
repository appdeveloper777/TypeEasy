/* te_value.h — Modelo de valores unificado (Fase E) + aritmética entera exacta (Fase 1b).
 *
 * Un TeValue es una Variable SIN identidad (id == NULL): el mismo layout (vtype + tag `type`
 * + union) que ya usan vars[], atributos de clase y __ret__, con ownership explícito del
 * string/tag. `te_eval_value()` evalúa CUALQUIER expresión a un TeValue — literales, variables,
 * aritmética (int64 exacto / decimal / float), concatenación, comparaciones (BOOL), ternario,
 * `??`, acceso a atributos e índices, llamadas, literales de lista/objeto/lambda, `new`.
 * Es el ÚNICO camino que usan declaración, asignación, return y binding de argumentos; antes
 * cada uno reimplementaba (parcialmente) esta tabla y las diferencias eran los gotchas
 * ("se guardó 0", "el string llegó vacío", "el bool salió como 1").
 *
 * Contrato de ownership: el TeValue producido es dueño de `type` y, si vtype==VAL_STRING, de
 * `string_value`. Los contenedores (LIST/MAP/OBJECT/LAMBDA) se ALIASAN (object_value apunta a
 * memoria que vive en el programa/request), igual que en todo el intérprete.
 */
#ifndef TE_VALUE_H
#define TE_VALUE_H

#include "ast.h"

typedef Variable TeValue;

/* --- ciclo de vida ------------------------------------------------------- */
void te_val_init(TeValue *v);                           /* INT 0, sin tag */
void te_val_free(TeValue *v);                           /* libera tag + string; deja INT 0 */
void te_val_copy(TeValue *dst, const Variable *src);    /* deep copy escalares, alias contenedores */
void te_val_move_into(Variable *dst, TeValue *src);     /* dst toma ownership (conserva id/is_const); src queda vacío */

/* --- constructores -------------------------------------------------------- */
void te_val_set_int(TeValue *v, long long i);
void te_val_set_bool(TeValue *v, int b);
void te_val_set_float(TeValue *v, double d);
void te_val_set_string(TeValue *v, const char *s);      /* copia s */
void te_val_set_string_tag(TeValue *v, const char *tag, const char *s);   /* STRING/DATETIME/UUID/DECIMAL */
void te_val_set_null(TeValue *v);
void te_val_set_ref(TeValue *v, const char *tag, void *ref);   /* LIST/MAP/OBJECT/LAMBDA/LAZY_ITER */

/* --- consultas ------------------------------------------------------------ */
int  te_val_is_null(const Variable *v);
const char *te_val_tag(const Variable *v);              /* nunca NULL: INT/FLOAT/STRING/BOOL/... */
/* Representación en CONTEXTO STRING (concat, print): malloc'd. BOOL -> "1"/"0" (histórico),
 * FLOAT shortest round-trip, DECIMAL texto canónico, null -> "null", LIST -> "[a, b]", MAP -> JSON. */
char *te_var_to_string(const Variable *v);

/* --- evaluación ------------------------------------------------------------ */
/* Evalúa `node` a un valor fresco en *out. Devuelve 1 siempre que produjo un valor (los errores
 * de runtime se reportan como hasta ahora y producen INT 0 / NULL según el caso); 0 solo si
 * node es NULL. Las llamadas (CALL_*) se ejecutan y su resultado se copia de __ret__. */
int  te_eval_value(ASTNode *node, TeValue *out);

/* Convierte un nodo de DATOS (item de lista, valor de KV_PAIR, hoja de json_parse) a valor. */
void te_leaf_to_value(ASTNode *leaf, TeValue *out);

/* Materializa un valor como nodo hoja (STRING/NUMBER/BOOL/FLOAT/DECIMAL/NULL frescos;
 * LIST/MAP/LAMBDA devuelven el propio nodo aliasado; OBJECT un wrapper fresco). */
ASTNode *te_val_to_leaf(const Variable *v);

/* __ret__ = *v (mueve). */
void te_set_ret_value(TeValue *v);

/* Binding de argumentos: evalúa cada arg con te_eval_value y lo liga al parámetro (coerción
 * int/float por tipo declarado). te_bind_param liga UN valor a un nombre sombreando el slot
 * externo dentro del frame actual (ast.c). */
void te_bind_args(ParameterNode *p, ASTNode *args);
void te_bind_param(const char *name, TeValue *v);
/* Declaración let/var: slot en el scope actual (te_decl_slot) + mueve *v (ast.c). */
void te_declare_value(const char *id, TeValue *v, int is_const);

/* --- Fase 1b: aritmética entera exacta (se mantiene; te_eval_value la usa) --- */
int te_eval_i64(ASTNode *node, long long *out);
int te_eval_num(ASTNode *node, long long *iv, double *dv);

#endif /* TE_VALUE_H */
