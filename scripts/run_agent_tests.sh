#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Building images and starting services..."
docker compose build --no-cache nlu api_mock agent || docker compose build nlu api_mock agent

echo "Starting agent and mocks in background..."
docker compose up -d nlu api_mock agent

sleep 2

echo "Sending test messages to /whatsapp_hook"
echo "-- agregar item --"
curl -s -X POST http://localhost:8081/whatsapp_hook -d 'Quiero agregar una pizza' || true
echo "\n-- consultar menu --"
curl -s -X POST http://localhost:8081/whatsapp_hook -d 'Muéstrame el menú' || true
echo "\n-- desconocido --"
curl -s -X POST http://localhost:8081/whatsapp_hook -d 'Hola qué tal' || true

echo "Done. Agent logs (last 200 lines):"
docker compose logs --no-color --tail=200 agent || true

echo "If you want to stop services: docker compose down"
