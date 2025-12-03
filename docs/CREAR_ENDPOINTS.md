# 🚀 Cómo Crear Endpoints con TypeEasy

TypeEasy te permite crear APIs REST de forma simple y rápida, usando sintaxis TypeEasy.

---

## 📋 Tabla de Contenidos

1. [Conceptos Básicos](#conceptos-básicos)
2. [Tu Primer Endpoint](#tu-primer-endpoint)
3. [Ejemplo Real: Consultar MySQL](#ejemplo-real-consultar-mysql)
4. [Estructura de Archivos](#estructura-de-archivos)
5. [Ejecutar tus Endpoints](#ejecutar-tus-endpoints)

---

## 🎯 Conceptos Básicos

### ¿Qué es un Endpoint?

Un endpoint es una URL que tu API expone para que otros servicios puedan consumirla. Por ejemplo:

```
GET  http://localhost:8080/api/usuarios
GET  http://localhost:8080/api/productos
POST http://localhost:8080/api/pedidos
```

### Estructura de un Archivo `.te`

Los endpoints se definen en archivos `.te` dentro de la carpeta `typeeasycode/apis/`:

```
TypeEasy/
└── typeeasycode/
    └── apis/
        ├── usuarios_endpoint.te
        ├── productos_endpoint.te
        └── pedidos_endpoint.te
```

---

## 🚀 Tu Primer Endpoint

### Paso 1: Crear el Archivo

Crea un archivo `typeeasycode/apis/proveedores_endpoint.te`:

```ts
class OrdenDeCompra {
    proveedor: string; 
    fecha: string;

    __constructor(_proveedor: string, _fecha: string) {
        this.proveedor = _proveedor;
        this.fecha = _fecha;
    }   
}

endpoint {
    [HttpGet("/api/proveedores")]
    GetProveedores() {
        let mi_orden = new OrdenDeCompra("Suministros Industriales S.A.", "2025-09-06");
        return json(mi_orden);
    }
}
```

### Paso 2: Levantar el Servidor

```bash
docker compose up -d --build api
```

### Paso 3: Probar el Endpoint

```bash
curl http://localhost:8080/api/proveedores
```

**Respuesta:**
```json
{
    "proveedor": "Suministros Industriales S.A.",
    "fecha": "2025-09-06"
}
```

---

## 💡 Ejemplo Real: Consultar MySQL

Este es un ejemplo completo basado en `usuarios_endpoint.te` que consulta una base de datos MySQL:

```ts
/********************************************************************
 * TypeEasy - Ejemplo de API con MySQL
 * 
 * Copyright (c) 2025 Tu Nombre
 * Licencia: MIT
 ********************************************************************/

// ======================= IMPORTACIONES ============================
import "models/Usuario.te";          // Modelo ORM para la tabla usuarios
import "settings/mysql_config.te";   // Configuración global de MySQL

// ======================= ENDPOINT ================================
endpoint {
  [HttpGet("/api/usuarios")]
  GetUsuarios() {
      string global_query = "SELECT * FROM usuarios LIMIT 10";

      // Conectar a MySQL
      let _conn_id = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
      
      // Ejecutar query con ORM
      let _usuarios = orm_query(_conn_id, global_query, UsuarioModel);
      
      // Cerrar conexión
      mysql_close(_conn_id);

      // Retornar resultados en XML
      return xml(_usuarios);
  }
}
```

### Archivos Necesarios

#### 1. Modelo: `typeeasycode/models/Usuario.te`

```ts
// Definir el modelo de Usuario
class UsuarioModel {
    int id;
    string nombre;
    string email;
    string created_at;
}
```

#### 2. Configuración: `typeeasycode/settings/mysql_config.te`

```ts
// Configuración global de MySQL
string global_host = "mysql";
string global_user = "root";
string global_pass = "password";
string global_db = "mi_base_datos";
int global_port = 3306;
```

### Probar el Endpoint

```bash
curl http://localhost:8080/api/usuarios
```

**Respuesta:**
```xml
<usuarios>
  <usuario>
    <id>1</id>
    <nombre>Juan Pérez</nombre>
    <email>juan@example.com</email>
    <created_at>2024-01-01 10:00:00</created_at>
  </usuario>
  <usuario>
    <id>2</id>
    <nombre>María García</nombre>
    <email>maria@example.com</email>
    <created_at>2024-01-02 11:00:00</created_at>
  </usuario>
</usuarios>
```

---

## 📁 Estructura de Archivos

### Organización Recomendada

```
TypeEasy/
└── typeeasycode/
    ├── apis/                    # Endpoints HTTP
    │   ├── usuarios_endpoint.te
    │   ├── productos_endpoint.te
    │   └── pedidos_endpoint.te
    ├── models/                  # Modelos ORM
    │   ├── Usuario.te
    │   ├── Producto.te
    │   └── Pedido.te
    └── settings/                # Configuraciones
        ├── mysql_config.te
        └── app_config.te
```

---

## 🔧 Métodos HTTP

TypeEasy soporta los principales métodos HTTP:

### GET - Obtener Datos

```ts
endpoint {
    [HttpGet("/api/productos")]
    GetProductos() {
        string query = "SELECT * FROM productos";
        let conn = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
        let productos = orm_query(conn, query, ProductoModel);
        mysql_close(conn);
        return xml(productos);
    }
}
```

### POST - Crear Datos

```ts
endpoint {
    [HttpPost("/api/productos")]
    CrearProducto() {
        // Obtener datos del request body
        string nombre = request.body.nombre;
        string precio = request.body.precio;
        
        // Insertar en base de datos
        string query = "INSERT INTO productos (nombre, precio) VALUES ('" + nombre + "', " + precio + ")";
        let conn = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
        mysql_execute(conn, query);
        mysql_close(conn);
        
        return xml({"mensaje": "Producto creado", "nombre": nombre});
    }
}
```

### PUT - Actualizar Datos

```ts
endpoint {
    [HttpPut("/api/productos/{id}")]
    ActualizarProducto() {
        // Obtener ID de la ruta
        string id = request.params.id;
        string nombre = request.body.nombre;
        
        // Actualizar en base de datos
        string query = "UPDATE productos SET nombre = '" + nombre + "' WHERE id = " + id;
        let conn = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
        mysql_execute(conn, query);
        mysql_close(conn);
        
        return xml({"mensaje": "Producto actualizado", "id": id});
    }
}
```

### DELETE - Eliminar Datos

```ts
endpoint {
    [HttpDelete("/api/productos/{id}")]
    EliminarProducto() {
        string id = request.params.id;
        
        string query = "DELETE FROM productos WHERE id = " + id;
        let conn = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
        mysql_execute(conn, query);
        mysql_close(conn);
        
        return xml({"mensaje": "Producto eliminado", "id": id});
    }
}
```

---

## 🚀 Ejecutar tus Endpoints

### Desarrollo Local

```bash
# Construir y levantar el servidor API
docker compose up -d --build api

# Ver logs
docker compose logs -f api

# Probar endpoint
curl http://localhost:8080/api/usuarios
```

### Ver Todos los Endpoints

Los endpoints están disponibles en:
```
http://localhost:8080/api/usuarios
http://localhost:8080/api/productos
http://localhost:8080/api/pedidos
```

---

## 🔍 Debugging

### Ver Logs del Servidor

```bash
docker compose logs -f api
```

### Activar Modo Debug

En tu `.env`:
```env
TYPEEASY_DEBUG=1
```

Luego reinicia:
```bash
docker compose restart api
```

### Probar con curl

```bash
# GET
curl http://localhost:8080/api/usuarios

# POST
curl -X POST http://localhost:8080/api/productos \
  -H "Content-Type: application/json" \
  -d '{"nombre":"Laptop","precio":"1200"}'

# PUT
curl -X PUT http://localhost:8080/api/productos/1 \
  -H "Content-Type: application/json" \
  -d '{"nombre":"Laptop Pro"}'

# DELETE
curl -X DELETE http://localhost:8080/api/productos/1
```

---

## 💡 Mejores Prácticas

### 1. Organización de Archivos

```
typeeasycode/apis/
├── usuarios_endpoint.te      # Endpoints de usuarios
├── productos_endpoint.te     # Endpoints de productos
└── auth_endpoint.te          # Autenticación
```

### 2. Nomenclatura de Rutas

```te
// ✅ Bueno
[HttpGet("/api/usuarios")]
[HttpGet("/api/usuarios/{id}")]
[HttpPost("/api/usuarios")]

// ❌ Evitar
[HttpGet("/getUsers")]
[HttpGet("/user")]
```

### 3. Cerrar Conexiones

Siempre cierra las conexiones a la base de datos:

```te
let conn = new mysql_connect(...);
// ... usar la conexión
mysql_close(conn);  // ✅ Importante
```

### 4. Validación de Datos

```ts
endpoint {
    [HttpPost("/api/usuarios")]
    CrearUsuario() {
        string nombre = request.body.nombre;
        
        // Validar datos
        if (nombre == "" || nombre == null) {
            return xml({"error": "El nombre es requerido"});
        }
        
        // Crear usuario...
    }
}
```

---

## 📚 Recursos Adicionales

- [Documentación de TypeEasy](../README.md)
- [Ejemplo completo: usuarios_endpoint.te](../typeeasycode/apis/usuarios_endpoint.te)
- [Configuración de MySQL](../typeeasycode/settings/mysql_config.te)

---

## 🎯 Próximos Pasos

1. **Crea tu primer endpoint** siguiendo el ejemplo de "Hola Mundo"
2. **Configura MySQL** con tus credenciales
3. **Crea modelos** para tus tablas
4. **Prueba con curl** o Postman
5. **Despliega en producción** con Docker

---

**¿Necesitas ayuda?** Abre un [Issue en GitHub](https://github.com/appdeveloper777/TypeEasy/issues)
