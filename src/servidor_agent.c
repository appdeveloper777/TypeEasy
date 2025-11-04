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
static char* int_to_string_agent(int x) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", x);
    return strdup(buf);
}

static char* double_to_string_agent(double x) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%f", x);
    return strdup(buf);
}

char* get_node_string(ASTNode *node) {
    if (!node) return strdup("");

    if (strcmp(node->type, "STRING") == 0) {
        return strdup(node->str_value);
    }

    if (strcmp(node->type, "ADD") == 0) {
        char *s1 = get_node_string(node->left);
        char *s2 = get_node_string(node->right);
        size_t len = strlen(s1) + strlen(s2) + 1;
        char *res = malloc(len);
        snprintf(res, len, "%s%s", s1, s2);
        free(s1);
        free(s2);
        return res;
    }
    return strdup("[unsupported value type]");
}


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

static int webhook_handler(struct mg_connection *conn, void *cbdata) {
    
const struct mg_request_info *req_info = mg_get_request_info(conn);
    printf("[Agente DEBUG] Request Method: %s\n", req_info->request_method);

    char post_data[2048] = {0};
    long body_len = req_info->content_length;
    int read = 0;

    // --- Lógica de Lectura Robusta ---
    if (body_len > 0 && body_len < sizeof(post_data)) {
        // Caso 1: Content-Length es conocido y positivo (lectura ideal)
        read = mg_read(conn, post_data, body_len);
        
        if (read > 0) {
            post_data[read] = '\0';
            printf("[Agente DEBUG] Bytes leídos (Content-Length): %d\n", read);
        }
    } else if (body_len == -1) {
        // Caso 2: Content-Length es -1 (desconocido, común en Postman Raw)
        // Intentamos leer el máximo del buffer para capturar el cuerpo.
        read = mg_read(conn, post_data, sizeof(post_data) - 1);
        if (read > 0) {
            post_data[read] = '\0';
            printf("[Agente DEBUG] Lectura máxima exitosa (Content-Length: -1). Bytes: %d\n", read);
        } else {
             printf("[Agente DEBUG] Lectura máxima devolvió 0. Cuerpo vacío.\n");
        }
    } else {
        printf("[Agente DEBUG] Body vacío o demasiado grande (Content-Length: %ld)\n", body_len);
    }
    // --- Fin Lógica de Lectura ---

    printf("[Agente] Webhook 'Chat.onMessage' recibido! Body: \"%s\"\n", post_data);


    ASTNode* listener = runtime_find_listener("Chat", "onMessage");
    if (!listener) {
        fprintf(stderr, "[Agente] Error: No se encontró listener 'Chat.onMessage'\n");
        mg_send_http_error(conn, 500, "Listener no configurado");
        g_current_conn = NULL;
        return 500;
    }

    // El resto de la lógica no cambia.
    ASTNode* msg_node = create_string_node(post_data);
    add_or_update_variable("mensaje", msg_node);
    free_ast(msg_node); // Liberamos el nodo temporal, ya que add_or_update_variable crea su propia copia.

    printf("[Agente] Ejecutando lógica del listener...\n");
    interpret_ast(listener->right); // El 'Motor' interpreta el cuerpo
    printf("[Agente] Lógica del listener finalizada.\n");

    g_current_conn = NULL; // Clear the global connection pointer
    
    // Si la respuesta no se envió desde el bridge, enviamos un 200 OK vacío.
    // La forma más segura es no hacer nada si el bridge ya respondió.
    // mg_printf enviará una respuesta OK por defecto si no se ha enviado nada.
    if (mg_get_response_info(conn) == NULL) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    }
    return 200; // Devolvemos 200 para que civetweb sepa que la petición fue manejada.
}

void runtime_start_bridges() {
    printf("[Agente] Iniciando Bridges (Servidor Web)...\n");
    const char *options[] = {
        "listening_ports", "8081",
        "request_timeout_ms", "30000",
        "enable_keep_alive", "yes",
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

    // --- AÑADIDO ---
    // Clase para el objeto 'entidad' (debe definirse ANTES de NluResult)
    ClassNode* entity_map_class = create_class("EntityMap");
    add_attribute_to_class(entity_map_class, "item", "string");
    add_attribute_to_class(entity_map_class, "cantidad", "int");
    add_class(entity_map_class);
    // --- FIN AÑADIDO ---

    // Clase para el resultado de NLU.parse()
    ClassNode* nlu_class = create_class("NluResult");
    add_attribute_to_class(nlu_class, "tipo", "string");
    add_attribute_to_class(nlu_class, "entidad", "EntityMap");
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
        .handle_nlu_bridge = NULL, // Aún no implementado
        .handle_api_bridge = NULL  // Aún no implementado
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