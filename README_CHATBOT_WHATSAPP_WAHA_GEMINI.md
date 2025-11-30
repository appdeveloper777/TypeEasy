# 🤖 Chatbot WhatsApp + WAHA + Gemini AI - Guía Completa

Esta guía te llevará paso a paso desde clonar el repositorio hasta tener tu chatbot funcionando con WhatsApp y Gemini AI.

---

## 📋 Tabla de Contenidos

1. [Requisitos Previos](#requisitos-previos)
2. [Instalación Local (Windows/Mac/Linux)](#instalación-local)
3. [Configuración de WAHA](#configuración-de-waha)
4. [Despliegue en Producción con Nginx](#despliegue-en-producción)
5. [Solución de Problemas](#solución-de-problemas)
6. [Comandos Útiles](#comandos-útiles)

---

## 📋 Requisitos Previos

### Software Necesario

- ✅ **Docker** y **Docker Compose** instalados
- ✅ **Git** instalado
- ✅ **WhatsApp** en tu teléfono
- ✅ **API Key de Google Gemini** ([Obtener aquí](https://makersuite.google.com/app/apikey))

### Verificar Instalaciones

```bash
# Verificar Docker
docker --version
docker compose version

# Verificar Git
git --version
```

---

## 🚀 Instalación Local

### Paso 1: Clonar el Repositorio

```bash
# Clonar desde GitHub
git clone https://github.com/appdeveloper777/TypeEasy.git

# Entrar al directorio
cd TypeEasy
```

### Paso 2: Configurar Variables de Entorno

```bash
# Copiar el archivo de ejemplo
cp .env.example .env

# Editar el archivo (usa tu editor favorito)
nano .env
# O en Windows: notepad .env
# O en Mac: open -e .env
```

**Contenido del archivo `.env`:**

```env
# ==========================================
# Gemini AI Configuration
# ==========================================
# Obtén tu API Key en: https://makersuite.google.com/app/apikey
GEMINI_API_KEY=tu_api_key_de_gemini_aqui

# ==========================================
# WAHA Configuration
# ==========================================
# No cambiar estos valores
WAHA_API_KEY=typeeasy_waha_key_2024
WAHA_API_URL=http://waha:3000

# ==========================================
# WhatsApp Provider
# ==========================================
# Opciones: waha, twilio, meta, mock
WHATSAPP_PROVIDER=waha

# ==========================================
# Agent Configuration
# ==========================================
# Comunicación interna entre servicios (no cambiar)
AGENT_WEBHOOK=http://agent_gemini:8081/whatsapp_hook

# ==========================================
# Debug Mode
# ==========================================
# 1 = activado, 0 = desactivado
TYPEEASY_DEBUG=1
```

**⚠️ IMPORTANTE:** Reemplaza `tu_api_key_de_gemini_aqui` con tu API Key real de Google Gemini.

### Paso 3: Levantar los Servicios

```bash
# Construir y levantar todos los servicios
docker compose up -d

# Esperar unos segundos a que todos los servicios inicien
sleep 10

# Verificar que todos los servicios están corriendo
docker compose ps
```

**Deberías ver algo como:**

```
NAME                           STATUS
typeeasy-agent_gemini-1        Up
typeeasy-gemini-1              Up
typeeasy-waha-1                Up
typeeasy-whatsapp_adapter-1    Up
typeeasy-api_mock-1            Up
typeeasy-nlu-1                 Up
```

✅ Si todos los servicios muestran **"Up"**, ¡todo está funcionando!

---

## 🔧 Configuración de WAHA

### Paso 1: Acceder al Dashboard de WAHA

Abre tu navegador y ve a:

```
http://localhost:3000
```

Deberías ver el dashboard de WAHA.

### Paso 2: Crear la Sesión de WhatsApp

**IMPORTANTE:** WAHA Core (versión gratuita) solo soporta UNA sesión llamada `default`.

1. **Haz clic en el botón "+" o "Add Session"** (esquina superior derecha del dashboard)

2. **Configurar la sesión con estos valores EXACTOS:**

   | Campo | Valor |
   |-------|-------|
   | **Name** | `default` |
   | **API URL** | `http://localhost:3000` |
   | **API Key** | `typeeasy_waha_key_2024` |

3. **Guardar la configuración:**
   - Haz clic en **"Save"** o **"Create"**

### Paso 3: Iniciar la Sesión

1. **Busca la sesión "default"** en la lista de sesiones
2. **Haz clic en el botón "Start"**
3. **Espera unos segundos** (10-15 segundos) a que aparezca el código QR

### Paso 4: Vincular WhatsApp

1. **En tu teléfono, abre WhatsApp**

2. **Ve a Configuración:**
   - **Android:** Menú (⋮) → Dispositivos vinculados
   - **iPhone:** Configuración → Dispositivos vinculados

3. **Toca "Vincular un dispositivo"**

4. **Escanea el código QR** que aparece en el dashboard de WAHA

5. **Espera la confirmación:**
   - El estado de la sesión cambiará a **"WORKING"** o **"AUTHENTICATED"**
   - Verás un mensaje de confirmación en WhatsApp

✅ **¡Listo!** Tu chatbot está conectado a WhatsApp.

### Paso 5: Probar el Chatbot

1. **Desde otro teléfono**, envía un mensaje de WhatsApp al número que acabas de vincular

2. **Ejemplo de conversación:**
   ```
   Tú: Hola, necesito información
   Bot: ¡Hola! 👋 Bienvenido a Rollers Perú, tu experto en cortinas Roller...
   
   Tú: ¿Qué tipos de cortinas tienen?
   Bot: Tenemos varios tipos de rollers:
        - Roller Blackout: Bloquea toda la luz...
        - Roller Screen: Permite el paso de luz...
        - Roller Duo: Moderno, con franjas opacas...
   ```

---

## 🌐 Despliegue en Producción

### Requisitos para Producción

- Servidor Linux (Ubuntu/Debian recomendado)
- Dominio configurado (ejemplo: `chatbot.tuempresa.com`)
- Nginx instalado
- Certificado SSL (Let's Encrypt recomendado)

### Paso 1: Preparar el Servidor

```bash
# Conectarse al servidor
ssh usuario@tu-servidor.com

# Actualizar sistema
sudo apt update && sudo apt upgrade -y

# Instalar Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Agregar usuario al grupo docker
sudo usermod -aG docker $USER

# Instalar Docker Compose
sudo apt install docker-compose-plugin -y

# Reiniciar sesión
newgrp docker
```

### Paso 2: Clonar y Configurar

```bash
# Clonar repositorio
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy

# Configurar .env
cp .env.example .env
nano .env
# Agregar tu GEMINI_API_KEY

# Levantar servicios
docker compose up -d
```

### Paso 3: Configurar Nginx

```bash
# Instalar Nginx
sudo apt install nginx -y

# Crear archivo de configuración
sudo nano /etc/nginx/sites-available/chatbot
```

**Contenido del archivo de configuración:**

```nginx
# ==========================================
# Configuración HTTP (redirige a HTTPS)
# ==========================================
server {
    listen 80;
    server_name chatbot.tuempresa.com;
    
    # Redirigir todo el tráfico HTTP a HTTPS
    return 301 https://$server_name$request_uri;
}

# ==========================================
# Configuración HTTPS
# ==========================================
server {
    listen 443 ssl http2;
    server_name chatbot.tuempresa.com;

    # ==========================================
    # Certificados SSL (Let's Encrypt)
    # ==========================================
    ssl_certificate /etc/letsencrypt/live/chatbot.tuempresa.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/chatbot.tuempresa.com/privkey.pem;

    # Configuración SSL recomendada
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;
    ssl_prefer_server_ciphers on;
    ssl_session_cache shared:SSL:10m;
    ssl_session_timeout 10m;

    # ==========================================
    # WAHA Dashboard (interfaz web)
    # ==========================================
    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        
        # Headers necesarios
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # Bypass de caché
        proxy_cache_bypass $http_upgrade;
        
        # Timeouts para WebSocket (necesario para WAHA)
        proxy_read_timeout 86400;
        proxy_send_timeout 86400;
        proxy_connect_timeout 60;
    }

    # ==========================================
    # API de WAHA
    # ==========================================
    location /api/ {
        proxy_pass http://localhost:3000/api/;
        proxy_http_version 1.1;
        
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # Timeouts
        proxy_read_timeout 60;
        proxy_connect_timeout 60;
    }

    # ==========================================
    # Webhook de WAHA (para webhooks externos)
    # ==========================================
    location /waha_webhook {
        proxy_pass http://localhost:5002/waha_webhook;
        proxy_http_version 1.1;
        
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # Timeouts
        proxy_read_timeout 60;
        proxy_connect_timeout 60;
    }

    # ==========================================
    # Logs
    # ==========================================
    access_log /var/log/nginx/chatbot_access.log;
    error_log /var/log/nginx/chatbot_error.log;
}
```

**⚠️ IMPORTANTE:** Reemplaza `chatbot.tuempresa.com` con tu dominio real.

### Paso 4: Habilitar el Sitio

```bash
# Crear symlink
sudo ln -s /etc/nginx/sites-available/chatbot /etc/nginx/sites-enabled/

# Verificar configuración
sudo nginx -t

# Si todo está OK, reiniciar nginx
sudo systemctl restart nginx
```

### Paso 5: Configurar SSL con Let's Encrypt

```bash
# Instalar Certbot
sudo apt install certbot python3-certbot-nginx -y

# Obtener certificado SSL
sudo certbot --nginx -d chatbot.tuempresa.com

# Verificar renovación automática
sudo certbot renew --dry-run
```

### Paso 6: Configurar Firewall

```bash
# Permitir HTTP y HTTPS
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# Permitir SSH (si no lo has hecho)
sudo ufw allow 22/tcp

# Habilitar firewall
sudo ufw enable

# Verificar estado
sudo ufw status
```

### Paso 7: Acceder al Dashboard

Ahora puedes acceder a tu chatbot desde:

```
https://chatbot.tuempresa.com
```

---

## 🐛 Solución de Problemas

### Problema 1: El chatbot no responde

**Síntomas:**
- Envías un mensaje y no recibes respuesta

**Soluciones:**

1. **Verificar que todos los servicios están corriendo:**
   ```bash
   docker compose ps
   ```
   Todos deben mostrar "Up"

2. **Verificar logs del agente:**
   ```bash
   docker compose logs agent_gemini --tail 50
   ```
   Busca errores o mensajes de "Incoming webhook received"

3. **Verificar que la sesión de WAHA está activa:**
   - Ir a http://localhost:3000 (o tu dominio)
   - El estado debe ser "WORKING"

4. **Reiniciar servicios:**
   ```bash
   docker compose restart agent_gemini whatsapp_adapter
   ```

### Problema 2: Error "Session does not exist"

**Síntomas:**
- Al intentar iniciar la sesión, aparece error

**Soluciones:**

1. **Verificar que el nombre de la sesión es exactamente "default":**
   - WAHA Core solo soporta una sesión llamada "default"
   - El nombre es case-sensitive (minúsculas)

2. **Eliminar y recrear la sesión:**
   ```bash
   # Desde el dashboard de WAHA
   # 1. Eliminar la sesión existente
   # 2. Crear nueva con nombre "default"
   ```

### Problema 3: Error 502 Bad Gateway (Nginx)

**Síntomas:**
- Al acceder al dominio, aparece "502 Bad Gateway"

**Soluciones:**

1. **Verificar que WAHA está corriendo:**
   ```bash
   docker compose ps waha
   ```

2. **Verificar que el puerto 3000 está mapeado:**
   ```bash
   docker compose ps waha
   # Debe mostrar: 0.0.0.0:3000->3000/tcp
   ```

3. **Verificar logs de nginx:**
   ```bash
   sudo tail -f /var/log/nginx/error.log
   ```
   Busca líneas con "Connection refused"

4. **Verificar que WAHA responde localmente:**
   ```bash
   curl http://localhost:3000/api/version
   ```
   Debe devolver un JSON con la versión

5. **Reiniciar nginx:**
   ```bash
   sudo systemctl restart nginx
   ```

### Problema 4: No aparece el código QR

**Síntomas:**
- Al iniciar la sesión, no aparece el código QR

**Soluciones:**

1. **Esperar más tiempo:**
   - WAHA puede tardar 10-20 segundos en generar el QR

2. **Verificar logs de WAHA:**
   ```bash
   docker compose logs waha --tail 50
   ```

3. **Reiniciar la sesión:**
   - Stop → Start en el dashboard

4. **Verificar que Chromium está instalado en el contenedor:**
   ```bash
   docker compose logs waha | grep chromium
   ```

### Problema 5: El chatbot responde pero el mensaje no llega a WhatsApp

**Síntomas:**
- Ves en los logs que Gemini genera una respuesta
- Pero el mensaje no llega a WhatsApp

**Soluciones:**

1. **Verificar logs del whatsapp_adapter:**
   ```bash
   docker compose logs whatsapp_adapter --tail 50
   ```
   Busca "Sending to WhatsApp via WAHA"

2. **Verificar que last_sender está guardado:**
   ```bash
   docker compose logs whatsapp_adapter | grep "Saved sender"
   ```

3. **Reiniciar servicios:**
   ```bash
   docker compose restart whatsapp_adapter agent_gemini
   ```

---

## 🔧 Comandos Útiles

### Ver Logs

```bash
# Ver logs de todos los servicios
docker compose logs -f

# Ver logs de un servicio específico
docker compose logs -f agent_gemini
docker compose logs -f waha
docker compose logs -f gemini
docker compose logs -f whatsapp_adapter

# Ver últimas 50 líneas
docker compose logs agent_gemini --tail 50

# Ver logs en tiempo real
docker compose logs -f --tail 100
```

### Reiniciar Servicios

```bash
# Reiniciar todos los servicios
docker compose restart

# Reiniciar un servicio específico
docker compose restart agent_gemini
docker compose restart waha

# Reiniciar múltiples servicios
docker compose restart agent_gemini whatsapp_adapter
```

### Detener y Eliminar

```bash
# Detener todos los servicios
docker compose down

# Detener y eliminar volúmenes (⚠️ borra datos)
docker compose down -v

# Detener un servicio específico
docker compose stop waha
```

### Reconstruir Servicios

```bash
# Reconstruir todos los servicios
docker compose build

# Reconstruir un servicio específico
docker compose build agent_gemini

# Reconstruir y reiniciar
docker compose up -d --build

# Reconstruir sin caché
docker compose build --no-cache
```

### Verificar Estado

```bash
# Ver estado de todos los servicios
docker compose ps

# Ver uso de recursos
docker stats

# Ver redes de Docker
docker network ls

# Inspeccionar un contenedor
docker inspect typeeasy-waha-1
```

### Acceder a un Contenedor

```bash
# Acceder a shell de un contenedor
docker compose exec waha /bin/bash
docker compose exec agent_gemini /bin/bash

# Ejecutar un comando en un contenedor
docker compose exec waha ls -la
```

---

## 📊 Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────────┐
│                     Usuario de WhatsApp                      │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│                    WAHA (Puerto 3000)                        │
│              WhatsApp HTTP API - Interfaz Web                │
└───────────────────────┬─────────────────────────────────────┘
                        │ (Webhook interno)
                        ↓
┌─────────────────────────────────────────────────────────────┐
│            WhatsApp Adapter (Puerto 5002)                    │
│          Adaptador Python - Routing de mensajes             │
│          - Guarda sender (last_sender)                       │
│          - Reenvía mensajes al agente                        │
└───────────────────────┬─────────────────────────────────────┘
                        │ (HTTP: AGENT_WEBHOOK)
                        ↓
┌─────────────────────────────────────────────────────────────┐
│            Agent Gemini (Puerto 8081)                        │
│          Servidor TypeEasy - Lógica del chatbot             │
│          - Recibe mensaje                                    │
│          - Llama a Gemini AI                                 │
│          - Envía respuesta                                   │
└───────────────────────┬─────────────────────────────────────┘
                        │ (HTTP interno)
                        ↓
┌─────────────────────────────────────────────────────────────┐
│            Gemini Service (Puerto 5003)                      │
│          Servicio Python - Integración con Google           │
│          - Procesa mensaje                                   │
│          - Llama API de Gemini                               │
│          - Devuelve respuesta JSON                           │
└─────────────────────────────────────────────────────────────┘
```

### Flujo de Mensajes

1. **Usuario → WhatsApp:** Usuario envía mensaje
2. **WhatsApp → WAHA:** WAHA recibe el mensaje
3. **WAHA → Adapter:** Webhook a `/waha_webhook`
4. **Adapter:** Guarda `last_sender` y reenvía a agente
5. **Adapter → Agent:** POST a `/whatsapp_hook?message=...`
6. **Agent:** Ejecuta listener `Chat.onMessage`
7. **Agent → Gemini Service:** POST a `/chat` con el mensaje
8. **Gemini Service → Google:** Llama API de Gemini
9. **Google → Gemini Service:** Devuelve respuesta
10. **Gemini Service → Agent:** JSON con respuesta
11. **Agent:** Ejecuta `Chat.sendMessage(respuesta)`
12. **Agent → Adapter:** POST a `/send` con respuesta
13. **Adapter:** Usa `last_sender` como destinatario
14. **Adapter → WAHA:** POST a `/api/sendText`
15. **WAHA → WhatsApp:** Envía mensaje
16. **WhatsApp → Usuario:** Usuario recibe respuesta

---

## 🎨 Personalizar el Chatbot

### Modificar el Prompt de Gemini

Edita el archivo:
```bash
nano tools/gemini_service/app.py
```

Busca la sección `SYSTEM_PROMPT` y modifica según tus necesidades:

```python
SYSTEM_PROMPT = """
Eres un asistente virtual de [TU EMPRESA].
Tu objetivo es ayudar a los clientes con [TU SERVICIO].

Características:
- Responde de manera amigable y profesional
- Usa emojis cuando sea apropiado
- Proporciona información clara y concisa
- Si no sabes algo, admítelo y ofrece alternativas

Información de la empresa:
- Nombre: [TU EMPRESA]
- Servicios: [TUS SERVICIOS]
- Horario: [TU HORARIO]
"""
```

Luego reconstruye el servicio:
```bash
docker compose build gemini
docker compose up -d gemini
```

### Modificar el Agente TypeEasy

Edita el archivo:
```bash
nano typeeasycode/agente_gemini_whatsapp.te
```

Luego reconstruye:
```bash
docker compose build agent_gemini
docker compose up -d agent_gemini
```

---

## 📝 Variables de Entorno Completas

```env
# ==========================================
# GEMINI AI
# ==========================================
GEMINI_API_KEY=                    # Tu API Key de Google Gemini
GEMINI_MODEL=gemini-2.0-flash      # Modelo a usar

# ==========================================
# WAHA
# ==========================================
WAHA_API_KEY=typeeasy_waha_key_2024
WAHA_API_URL=http://waha:3000

# ==========================================
# WHATSAPP PROVIDER
# ==========================================
WHATSAPP_PROVIDER=waha             # Opciones: waha, twilio, meta, mock

# ==========================================
# AGENT
# ==========================================
AGENT_WEBHOOK=http://agent_gemini:8081/whatsapp_hook

# ==========================================
# DEBUG
# ==========================================
TYPEEASY_DEBUG=1                   # 1 = activado, 0 = desactivado

# ==========================================
# OPCIONAL: Twilio (si usas Twilio)
# ==========================================
# TWILIO_ACCOUNT_SID=
# TWILIO_AUTH_TOKEN=
# TWILIO_FROM=

# ==========================================
# OPCIONAL: Meta WhatsApp (si usas Meta)
# ==========================================
# META_WHATSAPP_TOKEN=
# META_WHATSAPP_PHONE_ID=
```

---

## 🔒 Seguridad

### Recomendaciones de Seguridad

1. **Nunca subas `.env` a Git:**
   - Ya está en `.gitignore`
   - Contiene API keys sensibles

2. **Usa HTTPS en producción:**
   - Configura SSL con Let's Encrypt
   - Redirige HTTP a HTTPS

3. **Actualiza regularmente:**
   ```bash
   docker compose pull
   docker compose up -d
   ```

4. **Monitorea los logs:**
   ```bash
   docker compose logs -f | grep ERROR
   ```

5. **Limita acceso al servidor:**
   ```bash
   sudo ufw enable
   sudo ufw allow 22/tcp  # SSH
   sudo ufw allow 80/tcp  # HTTP
   sudo ufw allow 443/tcp # HTTPS
   ```

---

## 📞 Soporte y Recursos

### Documentación Oficial

- [WAHA Documentation](https://waha.devlike.pro/docs/)
- [Google Gemini API](https://ai.google.dev/docs)
- [Docker Compose](https://docs.docker.com/compose/)
- [Nginx](https://nginx.org/en/docs/)

### Comunidad

- 🐛 [Reportar un bug](https://github.com/appdeveloper777/TypeEasy/issues)
- 💬 [Discusiones](https://github.com/appdeveloper777/TypeEasy/discussions)
- 📧 Email: support@typeeasy.com

### Contribuir

Las contribuciones son bienvenidas. Para contribuir:

1. Fork el proyecto
2. Crea una rama (`git checkout -b feature/AmazingFeature`)
3. Commit tus cambios (`git commit -m 'Add AmazingFeature'`)
4. Push a la rama (`git push origin feature/AmazingFeature`)
5. Abre un Pull Request

---

## 📄 Licencia

Este proyecto está bajo la Licencia MIT. Ver el archivo `LICENSE` para más detalles.

---

## 🙏 Agradecimientos

- **WAHA** - Por proporcionar una excelente API de WhatsApp
- **Google Gemini** - Por la IA generativa
- **Comunidad TypeEasy** - Por el soporte continuo
- **Todos los contribuidores** - Por hacer este proyecto posible

---

**Desarrollado con ❤️ por el equipo de TypeEasy**

**Última actualización:** 30 de Noviembre, 2025
