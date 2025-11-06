import os
from flask import Flask, request, jsonify
import requests
import hmac
import hashlib
import base64
import os
from datetime import datetime

app = Flask(__name__)

# Configuration via env vars
TWILIO_ACCOUNT_SID = os.environ.get('TWILIO_ACCOUNT_SID')
TWILIO_AUTH_TOKEN = os.environ.get('TWILIO_AUTH_TOKEN')
TWILIO_FROM = os.environ.get('TWILIO_FROM')

META_TOKEN = os.environ.get('META_WHATSAPP_TOKEN')
META_PHONE_ID = os.environ.get('META_WHATSAPP_PHONE_ID')

# Agent endpoint (the TypeEasy agent webhook)
AGENT_WEBHOOK = os.environ.get('AGENT_WEBHOOK', 'http://agent:8081/whatsapp_hook')

# In-memory mock history for development (not persisted)
MOCK_HISTORY = []


@app.route('/webhook', methods=['POST'])
def incoming_webhook():
    # Generic adapter: try Twilio form, JSON from meta, or raw body
    sender = None
    text = None

    if request.is_json:
        data = request.get_json()
        # Meta webhook structure or generic
        text = data.get('text') or data.get('Body') or ''
        sender = data.get('from') or data.get('From') or data.get('from_number')
    else:
        # Twilio sends form-encoded: 'From' and 'Body'
        text = request.form.get('Body') or request.get_data(as_text=True) or ''
        sender = request.form.get('From')

    # Signature verification (optional)
    # Twilio: X-Twilio-Signature (HMAC-SHA1 over URL+params)
    # Meta: X-Hub-Signature-256 (sha256 HMAC over raw body)
    try:
        # Verify Twilio signature if configured
        twilio_token = os.environ.get('TWILIO_AUTH_TOKEN')
        meta_secret = os.environ.get('META_APP_SECRET')
        if twilio_token and request.headers.get('X-Twilio-Signature'):
            sig = request.headers.get('X-Twilio-Signature')
            # Build the expected signature per Twilio docs
            url = request.url
            # For form-encoded, use params sorted by key
            params = ''
            if request.form:
                # Twilio concatenates keys and values in alphabetical order
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

    # Forward to agent webhook as raw text body so the agent's /whatsapp_hook
    # handler (which reads raw body or ?message=) receives the message in 'mensaje'.
    try:
        # Prefer sending raw text body; include 'from' as header if available
        headers = {'Content-Type': 'text/plain'}
        if sender:
            headers['X-WhatsApp-From'] = sender
        requests.post(AGENT_WEBHOOK, data=text, headers=headers, timeout=5)
    except Exception:
        app.logger.exception('Failed forwarding to agent')

    return ('', 200)


@app.route('/webhook', methods=['GET'])
def verify_webhook():
    # Handle the verification handshake from Meta (Facebook) Webhooks
    mode = request.args.get('hub.mode')
    challenge = request.args.get('hub.challenge')
    verify_token = request.args.get('hub.verify_token')

    expected = os.environ.get('META_VERIFY_TOKEN')
    if mode == 'subscribe' and verify_token and expected and hmac.compare_digest(verify_token, expected):
        return challenge or ('', 200)
    return ('', 403)


@app.route('/send', methods=['POST'])
def send_message():
    # Accept JSON {to, message} or raw body (message) with ?to= parameter
    if request.is_json:
        j = request.get_json()
        to = j.get('to')
        message = j.get('message') or j.get('body') or ''
    else:
        to = request.args.get('to')
        message = request.get_data(as_text=True) or ''

    if not to:
        return jsonify({'error': 'missing "to" parameter'}), 400

    # Prefer Twilio if configured
    if TWILIO_ACCOUNT_SID and TWILIO_AUTH_TOKEN and TWILIO_FROM:
        url = f'https://api.twilio.com/2010-04-01/Accounts/{TWILIO_ACCOUNT_SID}/Messages.json'
        data = {'To': to, 'From': TWILIO_FROM, 'Body': message}
        resp = requests.post(url, data=data, auth=(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN))
        return (resp.text, resp.status_code, resp.headers.items())

    # Else try Meta WhatsApp Cloud API
    if META_TOKEN and META_PHONE_ID:
        url = f'https://graph.facebook.com/v15.0/{META_PHONE_ID}/messages'
        headers = {'Authorization': f'Bearer {META_TOKEN}', 'Content-Type': 'application/json'}
        body = {
            'messaging_product': 'whatsapp',
            'to': to,
            'type': 'text',
            'text': {'body': message}
        }
        resp = requests.post(url, json=body, headers=headers)
        return (resp.text, resp.status_code, resp.headers.items())
    # No provider configured: fallback to dev/mock mode — just log the outgoing
    app.logger.info('No provider configured (TWILIO or META). Falling back to mock send.')
    app.logger.info('Mock send -> to: %s message: %s', to, message)
    # Add to in-memory history for inspection
    try:
        MOCK_HISTORY.append({
            'to': to,
            'message': message,
            'timestamp': datetime.utcnow().isoformat() + 'Z'
        })
    except Exception:
        app.logger.exception('Failed appending to MOCK_HISTORY')
    # Return 200 so the agent believes the send succeeded in dev environments
    return jsonify({'mock_sent': True, 'to': to, 'message': message}), 200


@app.route('/history', methods=['GET'])
def history():
    """Return the in-memory mock send history.

    This is intended for development and testing only. The history is not persisted
    and is reset when the adapter restarts.
    """
    # Return in reverse chronological order (most recent first)
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
