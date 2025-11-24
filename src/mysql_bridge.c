#include "mysql_bridge.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pool de conexiones MySQL (máximo 10 conexiones concurrentes)
static MYSQL* connections[10] = {NULL};
static int next_conn_id = 0;

// Helper: Escape XML special characters
static void xml_escape(const char* input, char* output, size_t output_size) {
    size_t out_pos = 0;
    for (size_t i = 0; input[i] != '\0' && out_pos < output_size - 6; i++) {
        switch (input[i]) {
            case '&':
                if (out_pos + 5 < output_size) {
                    strcpy(output + out_pos, "&amp;");
                    out_pos += 5;
                }
                break;
            case '<':
                if (out_pos + 4 < output_size) {
                    strcpy(output + out_pos, "&lt;");
                    out_pos += 4;
                }
                break;
            case '>':
                if (out_pos + 4 < output_size) {
                    strcpy(output + out_pos, "&gt;");
                    out_pos += 4;
                }
                break;
            case '"':
                if (out_pos + 6 < output_size) {
                    strcpy(output + out_pos, "&quot;");
                    out_pos += 6;
                }
                break;
            case '\'':
                if (out_pos + 6 < output_size) {
                    strcpy(output + out_pos, "&apos;");
                    out_pos += 6;
                }
                break;
            default:
                output[out_pos++] = input[i];
                break;
        }
    }
    output[out_pos] = '\0';
}

// Devuelve lista de resultados para ORM
ASTNode* mysql_query_result(int conn_id, const char* query) {
    //printf("[ORM] mysql_query_result: conn_id=%d, query=%s\n", conn_id, query ? query : "NULL");
   // fflush(stdout);
    
    // Validar conexión
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
        printf("[ORM] Error: Conexión inválida (ID: %d)\n", conn_id);
        return NULL;
    }
    
    MYSQL* conn = connections[conn_id];
    
    // Ejecutar query
    if (mysql_query(conn, query)) {
        printf("[ORM] Error en query: %s\n", mysql_error(conn));
        return NULL;
    }
    
    // Obtener resultados
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        printf("[ORM] No hay resultados para la consulta\n");
        return NULL;
    }
    
    int num_fields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    
    //printf("[ORM] Query ejecutado exitosamente. Campos: %d\n", num_fields);
    
    // Construir lista de resultados
    ASTNode* first_row = NULL;
    ASTNode* last_row = NULL;
    MYSQL_ROW row;
    
    while ((row = mysql_fetch_row(res))) {
        // Crear nodo de argumentos para esta fila
        ASTNode* row_args = NULL;
        
        // Debug: mostrar campos de la primera fila
        /*if (!first_row) {
            printf("[MySQL DEBUG] Campos de la primera fila:\n");
            for (int i = 0; i < num_fields; i++) {
                printf("  Campo %d: name='%s', value='%s', type=%d\n", 
                       i, fields[i].name, row[i] ? row[i] : "NULL", fields[i].type);
            }
        }*/
        for (int i = 0; i < num_fields; i++) {
            const char* field_name = fields[i].name;
            const char* field_value = row[i] ? row[i] : "";
            ASTNode* field_node = NULL;
            if (IS_NUM(fields[i].type)) {
                int int_value = row[i] ? atoi(row[i]) : 0;
                field_node = create_ast_leaf("NUMBER", int_value, NULL, (char*)field_name);
            } else {
                field_node = create_ast_leaf("STRING", 0, (char*)field_value, (char*)field_name);
            }
            row_args = append_to_list(row_args, field_node);
        }
        ASTNode* row_node = create_ast_node("ARGS", row_args, NULL);
        if (!first_row) {
            first_row = row_node;
            last_row = row_node;
        } else {
            last_row->right = row_node;
            last_row = row_node;
        }
    }
    
    mysql_free_result(res);
    
    //printf("[ORM] Resultados procesados exitosamente\n");
   // printf("[MySQL DEBUG] mysql_query_result returning: first_row=%p\n", (void*)first_row);
    //fflush(stdout);
    
    return first_row;
}

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

    // Si es un número
    if (current->type && strcmp(current->type, "NUMBER") == 0) {
        return current->value;
    }

    // Si es un string que representa un número
    if (current->type && strcmp(current->type, "STRING") == 0 && current->str_value) {
        char* endptr = NULL;
        long val = strtol(current->str_value, &endptr, 10);
        if (endptr && *endptr == '\0') {
            // Es un número válido
            return (int)val;
        } else {
            // No es un número, intentar buscar como nombre de variable
            //printf("[DEBUG] String no numérico '%s', buscando como variable\n", current->str_value);
            Variable* v = find_variable(current->str_value);
            if (v) {
               // printf("[DEBUG] Variable encontrada: %s, tipo=%d, valor=%d\n", current->str_value, v->vtype, v->value.int_value);
                if (v->vtype == VAL_INT) {
                    return v->value.int_value;
                }
            }
            /*
            else {
                printf("[DEBUG] Variable NO encontrada: %s\n", current->str_value);
            }*/
        }
    }

    // Si no, verificar si tiene un left que sea NUMBER
    if (current->left && current->left->type && strcmp(current->left->type, "NUMBER") == 0) {
        return current->left->value;
    }

    // Si el left es string numérico
    if (current->left && current->left->type && strcmp(current->left->type, "STRING") == 0 && current->left->str_value) {
        char* endptr = NULL;
        long val = strtol(current->left->str_value, &endptr, 10);
        if (endptr && *endptr == '\0') {
            return (int)val;
        } else {
            // No es un número, intentar buscar como nombre de variable
            Variable* v = find_variable(current->left->str_value);
            if (v && v->vtype == VAL_INT) {
                return v->value.int_value;
            }
        }
    }

    // Si es un identificador, buscar la variable
    if (current->type && strcmp(current->type, "IDENTIFIER") == 0 && current->id) {
        printf("[DEBUG] Buscando variable IDENTIFIER: %s\n", current->id);
        Variable* v = find_variable(current->id);
        /*if (v) {
            printf("[DEBUG] Variable encontrada: %s, tipo=%d, valor=%d\n", current->id, v->vtype, v->value.int_value);
        } else {
            printf("[DEBUG] Variable NO encontrada: %s\n", current->id);
        }*/
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }

    // También verificar current->left si es IDENTIFIER
    if (current->left && current->left->type && strcmp(current->left->type, "IDENTIFIER") == 0 && current->left->id) {
        Variable* v = find_variable(current->left->id);
        if (v && v->vtype == VAL_INT) {
            return v->value.int_value;
        }
    }

    return -1;
}

