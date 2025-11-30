# 🚀 Guía de Configuración: Agente WhatsApp con Gemini AI

Esta guía te ayudará a configurar y ejecutar el agente WhatsApp inteligente con Gemini AI.

## 📋 Requisitos Previos

- Docker Desktop instalado y ejecutándose
- Cuenta de Google (para obtener API key de Gemini)
- (Opcional) Cuenta de WhatsApp Business para producción

---

## 🔑 Paso 1: Obtener API Key de Gemini

### 1.1 Acceder a Google AI Studio

1. Ve a [Google AI Studio](https://aistudio.google.com/app/apikey)
2. Inicia sesión con tu cuenta de Google
3. Acepta los términos de servicio si es la primera vez

### 1.2 Crear API Key

1. Haz clic en **"Get API Key"** o **"Create API Key"**
2. Selecciona un proyecto existente o crea uno nuevo
3. Copia la API key generada (empieza con `AIzaSy...`)

**⚠️ Importante**: Guarda esta API key en un lugar seguro. No la compartas públicamente.

### 1.3 Tier Gratuito

El tier gratuito de Gemini incluye:
- ✅ **1,500 consultas por día**
- ✅ Sin necesidad de tarjeta de crédito
- ✅ Perfecto para desarrollo y pruebas

---

## ⚙️ Paso 2: Configurar Variables de Entorno

### 2.1 Copiar archivo de ejemplo

```bash
# En la raíz del proyecto TypeEasy
cp .env.example .env
```

### 2.2 Editar archivo `.env`

Abre el archivo `.env` y agrega tu API key de Gemini:

```bash
# Gemini AI (para agente inteligente)
GEMINI_API_KEY=AIzaSy...tu_api_key_aqui
GEMINI_MODEL=gemini-2.0-flash-exp
```

**Modelos disponibles:**
- `gemini-2.0-flash-exp` - Más rápido y económico (recomendado)
- `gemini-1.5-pro` - Más potente para tareas complejas
- `gemini-1.5-flash` - Balance entre velocidad y capacidad

---

## 🐳 Paso 3: Construir e Iniciar Servicios

### 3.1 Construir imágenes Docker

```bash
# Construir todos los servicios necesarios
docker compose build gemini agent_gemini whatsapp_adapter
```

### 3.2 Iniciar servicios

```bash
# Iniciar en modo detached (background)
docker compose up -d gemini agent_gemini whatsapp_adapter

# Ver logs en tiempo real
docker compose logs -f agent_gemini gemini
```

### 3.3 Verificar que todo esté funcionando

```bash
# Health check del servicio Gemini
curl http://localhost:5003/health

# Debería retornar:
# {
#   "status": "healthy",
#   "service": "gemini_service",
#   "model": "gemini-2.0-flash-exp",
#   "api_key_configured": true
# }
```

---

## 🧪 Paso 4: Probar el Agente

### 4.1 Prueba Directa al Servicio Gemini

```bash
# Enviar mensaje de prueba
curl -X POST http://localhost:5003/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Hola, ¿qué pizzas tienen?", "from_number": "test123"}'
```

### 4.2 Prueba del Flujo Completo (WhatsApp → Agente → Gemini)

```bash
# Simular mensaje de WhatsApp
curl -X POST "http://localhost:8082/whatsapp_hook?message=Hola,%20quiero%20ordenar%20una%20pizza"

# Ver logs del agente
docker compose logs -f agent_gemini
```

### 4.3 Ver Historial de Conversación

```bash
# Ver historial de un usuario específico
curl "http://localhost:5003/history?from_number=test123"

# Ver estadísticas generales
curl http://localhost:5003/history
```

---

## 📱 Paso 5: Conectar con WhatsApp Real (Opcional)

### Opción A: Usar Twilio

1. Crea una cuenta en [Twilio](https://www.twilio.com/)
2. Obtén un número de WhatsApp Business
3. Configura las credenciales en `.env`:

```bash
TWILIO_ACCOUNT_SID=tu_account_sid
TWILIO_AUTH_TOKEN=tu_auth_token
TWILIO_FROM=whatsapp:+1234567890
```

### Opción B: Usar Meta WhatsApp Cloud API

1. Crea una app en [Meta for Developers](https://developers.facebook.com/)
2. Configura WhatsApp Business API
3. Agrega credenciales en `.env`:

```bash
META_WHATSAPP_TOKEN=tu_token
META_WHATSAPP_PHONE_ID=tu_phone_id
META_APP_SECRET=tu_app_secret
META_VERIFY_TOKEN=tu_verify_token
```

### Exponer con ngrok (para webhooks)

```bash
# Instalar ngrok si no lo tienes
# Luego ejecutar:
ngrok http 5002

# Copia la URL pública (ej: https://abc123.ngrok.io)
# Configúrala en Twilio o Meta como webhook URL
```

---

## 🎨 Paso 6: Personalizar el Bot

### 6.1 Modificar el Comportamiento

Edita `tools/gemini_service/app.py` y cambia el `SYSTEM_PROMPT`:

```python
SYSTEM_PROMPT = """Eres un asistente virtual para [TU NEGOCIO].

Tu trabajo es:
- [Tarea 1]
- [Tarea 2]
...

Menú disponible:
- [Producto 1]: $[Precio]
- [Producto 2]: $[Precio]
"""
```

### 6.2 Reconstruir el servicio

```bash
# Después de modificar app.py
docker compose build gemini
docker compose restart gemini
```

---

## 🐛 Solución de Problemas

### Problema: "api_key_configured": false

**Solución**: Verifica que `GEMINI_API_KEY` esté en tu archivo `.env`

```bash
# Ver variables de entorno del contenedor
docker compose exec gemini env | grep GEMINI
```

### Problema: Error 429 (Too Many Requests)

**Solución**: Has excedido el límite de 1,500 consultas/día del tier gratuito.
- Espera 24 horas
- O actualiza a plan de pago

### Problema: El agente no responde

**Solución**: Verifica logs

```bash
# Ver logs del agente
docker compose logs agent_gemini

# Ver logs de Gemini
docker compose logs gemini

# Ver logs del adapter
docker compose logs whatsapp_adapter
```

### Problema: "Connection refused" al llamar a Gemini

**Solución**: Asegúrate de que el servicio Gemini esté corriendo

```bash
# Ver servicios activos
docker compose ps

# Reiniciar servicio
docker compose restart gemini
```

---

## 📊 Monitoreo y Debugging

### Ver logs en tiempo real

```bash
# Todos los servicios
docker compose logs -f

# Solo agente y Gemini
docker compose logs -f agent_gemini gemini
```

### Limpiar historial conversacional

```bash
# Limpiar todo el historial
curl -X POST http://localhost:5003/clear_history

# Limpiar solo un usuario
curl -X POST http://localhost:5003/clear_history \
  -H "Content-Type: application/json" \
  -d '{"from_number": "test123"}'
```

### Reiniciar servicios

```bash
# Reiniciar todo
docker compose restart

# Reiniciar solo Gemini
docker compose restart gemini
```

---

## 💰 Estimación de Costos

### Desarrollo (Tier Gratuito)
- **Costo**: $0
- **Límite**: 1,500 consultas/día
- **Suficiente para**: Desarrollo y pruebas

### Producción (Estimado)

**Escenario: 1,000 usuarios/mes, 10 mensajes cada uno**

| Concepto | Cantidad | Costo |
|----------|----------|-------|
| Mensajes totales | 10,000 | - |
| Tokens estimados | ~1.5M | - |
| Costo Gemini | - | **$0.60/mes** |
| Costo WhatsApp (Twilio) | - | ~$5-10/mes |
| **Total estimado** | - | **$6-11/mes** |

---

## 📚 Recursos Adicionales

- [Documentación de Gemini](https://ai.google.dev/docs)
- [Google AI Studio](https://aistudio.google.com/)
- [Pricing de Gemini](https://ai.google.dev/pricing)
- [TypeEasy GitHub](https://github.com/appdeveloper777/TypeEasy)

---

## ✅ Checklist de Configuración

- [ ] Obtener API key de Gemini
- [ ] Configurar `.env` con GEMINI_API_KEY
- [ ] Construir servicios con `docker compose build`
- [ ] Iniciar servicios con `docker compose up -d`
- [ ] Verificar health check de Gemini
- [ ] Probar conversación de prueba
- [ ] (Opcional) Configurar WhatsApp real
- [ ] (Opcional) Personalizar SYSTEM_PROMPT

---

¡Listo! Tu agente WhatsApp con Gemini AI está configurado y funcionando. 🎉
