#ifndef VARIABLES_H
#define VARIABLES_H
#define MAX_LINE_LENGTH 1024

#include <stdbool.h>  // Para usar el tipo bool
#include <stdio.h>  // Asegúrate de que esta línea esté incluida
// Estructura para almacenar una fila del CSV
typedef struct {
    char **fields;  // Array de campos (columnas) de la fila
    int field_count;  // Número de campos en la fila
} CSVRow;

typedef struct {
    char **headers;       // Array de encabezados (nombres de columnas)
    int header_count;     // Número de encabezados
    CSVRow *rows;         // Array de filas
    int row_count;        // Número de filas
    int capacity;         // Capacidad actual del array de filas
} CSVData;

typedef enum {
    INT_TYPE,
    FLOAT_TYPE,
    STRING_TYPE,
    BOOL_TYPE,
    UNDEFINED
} VariableType;

typedef struct {
    VariableType type;  // Para identificar el tipo de variable
    int num;
    float fnum;
    char* str;
    bool bval;
} Variable;



// Declaraciones de funciones
int read_and_print_csv(const char *filename);
void statement_body();


// Funciones para variables
int add_variable(const char* name, VariableType type);
Variable get_variable(const char* name);
int get_variable_index(const char* name);
void set_variable_int(int index, int value);
void set_variable_float(int index, float value);
void set_variable_string(int index, const char* value);
void set_variable_bool(int index, bool value);
void print_variable(Variable var);

#endif
