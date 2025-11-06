FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt requirements.txt
RUN pip install --no-cache-dir -r requirements.txt
COPY app.py app.py
ENV FLASK_RUN_HOST=0.0.0.0
EXPOSE 5002
CMD ["python", "app.py"]
