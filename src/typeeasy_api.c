#include "typeeasy_api.h"
#include "debugger.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
  #define pipe(fds) _pipe((fds), 65536, _O_BINARY)
  #ifndef STDOUT_FILENO
    #define STDOUT_FILENO _fileno(stdout)
  #endif
#else
  #include <unistd.h>
#endif

// Declaraciones externas de funciones del intérprete
extern ASTNode* parse_file(FILE* file);
extern MethodNode* global_methods;
extern int g_debug_mode;
extern FILE *yyin;  // Variable global de Flex para el parser

// Declaraciones externas para manejo de return values
extern Variable __ret_var;
extern int __ret_var_active;
extern int return_flag;

// Funciones auxiliares para captura de stdout
static int stdout_backup = -1;
static int pipe_fds[2];

static void start_stdout_capture(TypeEasyEmbeddedContext* ctx) {
    // Crear pipe para capturar stdout
    if (pipe(pipe_fds) == -1) {
        return;
    }
    
    // Guardar stdout original
    stdout_backup = dup(STDOUT_FILENO);
    
    // Redirigir stdout al pipe
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    
    // Inicializar buffer de captura
    ctx->capture_capacity = 65536;
    ctx->captured_output = (char*)malloc(ctx->capture_capacity);
    ctx->capture_size = 0;
    if (ctx->captured_output) {
        ctx->captured_output[0] = '\0';
    }
}

static char* end_stdout_capture(TypeEasyEmbeddedContext* ctx) {
    // Restaurar stdout original
    fflush(stdout);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);
    
    // Leer del pipe
    char buffer[4096];
    ssize_t bytes_read;
    
    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        // Expandir buffer si es necesario
        if (ctx->capture_size + bytes_read + 1 > ctx->capture_capacity) {
            ctx->capture_capacity *= 2;
            char* new_buffer = (char*)realloc(ctx->captured_output, ctx->capture_capacity);
            if (!new_buffer) {
                break;
            }
            ctx->captured_output = new_buffer;
        }
        
        // Agregar al buffer
        memcpy(ctx->captured_output + ctx->capture_size, buffer, bytes_read);
        ctx->capture_size += bytes_read;
        ctx->captured_output[ctx->capture_size] = '\0';
    }
    
    close(pipe_fds[0]);
    
    // Retornar copia del output capturado
    char* result = ctx->captured_output ? strdup(ctx->captured_output) : NULL;
    
    // Limpiar buffer de captura
    if (ctx->captured_output) {
        free(ctx->captured_output);
        ctx->captured_output = NULL;
    }
    ctx->capture_size = 0;
    
    return result;
}

