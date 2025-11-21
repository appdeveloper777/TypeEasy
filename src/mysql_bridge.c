#include "mysql_bridge.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pool de conexiones MySQL (máximo 10 conexiones concurrentes)
static MYSQL* connections[10] = {NULL};
static int next_conn_id = 0;

// Prototipos de funciones auxiliares de ast.c
extern ASTNode* create_ast_leaf(char *type, int value, char *str_value, char *id);
extern void free_ast(ASTNode *node);

// Helper: Extrae argumento string de un nodo de argumentos
static const char* get_arg_string(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) {
        current = current->right;
    }
    if (!current) return NULL;
    
    // Primero verificar si el nodo actual es directamente un STRING
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) {
        return current->str_value;
    }
    
    // Si no, verificar si tiene un left que sea STRING_LITERAL
    if (current->left && current->left->type && 
        strcmp(current->left->type, "STRING_LITERAL") == 0 && 
        current->left->str_value) {
        return current->left->str_value;
    }
    
    return NULL;
}

// Helper: Extrae argumento int de un nodo de argumentos
static int get_arg_int(ASTNode* args, int index) {
    ASTNode* current = args;
    for (int i = 0; i < index && current; i++) {
        current = current->right;
    }
    if (!current) return -1;
    
    // Primero verificar si el nodo actual es directamente un NUMBER
    if (current->type && strcmp(current->type, "NUMBER") == 0) {
        return current->value;
    }
    
    // Si no, verificar si tiene un left que sea NUMBER
    if (current->left && current->left->type && 
        strcmp(current->left->type, "NUMBER") == 0) {
        return current->left->value;
    }
    
    // Si es un identificador, buscar la variable
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        Variable* v = find_variable(current->id);
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }
    
    // También verificar current->left si es IDENTIFIER
    if (current->left && current->left->type && 
        strcmp(current->left->type, "IDENTIFIER") == 0 && 
        current->left->id) {
        Variable* v = find_variable(current->left->id);
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }
    
    return -1;
}

// native_mysql_connect(host, user, password, database)
// Retorna connection_id en __ret__
void native_mysql_connect(ASTNode* args) {
    // Debug: imprimir estructura de argumentos
    printf("[MySQL DEBUG] native_mysql_connect called\n");
    printf("[MySQL DEBUG] args=%p\n", (void*)args);
    if (args) {
        printf("[MySQL DEBUG] args->type=%s\n", args->type ? args->type : "NULL");
        printf("[MySQL DEBUG] args->left=%p\n", (void*)args->left);
        printf("[MySQL DEBUG] args->right=%p\n", (void*)args->right);
        if (args->left) {
            printf("[MySQL DEBUG] args->left->type=%s\n", args->left->type ? args->left->type : "NULL");
            printf("[MySQL DEBUG] args->left->str_value=%s\n", args->left->str_value ? args->left->str_value : "NULL");
        }
    }
    
    const char* host = get_arg_string(args, 0);
    const char* user = get_arg_string(args, 1);
    const char* password = get_arg_string(args, 2);
    const char* database = get_arg_string(args, 3);
    
    printf("[MySQL DEBUG] host=%s, user=%s, password=%s, database=%s\n", 
           host ? host : "NULL", 
           user ? user : "NULL", 
           password ? password : "NULL", 
           database ? database : "NULL");
    
    int port = 3308; // valor por defecto
    // Si hay un quinto argumento, úsalo como puerto
    if (get_arg_int(args, 4) > 0) {
        port = get_arg_int(args, 4);
    }
    if (!host || !user || !password || !database) {
        printf("[MySQL] Error: Argumentos inválidos para mysql_connect\n");
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        printf("[MySQL] Error: mysql_init() falló\n");
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    if (!mysql_real_connect(conn, host, user, password, database, port, NULL, 0)) {
        printf("[MySQL] Error de conexión: %s\n", mysql_error(conn));
        mysql_close(conn);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    // Guardar conexión en el pool
    int conn_id = next_conn_id % 10;
    if (connections[conn_id]) {
        mysql_close(connections[conn_id]);
    }
    connections[conn_id] = conn;
    next_conn_id++;
    
    printf("[MySQL] Conexión exitosa (ID: %d)\n", conn_id);
    ASTNode* ret_node = create_ast_leaf("NUMBER", conn_id, NULL, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
}

// native_mysql_query(connection_id, query_string)
// Retorna JSON string en __ret__
void native_mysql_query(ASTNode* args) {
    int conn_id = get_arg_int(args, 0);
    const char* query = get_arg_string(args, 1);
    
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
        printf("[MySQL] Error: Conexión inválida (ID: %d)\n", conn_id);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_connection\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    if (!query) {
        printf("[MySQL] Error: Query inválido\n");
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_query\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL* conn = connections[conn_id];
    
    if (mysql_query(conn, query)) {
        printf("[MySQL] Error en query: %s\n", mysql_error(conn));
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"error\":\"%s\"}", mysql_error(conn));
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup(error_msg), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        // Query sin resultados (INSERT, UPDATE, DELETE)
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"affected_rows\":0}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    // Convertir resultado a JSON
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    
    // Buffer para construir JSON (máximo 64KB)
    char* json_buffer = malloc(65536);
    int offset = 0;
    offset += snprintf(json_buffer + offset, 65536 - offset, "[");
    
    MYSQL_ROW row;
    int first_row = 1;
    while ((row = mysql_fetch_row(result))) {
        if (!first_row) {
            offset += snprintf(json_buffer + offset, 65536 - offset, ",");
        }
        first_row = 0;
        
        offset += snprintf(json_buffer + offset, 65536 - offset, "{");
        for (int i = 0; i < num_fields; i++) {
            if (i > 0) {
                offset += snprintf(json_buffer + offset, 65536 - offset, ",");
            }
            offset += snprintf(json_buffer + offset, 65536 - offset, "\"%s\":", fields[i].name);
            
            if (row[i]) {
                // Escapar comillas en strings
                if (fields[i].type == MYSQL_TYPE_STRING || 
                    fields[i].type == MYSQL_TYPE_VAR_STRING ||
                    fields[i].type == MYSQL_TYPE_VARCHAR) {
                    offset += snprintf(json_buffer + offset, 65536 - offset, "\"%s\"", row[i]);
                } else {
                    offset += snprintf(json_buffer + offset, 65536 - offset, "%s", row[i]);
                }
            } else {
                offset += snprintf(json_buffer + offset, 65536 - offset, "null");
            }
        }
        offset += snprintf(json_buffer + offset, 65536 - offset, "}");
    }
    
    offset += snprintf(json_buffer + offset, 65536 - offset, "]");
    
    mysql_free_result(result);
    
    printf("[MySQL] Query ejecutado exitosamente\n");
    ASTNode* ret_node = create_ast_leaf("STRING", 0, json_buffer, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
}

// native_mysql_close(connection_id)
void native_mysql_close(ASTNode* args) {
    int conn_id = get_arg_int(args, 0);
    
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
        printf("[MySQL] Error: Conexión inválida (ID: %d)\n", conn_id);
        return;
    }
    
    mysql_close(connections[conn_id]);
    connections[conn_id] = NULL;
    printf("[MySQL] Conexión cerrada (ID: %d)\n", conn_id);
}
