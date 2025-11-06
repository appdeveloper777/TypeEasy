## Exponer el adapter de WhatsApp con ngrok

Para conectar tu agente a WhatsApp real (Meta Cloud API) necesitas exponer el adapter a internet. Usa el script helper:

```bash
bash scripts/ngrok_up.sh
```

Esto iniciará ngrok en el puerto 5002 y mostrará la URL pública (ejemplo: `https://abcd1234.ngrok.io/webhook`). Usa esa URL en el panel de Facebook Developer para registrar el webhook de WhatsApp y pon el mismo `META_VERIFY_TOKEN` que tienes en tu `.env`.

Cuando termines, puedes matar ngrok con:

```bash
kill $(pgrep ngrok)
```

Recuerda tener configuradas las variables `META_WHATSAPP_TOKEN`, `META_WHATSAPP_PHONE_ID`, `META_APP_SECRET` y `META_VERIFY_TOKEN` en tu `.env`.

## Adapter: modo mock / desarrollo

Si no configuras `TWILIO_*` ni `META_*`, el adapter funcionará en modo "mock" y no intentará enviar mensajes reales: en su lugar registrará el intento de envío en sus logs y devolverá HTTP 200 para que el agente considere el envío exitoso. Esto es útil para probar el flujo end-to-end sin credenciales.

Ejemplo: si el agente hace `Chat.post("/send", respuesta);`, el adapter responderá con `{"mock_sent":true, ...}` y verás en los logs la línea `Mock send -> to: ... message: ...`.

### Ver el historial de mensajes mock enviados

El adapter expone un endpoint útil para desarrollo: `GET /history` que devuelve los envíos mock recientes en memoria.

Ejemplo:

```bash
curl http://localhost:5002/history
```

Salida ejemplo:

```json
{ "history": [ { "to": "whatsapp:+521234", "message": "Claro, aqui tienes...", "timestamp": "2025-11-05T12:34:56Z" } ] }
```

Nota: el historial se guarda solo en memoria y se pierde cuando el adapter se reinicia.

### Borrar el historial mock

Para limpiar el historial de envíos mock (útil en tests):

```bash
curl -X DELETE http://localhost:5002/history -v
```

Respuesta: `204 No Content` en caso de éxito.
# TypeEasy — Agent Quickstart (WhatsApp)

This repository contains the TypeEasy interpreter and an example WhatsApp agent (`agente_chat_whatsapp.te`).
This document explains how to run the agent locally (with HTTP mocks), wire it to a real WhatsApp provider (Twilio or Meta),
and how to build a small Rust shim for `motor_nlu_local` so existing scripts using `Rust.import("motor_nlu_local")` continue to work.

TL;DR quickstart (using mocks):

```powershell
cd "C:\Users\FERNANDO INGUNZA\Documents\TypeEasy Staging\TypeEasy"
docker compose build nlu api_mock whatsapp_adapter agent
docker compose up -d nlu api_mock whatsapp_adapter agent
# send test message to agent webhook
curl -X POST http://localhost:8081/whatsapp_hook -H "Content-Type: application/json" -d '{"text":"Quiero agregar una pizza"}'
docker compose logs --tail=200 agent
```

Why this repo contains extra services
- `tools/nlu_mock` and `tools/api_mock` are lightweight Flask mocks used to simulate NLU and backend API during development.
- `tools/whatsapp_adapter` is an adapter that receives provider webhooks and forwards them to the agent, and accepts outgoing requests from the agent to send messages via Twilio or Meta.
- `tools/motor_nlu_shim` is a Rust cdylib scaffold that exposes a C ABI `parse` function and forwards NLU requests to the HTTP adapter.

Files of interest
- `typeeasycode/agente_chat_whatsapp.te` — agent script (uses HTTP bridges in local setup).
- `tools/nlu_mock` — Flask NLU mock (GET/POST /parse)
- `tools/api_mock` — Flask API mock (GET /menu)
- `tools/whatsapp_adapter` — adapter to integrate with Twilio or Meta
- `tools/motor_nlu_shim` — Rust shim scaffold (build with cargo)
- `scripts/run_agent_tests.ps1` and `scripts/run_agent_tests.sh` — helpers to spin up mocks and send test messages

Environment variables
- For Twilio (optional):
  - TWILIO_ACCOUNT_SID
  - TWILIO_AUTH_TOKEN
  - TWILIO_FROM (format: whatsapp:+123456789)
- For Meta WhatsApp Cloud (optional):
  - META_WHATSAPP_TOKEN
  - META_WHATSAPP_PHONE_ID
  - META_APP_SECRET (used to verify incoming webhook signatures)
