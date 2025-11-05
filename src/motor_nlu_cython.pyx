# Archivo: src/motor_nlu_cython.pyx

# Implementación mínima en C dentro del módulo Cython para evitar
# llamadas a la API de Python que puedan provocar segfaults cuando
# el módulo es invocado desde código C embebido.

from libc.string cimport strdup, strlen, strstr
from libc.stdlib cimport malloc, free
from libc.stdio cimport snprintf


cdef public char* parse_nlu(char* input_bytes):
    cdef const char* tipo = b"desconocido"
    cdef const char* item = b""
    cdef int cantidad = 0

    if not input_bytes:
        # Retornar default
        return strdup(b'{"tipo":"desconocido","item":"","cantidad":0}')

    # Busqueda simple por substring (case-sensitive). Es suficiente
    # para la demo y evita usar la API de Python.
    if strstr(input_bytes, "menu") != NULL or strstr(input_bytes, "carta") != NULL:
        tipo = b"consultarMenu"
    elif strstr(input_bytes, "hola") == NULL and strstr(input_bytes, "gracias") == NULL:
        tipo = b"agregarItem"
        item = b"Item (desde C)"
        cantidad = 5
    else:
        tipo = b"desconocido"

    # Construir JSON simple
    cdef char buf[256]
    snprintf(buf, sizeof(buf), '{"tipo":"%s","item":"%s","cantidad":%d}', tipo, item, cantidad)
    return strdup(buf)


cdef public void free_nlu_string(char* s):
    if s != NULL:
        free(s)