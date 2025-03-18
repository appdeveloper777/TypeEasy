#include "variables.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

Variable variables[MAX_VARIABLES];
static char* variable_names[MAX_VARIABLES];
int variable_count = 0;

int add_variable(const char* name, VariableType type) {
    if (variable_count >= MAX_VARIABLES) return -1;
    variable_names[variable_count] = strdup(name);
    variables[variable_count].type = type;
    if (type == STRING_TYPE) {
        variables[variable_count].str = NULL;
    }
    return variable_count++;
}

Variable get_variable(const char* name) {
    int idx = get_variable_index(name);
    if (idx < 0) {
        Variable v;
        v.type  = UNDEFINED;
        v.num   = 0;
        v.fnum  = 0.0f;
        v.str   = NULL;
        v.bval  = false;
        return v;
    }
    return variables[idx];
}

int get_variable_index(const char* name) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variable_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void set_variable_int(int index, int value) {
    if (index >= 0 && index < variable_count && variables[index].type == INT_TYPE) {
        variables[index].num = value;
    }
}

void set_variable_float(int index, float value) {
    if (index >= 0 && index < variable_count && variables[index].type == FLOAT_TYPE) {
        variables[index].fnum = value;
    }
}

void set_variable_string(int index, const char* value) {
    if (index >= 0 && index < variable_count && variables[index].type == STRING_TYPE) {
        if (variables[index].str != NULL) {
            free(variables[index].str);
        }
        variables[index].str = strdup(value);
    }
}

void set_variable_bool(int index, bool value) {
    if (index >= 0 && index < variable_count && variables[index].type == BOOL_TYPE) {
        variables[index].bval = value;
    }
}

void print_variable(Variable var) {
    switch (var.type) {
        case INT_TYPE:
            printf("INT: %d\n", var.num);
            break;
        case FLOAT_TYPE:
            printf("FLOAT: %.2f\n", var.fnum);
            break;
        case STRING_TYPE:
            printf("STRING: %s\n", var.str ? var.str : "NULL");
            break;
        case BOOL_TYPE:
            printf("BOOL: %s\n", var.bval ? "true" : "false");
            break;
        default:
            printf("Unknown type\n");
            break;
    }
}

void filter_csv(const char *filename, const char *filter_column, const char *filter_value) {

}