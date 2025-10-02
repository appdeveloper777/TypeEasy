# Instrucciones de Build para TypeEasy

Este documento proporciona instrucciones detalladas para construir TypeEasy desde el código fuente.

## Índice
1. [Requisitos del Sistema](#requisitos-del-sistema)
2. [Instalación de Dependencias](#instalación-de-dependencias)
3. [Proceso de Compilación](#proceso-de-compilación)
4. [Solución de Problemas](#solución-de-problemas)
5. [Verificación de la Instalación](#verificación-de-la-instalación)

## Requisitos del Sistema

### Requisitos Mínimos
- Sistema operativo Linux/Unix
- 1GB de RAM
- 100MB de espacio en disco
- Acceso a internet para descargar dependencias

### Software Necesario
- Git (para clonar el repositorio)
- Compilador GCC/G++
- Make
- Flex
- Bison

## Instalación de Dependencias

### En Ubuntu/Debian
```bash
# Actualizar el sistema
sudo apt-get update
sudo apt-get upgrade

# Instalar herramientas básicas de desarrollo
sudo apt-get install build-essential

# Instalar dependencias específicas
sudo apt-get install git flex bison gcc g++ make
```

### En Fedora/RHEL
```bash
# Actualizar el sistema
sudo dnf update

# Instalar herramientas de desarrollo
sudo dnf groupinstall "Development Tools"

# Instalar dependencias específicas
sudo dnf install git flex bison gcc gcc-c++ make
```

### En macOS
```bash
# Instalar Homebrew si no está instalado
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Instalar dependencias
brew install flex bison gcc make
```

## Proceso de Compilación

1. **Clonar el Repositorio**
```bash
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy
```

2. **Preparar el Entorno de Compilación**
```bash
cd src
```

3. **Limpiar Compilaciones Anteriores**
```bash
make clean
```

4. **Compilar el Proyecto**
```bash
make
```

El proceso de compilación generará varios archivos:
- `parser.tab.c` y `parser.tab.h` (generados por Bison)
- `lex.yy.c` (generado por Flex)
- Varios archivos objeto (.o)
- El ejecutable final `typeeasy`

## Solución de Problemas

### Errores Comunes y Soluciones

1. **Error: bison: command not found**
   ```bash
   sudo apt-get install bison
   ```

2. **Error: flex: command not found**
   ```bash
   sudo apt-get install flex
   ```

3. **Error: make: command not found**
   ```bash
   sudo apt-get install make
   ```

4. **Error de compilación relacionado con gcc/g++**
   ```bash
   sudo apt-get install build-essential
   ```

### Verificación de Versiones
Puedes verificar las versiones instaladas con:
```bash
gcc --version
g++ --version
make --version
bison --version
flex --version
```

## Verificación de la Instalación

1. **Verificar el Ejecutable**
```bash
# En el directorio src/
./typeeasy --version  # o el comando equivalente para ver la versión
```

2. **Ejecutar un Programa de Prueba**
```bash
# Desde el directorio src/
./typeeasy ../typeeasycode/crear_const_variable.te
```

3. **Verificar la Salida**
El programa debería ejecutarse sin errores y mostrar la salida esperada.

## Estructura del Proyecto Compilado

Después de una compilación exitosa, deberías ver la siguiente estructura:
```
src/
├── typeeasy (ejecutable principal)
├── *.o (archivos objeto)
├── parser.tab.c
├── parser.tab.h
└── lex.yy.c
```

## Notas Adicionales

- Los archivos generados durante la compilación se pueden limpiar con `make clean`
- Para recompilar después de cambios, usa `make clean && make`
- El ejecutable `typeeasy` se crea en el directorio `src/`
- Los archivos de ejemplo se encuentran en `typeeasycode/`

## Personalización del Build

Si necesitas personalizar el proceso de compilación, puedes modificar el `Makefile` en el directorio `src/`. Las principales variables que puedes modificar son:
- `CC`: Compilador de C
- `CXX`: Compilador de C++
- `CFLAGS`: Flags de compilación para C
- `CXXFLAGS`: Flags de compilación para C++