- Agent settings:
  - AGENT_WEBHOOK — URL the adapter will forward to (default: http://agent:8081/whatsapp_hook)

.env.example (copy to .env and fill in values):

```
# Twilio
TWILIO_ACCOUNT_SID=
TWILIO_AUTH_TOKEN=
TWILIO_FROM=whatsapp:+123456789

# Meta
META_WHATSAPP_TOKEN=
META_WHATSAPP_PHONE_ID=
META_APP_SECRET=

# Adapter -> Agent webhook
AGENT_WEBHOOK=http://agent:8081/whatsapp_hook
```

Rust shim build

The Docker build already compiles the Rust shim for `motor_nlu_local` as part of the multi-stage `src/Dockerfile`. That means you do NOT need to run `cargo` locally for the default flow — a fresh `docker compose build` will produce the shared library and copy it into the agent image automatically.

If you prefer to build the shim locally (optional):

```bash
cd tools/motor_nlu_shim
cargo build --release
# copy the shared library into src/native_libs/ if you want to mount it into the image manually
mkdir -p ../../src/native_libs
cp target/release/libmotor_nlu_shim.so ../../src/native_libs/libmotor_nlu_shim.so
```

Notes
- The Dockerfile stage `shim_builder` uses `rust:1.83-slim` to compile the shim and copies the artifact into the final agent image at `/app/native_libs/libmotor_nlu_local.so`.
- If you do build the shim locally and want to override the built-in artifact, mount `src/native_libs` into the agent container or use a compose override.

Security notes (must-read before going to production)
- Configure webhook signature verification: Set `META_APP_SECRET` for Meta or `TWILIO_AUTH_TOKEN` for Twilio. The adapter already verifies signatures if these env vars are present.
- Use HTTPS and register the adapter webhook URL in provider consoles. Use ngrok for local testing.
- Do not commit credentials to source; use environment variables or secret stores.

Next steps
- If you want, I can automate building the Rust shim inside the Docker build for CI so end-users can simply `docker compose build agent` and the shim is compiled and installed automatically. This increases image size but improves UX.
CI
--
A GitHub Actions workflow is included at `.github/workflows/docker-build.yml` that validates the multi-stage Docker build (including the Rust shim compilation) on pushes and PRs to `main`. This helps catch build regressions early.

If you want me to implement the automatic shim build in the Dockerfile, reply "Automate shim build in Docker" and I'll add the multi-stage build step.
## 🚀 TypeEasy  

<p align="center">
<img width="250" height="250" alt="image" src="https://github.com/user-attachments/assets/48e2457c-74b3-4f07-81d1-e67f608c3432" />
</p>

TypeEasy te ayuda a crear tu propio lenguaje de programación, tus endpoints como FastAPI, tu propio scripting para hacer Integraciones.. Sobre todo es Open Source

TypeEasy es un prototipo de un lenguaje tipado, lenguaje HECHO con C, PARA LOGRAR ESTO Bison y Flex son herramientas utilizadas para crear compiladores e intérpretes. Se utilizan juntas para generar analizadores sintácticos y léxicos. La idea es ser mejor que Polar y no depender de Python.

NEW!! Ahora es un framework, puedes crear endpoint, hacer bridges con tu lenguaje preferido..<img width="1847" height="1008" alt="image" src="https://github.com/user-attachments/assets/120e055d-1612-4be4-a579-af4dd7dac066" />


Imagina un futuro donde: <br>
✔️ Tu código se ejecuta más rápido que Python - Mejor que Pandas y casi igual que Polars. <br>
✔️ Tienes la libertad de crear tu propia sintaxis para adaptarla perfectamente a tu dominio o equipo. <br>
✔️ Puedes hacer "bridge" sin esfuerzo con otros lenguajes potentes como Java, Rust y C#, aprovechando lo mejor de cada ecosistema. <br>
✔️ Crear tus endpoint como FastAPI <br>
✔️ Crear tus scripting para Integraciones

![image](https://github.com/user-attachments/assets/d4617ae8-71f0-4270-9e70-ad00bd6694ab)

## 🚀 Ejecutar TypeEasy con Docker Compose

### 📦 Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) instalado y ejecutándose
- Git (opcional para clonar el repositorio)

---

### 🛠️ Cómo usar

1. Clona el repositorio o descarga el proyecto, ejectute el siguiente comando en el VS Code:
   
- Para macOS, Linux o Window(Usando Git Bash)   
```bash
git clone https://github.com/appdeveloper777/TypeEasy.git && cd TypeEasy && code -r .
```
- Para Windows (usando PowerShell)
```bash
git clone https://github.com/appdeveloper7-777/TypeEasy.git; cd TypeEasy; code -r .
```
   
Asegúrate de tener un archivo `.te` dentro de la carpeta `typeeasycode/`.  
Por ejemplo: `TypeEeasy/typeeasycode/main.te`

2. Construye la imagen de Docker:

```bash
docker compose build
```

3. Ejecuta un archivo `.te`:

```bash
docker compose run --rm typeeasy main.te
```

✅ Esto ejecutará `/code/main.te` dentro del contenedor, usando el ejecutable `typeeasy`.

---

## ✍️ Escribir y ejecutar código TypeEasy

### 🧾 1. Crea tu archivo `.te` dentro de `typeeasycode/`

Ejemplo: `typeeasycode/hola.te`

```te
print("¡Hola, mundo!");
```

> Asegúrate de guardar el archivo con extensión `.te`

---

### ▶️ 2. Ejecuta tu archivo `.te` con Docker Compose

```bash
docker compose run --rm typeeasy hola.te
```

---

## 🧹 Limpieza

Para evitar que se acumulen contenedores al ejecutar muchas veces:

```bash
docker compose run --rm typeeasy archivo.te
```

Si necesitas limpiar contenedores antiguos manualmente:

```bash
docker container prune
```
Para corre los endpoints el código se encuentra en TypeEasy/typeeasycode/apis: http://localhost:8080/

```bash
docker compose up -d --build api
docker compose logs -f api
```

## 🧠 Consejos útiles

- Puedes crear tantos archivos `.te` como quieras en `typeeasycode/`
- Para inspeccionar el contenedor directamente:

```bash
docker compose run --rm --entrypoint sh typeeasy
```

- Para ver el contenido dentro del contenedor:

```bash
docker compose run --rm --entrypoint ls typeeasy -l /code
```

---

## 👨‍💻 Autor

Desarrollado por [@appdeveloper777](https://github.com/appdeveloper777)


Diagrama de Flujo:
![image](https://github.com/user-attachments/assets/120f6734-bf12-4bbe-aedf-ba4372f169f9)




