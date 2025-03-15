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

CC = gcc
CFLAGS = -Wall -g

all: $(EXEC)

$(BISON_C) $(BISON_H): $(BISON_FILE)
	bison -d -o $(BISON_C) $(BISON_FILE)

$(FLEX_C): $(FLEX_FILE)
	flex -o $(FLEX_C) $(FLEX_FILE)

$(BISON_OBJ): $(BISON_C) $(BISON_H)
	$(CC) $(CFLAGS) -c $(BISON_C) -o $(BISON_OBJ)

$(FLEX_OBJ): $(FLEX_C)
	$(CC) $(CFLAGS) -c $(FLEX_C) -o $(FLEX_OBJ)

$(AST_OBJ): $(AST_FILE)
	$(CC) $(CFLAGS) -c $(AST_FILE) -o $(AST_OBJ)

$(EXEC): $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ)

clean:
	rm -f $(EXEC) $(BISON_C) $(BISON_H) $(FLEX_C) $(BISON_OBJ) $(FLEX_OBJ) $(AST_OBJ)