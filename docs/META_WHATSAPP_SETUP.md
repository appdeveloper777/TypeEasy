# 📱 Configuración de Meta WhatsApp Cloud API

Esta guía te ayudará a configurar el chatbot para usar la API oficial de Meta WhatsApp Cloud en lugar de WAHA.

## 🌟 Ventajas de Meta WhatsApp Cloud API

- ✅ **Más estable** para producción
- ✅ **API oficial** de Meta/Facebook
- ✅ **Sin necesidad de escanear QR** constantemente
- ✅ **Mejor para empresas** verificadas
- ✅ **Gratis** hasta 1,000 conversaciones/mes

## 📋 Requisitos Previos

1. Una cuenta de Facebook Business
2. Un número de teléfono dedicado para WhatsApp Business
3. Acceso a [Facebook Developers](https://developers.facebook.com/)

---

## 🚀 Paso 1: Crear una App en Facebook Developers

### 1.1 Acceder al Panel de Desarrolladores

1. Ve a [https://developers.facebook.com/apps/](https://developers.facebook.com/apps/)
2. Haz clic en **"Create App"** o **"Crear aplicación"**

### 1.2 Configurar la App

1. Selecciona **"Business"** como tipo de app
2. Completa los datos:
   - **App Name:** `TypeEasy Chatbot` (o el nombre que prefieras)
   - **App Contact Email:** Tu email
   - **Business Account:** Selecciona o crea una cuenta de negocio

3. Haz clic en **"Create App"**

---

## 🔧 Paso 2: Configurar WhatsApp

### 2.1 Agregar el Producto WhatsApp

1. En el dashboard de tu app, busca **"WhatsApp"** en la lista de productos
2. Haz clic en **"Set up"** o **"Configurar"**

### 2.2 Obtener Credenciales

En la sección de WhatsApp, encontrarás:

1. **Phone Number ID** (ID del número de teléfono)
   - Copia este valor para `META_WHATSAPP_PHONE_ID`

2. **WhatsApp Business Account ID**

3. **Temporary Access Token**
   - Copia este token temporal para `META_WHATSAPP_TOKEN`
   - ⚠️ **Importante:** Este token expira en 24 horas. Más adelante crearás uno permanente.

---

## 🔐 Paso 3: Crear Token Permanente

### 3.1 Generar Token de Sistema

1. Ve a **Settings** → **Business Settings** en tu cuenta de Facebook Business
2. En el menú lateral, selecciona **"System Users"**
3. Haz clic en **"Add"** para crear un nuevo usuario del sistema
4. Dale un nombre (ej: "TypeEasy Bot") y rol de **Admin**
5. Haz clic en **"Add Assets"**
6. Selecciona tu app y otorga permisos completos
7. Haz clic en **"Generate New Token"**
8. Selecciona los siguientes permisos:
   - `whatsapp_business_messaging`
   - `whatsapp_business_management`
9. Copia el token generado → Este es tu `META_WHATSAPP_TOKEN` permanente

---

## 🔑 Paso 4: Obtener App Secret

1. En el dashboard de tu app, ve a **Settings** → **Basic**
2. Busca **"App Secret"**
3. Haz clic en **"Show"** e ingresa tu contraseña de Facebook
4. Copia el valor → Este es tu `META_APP_SECRET`

---

## 🌐 Paso 5: Configurar Webhook

### 5.1 Exponer tu Servidor

Si estás en desarrollo local, necesitas exponer tu servidor a internet:

**Opción A: Usando ngrok**

```bash
# Instalar ngrok
# https://ngrok.com/download

# Exponer el puerto 5002 (whatsapp_adapter)
ngrok http 5002
```

Copia la URL que te da ngrok (ej: `https://abcd1234.ngrok.io`)

**Opción B: Servidor en producción**

Si ya tienes un servidor con dominio, usa tu URL:
```
https://tu-dominio.com
```

### 5.2 Configurar el Webhook en Meta

1. En el panel de WhatsApp de tu app, ve a **Configuration**
2. En la sección **Webhook**, haz clic en **"Edit"**
3. Configura:
   - **Callback URL:** `https://tu-url.com/webhook`
   - **Verify Token:** Elige un token único (ej: `mi_token_secreto_123`)
     - Guarda este valor como `META_VERIFY_TOKEN` en tu `.env`
4. Haz clic en **"Verify and Save"**

### 5.3 Suscribirse a Eventos

1. En la sección **Webhook Fields**, suscríbete a:
   - ✅ `messages`
   - ✅ `message_status` (opcional)
2. Haz clic en **"Save"**

---

## ⚙️ Paso 6: Configurar TypeEasy

### 6.1 Editar `.env`

```bash
cd TypeEasy
cp .env.example .env
nano .env
```

### 6.2 Configurar Variables

```env
# ==========================================
# Gemini AI
# ==========================================
GEMINI_API_KEY=tu_api_key_de_gemini

# ==========================================
# WhatsApp Provider
# ==========================================
WHATSAPP_PROVIDER=meta

# ==========================================
# Meta WhatsApp Cloud API
# ==========================================
META_WHATSAPP_TOKEN=tu_token_permanente_aqui
META_WHATSAPP_PHONE_ID=tu_phone_number_id_aqui
META_APP_SECRET=tu_app_secret_aqui
META_VERIFY_TOKEN=mi_token_secreto_123

# ==========================================
# Agent Configuration
# ==========================================
AGENT_WEBHOOK=http://agent_gemini:8081/whatsapp_hook

# ==========================================
# Debug
# ==========================================
TYPEEASY_DEBUG=1
```

### 6.3 Levantar Servicios

```bash
# Levantar servicios (sin WAHA)
docker compose up -d agent_gemini whatsapp_adapter gemini

# Verificar que estén corriendo
docker compose ps
```

---

## 🧪 Paso 7: Probar el Chatbot

### 7.1 Enviar Mensaje de Prueba

1. Desde tu teléfono, envía un mensaje de WhatsApp al número que configuraste
2. El chatbot debería responder automáticamente

### 7.2 Ver Logs

```bash
# Ver logs del adapter
docker compose logs -f whatsapp_adapter

# Ver logs del agente
docker compose logs -f agent_gemini

# Ver logs de Gemini
docker compose logs -f gemini
```

### 7.3 Verificar Webhook

Puedes verificar que el webhook está funcionando en:
```
https://developers.facebook.com/apps/TU_APP_ID/webhooks/
```

---

## 🐛 Solución de Problemas

### Problema 1: Webhook no se verifica

**Síntomas:**
- Error al guardar el webhook en Meta

**Soluciones:**

1. **Verificar que el servidor esté accesible:**
   ```bash
   curl https://tu-url.com/webhook
   ```

2. **Verificar logs del adapter:**
   ```bash
   docker compose logs whatsapp_adapter --tail 50
   ```

3. **Verificar que META_VERIFY_TOKEN coincida:**
   - El token en `.env` debe ser exactamente igual al que pusiste en Meta

### Problema 2: No recibo mensajes

**Síntomas:**
- El webhook se verificó, pero no llegan mensajes

**Soluciones:**

1. **Verificar suscripción a eventos:**
   - En Meta, asegúrate de estar suscrito a `messages`

2. **Verificar logs:**
   ```bash
   docker compose logs -f whatsapp_adapter
   ```
   Deberías ver: `Incoming webhook received`

3. **Verificar que el número esté agregado:**
   - En Meta, ve a **API Setup** → **To**
   - Agrega tu número de prueba

### Problema 3: El bot no responde

**Síntomas:**
- Recibes el mensaje pero el bot no responde

**Soluciones:**

1. **Verificar logs del agente:**
   ```bash
   docker compose logs agent_gemini --tail 50
   ```

2. **Verificar que Gemini esté funcionando:**
   ```bash
   docker compose logs gemini --tail 50
   ```

3. **Verificar GEMINI_API_KEY:**
   - Asegúrate de que tu API key sea válida

### Problema 4: Error "Invalid access token"

**Síntomas:**
- Error al enviar mensajes

**Soluciones:**

1. **Generar nuevo token permanente:**
   - Sigue los pasos del Paso 3

2. **Verificar permisos del token:**
   - Debe tener `whatsapp_business_messaging`

---

## 📊 Comparación: WAHA vs Meta API

| Característica | WAHA | Meta API |
|----------------|------|----------|
| **Costo** | Gratis | Gratis (hasta 1,000 conversaciones/mes) |
| **Estabilidad** | Buena | Excelente |
| **Configuración** | Escanear QR | Configurar webhook |
| **Mantenimiento** | Requiere QR periódicamente | Sin mantenimiento |
| **Límites** | Sin límites | 1,000 conversaciones gratis/mes |
| **Verificación** | No requiere | Requiere verificación para producción |
| **Mejor para** | Desarrollo y pruebas | Producción |

---

## 🔄 Migrar de WAHA a Meta API

Si ya tienes WAHA funcionando y quieres migrar:

### 1. Actualizar `.env`

```env
# Cambiar de:
WHATSAPP_PROVIDER=waha

# A:
WHATSAPP_PROVIDER=meta
```

### 2. Agregar credenciales de Meta

```env
META_WHATSAPP_TOKEN=tu_token
META_WHATSAPP_PHONE_ID=tu_phone_id
META_APP_SECRET=tu_app_secret
META_VERIFY_TOKEN=tu_verify_token
```

### 3. Reiniciar servicios

```bash
# Detener WAHA
docker compose stop waha

# Reiniciar adapter y agente
docker compose restart whatsapp_adapter agent_gemini
```

---

## 📝 Notas Importantes

### Límites de la API Gratuita

- **1,000 conversaciones gratis/mes**
- Una conversación = ventana de 24 horas con un usuario
- Después de 1,000: $0.005 - $0.009 USD por conversación

### Verificación de Negocio

Para producción, Meta requiere:
- Verificar tu negocio en Facebook Business
- Puede tomar 1-3 días hábiles

### Números de Prueba

Meta te da un número de prueba para desarrollo:
- Puedes agregar hasta 5 números para probar
- No requiere verificación de negocio

---

## 🔗 Recursos Adicionales

- [Documentación oficial de Meta WhatsApp](https://developers.facebook.com/docs/whatsapp)
- [Guía de inicio rápido](https://developers.facebook.com/docs/whatsapp/cloud-api/get-started)
- [Referencia de API](https://developers.facebook.com/docs/whatsapp/cloud-api/reference)
- [Precios de WhatsApp Business](https://developers.facebook.com/docs/whatsapp/pricing)

---

## 💡 Consejos

1. **Usa ngrok para desarrollo:**
   - Más fácil que configurar SSL local

2. **Guarda tu token permanente:**
   - No expira, guárdalo en un lugar seguro

3. **Monitorea el uso:**
   - Revisa el dashboard de Meta para ver cuántas conversaciones usas

4. **Prueba primero con números de prueba:**
   - Antes de ir a producción

---

**¿Necesitas ayuda?** Abre un [Issue en GitHub](https://github.com/appdeveloper777/TypeEasy/issues)
