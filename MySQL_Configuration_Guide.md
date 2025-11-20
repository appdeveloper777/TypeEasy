# Guía de Configuración MySQL para TypeEasy

## ✅ Configuración Flexible Implementada

El endpoint MySQL ahora es **completamente configurable** y puede conectarse a cualquier servidor MySQL:
- XAMPP local
- MySQL en Docker
- Servidor MySQL remoto
- Cualquier otro servidor MySQL

---

## 🔧 Cómo Configurar

### Opción 1: Editar docker-compose.yml (Recomendado)

Abre `docker-compose.yml` y modifica las variables de entorno en el servicio `api`:

```yaml
api:
  environment:
    - MYSQL_HOST=host.docker.internal  # Cambia según tu servidor
    - MYSQL_PORT=3308                   # Puerto de tu MySQL
    - MYSQL_USER=root                   # Usuario
    - MYSQL_PASSWORD=                   # Contraseña (vacía si no tienes)
    - MYSQL_DATABASE=test_db            # Nombre de la base de datos
```

**Ejemplos de configuración:**

#### Para XAMPP en Windows:
```yaml
- MYSQL_HOST=host.docker.internal
- MYSQL_PORT=3308
- MYSQL_USER=root
- MYSQL_PASSWORD=
- MYSQL_DATABASE=test_db
```

#### Para MySQL de Docker:
```yaml
- MYSQL_HOST=mysql
- MYSQL_PORT=3306
- MYSQL_USER=root
- MYSQL_PASSWORD=rootpassword
- MYSQL_DATABASE=test_db
```

#### Para servidor remoto:
```yaml
- MYSQL_HOST=192.168.1.100
- MYSQL_PORT=3306
- MYSQL_USER=mi_usuario
- MYSQL_PASSWORD=mi_password
- MYSQL_DATABASE=mi_base_datos
```

### Opción 2: Archivo .env

Crea un archivo `.env` en la raíz del proyecto:

```env
MYSQL_HOST=host.docker.internal
MYSQL_PORT=3308
MYSQL_USER=root
MYSQL_PASSWORD=
MYSQL_DATABASE=test_db
```

Luego modifica `docker-compose.yml`:

```yaml
api:
  environment:
    - MYSQL_HOST=${MYSQL_HOST}
    - MYSQL_PORT=${MYSQL_PORT}
    - MYSQL_USER=${MYSQL_USER}
    - MYSQL_PASSWORD=${MYSQL_PASSWORD}
    - MYSQL_DATABASE=${MYSQL_DATABASE}
```

---

## 📝 Valores por Defecto

Si NO configuras las variables de entorno, se usarán estos valores:

| Variable | Valor por Defecto |
|----------|-------------------|
| MYSQL_HOST | `host.docker.internal` |
| MYSQL_PORT | `3308` |
| MYSQL_USER | `root` |
| MYSQL_PASSWORD | `` (vacío) |
| MYSQL_DATABASE | `test_db` |

---

## 🚀 Aplicar Cambios

Después de modificar la configuración:

```bash
docker compose build api
docker compose up -d api
```

---

## 🔍 Verificar Conexión

El endpoint ahora muestra información detallada si falla la conexión:

```bash
curl http://localhost:8080/api/mysql/usuarios
```

Si hay error, verás algo como:
```
MySQL connection failed: Access denied (host=host.docker.internal, port=3308, user=root, db=test_db)
```

Esto te ayuda a identificar qué parámetro está mal configurado.

---

## ✅ Configurar XAMPP para Conexiones Remotas

Si usas XAMPP, necesitas permitir conexiones desde Docker:

1. Abre XAMPP Control Panel → Detén MySQL
2. Click en "Config" → "my.ini"
3. Busca o agrega en la sección `[mysqld]`:
   ```ini
   bind-address = 0.0.0.0
   ```
4. Guarda y reinicia MySQL

---

## 📊 Ejemplo Completo

### 1. Configurar docker-compose.yml
```yaml
api:
  environment:
    - MYSQL_HOST=host.docker.internal
    - MYSQL_PORT=3308
    - MYSQL_USER=root
    - MYSQL_PASSWORD=
    - MYSQL_DATABASE=meri
```

### 2. Crear la base de datos en XAMPP
```sql
CREATE DATABASE IF NOT EXISTS meri CHARACTER SET utf8mb4;
USE meri;

CREATE TABLE usuarios (
    id INT AUTO_INCREMENT PRIMARY KEY,
    nombre VARCHAR(100),
    email VARCHAR(100),
    edad INT
) CHARACTER SET utf8mb4;

INSERT INTO usuarios VALUES
(1, 'Juan Pérez', 'juan@example.com', 30),
(2, 'María García', 'maria@example.com', 25);
```

### 3. Rebuild y probar
```bash
docker compose build api && docker compose up -d api
curl http://localhost:8080/api/mysql/usuarios
```

---

## 🎯 Ventajas de esta Implementación

✅ **Flexible**: Funciona con cualquier servidor MySQL  
✅ **Configurable**: Sin necesidad de recompilar código  
✅ **Portable**: Fácil de mover entre entornos  
✅ **Debuggable**: Mensajes de error detallados  
✅ **Seguro**: Contraseñas en variables de entorno, no en código  

---

¡Ahora puedes conectarte a cualquier servidor MySQL simplemente cambiando las variables de entorno!
