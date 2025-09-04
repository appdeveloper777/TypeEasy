#include <stdio.h>
#include <stdlib.h>

extern FILE *yyin;
extern int yyparse(void);

int main(int argc, char **argv) {
    if (argc > 1) {
        yyin = fopen(argv[1], "r");
        if (!yyin) {
            perror("No se pudo abrir el archivo");
            return 1;
        }
    }
    return yyparse();
}