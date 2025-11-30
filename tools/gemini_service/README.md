# 🤖 Gemini AI Service

Servicio de inteligencia artificial para el agente WhatsApp de TypeEasy, usando Google Gemini API.

## 🎯 Características

- ✅ Conversaciones inteligentes con Gemini 2.0 Flash
- ✅ Manejo de contexto conversacional (historial por usuario)
- ✅ Múltiples endpoints (chat, health, history)
- ✅ Configuración personalizable del bot
- ✅ Logging detallado para debugging

## 🔑 Obtener API Key de Gemini

1. Ve a [Google AI Studio](https://aistudio.google.com/app/apikey)
2. Inicia sesión con tu cuenta de Google
3. Haz clic en **"Get API Key"** o **"Create API Key"**
4. Copia la API key generada

**Tier Gratuito**: 1,500 consultas por día (suficiente para desarrollo y pruebas)

## ⚙️ Configuración

### Variables de Entorno

```bash
# Requerida
GEMINI_API_KEY=tu_api_key_aqui

# Opcional (por defecto: gemini-2.0-flash-exp)
GEMINI_MODEL=gemini-2.0-flash-exp
```

### Agregar a `.env`

```bash
# Gemini AI
GEMINI_API_KEY=AIzaSy...tu_key_aqui
GEMINI_MODEL=gemini-2.0-flash-exp
```

## 🚀 Uso

### Ejecutar con Docker Compose

```bash
# Construir e iniciar el servicio
docker compose up -d gemini

# Ver logs
docker compose logs -f gemini
```

### Probar el servicio

```bash
# Health check
curl http://localhost:5003/health

# Enviar mensaje
curl -X POST http://localhost:5003/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Hola, ¿qué pizzas tienen?", "from_number": "test123"}'

# Ver historial
curl http://localhost:5003/history?from_number=test123

# Limpiar historial
curl -X POST http://localhost:5003/clear_history \
  -H "Content-Type: application/json" \
  -d '{"from_number": "test123"}'
```

## 📡 Endpoints

### `GET /health`
Health check del servicio

**Respuesta:**
```json
{
  "status": "healthy",
  "service": "gemini_service",
  "model": "gemini-2.0-flash-exp",
  "api_key_configured": true
}
```

### `POST /chat`
Enviar mensaje a Gemini

**Request (JSON):**
```json
{
  "message": "Quiero ordenar una pizza",
  "from_number": "whatsapp:+1234567890"
}
```

**Request (Text/Plain):**
```
Quiero ordenar una pizza
```
Con header: `X-WhatsApp-From: whatsapp:+1234567890`

**Response:**
```json
{
  "response": "¡Claro! 🍕 Tenemos pizzas medianas ($150) y grandes ($200). ¿Cuál prefieres?",
  "from_number": "whatsapp:+1234567890",
  "timestamp": "2025-11-29T18:00:00.000Z"
}
```

### `GET /history`
Obtener historial conversacional

**Query params:**
- `from_number` (opcional): Filtrar por usuario

**Response:**
```json
{
  "from_number": "test123",
  "history": [
    {"role": "user", "parts": ["Hola"]},
    {"role": "model", "parts": ["¡Hola! ¿En qué puedo ayudarte?"]}
  ]
}
```

### `POST /clear_history`
Limpiar historial conversacional

**Request (opcional):**
```json
{
  "from_number": "test123"
}
```

## 🎨 Personalización

Edita el `SYSTEM_PROMPT` en `app.py` para cambiar el comportamiento del bot:

```python
SYSTEM_PROMPT = """Eres un asistente virtual amigable...

Tu trabajo es:
- Ayudar a los clientes a hacer pedidos
- Responder preguntas sobre el menú
...
"""
```

## 🐛 Debugging

```bash
# Ver logs en tiempo real
docker compose logs -f gemini

# Verificar configuración
curl http://localhost:5003/health

# Probar sin Docker
export GEMINI_API_KEY=tu_key
python app.py
```

## 💰 Costos

- **Tier Gratuito**: 1,500 consultas/día
- **Modelo**: gemini-2.0-flash-exp
  - Input: $0.10 por millón de tokens
  - Output: $0.40 por millón de tokens

**Estimación**: ~$0.60/mes para 1,000 usuarios con 10 mensajes cada uno

## 📚 Recursos

- [Gemini API Docs](https://ai.google.dev/docs)
- [Google AI Studio](https://aistudio.google.com/)
- [Pricing](https://ai.google.dev/pricing)
