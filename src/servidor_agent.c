/* --- Archivo: src/servidor_agent.c --- */
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"      // Incluye tu "Motor" puro
#include "civetweb.h" // Incluye la librería del servidor

/* --- AÑADIDO PARA ARREGLAR WARNING --- */
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h> // Para sleep()
#endif
/* --- FIN AÑADIDO --- */


/* --- Prototipos de las funciones en tu "Motor" (ast.c) --- */
ASTNode* parse_file(FILE* file);
void interpret_ast(ASTNode* node);
void free_ast(ASTNode* node);
ASTNode *create_string_node(char *value);
void add_or_update_variable(char *id, ASTNode *value);
ObjectNode* clone_object(ObjectNode *original); // Asumiendo que está en ast.h o ast.c

// --- AÑADIDO: Prototipo para que el main lo vea ---
char* get_node_string(ASTNode *node);

// --- COPIADO DE ast.c PARA DESACOPLAR ---
// Estas funciones son necesarias para que handle_chat_bridge pueda convertir
// los argumentos del script a strings de C.


/* --- DEFINICIONES DEL SERVIDOR (Movidas de ast.h) --- */
typedef struct ActiveBridge {
    char* name;
    struct mg_context* context;
    struct ActiveBridge* next;
} ActiveBridge;

typedef struct RuntimeHost {
    ASTNode* agents; // Lista de ASTs de agentes
    ActiveBridge* bridges;
} RuntimeHost;

// Instancia global del Runtime (solo para este ejecutable)
RuntimeHost g_runtime;

// --- INICIO MEJORA: Variable global para la conexión actual ---
// Esto permite a los bridges interactuar con la petición HTTP actual.
static struct mg_connection *g_current_conn = NULL;

// Implementación del manejador del bridge 'Chat'
void handle_chat_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "sendMessage") == 0 && args != NULL) {
        // Extraer el primer argumento (el mensaje a enviar)
        char* message_to_send = get_node_string(args); // Necesitarás una función auxiliar para esto

        printf("\n--- Respuesta del Agente ---\n");
        printf(">>> %s\n", message_to_send);
        printf("--------------------------\n\n");

        // Enviar el mensaje como respuesta HTTP
        if (g_current_conn) {
            mg_send_http_ok(g_current_conn, "text/plain; charset=utf-8", strlen(message_to_send));
            mg_write(g_current_conn, message_to_send, strlen(message_to_send));
        }
        free(message_to_send);
    }
}

/* --- Implementación del Bridge NLU (Simulado) --- */

void handle_nlu_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "parse") == 0) {
        printf("[Agente] Bridge NLU: Recibido 'parse'. Simulará 'agregarItem'.\n");

        // 1. Encontrar la clase (¡ya no necesitamos EntityMap!)
        ClassNode* result_class = find_class("NluResult");

        printf("[DEBUG] Buscando clase 'NluResult' SAPE 1...\n");
        if (!result_class) { /* ... (error) ... */ return; }
       
        // 2. (Ya no creamos el objeto 'entidad')

        // 3. Crear el objeto 'NluResult' (PLANO)
        ObjectNode* result_obj = create_object(result_class);
        for(int i=0; i < result_obj->class->attr_count; i++) {
            if(strcmp(result_obj->attributes[i].id, "tipo") == 0) {
                result_obj->attributes[i].value.string_value = strdup("agregarItem");
                result_obj->attributes[i].vtype = VAL_STRING;
            }
            // Asignar los atributos aplanados
            if(strcmp(result_obj->attributes[i].id, "item") == 0) {
                result_obj->attributes[i].value.string_value = strdup("Tacos al Pastor");
                result_obj->attributes[i].vtype = VAL_STRING;
            }
            if(strcmp(result_obj->attributes[i].id, "cantidad") == 0) {
                result_obj->attributes[i].value.int_value = 2;
                result_obj->attributes[i].vtype = VAL_INT;
            }
        }
        
         // 4. Crear un nodo AST (sin cambios)
         ASTNode* result_node = create_ast_leaf("OBJECT", 0, NULL, NULL);
         result_node->type = strdup("OBJECT");   
         result_node->extra = (struct ASTNode*)result_obj;
        

        // 5. Devolverlo al intérprete (sin cambios)
        add_or_update_variable("__ret__", result_node);

        // NO liberar result_node aquí. El intérprete lo necesita.
        // free_ast(result_node); // <-- ¡Este es el error!
    }
}

