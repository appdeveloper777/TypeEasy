// Implementación básica de native_orm_query
#include "ast.h"
#include "db_params.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h> // For strcasecmp

extern char* mysql_escape_cb(const char* in, void* ctx);
extern void* mysql_get_conn(int conn_id);

/* Ejecuta un SELECT ya finalizado (con @params sustituidos) y mapea cada
 * fila a una instancia de `cls`, dejando la lista de objetos en __ret__.
 *
 * Usa el fast path streaming + arena/pool del bridge MySQL, que escala a
 * 1M+ filas (mismo modelo que from_csv_to_list).
 */
void orm_run_mapped_query(int conn_id, const char* query, ClassNode* cls) {
    if (!cls || !query) return;
    extern ASTNode* mysql_query_to_objects_fast(int conn_id, const char* query, ClassNode* cls);
    ASTNode* list_node = mysql_query_to_objects_fast(conn_id, query, cls);
    if (!list_node) return;
    add_or_update_variable("__ret__", list_node);
}

void native_orm_query(ASTNode* args) {
    //printf("[ORM] native_orm_query ejecutado\n"); fflush(stdout);
    // Espera: (conn_id, query, class[, params])
    if (!args) {
        printf("[ORM] args es NULL\n");
        return;
    }
    
    // Extraer argumentos
    int conn_id = -1;
    const char* query = NULL;
    const char* class_name = NULL;
    ASTNode* curr = args;
    
    // conn_id
    if (curr) {
        if (curr->type && strcmp(curr->type, "NUMBER") == 0) {
            conn_id = curr->value;
        } else if (curr->type && strcmp(curr->type, "IDENTIFIER") == 0) {
            Variable* v = find_variable(curr->id);
            if (v && v->vtype == VAL_INT) conn_id = v->value.int_value;
        }
        curr = curr->right;
    }
    
    // query
    if (curr) {
        if (curr->type && strcmp(curr->type, "STRING") == 0) {
            query = curr->str_value;
        } else if (curr->type && strcmp(curr->type, "IDENTIFIER") == 0) {
            Variable* v = find_variable(curr->id);
            if (v && v->vtype == VAL_STRING) query = v->value.string_value;
        }
        curr = curr->right;
    }
    
    // class
    if (curr) {
        if (curr->type && (strcmp(curr->type, "IDENTIFIER") == 0 || strcmp(curr->type, "ID") == 0)) {
            class_name = curr->id ? curr->id : curr->str_value;
        }
    }

    /* Estilo Dapper: arg #3 (índice 3) puede ser un MAP literal o instancia de clase con los @params. */
    int params_owned = 0;
    ASTNode* params_head = db_arg_as_map_head(args, 3, &params_owned);
    char* final_query = NULL;
    if (params_head && query) {
        void* conn = mysql_get_conn(conn_id);
        final_query = db_substitute_params(query, params_head, mysql_escape_cb, conn);
        if (final_query) query = final_query;
        if (params_owned) { free_ast(params_head); params_head = NULL; }
    }
    
    printf("[ORM] Argumentos finales: conn_id=%d, query=%s, class_name=%s\n", conn_id, query ? query : "NULL", class_name ? class_name : "NULL");
    fflush(stdout);
    
    // Buscar clase destino
    extern ClassNode* find_class(char* name);
    ClassNode* cls = NULL;
    if (class_name) {
        cls = find_class((char*)class_name);
    }
    if (!cls) {
        printf("[ORM] Clase destino '%s' no encontrada\n", class_name ? class_name : "NULL");
        if (final_query) free(final_query);
        return;
    }

    orm_run_mapped_query(conn_id, query, cls);
    if (final_query) free(final_query);
}