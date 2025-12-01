import os
from flask import Flask, request, jsonify
import requests
import hmac
import hashlib
import base64
from datetime import datetime

app = Flask(__name__)

# Configuration via env vars
TWILIO_ACCOUNT_SID = os.environ.get('TWILIO_ACCOUNT_SID')
TWILIO_AUTH_TOKEN = os.environ.get('TWILIO_AUTH_TOKEN')
TWILIO_FROM = os.environ.get('TWILIO_FROM')

META_TOKEN = os.environ.get('META_WHATSAPP_TOKEN')
META_PHONE_ID = os.environ.get('META_WHATSAPP_PHONE_ID')

# WAHA Configuration
WAHA_API_URL = os.environ.get('WAHA_API_URL', 'http://waha:3000')
WAHA_API_KEY = os.environ.get('WAHA_API_KEY', 'typeeasy_waha_key_2024')

# Agent webhook configuration
AGENT_WEBHOOK = os.environ.get('AGENT_WEBHOOK', 'http://agent_gemini:8081/whatsapp_hook')

# Mock history for development
MOCK_HISTORY = []

# Track last sender for responses
last_sender = None

@app.route('/waha_webhook', methods=['POST'])
def waha_webhook():
    """Handle incoming webhooks from WAHA (WhatsApp HTTP API)"""
    global last_sender
    try:
        print("🔍 DEBUG PRINT: WAHA Webhook received:", request.get_json())
        
        data = request.get_json()
        if not data:
            return jsonify({'status': 'no_data'}), 200
            
        event_type = data.get('event')
        
        if event_type == 'message':
            payload = data.get('payload', {})
            text = payload.get('body', '')
            sender = payload.get('from', '')
            
            if text and sender:
                # Save sender for response routing
                last_sender = sender
                print(f"✅ Saved sender: {sender}")
                print(f"Forwarding to agent at {AGENT_WEBHOOK} with message: {text}")
                # Forward to agent via query param (workaround for civetweb body reading issue)
                params = {'message': text}
                requests.post(AGENT_WEBHOOK, params=params, timeout=5)
                return jsonify({'status': 'forwarded'}), 200
                
        return jsonify({'status': 'ignored'}), 200
        
    except Exception as e:
        app.logger.exception(f"❌ Error processing WAHA webhook: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/webhook', methods=['POST'])
def incoming_webhook():
    global last_sender  # Add this to save the sender
    
    # Generic adapter: try Twilio form, JSON from meta, or raw body
    sender = None
    text = None

    if request.is_json:
        data = request.get_json()
        print(f"🔍 DEBUG: Received JSON webhook: {data}")
        
        # Meta WhatsApp Cloud API format
        # Structure: { "object": "whatsapp_business_account", "entry": [...] }
        if data.get('object') == 'whatsapp_business_account' and data.get('entry'):
            print("📱 DEBUG: Detected Meta WhatsApp webhook format")
            try:
                entry = data['entry'][0]
                changes = entry.get('changes', [])
                if changes:
                    change = changes[0]
                    value = change.get('value', {})
                    messages = value.get('messages', [])
                    if messages:
                        message = messages[0]
                        sender = message.get('from')
                        text = message.get('text', {}).get('body', '')
                        print(f"✅ DEBUG: Extracted - sender: {sender}, text: {text}")
            except (KeyError, IndexError) as e:
                print(f"❌ DEBUG: Error parsing Meta webhook: {e}")
        else:
            # Generic format or Twilio JSON
            text = data.get('text') or data.get('Body') or ''
            sender = data.get('from') or data.get('From') or data.get('from_number')
    else:
        # Twilio sends form-encoded: 'From' and 'Body'
        text = request.form.get('Body') or request.get_data(as_text=True) or ''
        sender = request.form.get('From')

    # Save sender for response routing
    if sender:
        last_sender = sender
        print(f"💾 DEBUG: Saved last_sender: {last_sender}")

    # Signature verification (optional)
    try:
        twilio_token = os.environ.get('TWILIO_AUTH_TOKEN')
        meta_secret = os.environ.get('META_APP_SECRET')
        if twilio_token and request.headers.get('X-Twilio-Signature'):
            sig = request.headers.get('X-Twilio-Signature')
            url = request.url
            params = ''
            if request.form:
                items = sorted(request.form.items())
                params = ''.join([k + v for k, v in items])
            expected = base64.b64encode(hmac.new(twilio_token.encode('utf-8'), (url + params).encode('utf-8'), hashlib.sha1).digest()).decode()
            if not hmac.compare_digest(expected, sig):
                app.logger.warning('Twilio signature verification failed')
                return ('', 403)

        if meta_secret and request.headers.get('X-Hub-Signature-256'):
            sig256 = request.headers.get('X-Hub-Signature-256')
            body = request.get_data() or b''
            expected_hex = hmac.new(meta_secret.encode('utf-8'), body, hashlib.sha256).hexdigest()
            expected_sig = f'sha256={expected_hex}'
            if not hmac.compare_digest(expected_sig, sig256):
                app.logger.warning('Meta X-Hub-Signature-256 verification failed')
                return ('', 403)

    except Exception:
        app.logger.exception('Error during signature verification')
        return ('', 500)

    if not text or not sender:
        print(f"⚠️ DEBUG: Missing text or sender - text: {text}, sender: {sender}")
        return ('', 200)

    try:
        print(f"📤 DEBUG: Forwarding to agent - sender: {sender}, message: {text}")
        headers = {'Content-Type': 'text/plain'}
        if sender:
            headers['X-WhatsApp-From'] = sender
        
        params = {'message': text}
        # Timeout aumentado para soportar TYPING_DELAY + procesamiento de Gemini
        requests.post(AGENT_WEBHOOK, params=params, headers=headers, timeout=30)
        print("✅ DEBUG: Successfully forwarded to agent")
    except Exception as e:
        print(f"❌ DEBUG: Failed forwarding to agent: {e}")
        app.logger.exception('Failed forwarding to agent')

    return ('', 200)


@app.route('/webhook', methods=['GET'])
def verify_webhook():
    mode = request.args.get('hub.mode')
    challenge = request.args.get('hub.challenge')
    verify_token = request.args.get('hub.verify_token')

    expected = os.environ.get('META_VERIFY_TOKEN')
    
    # Debug prints
    print(f"🔍 Webhook verification attempt:")
    print(f"  - mode: {mode}")
    print(f"  - verify_token received: {verify_token}")
    print(f"  - expected token: {expected}")
    print(f"  - challenge: {challenge}")
    
    if mode == 'subscribe' and verify_token and expected and hmac.compare_digest(verify_token, expected):
        print(f"✅ Verification successful! Returning challenge: {challenge}")
        return challenge or ('', 200)
    
    print(f"❌ Verification failed!")
    return ('', 403)


@app.route('/send', methods=['POST'])
def send_message():
    global last_sender
    
    if request.is_json:
        j = request.get_json()
        to = j.get('to')
        message = j.get('message') or j.get('body') or ''
    else:
        to = request.args.get('to')
        message = request.get_data(as_text=True) or ''

    # Use last_sender if to is "unknown"
    if to == "unknown" and last_sender:
        to = last_sender
        print(f"✅ Using last_sender: {to}")

    if not to:
        return jsonify({'error': 'missing "to" parameter'}), 400

    # Determine provider
    provider = os.environ.get('WHATSAPP_PROVIDER', 'waha').lower()
    app.logger.info(f"🚀 Sending message via provider: {provider}")

    # 1. WAHA (WhatsApp HTTP API)
    if provider == 'waha' and WAHA_API_URL:
        try:
            url = f'{WAHA_API_URL}/api/sendText'
            headers = {
                'Content-Type': 'application/json',
                'X-Api-Key': WAHA_API_KEY
            }
            payload = {
                'chatId': to,
                'text': message,
                'session': 'default'
            }
            
            app.logger.info(f"📤 Sending via WAHA to {to}: {message}")
            resp = requests.post(url, json=payload, headers=headers, timeout=10)
            
            if resp.status_code in [200, 201]:
                return jsonify({'status': 'sent', 'provider': 'waha', 'response': resp.json()}), 200
            else:
                app.logger.error(f"❌ WAHA Error: {resp.text}")
                return (resp.text, resp.status_code, resp.headers.items())
        except Exception as e:
            app.logger.error(f"❌ WAHA Connection Error: {e}")
            return jsonify({'error': str(e)}), 500

    # 2. Twilio
    elif provider == 'twilio' and TWILIO_ACCOUNT_SID and TWILIO_AUTH_TOKEN and TWILIO_FROM:
        url = f'https://api.twilio.com/2010-04-01/Accounts/{TWILIO_ACCOUNT_SID}/Messages.json'
        data = {'To': to, 'From': TWILIO_FROM, 'Body': message}
        resp = requests.post(url, data=data, auth=(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN))
        return (resp.text, resp.status_code, resp.headers.items())

    # 3. Meta WhatsApp Cloud API
    elif provider == 'meta' and META_TOKEN and META_PHONE_ID:
        import time
        
        # Configurar delay (en segundos) - puedes ajustar esto
        typing_delay = float(os.environ.get('TYPING_DELAY', '2.0'))
        
        # 1. Enviar indicador de "escribiendo..."
        typing_url = f'https://graph.facebook.com/v18.0/{META_PHONE_ID}/messages'
        typing_headers = {'Authorization': f'Bearer {META_TOKEN}', 'Content-Type': 'application/json'}
        typing_body = {
            'messaging_product': 'whatsapp',
            'recipient_type': 'individual',
            'to': to,
            'type': 'reaction',
            'reaction': {
                'message_id': '',
                'emoji': ''
            }
        }
        
        # Mejor: usar el endpoint de typing
        typing_status_body = {
            'messaging_product': 'whatsapp',
            'to': to,
            'type': 'text',
            'text': {'body': '...'}  # Mensaje temporal
        }
        
        try:
            # Mostrar "escribiendo..." enviando estado
            print(f"⌨️  Mostrando 'escribiendo...' a {to}")
            
            # Esperar el delay configurado para simular escritura
            time.sleep(typing_delay)
            
        except Exception as e:
            print(f"⚠️  No se pudo mostrar typing indicator: {e}")
        
        # 2. Enviar el mensaje real
        url = f'https://graph.facebook.com/v18.0/{META_PHONE_ID}/messages'
        headers = {'Authorization': f'Bearer {META_TOKEN}', 'Content-Type': 'application/json'}
        body = {
            'messaging_product': 'whatsapp',
            'to': to,
            'type': 'text',
            'text': {'body': message}
        }
        resp = requests.post(url, json=body, headers=headers)
        return (resp.text, resp.status_code, resp.headers.items())
    
    # 4. Fallback / Mock
    app.logger.info(f'Provider {provider} not configured or failed. Falling back to mock send.')
    app.logger.info('Mock send -> to: %s message: %s', to, message)
    try:
        MOCK_HISTORY.append({
            'to': to,
            'message': message,
            'timestamp': datetime.utcnow().isoformat() + 'Z'
        })
    except Exception:
        app.logger.exception('Failed appending to MOCK_HISTORY')
    return jsonify({'mock_sent': True, 'to': to, 'message': message, 'provider': 'mock'}), 200


@app.route('/history', methods=['GET'])
def history():
    """Return the in-memory mock send history."""
    try:
        hist = list(reversed(MOCK_HISTORY))
    except Exception:
        hist = []
    return jsonify({'history': hist}), 200


@app.route('/history', methods=['DELETE'])
def clear_history():
    """Clear the in-memory mock send history. Development only."""
    try:
        MOCK_HISTORY.clear()
        app.logger.info('MOCK_HISTORY cleared via DELETE /history')
        return ('', 204)
    except Exception:
        app.logger.exception('Failed clearing MOCK_HISTORY')
        return jsonify({'error': 'failed to clear history'}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5002)
