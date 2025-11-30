<div align="center">

<img src="https://github.com/user-attachments/assets/e4066c0d-07c1-419b-a479-3483488521eb" alt="TypeEasy Logo" width="200"/>


# TypeEasy

[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?logo=docker&logoColor=white)](https://www.docker.com/)
[![Gemini](https://img.shields.io/badge/Google-Gemini%20AI-4285F4?logo=google&logoColor=white)](https://ai.google.dev/)
[![WhatsApp](https://img.shields.io/badge/WhatsApp-Enabled-25D366?logo=whatsapp&logoColor=white)](https://www.whatsapp.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![GitHub Stars](https://img.shields.io/github/stars/appdeveloper777/TypeEasy?style=social)](https://github.com/appdeveloper777/TypeEasy/stargazers)

**Un intérprete / framework experimental para crear lenguajes, scripts y bridges con servicios externos**

[🚀 Inicio Rápido](#-ejecutar-typeeasy-con-docker-compose) • [📖 Documentación](#-documentación-completa-del-chatbot) • [💬 Chatbot WhatsApp](#-nuevo-chatbot-whatsapp-con-gemini-ai) • [⭐ Apoyar](#-apoya-el-proyecto)

</div>

---

## ¿Qué es TypeEasy?


## 🌟 Nuevo: Chatbot WhatsApp con Gemini AI

¿Quieres crear un chatbot inteligente para WhatsApp en minutos? Ahora TypeEasy incluye una integración completa con Google Gemini AI y WAHA.

### 🚀 Inicio Rápido del Chatbot

```bash
# 1. Clonar y configurar
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy
cp .env.example .env
# Editar .env y agregar tu GEMINI_API_KEY

# 2. Levantar servicios
docker compose up -d

# 3. Abrir dashboard y escanear QR
# http://localhost:3000
```

### 📖 Documentación Completa del Chatbot

**[→ Ver Guía Completa: Chatbot WhatsApp + WAHA + Gemini AI](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md)**

La guía incluye:
- ✅ Instalación paso a paso (Windows/Mac/Linux)
- ✅ Configuración de WAHA y escaneo de QR
- ✅ Despliegue en producción con Nginx
- ✅ Solución de problemas comunes
- ✅ Personalización del chatbot

---

## ¿Qué es TypeEasy?

✔️ Tienes la libertad de crear tu propia sintaxis para adaptarla perfectamente a tu dominio o equipo. <br>
✔️ Puedes hacer "bridge" sin esfuerzo con otros lenguajes potentes como Java, Rust y C#, aprovechando lo mejor de cada ecosistema. <br>
✔️ Crear tus endpoint como FastAPI <br>
✔️ Crear tus scripting para Integraciones

![image](https://github.com/user-attachments/assets/d4617ae8-71f0-4270-9e70-ad00bd6694ab)

## 🚀 Ejecutar TypeEasy con Docker Compose

### 📦 Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) instalado y ejecutándose
- Git (opcional para clonar el repositorio)

---

### 🛠️ Cómo usar

1. Clona el repositorio o descarga el proyecto, ejecuta el siguiente comando en el VS Code:
   
**Para macOS, Linux o Windows (Usando Git Bash)**

```bash
git clone https://github.com/appdeveloper777/TypeEasy.git && cd TypeEasy && code -r .
```

**Para Windows (usando PowerShell)**

```bash
git clone https://github.com/appdeveloper777/TypeEasy.git; cd TypeEasy; code -r .
```
   
Asegúrate de tener un archivo `.te` dentro de la carpeta `typeeasycode/`.  
Por ejemplo: `TypeEasy/typeeasycode/main.te`

2. Construye la imagen de Docker:

```bash
docker compose build
```

3. Ejecuta un archivo `.te`:

```bash
docker compose run --rm typeeasy main.te
```

✅ Esto ejecutará `/code/main.te` dentro del contenedor, usando el ejecutable `typeeasy`.

---

## ✍️ Escribir y ejecutar código TypeEasy

### 🧾 1. Crea tu archivo `.te` dentro de `typeeasycode/`

Ejemplo: `typeeasycode/hola.te`

```te
print("¡Hola, mundo!");
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

Para correr los endpoints el código se encuentra en TypeEasy/typeeasycode/apis: http://localhost:8080/

```bash
docker compose up -d --build api
docker compose logs -f api
```

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

## Exponer el adapter de WhatsApp con ngrok

Para conectar tu agente a WhatsApp real (Meta Cloud API) necesitas exponer el adapter a internet. Usa el script helper:

```bash
bash scripts/ngrok_up.sh
```

Esto iniciará ngrok en el puerto 5002 y mostrará la URL pública (ejemplo: `https://abcd1234.ngrok.io/webhook`). Usa esa URL en el panel de Facebook Developer para registrar el webhook de WhatsApp y pon el mismo `META_VERIFY_TOKEN` que tienes en tu `.env`.

Cuando termines, puedes matar ngrok con:

```bash
kill $(pgrep ngrok)
```

Recuerda tener configuradas las variables `META_WHATSAPP_TOKEN`, `META_WHATSAPP_PHONE_ID`, `META_APP_SECRET` y `META_VERIFY_TOKEN` en tu `.env`.

## Adapter: modo mock / desarrollo

Si no configuras `TWILIO_*` ni `META_*`, el adapter funcionará en modo "mock" y no intentará enviar mensajes reales: en su lugar registrará el intento de envío en sus logs y devolverá HTTP 200 para que el agente considere el envío exitoso. Esto es útil para probar el flujo end-to-end sin credenciales.

Ejemplo: si el agente hace `Chat.post("/send", respuesta);`, el adapter responderá con `{"mock_sent":true, ...}` y verás en los logs la línea `Mock send -> to: ... message: ...`.

### Ver el historial de mensajes mock enviados

El adapter expone un endpoint útil para desarrollo: `GET /history` que devuelve los envíos mock recientes en memoria.

Ejemplo:

```bash
curl http://localhost:5002/history
```

## Probar el Agente WhatsApp desde Windows: reconstruir sin cache y usar curl

Si estás en Windows y quieres forzar una reconstrucción limpia de la imagen del agente y ejecutar el contenedor para pruebas locales, usa los siguientes pasos y ejemplos. Estos comandos están pensados para CMD o PowerShell según tu preferencia.

- Reconstruir la imagen del agente sin usar cache:

```bash
docker compose build --no-cache agent
```

- Ejecutar el contenedor del agente exponiendo los puertos declarados (útil para pruebas locales):

```bash
docker compose run --rm --service-ports agent
```

Nota: `--service-ports` mapea los puertos del servicio tal como están declarados en `docker-compose` hacia el host (por ejemplo, el agente escucha en el puerto `8081`).

Ejemplos de `curl` desde Windows (CMD o PowerShell). Puedes ejecutar exactamente estas líneas si el servicio está escuchando en `http://localhost:8081`:

```powershell
C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=ver+el+menu"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=hola"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=hola"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=ver+el+menu"
```

<img width="1868" height="990" alt="agent logs typeeasy" src="https://github.com/user-attachments/assets/b2fee05a-19d7-46bb-9161-3fc32c839134" />

agente_chat_whatsapp.te: <img width="1777" height="978" alt="agent logs typeeasy codigo" src="https://github.com/user-attachments/assets/139c8574-b234-4701-aae1-192c6413ec45" />

Consejos adicionales:

- Para ver logs más verbosos del agente al ejecutar con `docker compose run` añade la variable de entorno `TYPEEASY_DEBUG=1`:

```bash
docker compose run --rm -e TYPEEASY_DEBUG=1 --service-ports agent
```

- En PowerShell puedes exportar la variable antes de ejecutar el contenedor:

```powershell
$env:TYPEEASY_DEBUG = '1'; docker compose run --rm --service-ports agent
```

- Después de modificar código en `src/` recuerda reconstruir la imagen del agente con `--no-cache` si quieres asegurarte de que los binarios nativos se recompilan:

```bash
docker compose build --no-cache agent
```

## Variables de entorno importantes

- Gemini AI (para chatbot): `GEMINI_API_KEY`
- WAHA (para chatbot): `WAHA_API_KEY`, `WAHA_API_URL`
- Twilio (opcional): `TWILIO_ACCOUNT_SID`, `TWILIO_AUTH_TOKEN`, `TWILIO_FROM`
- Meta WhatsApp (opcional): `META_WHATSAPP_TOKEN`, `META_WHATSAPP_PHONE_ID`, `META_APP_SECRET`, `META_VERIFY_TOKEN`
- Adapter -> Agent: `AGENT_WEBHOOK` (por defecto `http://agent:8081/whatsapp_hook`)

Ejemplo de `.env` mínimo (copiar desde `.env.example` si existe):

```env
# Gemini AI (para chatbot)
GEMINI_API_KEY=tu_api_key_aqui

# WAHA (para chatbot)
WAHA_API_KEY=typeeasy_waha_key_2024
WAHA_API_URL=http://waha:3000
WHATSAPP_PROVIDER=waha

# Twilio (opcional)
TWILIO_ACCOUNT_SID=
TWILIO_AUTH_TOKEN=
TWILIO_FROM=whatsapp:+123456789

# Meta (opcional)
META_WHATSAPP_TOKEN=
META_WHATSAPP_PHONE_ID=
META_APP_SECRET=
META_VERIFY_TOKEN=

AGENT_WEBHOOK=http://agent_gemini:8081/whatsapp_hook
```

## Compilar el shim Rust (opcional)

El Dockerfile incluye una etapa que compila el shim Rust para `motor_nlu_local`. No es obligatorio ejecutar `cargo` localmente para el flujo por defecto. Si quieres compilar localmente:

```bash
cd tools/motor_nlu_shim
cargo build --release
# copy the shared library into src/native_libs/ if you want to mount it into the image manually
mkdir -p ../../src/native_libs
cp target/release/libmotor_nlu_shim.so ../../src/native_libs/libmotor_nlu_shim.so
```

## 📊 Diagrama de Arquitectura

### Arquitectura General de TypeEasy

![image](https://github.com/user-attachments/assets/120f6734-bf12-4bbe-aedf-ba4372f169f9)

### Arquitectura del Chatbot WhatsApp + Gemini

```
Usuario WhatsApp → WAHA → Adapter → Agent Gemini → Gemini AI
```

**[Ver arquitectura detallada del chatbot →](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md#arquitectura-del-sistema)**

---

## 👨‍💻 Autor

Desarrollado por [@appdeveloper777](https://github.com/appdeveloper777)

---

## ⭐ ¿Te gusta este proyecto?

Si TypeEasy te resulta útil, considera:

- ⭐ **Darle una estrella** en GitHub
- 🐛 **Reportar bugs** o sugerir mejoras en [Issues](https://github.com/appdeveloper777/TypeEasy/issues)
- 📢 **Compartirlo** con otros desarrolladores
- 🤝 **Contribuir** al código con Pull Requests
- 💬 **Unirte** a las [Discusiones](https://github.com/appdeveloper777/TypeEasy/discussions)

---

## � Apoya el Proyecto

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

## �📚 Recursos Adicionales

- 📖 [Guía Completa del Chatbot WhatsApp](README_CHATBOT_WHATSAPP_WAHA_GEMINI.md)
- 🔧 [Documentación de WAHA](https://waha.devlike.pro/docs/)
- 🤖 [Google Gemini API](https://ai.google.dev/docs)
- 🐳 [Docker Compose](https://docs.docker.com/compose/)

---

## 📄 Licencia

Este proyecto está bajo la Licencia MIT. Ver el archivo [LICENSE](LICENSE) para más detalles.

---

**Desarrollado con ❤️ por el equipo de TypeEasy**
