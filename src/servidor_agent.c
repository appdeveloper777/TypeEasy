/* --- Archivo: src/servidor_agent.c --- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"      // Incluye tu "Motor" puro
#include "civetweb.h" // Incluye la librería del servidor
#include <stdarg.h>

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
// Expose debug flag from ast.c so we can gate noisy logs
extern int g_debug_mode;

/* Logging helper to produce consistent "TypeEasy Agent" messages */
static void te_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char *green = "\x1b[32m";
    const char *reset = "\x1b[0m";
    printf("%sTypeEasy Agent: ", green);
    vprintf(fmt, ap);
    printf("%s\n", reset);
    va_end(ap);
}

/* Print a decorative startup banner similar to the requested style */
static void print_startup_banner(const char *port, const char *url) {
    const char *blue = "\x1b[34m";
    const char *reset = "\x1b[0m";
    const char *title = "TypeEasy Agent";
    char line1[256];
    char line2[256];
    snprintf(line1, sizeof(line1), "Runtime: %s", "started");
    snprintf(line2, sizeof(line2), "Listening: %s%s%s", url ? url : "/whatsapp_hook", port ? ":" : "", port ? port : "8081");

    int width = 60;
    int pad;

    printf("%s", blue);
    // top border
    for (int i = 0; i < width; i++) putchar('=');
    putchar('\n');

    // title centered
    pad = (width - (int)strlen(title)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", title);

    // separator
    for (int i = 0; i < width; i++) putchar('-');
    putchar('\n');

    // line1
    pad = (width - (int)strlen(line1)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", line1);

    // line2
    pad = (width - (int)strlen(line2)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", line2);

    // bottom border
    for (int i = 0; i < width; i++) putchar('=');
    putchar('\n');
    printf("%s", reset);
}

// Implementación del manejador del bridge 'Chat'
void handle_chat_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "sendMessage") == 0 && args != NULL) {
        // Extraer el primer argumento (el mensaje a enviar)
        char* message_to_send = get_node_string(args); // Necesitarás una función auxiliar para esto

    te_log("Chat.sendMessage called: %s", message_to_send);

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
    // Support both direct NLU.parse(mensaje) and HTTP-style NLU.post(path, mensaje)
    if (strcmp(method_name, "parse") != 0 && strcmp(method_name, "post") != 0) return;

    // 1. Obtenemos el mensaje real
    char* mensaje_usuario = NULL;
    if (strcmp(method_name, "parse") == 0) {
        mensaje_usuario = get_node_string(args);
    } else {
        // method == "post" -> args: first = path (string), second = message
        if (args && args->right) {
            mensaje_usuario = get_node_string(args->right);
        } else {
            mensaje_usuario = strdup("");
        }
    }
    
    // 2. Variables para simular la intención
    //    (Ahora se asignan DENTRO de los 'if')
    const char* tipo_simulado = NULL;
    const char* item_simulado = "";
    int cant_simulada = 0;

    /* NLU bridge invoked; avoid verbose logging in production */

    // 3. Decidimos qué simular
    if (strstr(mensaje_usuario, "menu") != NULL || strstr(mensaje_usuario, "carta") != NULL) {
        /* simulate consultarMenu */
        tipo_simulado = "consultarMenu";
    } else if (strstr(mensaje_usuario, "hola") != NULL || strstr(mensaje_usuario, "gracias") != NULL) {
        /* simulate desconocido */
        tipo_simulado = "desconocido";
    } else {
        /* default: simulate agregarItem */
        tipo_simulado = "agregarItem";
        item_simulado = "Tacos al Pastor";
        cant_simulada = 2;
    }

    free(mensaje_usuario); 

    // 4. Encontrar la clase (sin cambios)
    ClassNode* result_class = find_class("NluResult");
    if (!result_class) { /* ... */ return; }
    
    // 5. Crear el objeto 'NluResult' (sin cambios)
    ObjectNode* result_obj = create_object(result_class);
    for(int i=0; i < result_obj->class->attr_count; i++) {
        
        if(strcmp(result_obj->attributes[i].id, "tipo") == 0) {
            result_obj->attributes[i].value.string_value = strdup(tipo_simulado);
            result_obj->attributes[i].vtype = VAL_STRING;
        }
        if(strcmp(result_obj->attributes[i].id, "item") == 0) {
            result_obj->attributes[i].value.string_value = strdup(item_simulado);
            result_obj->attributes[i].vtype = VAL_STRING;
        }
        if(strcmp(result_obj->attributes[i].id, "cantidad") == 0) {
            result_obj->attributes[i].value.int_value = cant_simulada;
            result_obj->attributes[i].vtype = VAL_INT;
        }
    }
    
    // 6. Devolverlo (sin cambios)
    ASTNode* result_node = create_ast_leaf("OBJECT", 0, NULL, NULL);
    result_node->type = strdup("OBJECT");   
    result_node->extra = (struct ASTNode*)result_obj;
    
    add_or_update_variable("__ret__", result_node);
    // Do not free result_node here — the interpreter holds a reference to it via __ret__
    // free_ast(result_node);

    /* no verbose diagnostics here; interpreter logs cover lifecycle if needed */
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

                                    if (g_debug_mode) te_log("Listener found for %s.%s", bridge_name, event_name);
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
        if (g_debug_mode) te_log("API.get invoked: returning simulated menu");
        
        // 1. Simplemente creamos un nodo de string con el menú
        ASTNode* menu_node = create_string_node(
            "Menú del Día: Tacos (3€), Burritos (5€), Enchiladas (4€)"
        );

        // 2. Devolverlo al intérprete
        add_or_update_variable("__ret__", menu_node);
        
        // NO liberar menu_node aquí. El intérprete lo necesita.
       //  free_ast(menu_node); // <-- ¡Este es el error!
    }
}

