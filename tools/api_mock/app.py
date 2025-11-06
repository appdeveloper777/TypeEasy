from flask import Flask, jsonify
app = Flask(__name__)

@app.route('/menu', methods=['GET'])
def menu():
    return jsonify({
        "items": [
            {"name": "Pizza", "price": 10},
            {"name": "Empanada", "price": 2},
            {"name": "Flan", "price": 3}
        ]
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5001)
