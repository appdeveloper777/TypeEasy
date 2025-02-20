TypeEasy 

TypeEasy es un prototipo de un lenguaje tipado, lenguaje HECHO con C, PARA LOGRAR ESTO Bison y Flex son herramientas utilizadas para crear compiladores e intérpretes. Se utilizan juntas para generar analizadores sintácticos y léxicos. La idea es se mejor que Polar y no depender de Python.

Para modificar el código te.: 

1. Abrir con el terminal la carpeta src
2. Ejecutar estos comandos uno por uno
   
* flex parser.l
* bison -d -o parser.tab.c parser.y
* gcc -o typeeasy parser.tab.c lex.yy.c variables.c

3. Mover a typeeasy.exe a la carpeta bin

Para correr el codigo .te:
1. Abrir la carpeta bin
2. ./typeeasy ../typeeasycode/main.te
3. ![TypeEasy PL](https://github.com/user-attachments/assets/caa440ea-3142-4b2b-887f-107826b3f610)


