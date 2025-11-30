#!/usr/bin/env python3
"""
Gemini AI Service for TypeEasy WhatsApp Agent

Este servicio expone la API de Gemini para conversaciones inteligentes
con manejo de contexto conversacional.
"""

import os
import logging
from datetime import datetime
from flask import Flask, request, jsonify
import google.generativeai as genai

# Configuración de logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

app = Flask(__name__)

# Configuración de Gemini
GEMINI_API_KEY = os.environ.get('GEMINI_API_KEY')
GEMINI_MODEL = os.environ.get('GEMINI_MODEL', 'models/gemini-1.5-flash-001')

if not GEMINI_API_KEY:
    logger.warning('⚠️  GEMINI_API_KEY no configurada. El servicio no funcionará correctamente.')
else:
    genai.configure(api_key=GEMINI_API_KEY)
    logger.info(f'✅ Gemini configurado con modelo: {GEMINI_MODEL}')

# Almacenamiento de historial conversacional en memoria
# Estructura: {from_number: [{"role": "user", "parts": ["mensaje"]}, ...]}
conversation_history = {}

# Límite de mensajes en historial por usuario
MAX_HISTORY_LENGTH = 10

# Prompt del sistema para configurar el comportamiento del bot
SYSTEM_PROMPT = """Eres el Asistente Virtual experto de 'Rollers Perú', una empresa líder en venta de cortinas tipo Roller.

TU OBJETIVO PRINCIPAL:
Ayudar al cliente a cotizar sus rollers. Para eso, es INDISPENSABLE que les ayudes a TOMAR LAS MEDIDAS de sus ventanas correctamente.

TUS FUNCIONES:
1. Asesorar sobre tipos de tela (Blackout, Screen, Duo).
2. Guiar paso a paso en la TOMA DE MEDIDAS (Tu prioridad).
3. Cotizar aproximados (si te dan medidas).

CATÁLOGO DE PRODUCTOS:
- Roller Blackout (Bloqueo total de luz): Ideal para dormitorios. Desde S/. 90 m2.
- Roller Screen (Paso de luz, visibilidad exterior, filtro UV): Ideal para salas. Desde S/. 110 m2.
- Roller Duo (Zebra - Franjas opacas y traslúcidas): Moderno y versátil. Desde S/. 140 m2.

GUÍA PARA TOMAR MEDIDAS (Sigue estos pasos estrictamente):

Paso 1: Preguntar el tipo de instalación
- "¿La instalación será DENTRO del marco de la ventana o FUERA del marco (sobre la pared)?"

Paso 2: Instrucciones según respuesta
- Si es DENTRO del marco: "Mide el ANCHO exacto de extremo a extremo en la parte superior. Luego mide el ALTO. Restaremos 1cm al ancho para que encaje perfecto."
- Si es FUERA del marco: "Mide el ancho de la ventana y AGREGA 10cm a cada lado (20cm total) para cubrir bien. Al alto agrégale 15cm arriba y abajo."

Paso 3: Confirmación
- "Por favor, indícame las medidas finales en formato: ANCHO x ALTO (ejemplo: 1.50m ancho x 2.00m alto)."

REGLAS DE COMPORTAMIENTO:
- Sé amable, profesional y paciente.
- Usa emojis relacionados (📏, 🪟, ✨, 🏠).
- NO des precios finales exactos sin medidas, da "precios desde" o estimados.
- Si el usuario no sabe medir, ofrécele la guía paso a paso.
- Responde siempre en Español.
"""


@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        'status': 'healthy',
        'service': 'gemini_service',
        'model': GEMINI_MODEL,
        'api_key_configured': bool(GEMINI_API_KEY)
    }), 200


