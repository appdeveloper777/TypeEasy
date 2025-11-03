// src/symtab.h

#ifndef SYMTAB_H
#define SYMTAB_H

#define SIZE 211

typedef struct list_t {
    char *st_name;
    int st_type;
    int inf_type;
    int array_size;
    union {
        int ival;
        double fval;
        char cval;
    } val;
    struct list_t *next;
    int reg_name;
    void* vals; // Placeholder for array values
} list_t;

extern list_t *hash_table[SIZE];
extern int num_of_msg;
extern char *str_messages[];
extern void *main_decl_tree;
extern void *main_func_tree;

// Placeholder for types
#define INT_TYPE 1
#define REAL_TYPE 2
#define CHAR_TYPE 3
#define ARRAY_TYPE 4
#define POINTER_TYPE 5
#define FUNCTION_TYPE 6

list_t *lookup(char *name);

#endif