ASTNode* runtime_find_listener(const char* bridge_name, const char* event_name) {
    ASTNode* agent = g_runtime.agents;
    while (agent) {
        if (agent->type && strcmp(agent->type, "AGENT") == 0) {
            ASTNode* listener_list = agent->left;
            for (ASTNode* listener = listener_list; listener; listener = listener->right) {
                if (!listener->type || strcmp(listener->type, "LISTENER") != 0) continue;

                ASTNode* expr = listener->left;
                if (expr && expr->type && strcmp(expr->type, "CALL_METHOD") == 0 &&
                    expr->left && expr->left->id && strcmp(expr->left->id, bridge_name) == 0 &&
                    expr->id && strcmp(expr->id, event_name) == 0) {

                    printf("[Agente] Listener encontrado para %s.%s\n", bridge_name, event_name);
                    return listener;
                }
            }
        }
        agent = agent->next;
    }
    return NULL;
}

/* --- Implementación del Bridge API (Simulado) --- */
void handle_api_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "get") == 0) {
        printf("[Agente] Bridge API: Recibido 'get'. Devolviendo menú simulado.\n");
        
        // 1. Simplemente creamos un nodo de string con el menú
        ASTNode* menu_node = create_string_node(
            "Menú del Día: Tacos (3€), Burritos (5€), Enchiladas (4€)"
        );

        // 2. Devolverlo al intérprete
        add_or_update_variable("__ret__", menu_node);
        
        // NO liberar menu_node aquí. El intérprete lo necesita.
         //free_ast(menu_node); // <-- ¡Este es el error!
    }
}

/*
 * WEBHOOK_HANDLER (Versión 10 - Corrección de compilación)
 * - Arregla la llamada a mg_get_var (faltaba data_len)
 * - Arregla el warning de printf (%ld -> %lld)
 */
static int webhook_handler(struct mg_connection *conn, void *cbdata) {
    
    const struct mg_request_info *req_info = mg_get_request_info(conn);

    char post_data[2048] = {0};
    int read = 0;

    // --- Diagnóstico (Corrección de warning %ld -> %lld) ---
    printf("[Agente DEBUG] Content-Length reportado: %lld\n", (long long)req_info->content_length);
    printf("[Agente DEBUG] Request Method: %s\n", req_info->request_method);

    // --- CORRECCIÓN 1: Establecer la conexión global PRIMERO ---
    g_current_conn = conn;

    // --- CORRECCIÓN 2: LECTURA ROBUSTA (Intento 1: Leer Body BxB) ---
    char c;
    while (read < (sizeof(post_data) - 1)) {
        int bytes_leidos_ahora = mg_read(conn, &c, 1);
        if (bytes_leidos_ahora <= 0) {
            break; 
        }
        post_data[read] = c;
        read++;
    }
    post_data[read] = '\0'; // Aseguramos el fin de string
    
    if (read > 0) {
        printf("[Agente DEBUG test] Bytes leídos (Bucle BxB): %d\n", read);
    } else {
        printf("[Agente DEBUG test] Bucle BxB devolvió 0. Cuerpo vacío.\n");
        
        // --- LECTURA ROBUSTA (Intento 2: Leer Query String) ---
        // Si el body estaba vacío, probamos la URL (ej: ?message=Hola)

        // --- CORRECCIÓN DE COMPILACIÓN (Añadido query_len) ---
        const char *query = req_info->query_string;
        size_t query_len = (query == NULL) ? 0 : strlen(query);
        
        read = mg_get_var(query, query_len, "message", post_data, sizeof(post_data) - 1);
        // --- FIN DE CORRECCIÓN DE COMPILACIÓN ---

        if (read > 0) {
            printf("[Agente DEBUG test] Lectura de Query String exitosa.\n");
        } else {
            printf("[Agente DEBUG test] Query String también vacío.\n");
        }
    }
    // --- Fin de la corrección de lectura ---


    printf("[Agente test] Webhook 'Chat.onMessage' recibido! Body FINAL: \"%s\"\n", post_data);

    // 2. Encontrar el listener (sin cambios)
    ASTNode* listener = runtime_find_listener("Chat", "onMessage");
    if (!listener) {
        fprintf(stderr, "[Agente] Error: No se encontró listener 'Chat.onMessage'\n");
        mg_send_http_error(conn, 500, "Listener no configurado");
        g_current_conn = NULL; // Limpiar
        return 500;
    }

    // 3. Poner el body en la variable 'mensaje' (sin cambios)
    ASTNode* msg_node = create_string_node(post_data);
    add_or_update_variable("mensaje", msg_node);
    free_ast(msg_node); 

    // 4. Ejecutar el listener (sin cambios)
    printf("[Agente] Ejecutando lógica del listener...\n");
    interpret_ast(listener->right); // El 'Motor' interpreta el cuerpo
    printf("[Agente] Lógica del listener finalizada.\n");

    // --- CORRECCIÓN 1 (B): Limpiar la conexión global DESPUÉS ---
    g_current_conn = NULL; 
    
    // 5. Enviar 200 OK si el bridge no lo hizo (sin cambios)
    if (mg_get_response_info(conn) == NULL) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    }
    return 200; 
}

