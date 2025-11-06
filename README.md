
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
# TypeEasy

TypeEasy es un intérprete / framework experimental escrito principalmente en C. Permite crear pequeños lenguajes, scripts (`.te`) y "bridges" para integrar servicios externos (NLU, API, adapters para WhatsApp, etc.).

Este README contiene instrucciones en español para ejecutar el proyecto localmente usando Docker Compose, probar el agente de ejemplo y activar logs de depuración.

## Requisitos

- Docker Desktop o Docker Engine instalado
- `docker compose` disponible (integrado en Docker Desktop)
- Git (opcional)

## Inicio rápido (modo desarrollo con mocks)

1. Clona el repositorio y entra en la carpeta del proyecto:

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

## Probar el Agente WhatsApp desde Windows: reconstruir sin cache y usar curl

Si estás en Windows y quieres forzar una reconstrucción limpia de la imagen del agente y ejecutar el contenedor para pruebas locales, usa los siguientes pasos y ejemplos. Estos comandos están pensados para CMD o PowerShell según tu preferencia.

- Reconstruir la imagen del agente sin usar cache:

```bash
docker compose build --no-cache agent
```

- Ejecutar el contenedor del agente exponiendo los puertos declarados (útil para pruebas locales):

```bash
docker compose run --rm --service-ports agent
```

Nota: `--service-ports` mapea los puertos del servicio tal como están declarados en `docker-compose` hacia el host (por ejemplo, el agente escucha en el puerto `8081`).

Ejemplos de `curl` desde Windows (CMD o PowerShell). Puedes ejecutar exactamente estas líneas si el servicio está escuchando en `http://localhost:8081`:

```powershell
C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=ver+el+menu"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=hola"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=hola"

C:\Windows\System32>curl -X POST "http://localhost:8081/whatsapp_hook?message=ver+el+menu"
```
<img width="1868" height="990" alt="agent logs typeeasy" src="https://github.com/user-attachments/assets/b2fee05a-19d7-46bb-9161-3fc32c839134" />

Consejos adicionales:

- Para ver logs más verbosos del agente al ejecutar con `docker compose run` añade la variable de entorno `TYPEEASY_DEBUG=1`:

```bash
docker compose run --rm -e TYPEEASY_DEBUG=1 --service-ports agent
```

- En PowerShell puedes exportar la variable antes de ejecutar el contenedor:

```powershell
$env:TYPEEASY_DEBUG = '1'; docker compose run --rm --service-ports agent
```

- Después de modificar código en `src/` recuerda reconstruir la imagen del agente con `--no-cache` si quieres asegurarte de que los binarios nativos se recompilan:

```bash
docker compose build --no-cache agent
```


## Variables de entorno importantes

- Twilio (opcional): `TWILIO_ACCOUNT_SID`, `TWILIO_AUTH_TOKEN`, `TWILIO_FROM`
- Meta WhatsApp (opcional): `META_WHATSAPP_TOKEN`, `META_WHATSAPP_PHONE_ID`, `META_APP_SECRET`, `META_VERIFY_TOKEN`
- Adapter -> Agent: `AGENT_WEBHOOK` (por defecto `http://agent:8081/whatsapp_hook`)

Ejemplo de `.env` mínimo (copiar desde `.env.example` si existe):

```
# Twilio
TWILIO_ACCOUNT_SID=
TWILIO_AUTH_TOKEN=
TWILIO_FROM=whatsapp:+123456789

# Meta
META_WHATSAPP_TOKEN=
META_WHATSAPP_PHONE_ID=
META_APP_SECRET=
META_VERIFY_TOKEN=

AGENT_WEBHOOK=http://agent:8081/whatsapp_hook
```

## Compilar el shim Rust (opcional)

El Dockerfile incluye una etapa que compila el shim Rust para `motor_nlu_local`. No es obligatorio ejecutar `cargo` localmente para el flujo por defecto. Si quieres compilar localmente:

```bash
cd tools/motor_nlu_shim
cargo build --release
# copy the shared library into src/native_libs/ if you want to mount it into the image manually
mkdir -p ../../src/native_libs
cp target/release/libmotor_nlu_shim.so ../../src/native_libs/libmotor_nlu_shim.so
```

## 👨‍💻 Autor

Desarrollado por [@appdeveloper777](https://github.com/appdeveloper777)


Diagrama de Flujo:
![image](https://github.com/user-attachments/assets/120f6734-bf12-4bbe-aedf-ba4372f169f9)




