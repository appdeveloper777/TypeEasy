# Configurar XAMPP MySQL para Conexiones desde Docker

## Problema
MySQL en XAMPP está rechazando conexiones desde Docker con el error "Server has gone away".

## Causa
MySQL en XAMPP está configurado para solo aceptar conexiones desde `localhost` (127.0.0.1), pero Docker intenta conectarse desde una IP diferente usando `host.docker.internal`.

## Solución

### Opción 1: Modificar my.ini (Recomendado)

1. **Abre XAMPP Control Panel**

2. **Detén MySQL** (botón "Stop")

3. **Abre el archivo de configuración**
   - Haz clic en "Config" junto a MySQL
   - Selecciona "my.ini"

4. **Busca la línea `bind-address`**
   - Presiona `Ctrl+F` y busca: `bind-address`
   - Si encuentras una línea como:
     ```ini
     bind-address = 127.0.0.1
     ```
   - Cámbiala a:
     ```ini
     bind-address = 0.0.0.0
     ```
   - Si NO encuentras esa línea, agrégala en la sección `[mysqld]`:
     ```ini
     [mysqld]
     bind-address = 0.0.0.0
     port = 3308
     ```

5. **Guarda el archivo** (`Ctrl+S`)

6. **Reinicia MySQL** en XAMPP Control Panel

### Opción 2: Usar el servicio MySQL de Docker (Más Simple)

Si prefieres no modificar XAMPP, puedes usar el servicio MySQL que ya está configurado en `docker-compose.yml`:

1. **Asegúrate que el servicio MySQL de Docker esté corriendo:**
   ```bash
   docker compose up -d mysql
   ```

2. **Conéctate a MySQL y crea la base de datos:**
   ```bash
   docker exec -it typeeasy_mysql mysql -uroot -prootpassword
   ```

3. **Ejecuta el SQL:**
   ```sql
   CREATE DATABASE IF NOT EXISTS test_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
   USE test_db;
   
   CREATE TABLE usuarios (
       id INT AUTO_INCREMENT PRIMARY KEY,
       nombre VARCHAR(100),
       email VARCHAR(100),
       edad INT
   ) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
   
   INSERT INTO usuarios (nombre, email, edad) VALUES
   ('Juan Pérez', 'juan@example.com', 30),
   ('María García', 'maria@example.com', 25),
   ('Carlos López', 'carlos@example.com', 35);
   ```

4. **Cambia el código para usar el servicio Docker:**
   - En `servidor_api.c`, cambia la conexión de:
     ```c
     mysql_real_connect(mysql_conn, "host.docker.internal", "root", "", "test_db", 3308, NULL, 0)
     ```
   - A:
     ```c
     mysql_real_connect(mysql_conn, "mysql", "root", "rootpassword", "test_db", 3306, NULL, 0)
     ```

## Verificación

Después de aplicar cualquiera de las dos opciones, prueba:

```bash
curl http://localhost:8080/api/mysql/usuarios
```

Deberías ver:
```json
[
  {"id":1,"nombre":"Juan Pérez","email":"juan@example.com","edad":30},
  {"id":2,"nombre":"María García","email":"maria@example.com","edad":25},
  {"id":3,"nombre":"Carlos López","email":"carlos@example.com","edad":35}
]
```

## ¿Cuál opción elegir?

- **Opción 1 (XAMPP)**: Si necesitas acceder a la misma base de datos desde otras aplicaciones en tu máquina
- **Opción 2 (Docker MySQL)**: Más simple, aislado, y ya está configurado correctamente

**Recomendación:** Usa la Opción 2 (Docker MySQL) por simplicidad.
