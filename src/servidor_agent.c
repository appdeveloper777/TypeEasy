/* --- Archivo: src/servidor_agent.c --- */
#include <stdio.h>
#include <stdlib.h>
#include "ast.h" // Incluye tu "Motor" puro
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

/* --- FUNCIONES DEL SERVIDOR (Movidas de ast.c) --- */

ASTNode* runtime_find_listener(const char* bridge_name, const char* event_name) {
    ASTNode* agent = g_runtime.agents;
    while (agent) {
        ASTNode* listener_list = agent->left; // El 'agent_body'
        while (listener_list) {
            ASTNode* listener = NULL;
            // Maneja la lista de listeners
            if (listener_list->type && strcmp(listener_list->type, "LISTENER_LIST") == 0) {
                listener = listener_list->right;
                listener_list = listener_list->left;
            } else if (listener_list->type && strcmp(listener_list->type, "LISTENER") == 0) {
                listener = listener_list;
                listener_list = NULL; // Fin de la lista
            } else { 
                listener_list = NULL; // No es un listener, parar
                break; 
            }

            if (listener && listener->left) {
                ASTNode* expr = listener->left;
                if (expr->type && strcmp(expr->type, "ACCESS_ATTR") == 0 &&
                    expr->left && expr->left->id && strcmp(expr->left->id, bridge_name) == 0 &&
                    expr->right && expr->right->id && strcmp(expr->right->id, event_name) == 0) {
                    printf("[Agente] Listener encontrado para %s.%s\n", bridge_name, event_name);
                    return listener;
                }
            }
        }
        agent = agent->next; // Revisa el siguiente agente (si hay)
    }
    return NULL;
}

static int webhook_handler(struct mg_connection *conn, void *cbdata) {
    char post_data[2048];
    int data_len = mg_read(conn, post_data, sizeof(post_data) - 1);
    if (data_len < 0) data_len = 0;
    post_data[data_len] = '\0';

    printf("\n[Agente] Webhook 'Chat.onMessage' recibido! Body: %s\n", post_data);

    ASTNode* listener = runtime_find_listener("Chat", "onMessage");
    if (!listener) {
        fprintf(stderr, "[Agente] Error: No se encontró listener 'Chat.onMessage'\n");
        mg_send_http_error(conn, 500, "Listener no configurado");
        return 500;
    }

    // El 'Motor' sabe cómo crear nodos y variables
    ASTNode* msg_node = create_string_node(strdup(post_data));
    add_or_update_variable("mensaje", msg_node);
    free_ast(msg_node);

    printf("[Agente] Ejecutando lógica del listener...\n");
    interpret_ast(listener->right); // El 'Motor' interpreta el cuerpo
    printf("[Agente] Lógica del listener finalizada.\n");

    mg_send_http_ok(conn, "text/plain", 0);
    return 200;
}

void runtime_start_bridges() {
    printf("[Agente] Iniciando Bridges (Servidor Web)...\n");
    const char *options[] = {"listening_ports", "8081", NULL}; // <-- PUERTO 8081
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