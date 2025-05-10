#include <string.h>
#include <stdlib.h>
#include "strvars.h"

#define MAX_STRING_VARS 100

typedef struct {
    const char* name;
    const char* value;
} StringVar;

static StringVar string_vars[MAX_STRING_VARS];
static int string_var_count = 0;

int is_var_string(const char* name) {
    for (int i = 0; i < string_var_count; i++) {
        if (strcmp(string_vars[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char* get_var_string(const char* name) {
    for (int i = 0; i < string_var_count; i++) {
        if (strcmp(string_vars[i].name, name) == 0) {
            return string_vars[i].value;
        }
    }
    return NULL;
}

void set_var_string(const char* name, const char* value) {
    for (int i = 0; i < string_var_count; i++) {
        if (strcmp(string_vars[i].name, name) == 0) {
            string_vars[i].value = value;
            return;
        }
    }
    // nuevo
    if (string_var_count < MAX_STRING_VARS) {
        string_vars[string_var_count].name = name;
        string_vars[string_var_count].value = value;
        string_var_count++;
    }
}
