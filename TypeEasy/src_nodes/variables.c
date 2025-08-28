#include <ctype.h>


#include <fcntl.h>

#include <string.h>
#include "variables.h"
#include <stdbool.h>
#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define CHUNK_SIZE (1024 * 1024 * 1024) // 1 GB
#define MAX_VARIABLES 100
#define MAX_LINE_LENGTH 1024
#define MAX_FIELDS 100 // Máximo número de campos por línea
#define MAX_FIELD_LENGTH 100 // Longitud máxima de un campo

#define INITIAL_CAPACITY 1048575
struct Row {
    char *data;
};



// Definiciones globales
Variable variables[MAX_VARIABLES];
char* variable_names[MAX_VARIABLES];
int variable_count = 0;



int row_count = 0;

#define BUFFER_SIZE 1024 * 1024 // 1 MB de buffer




// Estructura para representar una fila de datos CSV
typedef struct {
    char **fields; // Arreglo dinámico para columnas
    int field_count;
} CsvRowLib;

// Estructura para representar una colección de filas
typedef struct {
    CsvRowLib *rows;
    int row_count;
} CsvCollection;

// Prototipos de funciones para LINQ-like
typedef int (*Predicate)(CsvRowLib *row); // Función para filtrar
typedef void (*Projection)(CsvRowLib *row, char *output); // Función para transformar

void statement_body();


// Función para eliminar espacios en blanco al inicio y final de una cadena
char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

#define MAX_LINE 1024
//#define MAX_FIELDS 50  // Ajusta según el número máximo de columnas


// 🔹 Implementación de `strsep()` para Windows
char *custom_strsep(char **stringp, const char *delim) {
    if (*stringp == NULL) return NULL;
    char *token = *stringp;
    *stringp = strpbrk(token, delim);  // Busca el siguiente delimitador

    if (*stringp) {
        **stringp = '\0';  // Reemplaza el delimitador con `\0`
        (*stringp)++;      // Avanza el puntero
    }
    return token;
}

// 🔹 Función para leer el CSV de manera eficiente
void filter_csv(const char *filename, const char *filter_column, const char *filter_value) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error al abrir el archivo");
        return;
    }

    char buffer[BUFFER_SIZE];
    char *fields[MAX_FIELDS];
    int filter_index = -1;
    size_t bytes_read;

    // Leer encabezados
    if (fgets(buffer, sizeof(buffer), file)) {
        char *ptr = buffer;
        int i = 0;
        while ((fields[i] = custom_strsep(&ptr, ",\n")) != NULL && i < MAX_FIELDS) {
            if (strcmp(fields[i], filter_column) == 0) {
                filter_index = i;
            }
            i++;
        }
    }

    if (filter_index == -1) {
        printf("Columna '%s' no encontrada.\n", filter_column);
        fclose(file);
        return;
    }

    int row_count = 0;

    // Leer y procesar en bloques grandes
    while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file)) > 0) {
        buffer[bytes_read] = '\0';
        char *line = buffer;
        char *end = buffer + bytes_read;

        

        while (line < end) {
            char *next_line = memchr(line, '\n', end - line);
            if (next_line) {
                *next_line = '\0';
                next_line++;
            } else {
                break;
            }

            char *values[MAX_FIELDS];
            char *ptr = line;
            int i = 0;
            while ((values[i] = custom_strsep(&ptr, ",\n")) != NULL && i < MAX_FIELDS) {
                i++;
            }

            if (values[filter_index] && strcmp(values[filter_index], filter_value) == 0) {
               // printf("%s\n", line);
            }

            line = next_line;
           
        }
         row_count++;
       
    }

     printf("Fila %d: \n", row_count);

    fclose(file);
}



// Función para convertir una cadena a minúsculas
void to_lowercase(char *str) {
    for (; *str; ++str) *str = tolower(*str);
}

// Función para obtener el índice de una columna
int get_column_index(const char *header, const char *column_name) {
    char *header_copy = strdup(header);
    char *token = strtok(header_copy, ",");
    int index = 0;

    while (token) {
        to_lowercase(token);
        if (strcmp(token, column_name) == 0) {
            free(header_copy);
            return index;
        }
        token = strtok(NULL, ",");
        index++;
    }

    free(header_copy);
    return -1; // Campo no encontrado
}

