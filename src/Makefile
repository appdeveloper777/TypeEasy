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
#
# Conectores SQL OPCIONALES (default ON = build actual SIN cambios). Apagarlos
# permite un binario "sqlite-only" (p.ej. Android NDK/Bionic) que NO enlaza las
# libs pesadas de glibc. Ejemplo Android: make typeeasy WITH_MYSQL=0 WITH_PG=0 \
#   WITH_SYBASE=0 WITH_CURL=0 WITH_OPENMP=0. Con todo en 1, db_stubs_optional.c
# compila a NADA y se enlazan los bridges reales (cero cambio de comportamiento).
WITH_MYSQL  ?= 1
WITH_PG     ?= 1
WITH_SYBASE ?= 1
WITH_CURL   ?= 1
WITH_OPENMP ?= 1

MOTOR_C_FILES = ast.c bytecode.c db_params.c orm_bridge.c typeeasy_api.c typeeasy_api_server.c wasm_backend.c debugger.c te_builtins.c te_http.c te_json.c te_bytecode.c te_csv.c te_xlsx.c te_colcache.c te_stdlib.c te_bridge.c te_async.c te_evloop.c te_linq.c te_linq_ops.c te_math.c te_string.c te_list.c te_map.c db_stubs_optional.c

# Bridge real solo si su motor esta ON; si no, db_stubs_optional.c aporta stubs.
ifeq ($(WITH_MYSQL),1)
  MOTOR_C_FILES += mysql_bridge.c
endif
ifeq ($(WITH_PG),1)
  MOTOR_C_FILES += postgres_bridge.c
endif
ifeq ($(WITH_SYBASE),1)
  MOTOR_C_FILES += sqlserver_bridge.c
endif
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
#
# --- Defines + libs por feature (segun WITH_*). Default ON = build de siempre.
TE_DB_CFLAGS =
TE_DB_LIBS =
ifeq ($(WITH_MYSQL),1)
  TE_DB_CFLAGS += -DWITH_MYSQL
  TE_DB_LIBS += -lmysqlclient
endif
ifeq ($(WITH_PG),1)
  TE_DB_CFLAGS += -DWITH_PG -I/usr/include/postgresql
  TE_DB_LIBS += -lpq
endif
ifeq ($(WITH_SYBASE),1)
  TE_DB_CFLAGS += -DWITH_SYBASE
  TE_DB_LIBS += -lsybdb
endif
ifeq ($(WITH_CURL),1)
  TE_CURL_CFLAGS = -DTE_HAVE_LIBCURL
  TE_CURL_LIBS = -lcurl
else
  TE_CURL_CFLAGS =
  TE_CURL_LIBS =
endif
ifeq ($(WITH_OPENMP),1)
  TE_OMP_CFLAGS = -fopenmp -DTE_HAVE_OPENMP
  TE_OMP_LIBS = -lgomp
else
  TE_OMP_CFLAGS =
  TE_OMP_LIBS =
endif

CFLAGS = -Wall -g $(TE_OPT) $(TE_ARCH_FLAGS) -pthread $(TE_OMP_CFLAGS) -DTE_ENABLE_JIT=1 -I../api_server -DUSE_OPENSSL -DNO_SSL_DL -DOPENSSL_API_1_1 -DUSE_WEBSOCKET $(TE_DB_CFLAGS) $(TE_CURL_CFLAGS) $(CFLAGS_EXTRA)
CXXFLAGS = -Wall -g $(TE_OPT) -std=c++17 -fopenmp
# -lfl (runtime de flex) no existe en algunos toolchains (p.ej. Android NDK).
# parser.l define su propio yywrap(), asi que se puede enlazar sin -lfl pasando
# TE_FL_LIBS= (vacio). Default: -lfl (build de siempre intacto).
TE_FL_LIBS ?= -lfl
# Android Bionic trae pthread DENTRO de libc (no hay libpthread.a en el NDK r23+),
# asi que ahi se enlaza con TE_PTHREAD_LIBS= (vacio). Default: -lpthread (glibc).
TE_PTHREAD_LIBS ?= -lpthread
LIBS_SCRIPT = $(TE_FL_LIBS) -lm $(TE_DB_LIBS) $(TE_PTHREAD_LIBS) -ldl -lssl -lcrypto $(TE_CURL_LIBS) $(TE_OMP_LIBS) -lz
LIBS_AGENT = $(TE_FL_LIBS) $(TE_PTHREAD_LIBS) -ldl -lm -lssl -lcrypto $(TE_DB_LIBS) $(TE_CURL_LIBS) $(TE_OMP_LIBS) -lz

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