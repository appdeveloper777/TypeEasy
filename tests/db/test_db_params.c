/* test_db_params.c — regresión unitaria para db_substitute_params (estilo Dapper).
 *
 * Por qué un test en C y no un .te: db_substitute_params() es el motor de
 * binding de @placeholders que comparten los bridges MySQL/Postgres/SQL Server.
 * Probarlo desde TypeEasy requeriría una conexión a un servidor real (no hay
 * builtin que exponga el SQL final), así que la suite de lenguaje (tests/lang)
 * no puede cubrirlo en CI. Este harness enlaza SOLO db_params.c con stubs
 * mínimos de las funciones de ast.c que NO se ejercitan aquí, y verifica el
 * SQL generado para cada tipo de parámetro.
 *
 * BUG cubierto (jun 5 2026): un parámetro flotante literal `{ "@price": 19.9 }`
 * se interpolaba como NULL porque append_value() no tenía rama para el leaf
 * "FLOAT" (el parser guarda el número en str_value, value=0) y caía al default
 * (kind=4 -> NULL). Afectaba INSERT/UPDATE de la doc MySQL. Fix: rama FLOAT.
 *
 * Build/run: bash tests/db/run.sh   (gcc, sin DB, sin servidor).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_params.h"   /* ASTNode, db_substitute_params */

/* ── Stubs de ast.c ──────────────────────────────────────────────────────
 * db_params.o referencia estos símbolos desde db_arg_as_map_head(), que este
 * test NO invoca. Los definimos mínimamente para resolver el enlace sin
 * arrastrar todo el intérprete (ast.c).
 */
ASTNode *create_ast_leaf(char *type, int value, char *str_value, char *id) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    n->type = type ? strdup(type) : NULL;
    n->value = value;
    n->str_value = str_value ? strdup(str_value) : NULL;
    n->id = id ? strdup(id) : NULL;
    return n;
}
ASTNode *create_kv_pair_node(char *key, ASTNode *value) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    n->type = strdup("KV_PAIR");
    n->id = key ? strdup(key) : NULL;
    n->left = value;
    return n;
}
Variable *find_variable(char *name) { (void)name; return NULL; }

/* db_params.c llama get_node_string() para valores que son llamadas inline
 * (CALL_FUNC/CALL_METHOD, p.ej. now()/uuid_v4()). En el intérprete real ejecuta
 * la llamada y lee __ret__; aquí, sin runtime, devolvemos el str_value del leaf
 * para poder ejercitar la rama de forma determinista. */
char *get_node_string(ASTNode *node) {
    if (node && node->str_value) return strdup(node->str_value);
    return strdup("");
}

/* ── Escape de prueba: duplica la comilla simple (estándar SQL) ──────────── */
static char *test_escape(const char *in, void *ctx) {
    (void)ctx;
    if (!in) return strdup("");
    size_t n = 0; for (const char *p = in; *p; p++) n += (*p == '\'') ? 2 : 1;
    char *out = (char *)malloc(n + 1);
    char *w = out;
    for (const char *p = in; *p; p++) { if (*p == '\'') *w++ = '\''; *w++ = *p; }
    *w = '\0';
    return out;
}

/* Encadena pares { key: leaf } por ->right. */
static ASTNode *pair(const char *key, ASTNode *val, ASTNode *next) {
    ASTNode *p = create_kv_pair_node((char *)key, val);
    p->right = next;
    return p;
}

static int g_fail = 0;
static void check(const char *name, const char *got, const char *want) {
    if (got && strcmp(got, want) == 0) {
        printf("PASS  %-22s -> %s\n", name, got);
    } else {
        printf("FAIL  %-22s\n      got:  %s\n      want: %s\n",
               name, got ? got : "(null)", want);
        g_fail = 1;
    }
}

