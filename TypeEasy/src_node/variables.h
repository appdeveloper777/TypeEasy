#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdbool.h>
#include <stdio.h>  

#define MAX_VARIABLES 100

typedef enum {
    INT_TYPE,
    FLOAT_TYPE,
    STRING_TYPE,
    BOOL_TYPE,
    UNDEFINED
} VariableType;

// Estructura principal 'Variable'
typedef struct {
    VariableType type;
    int   num;    
    float fnum;  
    char* str;    
    bool  bval;   
} Variable;

extern Variable variables[MAX_VARIABLES];
int add_variable(const char* name, VariableType type);
Variable get_variable(const char* name);
int get_variable_index(const char* name);

void set_variable_int(int index, int value);
void set_variable_float(int index, float value);
void set_variable_string(int index, const char* value);
void set_variable_bool(int index, bool value);
void print_variable(Variable var);


typedef struct {
    char **fields;   
    int field_count; 
} CSVRow;

typedef struct {
    char  **headers;
    int   header_count;
    CSVRow *rows;
    int   row_count;
    int   capacity;
} CSVData;

int read_and_print_csv(const char *filename);
void filter_csv(const char *filename, const char *filter_column, const char *filter_value);


#endif 