/*
 * WEBHOOK_HANDLER (Versión 10 - Corrección de compilación)
 * - Arregla la llamada a mg_get_var (faltaba data_len)
 * - Arregla el warning de printf (%ld -> %lld)
 */
static int webhook_handler(struct mg_connection *conn, void *cbdata) {
    
    const struct mg_request_info *req_info = mg_get_request_info(conn);

    runtime_reset_vars_to_initial_state();

    char post_data[2048] = {0};
    int read = 0;

    // Establish the global connection for bridges that may use it
    g_current_conn = conn;

    // Read the request body (robust): first try body, otherwise query string ?message=
    char c;
    while (read < (sizeof(post_data) - 1)) {
        int bytes_leidos_ahora = mg_read(conn, &c, 1);
        if (bytes_leidos_ahora <= 0) {
            break;
        }
        post_data[read] = c;
        read++;
    }
    post_data[read] = '\0';

    if (read == 0) {
        const char *query = req_info->query_string;
        size_t query_len = (query == NULL) ? 0 : strlen(query);
        read = mg_get_var(query, query_len, "message", post_data, sizeof(post_data) - 1);
    }

    // Minimal log for incoming messages (only when debugging)
    if (g_debug_mode) te_log("Incoming webhook received. Message: \"%s\"", post_data);

    // 2. Encontrar el listener (sin cambios)
    ASTNode* listener = runtime_find_listener("Chat", "onMessage");
    if (!listener) {
        fprintf(stderr, "TypeEasy Agent: Error: listener 'Chat.onMessage' not configured\n");
        mg_send_http_error(conn, 500, "Listener no configurado");
        g_current_conn = NULL; // Limpiar
        return 500;
    }

    // 3. Poner el body en la variable 'mensaje' (sin cambios)
    ASTNode* msg_node = create_string_node(post_data);
    add_or_update_variable("mensaje", msg_node);
    free_ast(msg_node); 

    // 4. Ejecutar el listener (sin changes)
    if (g_debug_mode) te_log("Executing listener logic");
    interpret_ast(listener->right); // El 'Motor' interpreta el cuerpo
    te_log("Listener logic finished");

    // --- CORRECCIÓN 1 (B): Limpiar la conexión global DESPUÉS ---
    g_current_conn = NULL; 
    
    // 5. Enviar 200 OK si el bridge no lo hizo (sin cambios)
    if (mg_get_response_info(conn) == NULL) {
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    }
    return 200; 
}

void runtime_start_bridges() {
    te_log("Initializing bridges (web server)");
    const char *options[] = {
        "listening_ports", "8081",
        "request_timeout_ms", "30000",
        "enable_keep_alive", "no",
        NULL
    };

    struct mg_context *ctx = mg_start(NULL, 0, options);
    if (ctx == NULL) {
        fprintf(stderr, "TypeEasy Agent: Fatal error: could not start civetweb on 8081.\n");
        exit(1);
    }
    g_runtime.bridges = malloc(sizeof(ActiveBridge));
    g_runtime.bridges->name = strdup("ChatServer");
    g_runtime.bridges->context = ctx;
    g_runtime.bridges->next = NULL;
    mg_set_request_handler(ctx, "/whatsapp_hook", webhook_handler, NULL);
    te_log("Bridge server started: Chat listening on port 8081 (URL: /whatsapp_hook)");
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
            te_log("Agent registered: %s", current_stmt->id);
            current_stmt->next = g_runtime.agents;
            g_runtime.agents = current_stmt;
        }
    }

    if (g_debug_mode) te_log("Registering native bridge classes");
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
    
    print_startup_banner("8081", "/whatsapp_hook");
    // Disable stdout buffering so logs are immediately visible in container logs
    setbuf(stdout, NULL);
    /* Allow toggling debug logs with TYPEEASY_DEBUG=1 */
    const char *debug_env = getenv("TYPEEASY_DEBUG");
    if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
        g_debug_mode = 1;
    }
    
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
    runtime_save_initial_var_count();
    runtime_start_bridges(); 

    te_log("Runtime started. Waiting for events...");

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