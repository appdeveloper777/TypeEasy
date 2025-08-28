#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdbool.h>  // Para usar el tipo bool
#include <stdio.h>  // Asegúrate de que esta línea esté incluida
// Estructura para almacenar una fila del CSV

#define MAX_STATEMENTS 100

// Constantes para tipos de declaraciones
#define STMT_PRINT 1
#define STMT_ASSIGN 2
#define STMT_ADD 3       // Suma
#define STMT_SUBTRACT 4  // Resta
#define STMT_MULTIPLY 5  // Multiplicación
#define STMT_ACCUMULATE 6

    // Definición de tipos de declaraciones
  

// Estructuras
typedef struct {
    int type; // Tipo de declaración (STMT_PRINT, STMT_ASSIGN, etc.)
    char identifier[50]; // Nombre de la variable
    int value; // Valor (para asignaciones)
    int operand1; // Primer operando (para operaciones)
    int operand2; // Segundo operando (para operaciones)
} Statement;

typedef struct {
    Statement statements[MAX_STATEMENTS]; // Lista de declaraciones
    int count; // Número de declaraciones
} StatementBody;
typedef struct {
    char **fields;  // Array de campos (columnas) de la fila
    int field_count;  // Número de campos en la fila
} CSVRow;

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