void runtime_start_bridges() {
    printf("[Agente] Iniciando Bridges (Servidor Web)...\n");
    const char *options[] = {
        "listening_ports", "8081",
        "request_timeout_ms", "30000",
        "enable_keep_alive", "no",
        NULL
    };

    struct mg_context *ctx = mg_start(NULL, 0, options);
    if (ctx == NULL) {
        fprintf(stderr, "[Agente] Error fatal: No se pudo iniciar civetweb en 8081.\n");
        exit(1);
    }
    g_runtime.bridges = malloc(sizeof(ActiveBridge));
    g_runtime.bridges->name = strdup("ChatServer");
    g_runtime.bridges->context = ctx;
    g_runtime.bridges->next = NULL;
    mg_set_request_handler(ctx, "/whatsapp_hook", webhook_handler, NULL);
    printf("[Agente] Servidor 'Chat' escuchando en puerto 8081 (URL: /whatsapp_hook)\n");
}

void runtime_init(ASTNode* ast_root) {
    g_runtime.agents = NULL;
    g_runtime.bridges = NULL;
    ASTNode* node = ast_root;

    while (node) {
        ASTNode* current_stmt = NULL;
        if (node->type && (strcmp(node->type, "STATEMENT_LIST") == 0 || strcmp(node->type, "AGENT_LIST") == 0)) {
            current_stmt = node->right;
            node = node->left;
        } else {
            current_stmt = node;
            node = NULL;
        }
        if (current_stmt && current_stmt->type && strcmp(current_stmt->type, "AGENT") == 0) {
            printf("[Agente] Agente registrado: %s\n", current_stmt->id);
            current_stmt->next = g_runtime.agents;
            g_runtime.agents = current_stmt;
        }
    }

    printf("[Agente] Registrando clases nativas de bridges...\n");
    ClassNode* bridge_class = create_class("Bridge");
    add_class(bridge_class);

    // --- AÑADIDO ---
    // Clase para el objeto 'entidad' (debe definirse ANTES de NluResult)
    ClassNode* entity_map_class = create_class("EntityMap");
    add_attribute_to_class(entity_map_class, "item", "string");
    add_attribute_to_class(entity_map_class, "cantidad", "int");
    add_class(entity_map_class);
    // --- FIN AÑADIDO ---

    //printf("[Agente] Clase 'Bridge' registrada.\n");
    //ClassNode* bridge_class = create_class("Bridge");
    //add_class(bridge_class);

    // Clase para el resultado de NLU.parse()
    ClassNode* nlu_class = create_class("NluResult");
    add_attribute_to_class(nlu_class, "tipo", "string");
    add_attribute_to_class(nlu_class, "item", "string");
    add_attribute_to_class(nlu_class, "cantidad", "int");
    add_class(nlu_class);

    // Clase para el resultado de Chat.getSession()
    ClassNode* session_class = create_class("Session");
    add_attribute_to_class(session_class, "paso", "string");
    add_class(session_class);
}

/**
 * Main para el EJECUTABLE 'servidor_agent'.
 * Inicia el servicio persistente de WhatsApp en el puerto 8081.
 */
int main(int argc, char *argv[]) {
    
    printf("[Servidor Agente] Iniciando...\n");
    
    if (argc < 2) {
        printf("Uso: %s <archivo_agente.te>\n", argv[0]);
        return 1;
    }

    // --- INICIO MEJORA: Registrar los manejadores de bridges ---
    BridgeHandlers agent_handlers = {
        .handle_chat_bridge = handle_chat_bridge,
        .handle_nlu_bridge = handle_nlu_bridge, // Aún no implementado
        .handle_api_bridge = handle_api_bridge  // Aún no implementado
    };
    
    runtime_register_bridge_handlers(agent_handlers);
    // --- FIN MEJORA ---

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo de agente %s\n", argv[1]);
        return 1;
    }

    // 1. Parsea el "Workflow" del agente usando el "Motor"
    ASTNode* agent_ast = parse_file(file);
    fclose(file);

    if (!agent_ast) {
        fprintf(stderr, "Error fatal: No se pudo construir el AST del agente.\n");
        return 1;
    }

    // 2. Inicia el Runtime del Agente (funciones en ESTE archivo)
    runtime_init(agent_ast);
    interpret_ast(agent_ast);    
    runtime_start_bridges(); 

    printf("[Servidor Agente] Runtime iniciado. Esperando eventos...\n");

    // 3. Bucle de servidor infinito
    while(1) {
        #ifdef _WIN32
            Sleep(1000);
        #else
            sleep(1);    
        #endif
    }

    // (Esto nunca se alcanza)
    free_ast(agent_ast);
    return 0;
}