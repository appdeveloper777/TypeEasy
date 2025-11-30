/* --- Archivo: src/servidor_agent.c --- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "civetweb.h"
#include <stdarg.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --- Estructura para manejar la respuesta de libcurl --- */
struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) {
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

/* --- Prototipos --- */
ASTNode* parse_file(FILE* file);
void interpret_ast(ASTNode* node);
void free_ast(ASTNode* node);
ASTNode *create_string_node(char *value);
void add_or_update_variable(char *id, ASTNode *value);
ObjectNode* clone_object(ObjectNode *original);
char* get_node_string(ASTNode *node);

typedef struct ActiveBridge {
    char* name;
    struct mg_context* context;
    struct ActiveBridge* next;
} ActiveBridge;

typedef struct RuntimeHost {
    ASTNode* agents;
    ActiveBridge* bridges;
} RuntimeHost;

RuntimeHost g_runtime;
static struct mg_connection *g_current_conn = NULL;
extern int g_debug_mode;

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
    for (int i = 0; i < width; i++) putchar('=');
    putchar('\n');

    pad = (width - (int)strlen(title)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", title);

    for (int i = 0; i < width; i++) putchar('-');
    putchar('\n');

    pad = (width - (int)strlen(line1)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", line1);

    pad = (width - (int)strlen(line2)) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", line2);

    for (int i = 0; i < width; i++) putchar('=');
    putchar('\n');
    printf("%s", reset);
}

void handle_chat_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "sendMessage") == 0 && args != NULL) {
        char* message_to_send = get_node_string(args); 
        te_log("Chat.sendMessage called");

        // Extraer solo el campo "response" del JSON de Gemini
        char response_text[4096] = {0};
        char* response_field = strstr(message_to_send, "\"response\":\"");
        if (response_field) {
            response_field += 12; // Skip "response":"
            char* end = strstr(response_field, "\",\"timestamp\"");
            if (!end) end = strstr(response_field, "\"}");
            if (end) {
                int len = end - response_field;
                if (len > 0 && len < sizeof(response_text) - 1) {
                    strncpy(response_text, response_field, len);
                    response_text[len] = '\0';
                }
            }
        }
        
        if (response_text[0] == '\0') {
            strncpy(response_text, message_to_send, sizeof(response_text) - 1);
        }

        // Enviar a WAHA
        CURL *curl = curl_easy_init();
        if(curl) {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            
            char json_payload[8192];
            snprintf(json_payload, sizeof(json_payload), 
                     "{\"to\":\"unknown\",\"message\":\"%s\"}", response_text);
            
            curl_easy_setopt(curl, CURLOPT_URL, "http://whatsapp_adapter:5002/send");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            te_log("Sending to WhatsApp via WAHA");
            CURLcode res = curl_easy_perform(curl);
            
            if(res != CURLE_OK) {
                te_log("Failed to send: %s", curl_easy_strerror(res));
            } else {
                te_log("✅ Message sent to WhatsApp successfully");
            }
            
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
        }

        // También devolver como respuesta HTTP
        if (g_current_conn) {
            mg_send_http_ok(g_current_conn, "text/plain; charset=utf-8", strlen(message_to_send));
            mg_write(g_current_conn, message_to_send, strlen(message_to_send));
        }
        free(message_to_send);
    }
}

void handle_nlu_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "parse") != 0 && strcmp(method_name, "post") != 0) return;

    char* mensaje_usuario = NULL;
    if (strcmp(method_name, "parse") == 0) {
        mensaje_usuario = get_node_string(args);
    } else {
        if (args && args->right) {
            mensaje_usuario = get_node_string(args->right);
        } else {
            mensaje_usuario = strdup("");
        }
    }
    
    const char* tipo_simulado = NULL;
    const char* item_simulado = "";
    int cant_simulada = 0;

    if (strstr(mensaje_usuario, "menu") != NULL || strstr(mensaje_usuario, "carta") != NULL) {
        tipo_simulado = "consultarMenu";
    } else if (strstr(mensaje_usuario, "hola") != NULL || strstr(mensaje_usuario, "gracias") != NULL) {
        tipo_simulado = "desconocido";
    } else {
        tipo_simulado = "agregarItem";
        item_simulado = "Tacos al Pastor";
        cant_simulada = 2;
    }

    free(mensaje_usuario); 

    ClassNode* result_class = find_class("NluResult");
    if (!result_class) { return; }
    
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
    
    ASTNode* result_node = create_ast_leaf("OBJECT", 0, NULL, NULL);
    result_node->type = strdup("OBJECT");   
    result_node->extra = (struct ASTNode*)result_obj;
    
    add_or_update_variable("__ret__", result_node);
}

