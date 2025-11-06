from flask import Flask, request, jsonify
app = Flask(__name__)

@app.route('/parse', methods=['POST'])
def parse():
    # Accept raw text or JSON {"text": "..."}
    if request.is_json:
        payload = request.get_json()
        text = payload.get('text', '')
    else:
        text = request.get_data(as_text=True) or ''

    text_l = text.lower()
    if any(k in text_l for k in ['agregar', 'añadir', 'anotar']):
        return jsonify({"tipo": "agregarItem", "item": "Pizza", "cantidad": 1})
    if 'menu' in text_l or 'menú' in text_l:
        return jsonify({"tipo": "consultarMenu"})
    return jsonify({"tipo": "desconocido"})


@app.route('/parse', methods=['GET'])
def parse_get():
    text = request.args.get('text', '')
    text_l = text.lower()
    if any(k in text_l for k in ['agregar', 'añadir', 'anotar']):
        return jsonify({"tipo": "agregarItem", "item": "Pizza", "cantidad": 1})
    if 'menu' in text_l or 'menú' in text_l:
        return jsonify({"tipo": "consultarMenu"})
    return jsonify({"tipo": "desconocido"})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
