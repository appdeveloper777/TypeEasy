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
# Archivos del motor incluyendo la nueva API embebida
MOTOR_C_FILES = ast.c bytecode.c mysql_bridge.c postgres_bridge.c sqlserver_bridge.c db_params.c orm_bridge.c typeeasy_api.c typeeasy_api_server.c wasm_backend.c debugger.c te_builtins.c te_http.c te_json.c te_bytecode.c te_csv.c te_xlsx.c te_colcache.c te_stdlib.c te_bridge.c te_async.c te_evloop.c te_linq.c te_linq_ops.c te_math.c te_string.c te_list.c te_map.c
MOTOR_OBJS = $(MOTOR_C_FILES:.c=.o)

BISON_C = parser.tab.c
BISON_H = parser.tab.h
FLEX_C = lex.yy.c
BISON_OBJ = parser.tab.o
FLEX_OBJ = lex.yy.o
STRVARS_OBJ = strvars.o

# --- CIVETWEB (para el agente) ---
CIVETWEB_C = ../api_server/civetweb.c
CIVETWEB_OBJ = civetweb.o
TE_WS_C = ../api_server/te_websocket.c
TE_WS_OBJ = te_websocket.o

# Motor EasySpark
SPARK_DIR = easyspark
SPARK_OBJ = $(SPARK_DIR)/dataframe.o

CC = gcc
CXX = g++
# Nivel de optimización configurable. Producción usa -O3; para depurar con gdb
# (breakpoints fiables + inspección de variables sin "optimized out") usar
# `make TE_OPT=-O0`. El flag -g (símbolos) siempre está presente.
TE_OPT ?= -O3
# Flags SIMD dependientes de la arquitectura. -mavx2/-mbmi solo existen en
# x86/x86_64 y habilitan las rutas SIMD del motor (gated por __AVX2__). En
# ARM/aarch64 (Android, Raspberry Pi, Apple Silicon en Docker, runners arm64)
# esos flags no existen y romperian el build; al omitirlos el motor cae
# automaticamente a su camino escalar equivalente (te_colcache.c, ast.c).
# Se puede forzar manualmente con `make TE_ARCH_FLAGS="..."`.
UNAME_M := $(shell uname -m 2>/dev/null)
ifeq ($(filter x86_64 amd64 i686 i386,$(UNAME_M)),)
  TE_ARCH_FLAGS ?=
else
  TE_ARCH_FLAGS ?= -mavx2 -mbmi
endif
# TE_ENABLE_JIT=1: trace JIT compilado dentro del binario (OFF en runtime salvo
# TYPEEASY_JIT=1). Microsoft confirmo "No malware detected" para el binario.
CFLAGS = -Wall -g $(TE_OPT) $(TE_ARCH_FLAGS) -pthread -fopenmp -DTE_HAVE_OPENMP -DTE_ENABLE_JIT=1 -I../api_server -I/usr/include/postgresql -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET -DTE_HAVE_LIBCURL $(CFLAGS_EXTRA)
CXXFLAGS = -Wall -g $(TE_OPT) -std=c++17 -fopenmp
LIBS_SCRIPT = -lfl -lm -lmysqlclient -lpq -lsybdb -lpthread -ldl -lssl -lcrypto -lcurl -lgomp -lz
LIBS_AGENT = -lfl -lpthread -ldl -lm -lssl -lcrypto -lmysqlclient -lpq -lsybdb -lcurl -lgomp -lz

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

# Reglas para los archivos 'main'
$(MAIN_SCRIPT_OBJ): $(MAIN_SCRIPT_C)
	$(CC) $(CFLAGS) -c $(MAIN_SCRIPT_C) -o $(MAIN_SCRIPT_OBJ)
$(MAIN_AGENT_OBJ): $(MAIN_AGENT_C)
	$(CC) $(CFLAGS) -c $(MAIN_AGENT_C) -o $(MAIN_AGENT_OBJ)

# Compila civetweb.o
$(CIVETWEB_OBJ): $(CIVETWEB_C)
	$(CC) $(CFLAGS) -c $(CIVETWEB_C) -o $(CIVETWEB_OBJ)

# Compila te_websocket.o (WebSocket nativo para TypeEasy)
$(TE_WS_OBJ): $(TE_WS_C) ../api_server/te_websocket.h
	$(CC) $(CFLAGS) -c $(TE_WS_C) -o $(TE_WS_OBJ)
    
# Compila strvars.o
$(STRVARS_OBJ): strvars.c
	$(CC) $(CFLAGS) -c strvars.c -o $(STRVARS_OBJ)

# --- REGLAS DE EJECUTABLES ---
# 1. 'typeeasy' (intérprete para scripts) — ahora linkea civetweb para soporte --api
$(EXEC_SCRIPT): $(MAIN_SCRIPT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ) $(TE_WS_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC_SCRIPT) $(MAIN_SCRIPT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ) $(TE_WS_OBJ) $(LIBS_SCRIPT)

# 2. 'servidor_agent' (servidor del agente)
$(EXEC_AGENT): $(MAIN_AGENT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ) $(TE_WS_OBJ)
	$(CC) $(CFLAGS) -o $(EXEC_AGENT) $(MAIN_AGENT_OBJ) $(BISON_OBJ) $(FLEX_OBJ) $(MOTOR_OBJS) $(STRVARS_OBJ) $(CIVETWEB_OBJ) $(TE_WS_OBJ) $(LIBS_AGENT)

# --- REGLAS DE EASYSPARK ---
$(SPARK_OBJ): $(SPARK_DIR)/dataframe.cpp $(SPARK_DIR)/dataframe.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- REGLAS DE LIMPIEZA ---
clean:
	rm -f $(EXEC_SCRIPT) $(EXEC_AGENT) *.o $(BISON_C) $(BISON_H) $(FLEX_C) $(SPARK_OBJ)