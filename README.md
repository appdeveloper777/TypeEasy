## 🚀 TypeEasy  


<img width="1024" height="1024" alt="image" src="https://github.com/user-attachments/assets/48e2457c-74b3-4f07-81d1-e67f608c3432" />


TypeEasy es un prototipo de un lenguaje tipado, lenguaje HECHO con C, PARA LOGRAR ESTO Bison y Flex son herramientas utilizadas para crear compiladores e intérpretes. Se utilizan juntas para generar analizadores sintácticos y léxicos. La idea es ser mejor que Polar y no depender de Python.

Para modificar y correr el código .te: 

1. Abrir con el terminal la carpeta src_nodes
2. Hacer los cambios que consideres
3. Ejecutar en el terminal:
   
* make clean
* make
*  ./typeeasy ../typeeasycode/main.te
*  ./typeeasy ../typeeasycode/buble_for.te
*  ./typeeasy ../typeeasycode/crear_clase.te
*  ./typeeasy ../typeeasycode/machine_learning.te
*  ./typeeasy ../typeeasycode/main.te
*  ./typeeasy ../typeeasycode/link_to_objects.te
![image](https://github.com/user-attachments/assets/d4617ae8-71f0-4270-9e70-ad00bd6694ab)

## 🚀 Ejecutar TypeEasy con Docker Compose

### 📦 Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) instalado y ejecutándose
- Git (opcional para clonar el repositorio)

---

### 🛠️ Cómo usar

Clona el repositorio o descarga el proyecto:

```bash
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy
```

Asegúrate de tener un archivo `.te` dentro de la carpeta `typeeasycode/`.  
Por ejemplo: `typeeasycode/main.te`

Construye la imagen de Docker:

```bash
docker compose build
```

Ejecuta un archivo `.te`:

```bash
docker compose run --rm typeeasy main.te
```

✅ Esto ejecutará `/code/main.te` dentro del contenedor, usando el ejecutable `typeeasy`.

---

## ✍️ Escribir y ejecutar código TypeEasy

### 🧾 1. Crea tu archivo `.te` dentro de `typeeasycode/`

Ejemplo: `typeeasycode/hola.te`

```te
imprimir("¡Hola, mundo!")
```

> Asegúrate de guardar el archivo con extensión `.te`

---

### ▶️ 2. Ejecuta tu archivo `.te` con Docker Compose

```bash
docker compose run --rm typeeasy hola.te
```

---

## 🧹 Limpieza

Para evitar que se acumulen contenedores al ejecutar muchas veces:

```bash
docker compose run --rm typeeasy archivo.te
```

Si necesitas limpiar contenedores antiguos manualmente:

```bash
docker container prune
```

---

## 🧠 Consejos útiles

- Puedes crear tantos archivos `.te` como quieras en `typeeasycode/`
- Para inspeccionar el contenedor directamente:

```bash
docker compose run --rm --entrypoint sh typeeasy
```

- Para ver el contenido dentro del contenedor:

```bash
docker compose run --rm --entrypoint ls typeeasy -l /code
```

---

## 👨‍💻 Autor

Desarrollado por [@appdeveloper777](https://github.com/appdeveloper777)


Diagrama de Flujo:
![image](https://github.com/user-attachments/assets/120f6734-bf12-4bbe-aedf-ba4372f169f9)




