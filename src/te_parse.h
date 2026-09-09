/* te_parse.h — Contexto de UNA pasada de parseo (lexer flex reentrante + parser
 * bison puro). Antes este estado eran globales de proceso en parser.l/parser.y
 * (root, last_class, flags @auth/@guard/async, bloques on_open/on_message,
 * pila de imports, lista de importados, último token), lo que impedía parsear
 * dos programas a la vez y arrastraba residuos entre parses abortados. Ahora
 * parse_file() crea un TeParseCtx + un scanner por llamada y los destruye al
 * salir: no hay nada que "resetear" entre parses. */
#ifndef TE_PARSE_H
#define TE_PARSE_H

#include <stdio.h>
#include "ast.h"

#define TE_MAX_INCLUDE_DEPTH 10

typedef struct TeImportedFile {
    char *path;
    struct TeImportedFile *next;
} TeImportedFile;

typedef struct TeParseCtx {
    /* --- parser (parser.y) --- */
    ASTNode   *root;
    ClassNode *last_class;
    int   pending_auth;          /* @auth sobre el próximo endpoint_method */
    int   endpoint_auth_all;     /* @auth a nivel de bloque endpoint */
    int   pending_async;         /* el lexer tragó un `async` tras ']' */
    char *endpoint_guard_all;    /* @<guard> a nivel de bloque endpoint */
    char *pending_guard;         /* @<guard> sobre el próximo endpoint_method */
    int      ws_is_lifecycle;    /* [WebSocket] con on_open/on_message/on_close */
    ASTNode *ws_on_open, *ws_on_message, *ws_on_close;
    char    *ws_msg_param;
    /* --- lexer (parser.l) --- */
    void  *include_stack[TE_MAX_INCLUDE_DEPTH];      /* YY_BUFFER_STATE */
    FILE  *include_file_stack[TE_MAX_INCLUDE_DEPTH];
    int    include_line_stack[TE_MAX_INCLUDE_DEPTH];
    int    include_file_id_stack[TE_MAX_INCLUDE_DEPTH];
    int    include_stack_ptr;
    TeImportedFile *imported;    /* dedup de imports dentro de ESTE parse */
    int    prev_lex_tok;         /* para `]` + `async` y demotion tras `.` */
} TeParseCtx;

/* Parsea `file` completo en la VM actual (clases/endpoints se registran en g_vm).
 * Devuelve el AST o NULL si hubo error de sintaxis. Reentrante: cada llamada
 * usa su propio scanner y contexto. */
ASTNode *parse_file(FILE *file);

#endif /* TE_PARSE_H */
