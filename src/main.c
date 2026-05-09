#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugger.h"

extern FILE *yyin;
extern int yyparse(void);

int main(int argc, char **argv) {
    int debug_port = 0;
    const char *script_path = NULL;

    /* CLI:  typeeasy [--debug-port N] <script.te> */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug-port") == 0 && i + 1 < argc) {
            debug_port = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--debug-port=", 13) == 0) {
            debug_port = atoi(argv[i] + 13);
        } else if (!script_path) {
            script_path = argv[i];
        }
    }

    if (script_path) {
        yyin = fopen(script_path, "r");
        if (!yyin) {
            perror("No se pudo abrir el archivo");
            return 1;
        }
    }

    if (debug_port > 0) {
        debugger_init(debug_port, script_path ? script_path : "<stdin>");
    }

    int rc = yyparse();
    debugger_terminate(rc);
    return rc;
}