int main(void) {
    /* 1) int */
    {
        ASTNode *params = pair("@n", create_ast_leaf("NUMBER", 5, NULL, NULL), NULL);
        char *sql = db_substitute_params("SELECT * FROM t WHERE id = @n", params, test_escape, NULL);
        check("int_param", sql, "SELECT * FROM t WHERE id = 5");
        free(sql);
    }
    /* 2) string con comilla -> escapado y entre comillas simples */
    {
        ASTNode *params = pair("@name", create_ast_leaf("STRING", 0, "O'Reilly Book", NULL), NULL);
        char *sql = db_substitute_params("INSERT INTO t(name) VALUES (@name)", params, test_escape, NULL);
        check("string_escape", sql, "INSERT INTO t(name) VALUES ('O''Reilly Book')");
        free(sql);
    }
    /* 3) FLOAT literal -> el bug: antes interpolaba NULL, ahora 19.9 */
    {
        ASTNode *params = pair("@price", create_ast_leaf("FLOAT", 0, "19.9", NULL), NULL);
        char *sql = db_substitute_params("UPDATE t SET price = @price WHERE id = 1", params, test_escape, NULL);
        check("float_param", sql, "UPDATE t SET price = 19.9 WHERE id = 1");
        free(sql);
    }
    /* 4) NULL -> NULL */
    {
        ASTNode *params = pair("@x", create_ast_leaf("NULL", 0, NULL, NULL), NULL);
        char *sql = db_substitute_params("SELECT @x", params, test_escape, NULL);
        check("null_param", sql, "SELECT NULL");
        free(sql);
    }
    /* 5) mezcla int + string + float (INSERT de la doc) */
    {
        ASTNode *params =
            pair("@name", create_ast_leaf("STRING", 0, "O'Reilly Book", NULL),
            pair("@price", create_ast_leaf("FLOAT", 0, "19.9", NULL), NULL));
        char *sql = db_substitute_params(
            "INSERT INTO products (name, price) VALUES (@name, @price)",
            params, test_escape, NULL);
        check("mixed_insert", sql,
              "INSERT INTO products (name, price) VALUES ('O''Reilly Book', 19.9)");
        free(sql);
    }
    /* 6) literal SQL con @ dentro de comillas NO se sustituye */
    {
        ASTNode *params = pair("@n", create_ast_leaf("NUMBER", 7, NULL, NULL), NULL);
        char *sql = db_substitute_params("SELECT '@n' , @n", params, test_escape, NULL);
        check("at_inside_string", sql, "SELECT '@n' , 7");
        free(sql);
    }
    /* 7) BOOL literal -> entero 1/0 (antes caía a NULL) */
    {
        ASTNode *params =
            pair("@active", create_ast_leaf("BOOL", 1, NULL, NULL),
            pair("@deleted", create_ast_leaf("BOOL", 0, NULL, NULL), NULL));
        char *sql = db_substitute_params(
            "UPDATE t SET active = @active, deleted = @deleted WHERE id = 1",
            params, test_escape, NULL);
        check("bool_param", sql,
              "UPDATE t SET active = 1, deleted = 0 WHERE id = 1");
        free(sql);
    }
    /* 8) llamada inline (CALL_FUNC, p.ej. now()/uuid_v4()) -> el bug: antes
     *    caía a NULL; ahora get_node_string la evalúa y se escapa+entrecomilla.
     *    El stub get_node_string devuelve el str_value del leaf. */
    {
        ASTNode *params =
            pair("@c", create_ast_leaf("CALL_FUNC", 0, "2026-01-01T00:00:00Z", NULL),
            pair("@u", create_ast_leaf("CALL_FUNC", 0, "1d360e11-94d5-4a91-9785-67b134f3911a", NULL), NULL));
        char *sql = db_substitute_params(
            "INSERT INTO ev (created, uid) VALUES (@c, @u)",
            params, test_escape, NULL);
        check("inline_call_param", sql,
              "INSERT INTO ev (created, uid) VALUES ('2026-01-01T00:00:00Z', '1d360e11-94d5-4a91-9785-67b134f3911a')");
        free(sql);
    }
    /* 9) leaf "INT" (lo que produce json_parse para enteros) -> entero, NO NULL.
     *    Bug: un objeto reusado como bind-params, p.ej.
     *    json_parse(mysql_query(...,"json"))[0], enlazaba sus columnas
     *    numéricas como SQL NULL porque append_value solo conocía "NUMBER"
     *    y los enteros de te_json son leaves "INT". Mezclamos con un string
     *    para reflejar el caso real (activo numérico + nombre string). */
    {
        ASTNode *params =
            pair("@activo", create_ast_leaf("INT", 0, NULL, NULL),
            pair("@nombre", create_ast_leaf("STRING", 0, "x", NULL), NULL));
        char *sql = db_substitute_params(
            "INSERT INTO t (activo, nombre) VALUES (@activo, @nombre)",
            params, test_escape, NULL);
        check("int_leaf_param", sql,
              "INSERT INTO t (activo, nombre) VALUES (0, 'x')");
        free(sql);
    }
    /* 10) ACCESS_EXPR (indexación inline `row["k"]` / `arr[i]`) como valor de
     *     param. Se resuelve por get_node_string con detección numérica:
     *     numérico -> sin comillas; texto -> entrecomillado; vacío -> NULL.
     *     El stub get_node_string devuelve str_value del leaf. */
    {
        ASTNode *params =
            pair("@a", create_ast_leaf("ACCESS_EXPR", 0, "0", NULL),      /* numérico */
            pair("@b", create_ast_leaf("ACCESS_EXPR", 0, "O'Hara", NULL), /* texto con comilla */
            pair("@c", create_ast_leaf("ACCESS_EXPR", 0, NULL, NULL),     /* vacío -> NULL */
            NULL)));
        char *sql = db_substitute_params(
            "INSERT INTO t (a, b, c) VALUES (@a, @b, @c)",
            params, test_escape, NULL);
        check("access_expr_param", sql,
              "INSERT INTO t (a, b, c) VALUES (0, 'O''Hara', NULL)");
        free(sql);
    }

    if (g_fail) { printf("\nRESULT: FAIL\n"); return 1; }
    printf("\nRESULT: OK (10/10)\n");
    return 0;
}
