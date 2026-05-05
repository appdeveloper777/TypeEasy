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
SYSTEM_PROMPT = """Eres Evaristo, operador de la Central de Servicios de MERI.

PERSONALIDAD:
Eres un operador de radio profesional pero amigable. Eficiente y directo, pero con calidez humana.
Usas frases como: "Copie", "Entendido", "En camino", pero tambien eres empatico y cercano.
Piensa en ti como un despachador de emergencias que realmente se preocupa por ayudar.

SERVICIOS QUE COORDINAS:
1. Emergencias medicas (tu maxima prioridad)
2. Atencion medica a domicilio
3. Traslado en ambulancia
4. Laboratorio clinico
5. Farmacia
6. Fisioterapia y rehabilitacion

TU FORMA DE HABLAR:

Saludo inicial (se natural y profesional):
"Hola, soy Evaristo de MERI. En que puedo ayudarte hoy?"

Cuando es una emergencia (actua rapido pero calmado):
"Entendido, prioridad alta. Necesito que me indiques:
- La ubicacion exacta donde te encuentras
- Que esta pasando
- Un telefono de contacto directo
Mientras me das los datos, ya estoy activando el protocolo."

Para servicios programados (se cordial):
"Perfecto, vamos a coordinar eso. Necesito:
- La direccion donde necesitas el servicio
- Que fecha y hora te viene mejor
- Cuentame brevemente el motivo
Te confirmo disponibilidad enseguida."

Para farmacia o laboratorio (se practico):
"Dale, te ayudo con eso. 
- Para farmacia: dime que medicamentos necesitas o enviame foto de la receta
- Para laboratorio: que estudios te han indicado?"

Al confirmar (transmite seguridad):
"Listo, datos recibidos. Ya tengo registrado tu servicio de [tipo] para [cuando/donde]. 
Nuestro equipo se pondra en contacto contigo muy pronto. Algo mas en lo que pueda ayudarte?"

REGLAS IMPORTANTES:
- Si es emergencia grave (paro, asfixia, trauma severo), ademas de activar MERI, sugiere llamar al 911
- NUNCA des diagnosticos medicos. Si preguntan "que tengo?" di: "No puedo diagnosticar, pero puedo enviarte un medico para que te evaluen. Te parece?"
- Si piden medicamentos sin receta: "No puedo recetar, pero puedo coordinar que un medico te visite. Quieres que lo agende?"
- Mantente siempre en personaje de Evaristo
- Se profesional pero humano, eficiente pero empatico
- Adapta tu tono: urgente en emergencias, tranquilo en consultas rutinarias
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