@app.route('/chat', methods=['POST'])
def chat():
    """
    Endpoint principal para conversaciones con Gemini
    
    Acepta:
    - JSON: {"message": "texto", "from_number": "identificador"}
    - Text/plain: mensaje directo (usa header X-WhatsApp-From para identificar usuario)
    
    Retorna:
    - JSON: {"response": "respuesta de Gemini"}
    """
    try:
        # Extraer mensaje y número del usuario
        if request.is_json:
            data = request.get_json()
            message = data.get('message', '')
            from_number = data.get('from_number', 'unknown')
        else:
            message = request.get_data(as_text=True) or ''
            from_number = request.headers.get('X-WhatsApp-From', 'unknown')
        
        if not message:
            return jsonify({'error': 'No message provided'}), 400
        
        logger.info(f'📨 Mensaje de {from_number}: {message}')
        
        # Verificar API key
        if not GEMINI_API_KEY:
            logger.error('❌ GEMINI_API_KEY no configurada')
            return jsonify({
                'response': 'Lo siento, el servicio de IA no está configurado correctamente. Por favor contacta al administrador.'
            }), 500
        
        # Obtener o crear historial para este usuario
        if from_number not in conversation_history:
            conversation_history[from_number] = []
            logger.info(f'🆕 Nueva conversación iniciada para {from_number}')
        
        # Agregar mensaje del usuario al historial
        conversation_history[from_number].append({
            'role': 'user',
            'parts': [message]
        })
        
        # Limitar tamaño del historial
        if len(conversation_history[from_number]) > MAX_HISTORY_LENGTH * 2:
            # Mantener solo los últimos MAX_HISTORY_LENGTH intercambios (user + model)
            conversation_history[from_number] = conversation_history[from_number][-(MAX_HISTORY_LENGTH * 2):]
            logger.info(f'🗑️  Historial recortado para {from_number}')
        
        # Crear modelo con configuración
        model = genai.GenerativeModel(
            model_name=GEMINI_MODEL,
            system_instruction=SYSTEM_PROMPT
        )
        
        # Iniciar chat con historial
        chat = model.start_chat(history=conversation_history[from_number][:-1])
        
        # Enviar mensaje y obtener respuesta
        response = chat.send_message(message)
        response_text = response.text
        
        # Agregar respuesta al historial
        conversation_history[from_number].append({
            'role': 'model',
            'parts': [response_text]
        })
        
        logger.info(f'🤖 Respuesta para {from_number}: {response_text[:100]}...')
        
        return jsonify({
            'response': response_text,
            'from_number': from_number,
            'timestamp': datetime.utcnow().isoformat() + 'Z'
        }), 200
        
    except Exception as e:
        logger.exception(f'❌ Error procesando mensaje: {e}')
        return jsonify({
            'response': 'Lo siento, ocurrió un error al procesar tu mensaje. Por favor intenta de nuevo.',
            'error': str(e)
        }), 500


@app.route('/clear_history', methods=['POST'])
def clear_history():
    """
    Limpia el historial conversacional de un usuario específico o de todos
    
    Acepta:
    - JSON: {"from_number": "identificador"} - Limpia solo ese usuario
    - Sin body: Limpia todo el historial
    """
    try:
        if request.is_json:
            data = request.get_json()
            from_number = data.get('from_number')
            if from_number and from_number in conversation_history:
                del conversation_history[from_number]
                logger.info(f'🗑️  Historial limpiado para {from_number}')
                return jsonify({'message': f'Historial limpiado para {from_number}'}), 200
            elif from_number:
                return jsonify({'message': f'No hay historial para {from_number}'}), 404
        
        # Limpiar todo el historial
        conversation_history.clear()
        logger.info('🗑️  Todo el historial conversacional limpiado')
        return jsonify({'message': 'Todo el historial limpiado'}), 200
        
    except Exception as e:
        logger.exception(f'❌ Error limpiando historial: {e}')
        return jsonify({'error': str(e)}), 500


@app.route('/history', methods=['GET'])
def get_history():
    """
    Obtiene el historial conversacional (útil para debugging)
    
    Query params:
    - from_number: Obtener historial de un usuario específico
    """
    try:
        from_number = request.args.get('from_number')
        
        if from_number:
            if from_number in conversation_history:
                return jsonify({
                    'from_number': from_number,
                    'history': conversation_history[from_number]
                }), 200
            else:
                return jsonify({
                    'from_number': from_number,
                    'history': []
                }), 200
        
        # Retornar estadísticas generales
        return jsonify({
            'total_users': len(conversation_history),
            'users': list(conversation_history.keys())
        }), 200
        
    except Exception as e:
        logger.exception(f'❌ Error obteniendo historial: {e}')
        return jsonify({'error': str(e)}), 500


if __name__ == '__main__':
    logger.info('🚀 Iniciando Gemini AI Service...')
    app.run(host='0.0.0.0', port=5003, debug=False)
