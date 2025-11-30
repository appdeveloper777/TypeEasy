<div align="center">

<img src="https://github.com/user-attachments/assets/e4066c0d-07c1-419b-a479-3483488521eb" alt="TypeEasy Logo" width="200"/>


# TypeEasy

[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?logo=docker&logoColor=white)](https://www.docker.com/)
[![Gemini](https://img.shields.io/badge/Google-Gemini%20AI-4285F4?logo=google&logoColor=white)](https://ai.google.dev/)
[![WhatsApp](https://img.shields.io/badge/WhatsApp-Enabled-25D366?logo=whatsapp&logoColor=white)](https://www.whatsapp.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![GitHub Stars](https://img.shields.io/github/stars/appdeveloper777/TypeEasy?style=social)](https://github.com/appdeveloper777/TypeEasy/stargazers)

**Un intérprete / framework experimental para crear lenguajes, scripts y bridges con servicios externos**

[🚀 Inicio Rápido](#-inicio-rápido) • [📖 Chatbot WhatsApp](#-chatbot-whatsapp-con-gemini-ai) • [🔌 APIs REST](#-crear-apis-rest) • [⭐ Apoyar](#-apoya-el-proyecto)

</div>

---

## ¿Qué es TypeEasy?

TypeEasy es un intérprete / framework experimental escrito principalmente en C que te permite:

✔️ **Crear tu propia sintaxis** adaptada perfectamente a tu dominio o equipo <br>
✔️ **Hacer "bridge"** sin esfuerzo con otros lenguajes potentes como Java, Rust y C# <br>
✔️ **Crear endpoints REST** como FastAPI pero con sintaxis TypeEasy <br>
✔️ **Crear scripts** para integraciones y automatizaciones

![image](https://github.com/user-attachments/assets/d4617ae8-71f0-4270-9e70-ad00bd6694ab)

---

## 🚀 Inicio Rápido

### 📦 Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) instalado
- Git (opcional)

### 🛠️ Instalación

**Para macOS, Linux o Windows (Git Bash):**

```bash
git clone https://github.com/appdeveloper777/TypeEasy.git && cd TypeEasy && code -r .
```

**Para Windows (PowerShell):**

```bash
git clone https://github.com/appdeveloper777/TypeEasy.git; cd TypeEasy; code -r .
```

### ▶️ Ejecutar un Script TypeEasy

1. Crea un archivo `.te` en `typeeasycode/`:

```te
// typeeasycode/hola.te
print("¡Hola, mundo!");
```

2. Construye y ejecuta:

```bash
docker compose build
docker compose run --rm typeeasy hola.te
```

---

## 💬 Chatbot WhatsApp con Gemini AI

Crea un chatbot inteligente para WhatsApp en minutos usando Google Gemini AI.

### 🚀 Inicio Rápido del Chatbot

```bash
# 1. Clonar y configurar
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy
cp .env.example .env
# Editar .env y agregar tu GEMINI_API_KEY

# 2. Levantar servicios
docker compose up -d

# 3a. Con WAHA: Abrir dashboard y escanear QR
# http://localhost:3000

# 3b. Con Meta API: Configurar webhook
# Ver guía: docs/META_WHATSAPP_SETUP.md
```

### 📖 Opciones de Integración

| Opción | Descripción | Mejor Para |
|--------|-------------|------------|
| 🔷 **[WAHA](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md)** | Gratis, escanear QR | Desarrollo y pruebas |
| 🔶 **[Meta WhatsApp Cloud API](docs/META_WHATSAPP_SETUP.md)** | API oficial de Meta | Producción |

> ⚠️ **Nota:** WAHA no es recomendable para producción debido a posibles bloqueos de WhatsApp por parte de Meta.

**Las guías incluyen:**
- ✅ Instalación paso a paso (Windows/Mac/Linux)
- ✅ Configuración completa
- ✅ Despliegue en producción con Nginx
- ✅ Solución de problemas
- ✅ Personalización del chatbot

---

## 🔌 Crear APIs REST

TypeEasy te permite crear endpoints REST con clases, tipado fuerte y sintaxis simple.

### 🚀 Tu Primer Endpoint

Crea `typeeasycode/apis/proveedores_endpoint.te`:

```te
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
        return jsonl(mi_orden);
    }
}
```

Levanta el servidor y prueba:

```bash
docker compose up -d --build api
curl http://localhost:8080/api/proveedores
```

**Respuesta:**
```json
{
    "proveedor": "Suministros Industriales S.A.",
    "fecha": "2025-09-06"
}
```

### 📖 Guía Completa de Endpoints

**[→ Ver Guía: Cómo Crear Endpoints con TypeEasy](docs/CREAR_ENDPOINTS.md)**

La guía incluye:
- ✅ Métodos HTTP (GET, POST, PUT, DELETE)
- ✅ Clases y tipado fuerte
- ✅ Parámetros de ruta y query
- ✅ Request body y validación
- ✅ Respuestas JSON y XML
- ✅ Integración con MySQL/PostgreSQL
- ✅ Ejemplos completos de CRUD

---

## 🧠 Características Avanzadas

### Scripts y Automatizaciones

Crea scripts para automatizar tareas:

```te
// typeeasycode/backup.te
print("Iniciando backup...");
// Tu lógica aquí
```

Ejecuta:
```bash
docker compose run --rm typeeasy backup.te
```

### Integración con Bases de Datos

```te
import "models/Usuario.te";
import "settings/mysql_config.te";

endpoint {
  [HttpGet("/api/usuarios")]
  GetUsuarios() {
      let conn = new mysql_connect(global_host, global_user, global_pass, global_db, global_port);
      let usuarios = orm_query(conn, "SELECT * FROM usuarios", UsuarioModel);
      mysql_close(conn);
      return xml(usuarios);
  }
}
```

### Modo Mock para Desarrollo

Prueba sin credenciales reales:

```bash
# El adapter funcionará en modo "mock"
docker compose up -d whatsapp_adapter

# Ver historial de mensajes mock
curl http://localhost:5002/history
```

---

## 🧹 Comandos Útiles

```bash
# Ejecutar un archivo .te
docker compose run --rm typeeasy archivo.te

# Levantar API server
docker compose up -d --build api

# Ver logs
docker compose logs -f api

# Limpiar contenedores
docker container prune

# Inspeccionar contenedor
docker compose run --rm --entrypoint sh typeeasy
```

---

## 📊 Diagrama de Arquitectura

### Arquitectura General de TypeEasy

![image](https://github.com/user-attachments/assets/120f6734-bf12-4bbe-aedf-ba4372f169f9)

### Arquitectura del Chatbot WhatsApp + Gemini

```
Usuario WhatsApp → WAHA/Meta API → Adapter → Agent Gemini → Gemini AI
```

**[Ver arquitectura detallada del chatbot →](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md#arquitectura-del-sistema)**

---

## 📚 Documentación

| Guía | Descripción |
|------|-------------|
| [Chatbot con WAHA](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md) | Configuración completa con WAHA |
| [Chatbot con Meta API](docs/META_WHATSAPP_SETUP.md) | Configuración con WhatsApp Cloud API |
| [Crear Endpoints REST](docs/CREAR_ENDPOINTS.md) | Guía completa de APIs REST |

---

## ⭐ ¿Te gusta este proyecto?

Si TypeEasy te resulta útil, considera:

- ⭐ **Darle una estrella** en GitHub
- 🐛 **Reportar bugs** o sugerir mejoras en [Issues](https://github.com/appdeveloper777/TypeEasy/issues)
- 📢 **Compartirlo** con otros desarrolladores
- 🤝 **Contribuir** al código con Pull Requests
- 💬 **Unirte** a las [Discusiones](https://github.com/appdeveloper777/TypeEasy/discussions)

---

## 💖 Apoya el Proyecto

TypeEasy es un proyecto de código abierto desarrollado con pasión. Tu apoyo nos ayuda a:

- 🚀 Desarrollar nuevas características
- 🐛 Corregir bugs y mejorar la estabilidad
- 📚 Crear mejor documentación
- 🌍 Mantener el proyecto activo y en crecimiento

### Formas de Apoyar

#### ☕ Invítanos un café

Si TypeEasy te ha ahorrado tiempo o te ha ayudado en tu proyecto, considera invitarnos un café:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Apoyar-yellow?logo=buy-me-a-coffee&logoColor=white)](https://www.buymeacoffee.com/appdeveloper777)
[![PayPal](https://img.shields.io/badge/PayPal-Donar-blue?logo=paypal&logoColor=white)](https://paypal.me/appdeveloper777)

#### 💎 Conviértete en Sponsor

Apoya el desarrollo continuo convirtiéndote en sponsor:

[![GitHub Sponsors](https://img.shields.io/badge/GitHub-Sponsor-pink?logo=github&logoColor=white)](https://github.com/sponsors/appdeveloper777)

#### 🌟 Otras Formas de Ayudar

- **Comparte el proyecto** en redes sociales
- **Escribe un artículo** sobre cómo usas TypeEasy
- **Crea tutoriales** en YouTube o tu blog
- **Traduce la documentación** a otros idiomas
- **Ayuda a otros usuarios** en las Discusiones

### 🙏 Agradecimientos Especiales

Gracias a todos nuestros sponsors y contribuidores que hacen posible este proyecto:

<!-- sponsors -->
<!-- Este espacio se actualizará automáticamente con nuestros sponsors -->
<!-- /sponsors -->

---

## 📄 Licencia

Este proyecto está bajo la Licencia MIT. Ver el archivo [LICENSE](LICENSE) para más detalles.

---

## 👨‍💻 Autor

Desarrollado por [@appdeveloper777](https://github.com/appdeveloper777)

---

**Desarrollado con ❤️ por el equipo de TypeEasy**