void handle_gemini_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "post") == 0 && args != NULL) {
        char* path = NULL;
        char* message = NULL;
        
        path = get_node_string(args);
        if (args->right) {
            message = get_node_string(args->right);
        }
        
        if (!path) path = strdup("");
        if (!message) message = strdup("");
        
        te_log("Gemini.post called. Path: %s, Message: %s", path, message);
        
        char url[512];
        snprintf(url, sizeof(url), "http://gemini:5003%s", path);
        
        CURL *curl;
        CURLcode res;
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;
        
        curl = curl_easy_init();
        if(curl) {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: text/plain");
            
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            
            res = curl_easy_perform(curl);
            
            if(res != CURLE_OK) {
                te_log("curl_easy_perform() failed: %s", curl_easy_strerror(res));
            } else {
                te_log("Gemini response: %s", chunk.memory);
                ASTNode* resp_node = create_string_node(chunk.memory);
                add_or_update_variable("__ret__", resp_node);
            }
            
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
        }
        
        free(chunk.memory);
        
        free(path);
        free(message);
    }
}

void handle_api_bridge(char* method_name, ASTNode* args) {
    if (strcmp(method_name, "get") == 0) {
        if (g_debug_mode) te_log("API.get invoked: returning simulated menu");
        
        ASTNode* menu_node = create_string_node(
            "Menú del Día: Tacos (3€), Burritos (5€), Enchiladas (4€)"
        );

        add_or_update_variable("__ret__", menu_node);
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

                    if (g_debug_mode) te_log("Listener found for %s.%s", bridge_name, event_name);
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
    runtime_reset_vars_to_initial_state();

    char post_data[2048] = {0};
    int read = 0;

    g_current_conn = conn;

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

    if (g_debug_mode) te_log("Incoming webhook received. Message: \"%s\"", post_data);

    ASTNode* listener = runtime_find_listener("Chat", "onMessage");
    if (!listener) {
        fprintf(stderr, "TypeEasy Agent: Error: listener 'Chat.onMessage' not configured\n");
        mg_send_http_error(conn, 500, "Listener no configurado");
        g_current_conn = NULL;
        return 500;
    }

    ASTNode* msg_node = create_string_node(post_data);
    add_or_update_variable("mensaje", msg_node);
    free_ast(msg_node); 

    if (g_debug_mode) te_log("Executing listener logic");
    interpret_ast(listener->right);
    te_log("Listener logic finished");

    g_current_conn = NULL; 
    
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

    ClassNode* entity_map_class = create_class("EntityMap");
    add_attribute_to_class(entity_map_class, "item", "string");
    add_attribute_to_class(entity_map_class, "cantidad", "int");
    add_class(entity_map_class);

    ClassNode* nlu_class = create_class("NluResult");
    add_attribute_to_class(nlu_class, "tipo", "string");
    add_attribute_to_class(nlu_class, "item", "string");
    add_attribute_to_class(nlu_class, "cantidad", "int");
    add_class(nlu_class);

    ClassNode* session_class = create_class("Session");
    add_attribute_to_class(session_class, "paso", "string");
    add_class(session_class);
}

int main(int argc, char *argv[]) {
    print_startup_banner("8081", "/whatsapp_hook");
    setbuf(stdout, NULL);
    const char *debug_env = getenv("TYPEEASY_DEBUG");
    if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
        g_debug_mode = 1;
    }
    
    if (argc < 2) {
        printf("Uso: %s <archivo_agente.te>\n", argv[0]);
        return 1;
    }

    BridgeHandlers agent_handlers = {
        .handle_chat_bridge = handle_chat_bridge,
        .handle_nlu_bridge = handle_nlu_bridge,
        .handle_api_bridge = handle_api_bridge,
        .handle_gemini_bridge = handle_gemini_bridge
    };
    
    runtime_register_bridge_handlers(agent_handlers);

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Error abriendo el archivo de agente %s\n", argv[1]);
        return 1;
    }

    ASTNode* agent_ast = parse_file(file);
    fclose(file);

    if (!agent_ast) {
        fprintf(stderr, "Error fatal: No se pudo construir el AST del agente.\n");
        return 1;
    }

    runtime_init(agent_ast);
    interpret_ast(agent_ast);    
    runtime_save_initial_var_count();
    runtime_start_bridges(); 

    te_log("Runtime started. Waiting for events...");

    while(1) {
        #ifdef _WIN32
            Sleep(1000);
        #else
            sleep(1);    
        #endif
    }

    free_ast(agent_ast);
    return 0;
}