// native_mysql_connect(host, user, password, database, [port])
// Retorna connection_id en __ret__
void native_mysql_connect(ASTNode* args) {
        // Imprimir todos los argumentos recibidos
        ASTNode* curr = args;
        int idx = 0;
        while (curr) {
            //printf("[DEBUG][native_mysql_connect] Arg #%d: type=%s, id=%s, str_value=%s, value=%d\n", idx, curr->type ? curr->type : "NULL", curr->id ? curr->id : "NULL", curr->str_value ? curr->str_value : "NULL", curr->value);
            curr = curr->right;
            idx++;
        }
    // Debug: imprimir estructura de argumentos
   // printf("[MySQL DEBUG] native_mysql_connect called\n");
   // printf("[MySQL DEBUG] args=%p\n", (void*)args);
    //printf("[MySQL] native_mysql_connect called\n"); fflush(stdout);
    
    const char* host = get_arg_string(args, 0);
    const char* user = get_arg_string(args, 1);
    const char* pass = get_arg_string(args, 2);
    const char* db   = get_arg_string(args, 3);
    int port = get_arg_int(args, 4);  // Optional 5th parameter

    // Debug: imprimir los valores recibidos
   /* printf("[MySQL DEBUG] Valores recibidos en native_mysql_connect:\n");
    printf("  host: '%s'\n", host ? host : "NULL");
    printf("  user: '%s'\n", user ? user : "NULL");
    printf("  pass: '%s'\n", pass ? pass : "NULL");
    printf("  db:   '%s'\n", db ? db : "NULL");
    printf("  port: %d\n", port);
    fflush(stdout);*/

    // If port is -1 (not provided or invalid), use default 3306
    if (port <= 0) {
        port = 3306;
    }

   /* printf("[MySQL] Args extracted: host=%s, user=%s, pass=%s, db=%s, port=%d\n", 
           host ? host : "NULL", 
           user ? user : "NULL", 
           pass ? pass : "NULL", 
           db ? db : "NULL",
           port);
    fflush(stdout);*/

    if (!host || !user || !pass || !db) {
        //printf("[MySQL] Error: Argumentos inválidos para mysql_connect\n"); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    if (next_conn_id >= 10) {
       // printf("[MySQL] Error: Max conexiones alcanzado\n"); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        //printf("[MySQL] Error: mysql_init failed\n"); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    // Habilitar SSL (necesario para TiDB Cloud y otros servicios seguros)
    mysql_ssl_set(conn, NULL, NULL, NULL, NULL, NULL);

    if (!mysql_real_connect(conn, host, user, pass, db, port, NULL, CLIENT_SSL)) {
        //printf("[MySQL] Error de conexión: %s\n", mysql_error(conn)); fflush(stdout);
        ASTNode* ret_node = create_ast_leaf("NUMBER", -1, NULL, NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        mysql_close(conn);
        return;
    }
    
    int conn_id = next_conn_id;
    connections[conn_id] = conn;
    next_conn_id++;
    
    //printf("[MySQL] Conexión exitosa (ID: %d)\n", conn_id); fflush(stdout);
    ASTNode* ret_node = create_ast_leaf("NUMBER", conn_id, NULL, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
}

// native_mysql_query(connection_id, query_string, [format])
// format can be "json" (default) or "xml"
// Retorna JSON or XML string en __ret__
void native_mysql_query(ASTNode* args) {

    //printf("[MySQL] native_mysql_query called\n");
    //printf("[MySQL] args=%p\n", (void*)args);
   // return;

    int conn_id = get_arg_int(args, 0);
    const char* query = get_arg_string(args, 1);
    const char* format = get_arg_string(args, 2);  // Optional format parameter
    
    // Default to JSON if format not specified
    if (!format || strlen(format) == 0) {
        format = "json";
    }
    
   // printf("[MySQL] Query format: %s\n", format);
    
    if (conn_id < 0 || conn_id >= 10 || !connections[conn_id]) {
       // printf("[MySQL] Error: Conexión inválida (ID: %d)\n", conn_id);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_connection\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    if (!query) {
        //printf("[MySQL] Error: Query inválido\n");
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"invalid_query\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL* conn = connections[conn_id];
    
    if (mysql_query(conn, query)) {
      //  printf("[MySQL] Error en query: %s\n", mysql_error(conn));
        char error_buffer[512];
        snprintf(error_buffer, sizeof(error_buffer), "{\"error\":\"%s\"}", mysql_error(conn));
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup(error_buffer), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        // Query sin resultados (INSERT, UPDATE, DELETE) o error al obtener resultados
        if (mysql_field_count(conn) == 0) { // No result set for this query
            ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"affected_rows\":0}"), NULL);
            add_or_update_variable("__ret__", ret_node);
            free_ast(ret_node);
            return;
        } else { // Error fetching result set for a query that should have one
           // printf("[MySQL] Error al obtener resultado: %s\n", mysql_error(conn));
            ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"no_result\"}"), NULL);
            add_or_update_variable("__ret__", ret_node);
            free_ast(ret_node);
            return;
        }
    }
    
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    
    // Allocate buffer for result (2MB for large queries)
    char* result_buffer = (char*)malloc(2097152);
    if (!result_buffer) {
        mysql_free_result(result);
        ASTNode* ret_node = create_ast_leaf("STRING", 0, strdup("{\"error\":\"memory_allocation_failed\"}"), NULL);
        add_or_update_variable("__ret__", ret_node);
        free_ast(ret_node);
        return;
    }
    
    int offset = 0;
    
    // Check format and generate accordingly
    size_t buffer_size = 2097152;
    if (strcmp(format, "xml") == 0) {
        // Generate XML
        offset += snprintf(result_buffer + offset, buffer_size - offset, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n");
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            if (offset > buffer_size - 8192) {
                // Clear buffer and write only valid XML error message
                snprintf(result_buffer, buffer_size, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rows>\n  <error>Result too large</error>\n</rows>");
                offset = strlen(result_buffer);
                break;
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "  <row>\n");
            for (int i = 0; i < num_fields; i++) {
                offset += snprintf(result_buffer + offset, buffer_size - offset, "    <%s>", fields[i].name);
            if (row[i]) {
                // Escape XML special characters
                char escaped[8192];
                xml_escape(row[i], escaped, sizeof(escaped));
                offset += snprintf(result_buffer + offset, buffer_size - offset, "%s", escaped);
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "</%s>\n", fields[i].name);
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "  </row>\n");
        }
        offset += snprintf(result_buffer + offset, buffer_size - offset, "</rows>");
    } else {
        // Generate JSON (default)
        offset += snprintf(result_buffer + offset, buffer_size - offset, "[");
        MYSQL_ROW row;
        int first_row = 1;
        while ((row = mysql_fetch_row(result))) {
            if (offset > buffer_size - 4096) {
                // Clear buffer and write only error message
                snprintf(result_buffer, buffer_size, "{\"error\":\"Result too large\"}");
                offset = strlen(result_buffer);
                break;
            }
            if (!first_row) {
                offset += snprintf(result_buffer + offset, buffer_size - offset, ",");
            }
            first_row = 0;
            offset += snprintf(result_buffer + offset, buffer_size - offset, "{");
            for (int i = 0; i < num_fields; i++) {
                if (i > 0) {
                    offset += snprintf(result_buffer + offset, buffer_size - offset, ",");
                }
                offset += snprintf(result_buffer + offset, buffer_size - offset, "\"%s\":", fields[i].name);
                if (row[i]) {
                    // Check if field is numeric using IS_NUM flag
                    // This is more reliable than checking field type
                    if (IS_NUM(fields[i].type)) {
                        // Numeric field - no quotes
                        offset += snprintf(result_buffer + offset, buffer_size - offset, "%s", row[i]);
                    } else {
                        // String field - add quotes
                        offset += snprintf(result_buffer + offset, buffer_size - offset, "\"%s\"", row[i]);
                    }
                } else {
                    offset += snprintf(result_buffer + offset, buffer_size - offset, "null");
                }
            }
            offset += snprintf(result_buffer + offset, buffer_size - offset, "}");
        }
        offset += snprintf(result_buffer + offset, buffer_size - offset, "]");
    }
    
    mysql_free_result(result);
    
    //printf("[MySQL] Query ejecutado exitosamente\n");
    ASTNode* ret_node = create_ast_leaf("STRING", 0, result_buffer, NULL);
    add_or_update_variable("__ret__", ret_node);
    free_ast(ret_node);
    free(result_buffer);
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
   // printf("[MySQL] Conexión cerrada (ID: %d)\n", conn_id);
}
