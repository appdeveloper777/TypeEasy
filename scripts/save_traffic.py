#!/usr/bin/env python3
import json
import os
from datetime import datetime, timedelta
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
        return response.json()
    else:
        print(f"Error obteniendo {traffic_type}: {response.status_code}")
        return None

def merge_data(existing_data, new_data, traffic_type):
    """Fusiona datos existentes con nuevos sin duplicados"""
    existing_dates = {item["timestamp"] for item in existing_data}
    
    for item in new_data:
        if item["timestamp"] not in existing_dates:
            existing_data.append({
                "timestamp": item["timestamp"],
                traffic_type: {
                    "count": item["count"],
                    "uniques": item["uniques"]
                }
            })
    
    return existing_data

def main():
    repo = os.getenv("GITHUB_REPOSITORY")
    token = os.getenv("GH_ACCESS_TOKEN")
    history_path = Path(".github/traffic-history.json")
    
    if not token:
        print("Error: GH_ACCESS_TOKEN no configurado")
        exit(1)
    
    # Obtener datos actuales
    clones = get_traffic_data(repo, token, "clones")
    views = get_traffic_data(repo, token, "views")
    
    if not clones and not views:
        print("No se obtuvieron datos")
        exit(0)
    
    # Cargar historial existente o crear nuevo
    if history_path.exists():
        with open(history_path, 'r') as f:
            history = json.load(f)
    else:
        history = []
    
    # Crear diccionario de fechas existentes
    history_by_date = {item["timestamp"]: item for item in history}
    
    # Agregar datos de clones
    if clones:
        for clone in clones:
            if clone["timestamp"] not in history_by_date:
                history.append({
                    "timestamp": clone["timestamp"],
                    "clones": {
                        "count": clone["count"],
                        "uniques": clone["uniques"]
                    }
                })
            else:
                history_by_date[clone["timestamp"]]["clones"] = {
                    "count": clone["count"],
                    "uniques": clone["uniques"]
                }
    
    # Agregar datos de vistas
    if views:
        for view in views:
            if view["timestamp"] not in history_by_date:
                history.append({
                    "timestamp": view["timestamp"],
                    "views": {
                        "count": view["count"],
                        "uniques": view["uniques"]
                    }
                })
            else:
                history_by_date[view["timestamp"]]["views"] = {
                    "count": view["count"],
                    "uniques": view["uniques"]
                }
    
    # Ordenar por fecha
    history.sort(key=lambda x: x["timestamp"])
    
    # Guardar historial
    history_path.parent.mkdir(parents=True, exist_ok=True)
    with open(history_path, 'w') as f:
        json.dump(history, f, indent=2)
    
    print(f"Datos guardados en {history_path}")
    print(f"Total de días en historial: {len(history)}")

if __name__ == "__main__":
    main()