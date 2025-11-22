// Implementación básica de native_orm_query
#include "ast.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h> // For strcasecmp

void native_orm_query(ASTNode* args) {
    printf("[ORM] native_orm_query ejecutado\n"); fflush(stdout);
    // Espera: (conn_id, query, class)
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
        return;
    }
    
    // Ejecutar consulta MySQL
    extern ASTNode* mysql_query_result(int conn_id, const char* query);
    ASTNode* result_list = mysql_query_result(conn_id, query);
    if (!result_list) {
        printf("[ORM] No se obtuvieron resultados de la consulta\n");
        return;
    }
    
    // Mapear resultados
    ASTNode* first_obj = NULL;
    ASTNode* last_obj = NULL;
    ASTNode* item = result_list;
    int obj_count = 0;
    
    while (item) {
        // Detach arguments from the result list so they are not freed later
        ASTNode* args_to_keep = item->left;
        item->left = NULL; 

        // Crear objeto (sin constructor automático)
        ASTNode* obj_node = create_object_with_args(cls, args_to_keep);
        
        if (!obj_node) {
            printf("[ORM] Error: No se pudo crear objeto\n");
            item = item->right;
            continue;
        }
        
        // --- MAPPING LOGIC ---
        ObjectNode* real_obj = (ObjectNode*)obj_node->extra;
        if (real_obj) {
            ASTNode* args_wrapper = args_to_keep; // Use the detached args
            ASTNode* field = NULL;
            
            // Desempaquetar nodo LIST si es necesario
            if (args_wrapper && args_wrapper->type && strcmp(args_wrapper->type, "LIST") == 0) {
                field = args_wrapper->left;
            } else {
                field = args_wrapper;
            }

            while (field) {
                if (field->id) {
                    // Buscar atributo coincidente (case-insensitive)
                    for (int i = 0; i < real_obj->class->attr_count; i++) {
                        if (strcasecmp(real_obj->attributes[i].id, field->id) == 0) {
                            // Asignar valor según tipo
                            if (strcmp(field->type, "NUMBER") == 0) {
                                real_obj->attributes[i].vtype = VAL_INT;
                                real_obj->attributes[i].value.int_value = field->value;
                            } else if (strcmp(field->type, "STRING") == 0) {
                                real_obj->attributes[i].vtype = VAL_STRING;
                                if (real_obj->attributes[i].value.string_value) free(real_obj->attributes[i].value.string_value);
                                real_obj->attributes[i].value.string_value = strdup(field->str_value ? field->str_value : "");
                            }
                            break;
                        }
                    }
                }
                field = field->next; // Usar next para iterar lista de argumentos
            }
        }
        // ---------------------

        obj_count++;
        
        if (!first_obj) {
            first_obj = obj_node;
            last_obj = obj_node;
        } else {
            last_obj->next = obj_node;
            last_obj = obj_node;
        }
        
        obj_node->next = NULL;
        obj_node->right = NULL;
        
        item = item->right;
    }
    
    printf("[ORM] Total objetos mapeados: %d\n", obj_count);
    fflush(stdout);
    
    // Retornar lista
    ASTNode* list_node = (ASTNode*)malloc(sizeof(ASTNode));
    list_node->type = strdup("LIST");
    list_node->left = first_obj;
    list_node->right = NULL;
    list_node->next = NULL;
    list_node->id = NULL;
    list_node->str_value = NULL;
    list_node->value = 0;
    add_or_update_variable("__ret__", list_node);
    
    printf("[ORM] Cleaning up result_list...\n"); fflush(stdout);
    // free_ast(result_list); // Commented out to debug crash
    printf("[ORM] native_orm_query finished\n"); fflush(stdout);
}