// Función para liberar la memoria de una colección CSV
void free_collection(CsvCollection collection) {
    if (collection.rows == NULL) return;

    for (int i = 0; i < collection.row_count; i++) {
        if (collection.rows[i].fields == NULL) continue;

        for (int j = 0; j < collection.rows[i].field_count; j++) {
            if (collection.rows[i].fields[j] != NULL) {
                free(collection.rows[i].fields[j]);
                collection.rows[i].fields[j] = NULL;
            }
        }

        free(collection.rows[i].fields);
        collection.rows[i].fields = NULL;
    }

    free(collection.rows);
    collection.rows = NULL;
}

// Función para liberar la memoria de la colección
void free_csv_collection(CsvCollection *collection) {
    for (int i = 0; i < collection->row_count; i++) {
        for (int j = 0; j < collection->rows[i].field_count; j++) {
            free(collection->rows[i].fields[j]);
        }
        free(collection->rows[i].fields);
    }
    free(collection->rows);
}
  
// Función where: Filtrar filas según un criterio
CsvCollection where(CsvCollection collection, Predicate predicate) {
    CsvCollection filtered = {NULL, 0};

    for (int i = 0; i < collection.row_count; i++) {
        if (predicate(&collection.rows[i])) {
            filtered.rows = realloc(filtered.rows, sizeof(CsvRowLib) * (filtered.row_count + 1));
            if (!filtered.rows) {
                printf("Error de memoria al realocar\n");
                return filtered;
            }
            filtered.rows[filtered.row_count] = collection.rows[i];
            filtered.row_count++;
        }
    }

    printf("Filtered Results:%d \n", filtered.row_count);  // Imprimir los resultados filtrados
    printf("collection.row_count: %d \n", collection.row_count);

    return filtered;
}

// Función select: Proyectar filas a un formato de salida
void selectquery(CsvCollection collection, Projection projection) {
    char output[1024];
    for (int i = 0; i < collection.row_count; i++) {
        memset(output, 0, sizeof(output));
        projection(&collection.rows[i], output);
        printf("%s\n", output);
    }
}






// Función para dividir una línea en campos
void split_line(char *line, char **fields, int max_fields) {
    printf("SAPE ");
    char *token = strtok(line, ",");
    int i = 0;
   
    while (token && i < max_fields) {
        
        fields[i++] = token;
        token = strtok(NULL, ",");
    }
}

// Función para encontrar el índice de un campo en el encabezado
int find_field_index(char **header, int num_fields, const char *field_name) {
    for (int i = 0; i < num_fields; i++) {
        if (strcmp(header[i], field_name) == 0) {
            return i;
        }
    }
    return -1; // Campo no encontrado
}




// Ejemplo de proyección: Seleccionar nombre y edad
void project_name_age(CsvRowLib *row, char *output) {
    sprintf(output, "Name: %s, Age: %s", row->fields[1], row->fields[2]);
}

