# --- EJECUTABLES ---
EXEC_SCRIPT = typeeasy
EXEC_AGENT = servidor_agent

# --- ARCHIVOS 'main' ---
MAIN_SCRIPT_C = typeeasy_main.c
MAIN_AGENT_C = servidor_agent.c
MAIN_SCRIPT_OBJ = $(MAIN_SCRIPT_C:.c=.o)
MAIN_AGENT_OBJ = $(MAIN_AGENT_C:.c=.o)

# --- ARCHIVOS DEL "MOTOR" ---
BISON_FILE = parser.y
FLEX_FILE = parser.l
# (Asumiendo que estos son todos tus archivos de motor)
MOTOR_C_FILES = ast.c bytecode.c 
MOTOR_OBJS = $(MOTOR_C_FILES:.c=.o)

BISON_C = parser.tab.c
BISON_H = parser.tab.h
FLEX_C = lex.yy.c
BISON_OBJ = parser.tab.o
FLEX_OBJ = lex.yy.o
STRVARS_OBJ = strvars.o # (Asumiendo que tienes strvars.c)

# --- CIVETWEB (para el agente) ---
CIVETWEB_C = ../api_server/civetweb.c
CIVETWEB_OBJ = civetweb.o

# Motor EasySpark (basado en tu Makefile original)
SPARK_DIR = easyspark
SPARK_OBJ = $(SPARK_DIR)/dataframe.o

CC = gcc
CXX = g++
# ¡¡ARREGLO AQUÍ!! Añadidas las flags de OpenSSL
CFLAGS = -Wall -g -I../api_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1
CXXFLAGS = -Wall -g -std=c++17
LIBS_SCRIPT = -lfl -lm
# ¡¡ARREGLO AQUÍ!! Añadidas las bibliotecas de OpenSSL
LIBS_AGENT = -lfl -lpthread -ldl -lm -lssl -lcrypto

# --- OBJETIVO PRINCIPAL ---
all: $(EXEC_SCRIPT) $(EXEC_AGENT)

# --- REGLAS DE BISON/FLEX ---
$(BISON_C) $(BISON_H): $(BISON_FILE)
	bison -d -o $(BISON_C) $(BISON_FILE) --warnings=none
$(FLEX_C): $(FLEX_FILE)
	flex -o $(FLEX_C) $(FLEX_FILE)

# --- REGLAS DE OBJETOS ---
# Regla genérica para compilar todos los .c del motor
$(MOTOR_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -fno-semantic-interposition -c $< -o $@

$(BISON_OBJ): $(BISON_C) $(BISON_H)
	$(CC) $(CFLAGS) -c $(BISON_C) -o $(BISON_OBJ)
$(FLEX_OBJ): $(FLEX_C)
	$(CC) $(CFLAGS) -c $(FLEX_C) -o $(FLEX_OBJ)

# Reglas para los NUEVOS archivos 'main'
$(MAIN_SCRIPT_OBJ): $(MAIN_SCRIPT_C)
	$(CC) $(CFLAGS) -c $(MAIN_SCRIPT_C) -o $(MAIN_SCRIPT_OBJ)
$(MAIN_AGENT_OBJ): $(MAIN_AGENT_C)
	$(CC) $(CFLAGS) -c $(MAIN_AGENT_C) -o $(MAIN_AGENT_OBJ)

# Compila civetweb.o con las CFLAGS correctas
$(CIVETWEB_OBJ): $(CIVETWEB_C)
	$(CC) $(CFLAGS) -c $(CIVETWEB_C) -o $(CIVETWEB_OBJ)
    
# (Asumiendo que tienes strvars.c)
$(STRVARS_OBJ): strvars.c
	$(CC) $(CFLAGS) -c strvars.c -o $(STRVARS_OBJ)

# --- REGLAS DE EJECUTABLES ---
# 1. 'typeeasy' (para tu API)
$(EXEC_SCRIPT): $(MAIN_SCRIPT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC_SCRIPT) $(MAIN_SCRIPT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(LIBS_SCRIPT)

# 2. 'servidor_agent' (para tu Agente)
$(EXEC_AGENT): $(MAIN_AGENT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC_AGENT) $(MAIN_AGENT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ) $(LIBS_AGENT)

# --- REGLAS DE EASYSPARK (sin cambios) ---
$(SPARK_OBJ): $(SPARK_DIR)/dataframe.cpp $(SPARK_DIR)/dataframe.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- REGLAS DE LIMPIEZA ---
clean:
	rm -f $(EXEC_SCRIPT) $(EXEC_AGENT) *.o $(BISON_C) $(BISON_H) $(FLEX_C) $(SPARK_OBJ)