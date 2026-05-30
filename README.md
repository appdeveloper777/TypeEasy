<div align="center">

<img src="https://github.com/user-attachments/assets/e4066c0d-07c1-419b-a479-3483488521eb" alt="TypeEasy Logo" width="200"/>


# TypeEasy

[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?logo=docker&logoColor=white)](https://www.docker.com/)
[![Gemini](https://img.shields.io/badge/Google-Gemini%20AI-4285F4?logo=google&logoColor=white)](https://ai.google.dev/)
[![WhatsApp](https://img.shields.io/badge/WhatsApp-Enabled-25D366?logo=whatsapp&logoColor=white)](https://www.whatsapp.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![GitHub Stars](https://img.shields.io/github/stars/appdeveloper777/TypeEasy?style=social)](https://github.com/appdeveloper777/TypeEasy/stargazers)

**Crea APIs REST con sintaxis tipo C# / TypeScript — sin frameworks pesados ni runtime gigante.**

Un intérprete / framework escrito en C: tu propio lenguaje, scripts, bridges con servicios externos y *API Endpoint maker*.

</div>

```ts
endpoint {
    [HttpGet("/api/hola")]
    Hola() { return json({ mensaje: "¡Hola desde TypeEasy!" }); }
}
```

<div align="center">

[🚀 Inicio Rápido](#-inicio-rápido) • [📖 Chatbot WhatsApp](#-chatbot-whatsapp-con-gemini-ai) • [🔌 APIs REST](#-crear-apis-rest) • [⭐ Apoyar](#-apoya-el-proyecto)

</div>

---

## ¿Qué es TypeEasy?

TypeEasy es un intérprete / framework experimental escrito principalmente en C que te permite:

✔️ **Crear endpoints REST** como FastAPI pero con sintaxis familiar (C# / TypeScript) <br>
✔️ **Crear tu propia sintaxis** adaptada perfectamente a tu dominio o equipo <br>
✔️ **Hacer "bridge"** sin esfuerzo con otros lenguajes potentes como Java, Rust y C# (vía subproceso + protocolo de líneas JSON) <br>
✔️ **Crear scripts** para integraciones y automatizaciones

**¿Por qué TypeEasy?**

- ⚡ **Ligero y rápido** — un solo binario nativo, sin SDK pesado que instalar.
- 🧩 **Sintaxis familiar** — clases, tipado y atributos `[HttpGet]` estilo C#/.NET.
- 🔐 **Listo para producción** — JWT (HS256), CORS multi-dominio, worker pool y Swagger UI integrado.
- 🛠️ **CLI estilo Rails** — `te new`, `te gen`, `te serve` sin boilerplate.
- 🤖 **Baterías incluidas** — bridge con Gemini AI, chatbot de WhatsApp y conectores de base de datos.
- 🔀 **Async cooperativo real** — event loop con `spawn` / `await` / `await_all` que solapa E/S de subprocesos (no bloqueante); 3 llamadas lentas en paralelo ≈ el tiempo de una. Ver `typeeasycode/examples/20_async/`.

<img width="1910" height="932" alt="image" src="https://github.com/user-attachments/assets/6abbb4f1-9066-44e1-8da6-dfd804cd7d1a" />


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

```ts
// typeeasycode/hola.te
print("¡Hola, mundo!");
```

2. Construye y ejecuta:

```bash
docker compose up -d --build typeeasy
docker compose run --rm typeeasy hola.te
```

---

## ⚡ CLI `typeeasy` (alias `te`) — estilo Rails

Desde **v0.0.10** TypeEasy incluye un CLI tipo Rails/Cargo para crear proyectos, generar endpoints y levantar el server sin boilerplate:

```bash
te new mi-app                  # crea estructura completa del proyecto
cd mi-app
te gen resource producto       # apis/producto.te + migrations/NNN_create_producto.sql
te serve --dev                 # levanta el server local con hot-reload
te serve --prod                # modo producción con watchdog auto-restart
```

Subcomandos principales: `new`, `gen resource|endpoint`, `serve [--dev|--prod] [--docker]`, `migrate`, `console`, `version`, `help`.

El CLI detecta automáticamente el binario nativo instalado (Windows installer, `.deb` de Linux o árbol fuente) y, si no encuentra ninguno, hace fallback transparente a Docker.

📖 **Detalles completos, flags, modos `--dev`/`--prod`, instalación nativa y producción con systemd/nginx**:
👉 https://github.com/appdeveloper777/TypeEasy/releases/tag/v0.0.10

---


## 🔌 Crear APIs REST

TypeEasy te permite crear endpoints REST con clases, tipado fuerte y sintaxis simple.

### 🚀 Tu Primer Endpoint

Crea `typeeasycode/apis/proveedores_endpoint.te`:

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

## 🎨 Soporte de VS Code (colores + debugger)

El repo incluye una extensión local de VS Code que aporta **resaltado de sintaxis** y **debugger F5/F10/F11** para archivos `.te`. Para instalarla después de clonar:

**Linux / macOS / Git Bash:**
```bash
bash scripts/install_vscode_extension.sh
```

**Windows PowerShell:**
```powershell
powershell -ExecutionPolicy Bypass -File scripts\install_vscode_extension.ps1
```

Reinicia VS Code. Abre cualquier `.te` y verás colores. Pulsa **F5** para debug con breakpoints, step-over (F10), step-in (F11) y panel de Variables expandible (listas y objetos).

> Requisitos: el comando `code` debe estar en el PATH (en VS Code: `Ctrl+Shift+P` → *Shell Command: Install 'code' command in PATH*) y Docker corriendo (el debugger se ejecuta dentro del contenedor `typeeasy`).

---

## � Cómo depurar (debug) un archivo `.te`

Una vez instalada la extensión de VS Code (sección anterior), puedes depurar cualquier `.te` con breakpoints reales, step-over, step-in, inspección de variables y `print()` redirigido al **Debug Console**.

### 1. Abre el archivo `.te` que quieres depurar
Por ejemplo `typeeasycode/link_to_objects.te`.

### 2. Pon un breakpoint
Haz clic en el margen izquierdo de la línea (aparece un punto rojo) o pulsa **F9** sobre la línea.

```ts
class Producto {
  nombre : string;
  precio : int;
  __constructor(nombre : string, precio : int) {
    this.nombre = nombre;
    this.precio = precio;
  }
}

let productos = from "productos.csv", Producto;   // ← breakpoint aquí
for (let item in productos) {
    print("Producto:");
    item.Mostrar();
}
```

### 3. Pulsa **F5** para iniciar el debug
La primera vez VS Code te preguntará el tipo de configuración: elige **TypeEasy**. Esto crea automáticamente `.vscode/launch.json` con:

```json
{
  "type": "typeeasy",
  "request": "launch",
  "name": "TypeEasy: debug current .te",
  "program": "${file}",
  "stopOnEntry": false,
  "port": 4711
}
```

VS Code lanzará el contenedor Docker, conectará el adapter al intérprete y se detendrá en tu breakpoint.

### 4. Atajos disponibles

| Acción | Tecla |
|---|---|
| Continuar | **F5** |
| Step Over (siguiente línea) | **F10** |
| Step Into (entrar al método) | **F11** |
| Step Out (salir del método) | **Shift+F11** |
| Pausar | **F6** |
| Detener | **Shift+F5** |
| Toggle breakpoint | **F9** |

### 5. Panel de Variables
En **Run and Debug → Variables → Locals** verás todas las variables locales del frame actual. Haz clic en la flecha para expandir:

- **Listas** se muestran como `[N items]` y se expanden mostrando `[0]`, `[1]`, …
- **Objetos** se muestran como `<NombreClase>` y se expanden mostrando sus atributos.
- **Tipos primitivos** (`int`, `float`, `string`) se muestran inline.

### 6. Debug Console
Todo lo que imprima `print()` / `println()` aparece en la pestaña **Debug Console** abajo, junto con los logs del adapter (`[typeeasy-dap] ...`).

### 7. Pila de llamadas (Call Stack)
Cuando estés dentro de un método, el panel **Call Stack** muestra `Mostrar() → <main>` y puedes hacer clic para saltar entre frames.

### 8. Depurar endpoints HTTP (modo `--api`)
Para depurar handlers `HttpGet`/`HttpPost`/`HttpPut`/`HttpDelete`/`HttpPatch` necesitas que el intérprete corra como **servidor HTTP** y que VS Code se conecte al DAP en paralelo. Usa esta config en `.vscode/launch.json` (puerto DAP 4712, HTTP 8081):

```json
{
  "type": "typeeasy",
  "request": "launch",
  "name": "TypeEasy: debug API (endpoints HTTP)",
  "program": "${file}",
  "stopOnEntry": false,
  "port": 4712,
  "api": true,
  "httpPort": 8081,
  "apiHost": "0.0.0.0"
}
```

Flujo:
1. Pon breakpoints dentro del cuerpo del handler (`debug_log(...)`, `let ... = ...`, `return json(...)`).
2. **F5** — el contenedor arranca en modo `--api` y el adapter conecta. En el Debug Console verás `Escuchando en http://0.0.0.0:8081 (N rutas)`.
3. Desde otra terminal lanza un request: `curl -X POST http://localhost:8081/api/users -H "Content-Type: application/json" -d '{"name":"Ana"}'`.
4. VS Code se detendrá en tu breakpoint dentro del handler. Inspecciona `request.body`, locals, etc.

**Depurar varios endpoints en una sesión:** el intérprete recibe **un único archivo** como `program`. Para registrar varios endpoints, crea un bootstrap (por ejemplo `apis/_all_debug.te`) que los importe:

```ts
import "example_httpget.te";
import "example_httppost.te";
import "product_endpoint.te";
```

y abre ese archivo como activo antes de pulsar **F5**. Verás `Escuchando en ... (N rutas)` con todas las rutas listadas.

### Solución de problemas

| Problema | Solución |
|---|---|
| `connect ECONNREFUSED 127.0.0.1:4711` o `:4712` | Puerto ocupado por un contenedor zombie. Mátalo: `docker rm -f $(docker ps -aq --filter "name=typeeasy-dap")` (Linux/macOS/Git Bash) o `docker rm -f (docker ps -aq --filter "name=typeeasy-dap")` en PowerShell. Si no, listar y matar por ID: `docker ps` → `docker rm -f <ID>`. |
| `Bind for 0.0.0.0:4712 failed: port is already allocated` | Mismo caso: matar el contenedor anterior con `docker rm -f` (matar el proceso de VS Code **no** limpia el contenedor — lo gestiona el daemon de Docker). La extensión nueva ya etiqueta los contenedores con `--name typeeasy-dap-<pid>-<ts>` y los borra al hacer Stop limpio. |
| Breakpoint no se activa | Verifica que la línea sea una sentencia ejecutable (no comentario, no `}` cerrado). Si una sentencia válida nunca para, exporta `TYPEEASY_DEBUG_VERBOSE=1` en el container — emitirá `[typeeasy-debugger] SKIP line<=0 kind=K type=T`: indica que el nodo AST tiene `line=0` (falta `node->line = yylineno` en el `create_*_node` de ese tipo). |
| Sin colores en `.te` | Reinstala la extensión: `bash scripts/install_vscode_extension.sh` y reinicia VS Code |
| `Docker daemon not running` | Inicia Docker Desktop |
| Modo `--api`: solo registra 1 ruta | Es por diseño: el intérprete carga **un** archivo. Usa el patrón bootstrap con `import` descrito en la sección 8. |

---

## �💬 Chatbot WhatsApp con Gemini AI

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

## 🧠 Características Avanzadas

### Scripts y Automatizaciones

Crea scripts para automatizar tareas:

```ts
// typeeasycode/backup.te
print("Iniciando backup...");
// Tu lógica aquí
```

Ejecuta:
```bash
docker compose run --rm typeeasy backup.te
```

### Integración con Bases de Datos

```ts
import "models/Usuario.te";
import "settings/mysql_config.te";

endpoint {
  [HttpGet("/api/usuarios")]
  GetUsuarios() {
      let conn = mysql_connect(global_host, global_user, global_pass, global_db, global_port);
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

## 📦 Paquetes nativos (plugins `.so`)

TypeEasy soporta **plugins instalables** sin recompilar el motor. Un paquete
es una librería `libte_<nombre>.so` que registra builtins en runtime cuando
el script lo carga con `load_native("nombre")`.

### Instalar un paquete (ej. mongo, en el futuro redis, kafka, etc.)

```bash
# Desde URL (modelo actual)
tools/te-install/te-install mongo \
    --from https://example.com/libte_mongo.so \
    --sha256 abc123...

# O desde manifiesto te-packages.json en la raíz
tools/te-install/te-install
```

El `.so` se instala en `~/.te/packages/<nombre>/libte_<nombre>.so`, una de
las rutas que `load_native` busca por defecto (junto con `./`,
`/usr/local/lib`, `/typeeasy`).

### Usarlo desde un `.te`

```ts
load_native("mongo");

let conn = mongo_connect("mongodb://localhost:27017/meri");
let r    = mongo_query(conn, "usuarios", { "activo": 1 }, "json");
println(r);
mongo_close(conn);
```

Ver [`tools/te-install/README.md`](tools/te-install/README.md) para escribir
tus propios paquetes (cualquier `.c` que exporte `te_module_register` y
llame a `host->register_builtin`).

> Nota: el índice central (`te-install <name>` sin `--from`) está reservado
> para una futura "registry" oficial de librerías TypeEasy.

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

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Apoyar-yellow?logo=buy-me-a-coffee&logoColor=white)](https://www.paypal.com/paypalme/fingunza)
[![PayPal](https://img.shields.io/badge/PayPal-Donar-blue?logo=paypal&logoColor=white)](https://www.paypal.com/paypalme/fingunza)

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
