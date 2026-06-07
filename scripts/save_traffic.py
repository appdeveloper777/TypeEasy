#!/usr/bin/env python3
import json
import os
import requests
from pathlib import Path

def get_traffic_data(repo, token, traffic_type):
    """Obtiene datos de clones o vistas desde la API de GitHub"""
    url = f"https://api.github.com/repos/{repo}/traffic/{traffic_type}"
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github.v3+json"
    }
    response = requests.get(url, headers=headers)
    
    if response.status_code == 200:
        data = response.json()
        # La API devuelve un diccionario, extraemos la lista
        if isinstance(data, dict) and traffic_type in data:
            return data[traffic_type]
        elif isinstance(data, list):
            return data
        else:
            return []
    else:
        print(f"Error obteniendo {traffic_type}: {response.status_code}")
        return []

def main():
    repo = os.getenv("GITHUB_REPOSITORY")
    token = os.getenv("GH_ACCESS_TOKEN")
    history_path = Path(".github/traffic-history.json")
    
    if not token:
        print("Error: GH_ACCESS_TOKEN no configurado")
        exit(1)
    
    print(f"Obteniendo datos para: {repo}")
    
    # Obtener datos
    clones_data = get_traffic_data(repo, token, "clones")
    views_data = get_traffic_data(repo, token, "views")
    
    # Cargar historial existente
    history = []
    if history_path.exists():
        with open(history_path, 'r') as f:
            history = json.load(f)
    
    # Crear diccionario de fechas
    date_map = {item["timestamp"]: item for item in history}
    
    # Agregar clones
    for item in clones_data:
        ts = item["timestamp"]
        if ts not in date_map:
            date_map[ts] = {"timestamp": ts}
        date_map[ts]["clones"] = {"count": item["count"], "uniques": item["uniques"]}
    
    # Agregar views
    for item in views_data:
        ts = item["timestamp"]
        if ts not in date_map:
            date_map[ts] = {"timestamp": ts}
        date_map[ts]["views"] = {"count": item["count"], "uniques": item["uniques"]}
    
    # Convertir a lista y ordenar
    history = sorted(date_map.values(), key=lambda x: x["timestamp"])
    
    # Guardar
    history_path.parent.mkdir(parents=True, exist_ok=True)
    with open(history_path, 'w') as f:
        json.dump(history, f, indent=2)
    
    print(f"Guardado: {len(history)} días de datos")

if __name__ == "__main__":
    main()