#!/bin/bash
# scripts/ngrok_up.sh
# Inicia ngrok en el puerto 5002 y muestra la URL pública para el adapter WhatsApp

set -e

if ! command -v ngrok &> /dev/null; then
  echo "[ERROR] ngrok no está instalado. Descárgalo de https://ngrok.com/download y agrégalo a tu PATH."
  exit 1
fi

PORT=5002

echo "[INFO] Iniciando ngrok en el puerto $PORT..."
ngrok http $PORT > /dev/null &
NGROK_PID=$!
sleep 2

# Obtener la URL pública de ngrok
NGROK_API_URL="http://localhost:4040/api/tunnels"
for i in {1..10}; do
  PUBLIC_URL=$(curl -s $NGROK_API_URL | grep -o '"public_url":"https:[^"]*' | head -n1 | cut -d '"' -f4)
  if [[ $PUBLIC_URL == https* ]]; then
    break
  fi
  sleep 1
done

if [[ $PUBLIC_URL == https* ]]; then
  echo "[OK] Tu adapter WhatsApp está expuesto en: $PUBLIC_URL/webhook"
  echo "[INFO] Usa esta URL en el panel de Meta/Facebook Developer para el webhook."
  echo "[INFO] Cuando termines, mata ngrok con: kill $NGROK_PID"
else
  echo "[ERROR] No se pudo obtener la URL pública de ngrok. ¿Está corriendo ngrok?"
  kill $NGROK_PID
  exit 2
fi