void open_csv(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error abriendo el archivo\n");
        return;
    }

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        printf("Error de memoria\n");
        fclose(file);
        return;
    }

    size_t capacity = INITIAL_CAPACITY;
    struct Row *rows = malloc(sizeof(struct Row) * capacity);
    if (!rows) {
        printf("Error de memoria\n");
        free(buffer);
        fclose(file);
        return;
    }

    int row_count = 0;
    int row_count_total = 0;
    size_t buffer_pos = 0;
    size_t bytes_read;

    char header[BUFFER_SIZE];
    if (!fgets(header, BUFFER_SIZE, file)) {
        printf("Error leyendo la fila de encabezados\n");
        free(buffer);
        free(rows);
        fclose(file);
        return;
    }
    header[strcspn(header, "\n")] = '\0';

    // Convertir el encabezado a minúsculas
    //to_lowercase(header);

    int column_index = get_column_index(header, "adult");
    if (column_index == -1) {
        printf("Columna 'adult' no encontrada\n");
        free(buffer);
        free(rows);
        fclose(file);
        return;
    }

    while ((bytes_read = fread(buffer + buffer_pos, 1, BUFFER_SIZE - buffer_pos, file)) > 0) {
        buffer_pos += bytes_read;
        char *line_start = buffer;
        char *line_end;

        while ((line_end = memchr(line_start, '\n', buffer_pos - (line_start - buffer))) != NULL) {
            *line_end = '\0';

            // Redimensionar el arreglo si es necesario
            if (row_count >= capacity) {
                capacity = (capacity * 3) / 2; // Incremento del 50%
                struct Row *temp = realloc(rows, sizeof(struct Row) * capacity);
                if (!temp) {
                    printf("Error de memoria al realocar\n");
                    free(buffer);
                    for (int i = 0; i < row_count; i++) {
                        free(rows[i].data);
                    }
                    free(rows);
                    fclose(file);
                    return;
                }
                rows = temp;
            }

            // Procesar la línea
            int in_quotes = 0;
            char *field_start = line_start;
            int current_column = 0;

           /* while (*line_start) {
                if (*line_start == '"') {
                    in_quotes = !in_quotes;
                } else if (*line_start == ',' && !in_quotes) {
                    *line_start = '\0';
                    if (current_column == 8) {
                       // to_lowercase(field_start); // Convertir a minúsculas
                        if (strcmp(field_start, "FALSE") == 0) {
                            rows[row_count].data = strdup(line_start);
                            row_count++;
                            break;
                        }
                    }
                    field_start = line_start + 1;
                    current_column++;
                }
                line_start++;
            }*/

            
            line_start = line_end + 1;
            row_count_total++;
        }

        buffer_pos -= (line_start - buffer);
        memmove(buffer, line_start, buffer_pos);
    }

    fclose(file);
    printf("Total de filas MEJOR SAPE: %d\n", row_count);
    printf("Total de filas MEJOR SAPE TOTAL: %d\n", row_count_total);

    for (int i = 0; i < row_count; i++) {
        free(rows[i].data);
    }

    free(buffer);
    free(rows);
}



Variable get_variable(const char* name) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variable_names[i], name) == 0) {
            return variables[i];
        }
    }
    Variable null_variable = {0};
    return null_variable;  // Devuelve una variable vacía si no se encuentra
}

int get_variable_index(const char* name) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variable_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void set_variable_int_for_loop(int index, int value) {
    //printf("Debug: Intentando asignar %d a variables[%d]\n", value, index);

    if (index >= 0 && index < variable_count) {
       // printf("Debug: Index %d está dentro del rango.\n", index);
        
        if (variables[index].type == INT_TYPE) {
            variables[index].num = value;
           // printf("Debug: Asignación exitosa, variables[%d].num = %d\n", index, variables[index].num);
        } else {
           // printf("Error: variables[%d] no es de tipo INT_TYPE (type = %d)\n", index, variables[index].type);
        }
    } else {
       // printf("Error: Index fuera de rango (%d)\n", index);
    }
}

void set_variable_int(int index, int value) {
    //printf("Debug: Intentando asignar %d a variables[%d]\n", value, index);

    if (index >= 0 && index < variable_count) {
       // printf("Debug: Index %d está dentro del rango.\n", index);
        
        if (variables[index].type == INT_TYPE) {
            variables[index].num = value;
           // printf("Debug: Asignación exitosa, variables[%d].num = %d\n", index, variables[index].num);
        } else {
           // printf("Error: variables[%d] no es de tipo INT_TYPE (type = %d)\n", index, variables[index].type);
        }
    } else {
       // printf("Error: Index fuera de rango (%d)\n", index);
    }
}

void set_variable_float(int index, float value) {
    if (index >= 0 && index < variable_count && variables[index].type == FLOAT_TYPE) {
        variables[index].fnum = value;
    }
}

void set_variable_string(int index, const char* value) {
    if (index >= 0 && index < variable_count && variables[index].type == STRING_TYPE) {
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
            printf("%d\n", var.num);
            break;
        case FLOAT_TYPE:
            printf("%lf\n", var.fnum);
            break;
        case STRING_TYPE:
            printf("%s\n", var.str ? var.str : "NULL");
            break;
        case BOOL_TYPE:
            printf("%s\n", var.bval ? "true" : "false");
            break;
        default:
            printf("Unknown type\n");
            break;
    }
}

int add_variable(const char* name, VariableType type) {
    if (variable_count >= MAX_VARIABLES) return -1;

    variable_names[variable_count] = strdup(name);
    variables[variable_count].type = type;
    return variable_count++;
}
