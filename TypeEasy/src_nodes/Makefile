EXEC = typeeasy

BISON_FILE = parser.y
FLEX_FILE = parser.l
AST_FILE = ast.c

BISON_C = parser.tab.c
BISON_H = parser.tab.h
FLEX_C = lex.yy.c

BISON_OBJ = parser.tab.o
FLEX_OBJ = lex.yy.o
AST_OBJ = ast.o
STRVARS_OBJ = strvars.o

# Motor EasySpark
SPARK_DIR = src_nodes/easyspark
SPARK_OBJ = $(SPARK_DIR)/dataframe.o

# Código generado
GEN_CPP = generated.cpp
GEN_BIN = typeeasy_output

CC = gcc
CXX = g++
CFLAGS = -Wall -g
CXXFLAGS = -Wall -g -std=c++17

all: $(EXEC)

# Compilador TypeEasy
$(BISON_C) $(BISON_H): $(BISON_FILE)
	bison -d -o $(BISON_C) $(BISON_FILE) --warnings=none

$(FLEX_C): $(FLEX_FILE)
	flex -o $(FLEX_C) $(FLEX_FILE)

$(BISON_OBJ): $(BISON_C) $(BISON_H)
	$(CC) $(CFLAGS) -c $(BISON_C) -o $(BISON_OBJ)

$(FLEX_OBJ): $(FLEX_C)
	$(CC) $(CFLAGS) -c $(FLEX_C) -o $(FLEX_OBJ)

$(AST_OBJ): $(AST_FILE)
	$(CC) $(CFLAGS) -fno-semantic-interposition -c $(AST_FILE) -o $(AST_OBJ)

$(EXEC): $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ) $(STRVARS_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ) $(STRVARS_OBJ)

# EasySpark - compilar una vez
$(SPARK_OBJ): $(SPARK_DIR)/dataframe.cpp $(SPARK_DIR)/dataframe.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Programa generado desde TypeEasy
$(GEN_BIN): $(GEN_CPP) $(SPARK_OBJ)
	$(CXX) $(CXXFLAGS) $(GEN_CPP) $(SPARK_OBJ) -o $(GEN_BIN)

# Ejecutar el programa generado
run: $(GEN_BIN)
	./$(GEN_BIN)

# Ejecutar el compilador y luego el programa generado
full: $(EXEC)
	./$(EXEC) ../typeeasycode/main.te
	make run

clean:
	rm -f $(EXEC) $(GEN_BIN) $(GEN_CPP) $(BISON_C) $(BISON_H) $(FLEX_C) \
	      $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ) $(STRVARS_OBJ) $(SPARK_OBJ)
