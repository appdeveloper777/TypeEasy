# Nombre del ejecutable
EXEC = steemit

# Archivos fuente
BISON_FILE = parser.y
FLEX_FILE = parser.l

# Archivos generados
BISON_C = parser.tab.c
BISON_H = parser.tab.h
FLEX_C = lex.yy.c
VARIABLE_C = variables.c

# Archivos objeto
BISON_OBJ = parser.tab.o
FLEX_OBJ = lex.yy.o

# Compilador y banderas
CC = gcc
CFLAGS = -Wall -g

# Regla principal
all: $(EXEC)

# Generar el analizador sintáctico con Bison
$(BISON_C) $(BISON_H): $(BISON_FILE)
	bison -d -t -o $(BISON_C) $(BISON_FILE)

# Generar el analizador léxico con Flex
$(FLEX_C): $(FLEX_FILE)
	flex -o $(FLEX_C) $(FLEX_FILE)

# Compilar los archivos objeto
$(BISON_OBJ): $(BISON_C) $(BISON_H)
	$(CC) $(CFLAGS) -c $(BISON_C) -o $(BISON_OBJ)

$(FLEX_OBJ): $(FLEX_C)
	$(CC) $(CFLAGS) -c $(FLEX_C) -o $(FLEX_OBJ)

# Enlazar el ejecutable
$(EXEC): $(BISON_OBJ) $(FLEX_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(BISON_OBJ) $(FLEX_OBJ) $(VARIABLE_C)

# Limpiar archivos generados
clean:
	rm -f $(EXEC) $(BISON_C) $(BISON_H) $(FLEX_C) $(BISON_OBJ) $(FLEX_OBJ)

.PHONY: all clean