/* te_vm.h — Estado central del intérprete agrupado en UNA struct (Fase 3 del plan de deuda).
 * Paso A (este archivo): los antiguos globales sueltos vars[] / var_count / return_flag /
 * throw_flag / break_flag / continue_flag / return_node viven en g_vm.*; semántica idéntica.
 * Paso B (siguiente): pasar TeVM* como parámetro por módulo para que el intérprete sea
 * reentrante (varias VMs por proceso, tests unitarios en C, hilos en vez de prefork).
 * Regla: NO agregar globales nuevos al intérprete; el estado nuevo va aquí. */
#ifndef TE_VM_H
#define TE_VM_H

#include "ast.h"

typedef struct TeVM {
    Variable vars[MAX_VARS];   /* slots de variables (globales + locales por request) */
    int var_count;
    int return_flag;           /* un return está en vuelo */
    int throw_flag;            /* un throw está en vuelo */
    int break_flag;
    int continue_flag;
    ASTNode *return_node;      /* valor del return en vuelo */
} TeVM;

extern TeVM g_vm;

#endif /* TE_VM_H */
