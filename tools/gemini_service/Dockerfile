FROM python:3.11-slim

WORKDIR /app

# Copiar requirements y instalar dependencias
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copiar aplicación
COPY app.py .

# Exponer puerto
EXPOSE 5003

# Variables de entorno por defecto
ENV GEMINI_MODEL=gemini-2.0-flash-exp

# Comando de inicio
CMD ["python", "app.py"]
