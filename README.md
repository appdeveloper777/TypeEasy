Para modificar el código: 

1. Abrir con el terminal la carpeta src
2. Ejecutar estos comandos uno por uno
   
flex parser.l
bison -d -o parser.tab.c parser.y
gcc -o typeeasy parser.tab.c lex.yy.c variables.c

3. Mover a typeeasy.exe a la carpeta bin

Para correr el codigo .te:
1. Abrir la carpeta bin
2. ./typeeasy ../typeeasycode/main.te
3. ![Uploading image.png…]()