TypeEasyEmbeddedContext* typeeasy_embedded_init(void) {
    TypeEasyEmbeddedContext* ctx = (TypeEasyEmbeddedContext*)malloc(sizeof(TypeEasyEmbeddedContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->scripts = NULL;
    ctx->script_count = 0;
    ctx->stdout_capture = NULL;
    ctx->captured_output = NULL;
    ctx->capture_size = 0;
    ctx->capture_capacity = 0;
    
    // NOTA: el snapshot de variables iniciales se toma DESPUES de cargar el
    // script en typeeasy_load_script_embedded(), no aqui, para que los `let`
    // de top-level del bootstrap (ej. `let db = sqlite_connect(...)`)
    // sobrevivan a runtime_reset_vars_to_initial_state() entre requests HTTP.
    
    return ctx;
}

void typeeasy_embedded_cleanup(TypeEasyEmbeddedContext* ctx) {
    if (!ctx) {
        return;
    }
    
    // Liberar todos los scripts cargados
    LoadedScript* script = ctx->scripts;
    while (script) {
        LoadedScript* next = script->next;
        if (script->script_path) {
            free(script->script_path);
        }
        if (script->parsed_ast) {
            free_ast(script->parsed_ast);
        }
        free(script);
        script = next;
    }
    
    if (ctx->captured_output) {
        free(ctx->captured_output);
    }
    
    free(ctx);
}

LoadedScript* find_loaded_script(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    LoadedScript* script = ctx->scripts;
    while (script) {
        if (strcmp(script->script_path, script_path) == 0) {
            return script;
        }
        script = script->next;
    }
    
    return NULL;
}

int typeeasy_embedded_load_script(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return 0;
    }
    
    // Verificar si ya está cargado
    if (find_loaded_script(ctx, script_path)) {
        return 1; // Ya está cargado
    }
    
    printf("[TYPEEASY_API] Intentando cargar script: %s\n", script_path);
    
    // Abrir archivo
    FILE* file = fopen(script_path, "r");
    if (!file) {
        fprintf(stderr, "[TYPEEASY_API] Error al abrir: %s\n", script_path);
        perror("fopen");
        return 0;
    }
    
    printf("[TYPEEASY_API] Archivo abierto correctamente\n");
    
    // Configurar yyin para el parser de Flex (aunque parse_file lo hace, es bueno asegurarse)
    yyin = file;
    
    printf("[TYPEEASY_API] Llamando a parse_file()...\n");
    
    // Parsear archivo
    ASTNode* ast = parse_file(file);
    fclose(file);
    
    if (!ast) {
        fprintf(stderr, "[TYPEEASY_API] ERROR: parse_file() retornó NULL para: %s\n", script_path);
        fprintf(stderr, "[TYPEEASY_API] El archivo contiene errores de sintaxis y no se cargará.\n");
        // Crear un AST dummy para evitar crashes posteriores
        ast = create_ast_node("STATEMENT_LIST", NULL, NULL);
        if (!ast) {
            fprintf(stderr, "[TYPEEASY_API] CRITICAL: No se pudo crear AST dummy\n");
            return 0;
        }
    }
    
    printf("[TYPEEASY_API] parse_file() exitoso, AST creado\n");
    
    // Crear entrada de script cargado
    LoadedScript* script = (LoadedScript*)malloc(sizeof(LoadedScript));
    if (!script) {
        free_ast(ast);
        return 0;
    }
    
    script->script_path = strdup(script_path);
    script->parsed_ast = ast;
    script->next = ctx->scripts;
    ctx->scripts = script;
    ctx->script_count++;
    
    printf("[TYPEEASY_API] Ejecutando scope global...\n");
    
    // CRITICAL: Activar __ret_var antes de ejecutar scope global
    if (!__ret_var_active) {
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 1;
    }
    fprintf(stderr, "[DEBUG] ANTES de interpret_ast(scope global): __ret_var_active=%d\n", __ret_var_active);
    
    /* Bloque D: clear interpreter control-flow flags before running this
     * file's global scope. Without this, an aborted previous file (uncaught
     * throw, top-level return, leaked break/continue) leaves a flag set and
     * interpret_ast() short-circuits, silently dropping endpoints. */
    te_runtime_reset_flags();

    // Ejecutar scope global para inicializar clases, variables globales, etc.
    interpret_ast(ast);
    
    fprintf(stderr, "[DEBUG] DESPUÉS de interpret_ast(scope global): __ret_var_active=%d\n", __ret_var_active);
    
    // Snapshot DESPUES del bootstrap: top-level `let`/`var` quedan por debajo
    // del marcador y sobreviven a runtime_reset_vars_to_initial_state() entre
    // requests. Si se cargan multiples scripts, cada uno re-snapshotea para
    // que su propio bootstrap tambien sobreviva.
    runtime_save_initial_var_count();
    
    printf("[TYPEEASY_API] Script loaded successfully (true embedded): %s\n", script_path);
    
    return 1;
}

const char* detect_response_type_embedded(ASTNode *body) {
    if (!body) return "json";
    
    if (body->type) {
        if (strcmp(body->type, "RETURN_XML") == 0) return "xml";
        if (strcmp(body->type, "RETURN_JSON") == 0) return "json";
    }
    
    if (body->left) {
        const char *left_type = detect_response_type_embedded(body->left);
        if (strcmp(left_type, "xml") == 0) return "xml";
    }
    if (body->right) {
        const char *right_type = detect_response_type_embedded(body->right);
        if (strcmp(right_type, "xml") == 0) return "xml";
    }
    
    return "json";
}

char* typeeasy_embedded_discover(TypeEasyEmbeddedContext* ctx, const char* script_path) {
    if (!ctx || !script_path) {
        return NULL;
    }
    
    // Verificar que el script esté cargado
    LoadedScript* script = find_loaded_script(ctx, script_path);
    if (!script) {
        fprintf(stderr, "[TYPEEASY_API] Script no cargado: %s\n", script_path);
        return NULL;
    }
    
    // NUEVO ENFOQUE: Descubrir endpoints directamente desde global_methods
    // Esto evita re-parsear el archivo (que causaría segfault con imports)
    char* result = (char*)malloc(65536);
    if (!result) {
        return NULL;
    }
    
    strcpy(result, "[");
    int first = 1;
    
    MethodNode *m = global_methods;
    while (m) {
        if (m->route_path) {
            if (!first) strcat(result, ",");
            
            // Detect response type
            const char *response_type = detect_response_type_embedded(m->body);
            const char *http_method = m->http_method ? m->http_method : "GET";
            
            // Build JSON entry (now includes cache_ttl)
            char entry[1024];
            snprintf(entry, sizeof(entry), 
                     "{\"route\": \"%s\", \"method\": \"%s\", \"function\": \"%s\", \"response_type\": \"%s\", \"cache_ttl\": %d}", 
                     m->route_path, 
                     http_method, 
                     m->name, 
                     response_type,
                     m->cache_ttl);
            strcat(result, entry);
            first = 0;
        }
        m = m->next;
    }
    
    strcat(result, "]");
    
    return result;
}

/* Verbose flag — set TYPEEASY_VERBOSE=1 to enable per-request logs. */
static int te_verbose_cached = -1;
static int te_verbose(void) {
    if (te_verbose_cached < 0) {
        const char *v = getenv("TYPEEASY_VERBOSE");
        te_verbose_cached = (v && (*v == '1' || *v == 't' || *v == 'T')) ? 1 : 0;
    }
    return te_verbose_cached;
}

/* Lookup MethodNode* por nombre. Cachealo en el router para evitar repetir. */
MethodNode* typeeasy_find_method(const char* function_name) {
    if (!function_name) return NULL;
    for (MethodNode* m = global_methods; m; m = m->next) {
        if (m->name && strcmp(m->name, function_name) == 0) return m;
    }
    return NULL;
}

/* v0.0.13 — model binding helpers (defined in src/ast.c). */
extern ObjectNode *te_object_from_json(ClassNode *cls, const char *json);
extern char       *te_validate_body_against_class(ClassNode *cls, const char *json);
extern void        typeeasy_http_set_status(int s);
extern ClassNode  *find_class(char *name);
extern const char *typeeasy_http_get_body(void);
extern const char *typeeasy_http_find_param(const char *k);
extern const char *typeeasy_http_find_query(const char *k);
extern ASTNode    *create_ast_leaf(char *type, int value, char *str_value, char *id);
extern ASTNode    *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
extern void        add_or_update_variable(char *id, ASTNode *value);
extern void        free_ast(ASTNode *node);
extern void        free_object_node(ObjectNode *obj);

/* item #6 (leak audit): registro de ObjectNodes creados por model binding en
 * el request actual. Se liberan una sola vez en typeeasy_embedded_invoke_method
 * tras el reset, evitando el double-free por aliasing (un entry por objeto
 * creado, no por variable). */
#define TE_REQ_OWNED_MAX 64
static ObjectNode *g_req_owned_objects[TE_REQ_OWNED_MAX];
static int         g_req_owned_count = 0;

static void te_req_owned_register(ObjectNode *obj) {
    if (obj && g_req_owned_count < TE_REQ_OWNED_MAX)
        g_req_owned_objects[g_req_owned_count++] = obj;
}

static void te_req_owned_free_all(void) {
    for (int i = 0; i < g_req_owned_count; i++) free_object_node(g_req_owned_objects[i]);
    g_req_owned_count = 0;
}

/* v0.0.16 — @auth decorator helpers. */
extern char       *te_jwt_verify_alloc(const char *token, const char *secret);
extern const char *typeeasy_http_get_header(const char *k);
extern void        typeeasy_set_current_claims(const char *json);

/* Pending validation error for the current request (NULL when valid).
 * Set by te_bind_param when a typed-class body fails validation; consumed
 * by typeeasy_embedded_invoke_method which replies HTTP 422. */
static char *g_param_validation_error = NULL;

/* Bind a single MethodNode parameter before interpret_ast.
 * Resolution order:
 *   1) If type is a registered class  -> parse request body JSON into ObjectNode.
 *   2) Else look up path-param, query-param (string sources).
 * The parameter is then exposed as a local variable in the interpreter scope. */
static void te_bind_param(ParameterNode *p) {
    if (!p || !p->name) return;
    const char *ptype = p->type ? p->type : "";

    /* (1) Typed-class parameter <- request body JSON */
    if (ptype && *ptype) {
        ClassNode *cls = find_class((char*)ptype);
        if (cls) {
            const char *body = typeeasy_http_get_body();
            /* FastAPI-style automatic validation: reject malformed bodies
             * with HTTP 422 before the handler runs. Only the first failing
             * typed-class parameter's error is reported. */
            char *verr = te_validate_body_against_class(cls, body ? body : "");
            if (verr) {
                if (!g_param_validation_error) g_param_validation_error = verr;
                else free(verr);
                return;
            }
            ObjectNode *obj = te_object_from_json(cls, body ? body : "");
            if (obj) {
                te_req_owned_register(obj);
                ASTNode *wrap = (ASTNode*)calloc(1, sizeof(ASTNode));
                wrap->type  = strdup("OBJECT");
                wrap->id    = strdup(cls->name);
                wrap->extra = (struct ASTNode*)obj;
                add_or_update_variable(p->name, wrap);
                /* wrap held only as a transient carrier; the Variable now
                 * owns the ObjectNode pointer. Free the wrapper shell. */
                free(wrap->type); free(wrap->id); free(wrap);
            }
            return;
        }
    }

    /* (2) Primitive: take from path-params, then query-params. */
    const char *sval = typeeasy_http_find_param(p->name);
    if (!sval) sval = typeeasy_http_find_query(p->name);
    if (!sval) return;

    if (ptype && strcmp(ptype, "int") == 0) {
        ASTNode *n = create_ast_leaf_number((char*)"INT", atoi(sval), NULL, NULL);
        add_or_update_variable(p->name, n);
        free_ast(n);
    } else if (ptype && strcmp(ptype, "float") == 0) {
        ASTNode *n = create_ast_leaf((char*)"FLOAT", 0, (char*)sval, NULL);
        add_or_update_variable(p->name, n);
        free_ast(n);
    } else {
        ASTNode *n = create_ast_leaf((char*)"STRING", 0, (char*)sval, NULL);
        add_or_update_variable(p->name, n);
        free_ast(n);
    }
}

/* Ejecuta un MethodNode* ya resuelto. Hot path. */
char* typeeasy_embedded_invoke_method(MethodNode* m) {
    if (!m || !m->body) return NULL;

    /* C#/.NET async semantics: record whether this handler was declared with
     * the `async` modifier so te_coop_yield_begin() knows whether to release
     * the invoke lock while parked in an await. A plain handler keeps the lock
     * (blocking/serialised); an `async` handler lets other requests overlap. */
    extern __thread int g_current_handler_async;
    g_current_handler_async = m->is_async;

    /* Mark that we are inside per-request handling so create_ast_leaf() does
     * NOT intern runtime-generated STRING leaves (they are unique per request
     * and would grow the immortal intern table without bound == memory leak).
     * Cleared at every return below. */
    extern int g_te_request_active;
    g_te_request_active++;

    if (!__ret_var_active) {
        memset(&__ret_var, 0, sizeof(Variable));
        __ret_var_active = 1;
    }
    if (__ret_var.vtype == VAL_STRING && __ret_var.value.string_value) {
        free(__ret_var.value.string_value);
        __ret_var.value.string_value = NULL;
    }
    if (__ret_var.id)   { free(__ret_var.id);   __ret_var.id = NULL; }
    if (__ret_var.type) { free(__ret_var.type); __ret_var.type = NULL; }
    __ret_var.vtype = VAL_INT;
    __ret_var.value.int_value = 0;
    return_flag = 0;

    /* v0.0.16: @auth decorator — require a valid Bearer JWT before the handler
     * runs. Secret comes from the JWT_SECRET environment variable. On success
     * the payload is exposed to the handler via current_claims(). */
    typeeasy_set_current_claims(NULL);
    if (m->requires_auth) {
        const char *secret = getenv("JWT_SECRET");
        const char *auth   = typeeasy_http_get_header("Authorization");
        const char *tok    = NULL;
        if (auth && strncmp(auth, "Bearer ", 7) == 0) tok = auth + 7;
        char *claims = (secret && secret[0] && tok)
                         ? te_jwt_verify_alloc(tok, secret) : strdup("");
        if (!claims || claims[0] == '\0') {
            if (claims) free(claims);
            typeeasy_http_set_status(401);
            runtime_reset_vars_to_initial_state();
            te_req_owned_free_all();
            g_te_request_active--;
            return strdup("{\"error\":\"unauthorized\"}");
        }
        typeeasy_set_current_claims(claims);
        free(claims);
    }

    /* v0.0.24: user-defined `@<name>` decorator guard. Runs a dev-defined global
     * guard lambda (e.g. `let login = fn() => { ... };` applied as `@login`)
     * before the handler. If the guard returns a falsy value (empty string, 0,
     * false) — or is misconfigured/not found — the request is denied, mirroring
     * the @auth reset pattern. The handler itself can read the authed identity by
     * calling the same helper again (e.g. session_nick()).
     *
     * v0.0.26: the guard may pick the denial HTTP status by calling
     * response_status(N) before returning a falsy value (e.g. 403, 500). If the
     * guard does not set a status, denial defaults to 401. A not-found/not-callable
     * guard always fails closed with 401 (its body never ran, so it cannot have
     * chosen a status). */
    if (m->guard_name && m->guard_name[0]) {
        int ok = te_invoke_decorator_guard(m->guard_name);
        if (ok != 1) {
            int deny_status = 401;
            if (ok == 0) {
                /* guard body ran and returned falsy: honor a status it set */
                int chosen = typeeasy_http_get_status();
                if (chosen > 0 && chosen != 200) deny_status = chosen;
            } else {
                fprintf(stderr,
                    "[TypeEasy] decorator guard '@%s' is not defined as a callable "
                    "lambda; denying request (fail-closed).\n", m->guard_name);
            }
            typeeasy_http_set_status(deny_status);
            runtime_reset_vars_to_initial_state();
            te_req_owned_free_all();
            g_te_request_active--;
            return strdup("{\"error\":\"unauthorized\"}");
        }
    }

    /* v0.0.13: bind declared handler parameters from HTTP request context. */
    g_param_validation_error = NULL;
    for (ParameterNode *p = m->params; p; p = p->next) te_bind_param(p);

    /* Typed body validation failed: short-circuit with HTTP 422 and the
     * error JSON as the response body. The handler body is never executed. */
    if (g_param_validation_error) {
        typeeasy_http_set_status(422);
        char *err = g_param_validation_error;
        g_param_validation_error = NULL;
        runtime_reset_vars_to_initial_state();
        te_req_owned_free_all();
        g_te_request_active--;
        return err;
    }

    debugger_push_frame(m->name ? m->name : "<endpoint>", m->body);
    interpret_ast(m->body);
    debugger_pop_frame();

    char* result = NULL;
    Variable* ret_var = find_variable("__ret__");
    if (ret_var && ret_var->vtype == VAL_STRING && ret_var->value.string_value) {
        result = strdup(ret_var->value.string_value);
    }
    runtime_reset_vars_to_initial_state();
    te_req_owned_free_all();
    g_te_request_active--;
    if (!result) result = strdup("");
    return result;
}

/* v0.1.0 — WebSocket lifecycle invocation. -----------------------------------
 * te_websocket.c calls these from the civetweb callbacks. The lifecycle bodies
 * live on the MethodNode: ws_on_open, body (= on_message), ws_on_close. We run
 * each one through the same machinery as a normal handler by temporarily
 * pointing m->body at the target body. Legacy flat [WebSocket] handlers
 * (ws_lifecycle == 0) keep their single-shot body, run via *_invoke_open, so
 * their behavior is unchanged. */
int typeeasy_ws_is_lifecycle(MethodNode *m) {
    return (m && m->ws_lifecycle) ? 1 : 0;
}

char *typeeasy_ws_invoke_open(MethodNode *m) {
    if (!m) return NULL;
    if (m->ws_lifecycle) {
        if (!m->ws_on_open) return NULL;       /* on_open is optional */
        ASTNode *saved = m->body;
        m->body = m->ws_on_open;
        char *r = typeeasy_embedded_invoke_method(m);
        m->body = saved;
        return r;
    }
    /* Legacy: the single-shot connect handler is m->body. */
    return typeeasy_embedded_invoke_method(m);
}

char *typeeasy_ws_invoke_message(MethodNode *m) {
    if (!m || !m->ws_lifecycle || !m->body) return NULL;
    /* Bind the declared on_message(param) name to the incoming frame text so
     * the handler can use it directly; request_body() also returns it. The
     * frame text was placed into the request body by the caller. */
    if (m->ws_msg_param) {
        const char *frame = typeeasy_http_get_body();
        ASTNode *n = create_ast_leaf((char*)"STRING", 0, (char*)(frame ? frame : ""), NULL);
        add_or_update_variable(m->ws_msg_param, n);
        free_ast(n);
    }
    return typeeasy_embedded_invoke_method(m);
}

char *typeeasy_ws_invoke_close(MethodNode *m) {
    if (!m || !m->ws_lifecycle || !m->ws_on_close) return NULL;
    ASTNode *saved = m->body;
    m->body = m->ws_on_close;
    char *r = typeeasy_embedded_invoke_method(m);
    m->body = saved;
    return r;
}

char* typeeasy_embedded_invoke(TypeEasyEmbeddedContext* ctx, const char* script_path, const char* function_name) {
    if (!ctx || !script_path || !function_name) return NULL;
    LoadedScript* script = find_loaded_script(ctx, script_path);
    if (!script) {
        if (te_verbose()) fprintf(stderr, "[TYPEEASY_API] Script no cargado: %s\n", script_path);
        return NULL;
    }
    MethodNode* m = typeeasy_find_method(function_name);
    if (!m) {
        if (te_verbose()) fprintf(stderr, "[TYPEEASY_API] Función no encontrada: %s\n", function_name);
        return NULL;
    }
    if (te_verbose()) printf("[TYPEEASY_API] Invocando: %s\n", function_name);
    return typeeasy_embedded_invoke_method(m);
}
