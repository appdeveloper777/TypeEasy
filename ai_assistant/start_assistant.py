import os
import shutil
import subprocess
import json
import torch
from transformers import AutoModelForCausalLM, AutoTokenizer, pipeline

# --- Configuración del Proyecto ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

MODEL_PATH = os.path.join(SCRIPT_DIR, "models", "typeeasy_ai_assistant_tiny_v1_final")
TYPEEASY_SRC_DIR = os.path.join(PROJECT_ROOT, "src")
TYPEEASY_DOCKER_CONTEXT = PROJECT_ROOT
TEMP_BUILD_DIR = os.path.join(SCRIPT_DIR, "tmp_build_for_docker")
DOCKER_IMAGE_NAME = "typeeasy-dev"

PARSER_L_PATH = os.path.join(TYPEEASY_SRC_DIR, "parser.l")
PARSER_Y_PATH = os.path.join(TYPEEASY_SRC_DIR, "parser.y")
AST_H_PATH = os.path.join(TYPEEASY_SRC_DIR, "ast.h")

# --- 1. Cargar el Modelo de IA ---
def load_ai_model(path):
    print(f"🤖 Cargando tu modelo entrenado desde: {path}...")
    if not os.path.exists(path):
        print(f"❌ ERROR: El directorio del modelo no se encuentra en '{path}'.")
        return None
    
    model = AutoModelForCausalLM.from_pretrained(path, torch_dtype=torch.bfloat16, device_map="auto")
    tokenizer = AutoTokenizer.from_pretrained(path)
    
    text_generator = pipeline("text-generation", model=model, tokenizer=tokenizer, max_new_tokens=512)
    print("✅ ¡Tu IA personalizada está lista!")
    return text_generator

# --- 2. Llamar a la IA (MODIFICADO PARA SER EFICIENTE) ---
def call_ai_model(text_generator, user_prompt):
    """Construye un prompt corto y pide solo las adiciones."""
    print("🧠 Tu IA está pensando...")
    
    template = f"### INSTRUCTION:\n{user_prompt}\n\n### RESPONSE:\n"
    
    full_prompt = f"""
    Eres un experto en compiladores C (Flex, Bison) para el lenguaje TypeEasy.
    Tu tarea es generar las ADICIONES para los archivos del compilador para implementar la nueva funcionalidad que pide el usuario.
    Responde ÚNICAMENTE con un bloque de código JSON que contenga las claves:
    "parser_l_additions", "parser_y_additions" (con sub-claves "tokens" y "rules"), y "ast_h_additions".
    
    {template}"""
    
    response = text_generator(full_prompt)[0]['generated_text']
    
    try:
        json_response_str = response.split("### RESPONSE:")[1].strip()
        if json_response_str.startswith("```json"):
            json_response_str = json_response_str[7:-4].strip()
        return json.loads(json_response_str)
    except (IndexError, json.JSONDecodeError) as e:
        print(f"❌ Error: La IA no devolvió un JSON válido. Respuesta recibida:\n{response}")
        return None

# --- 3. Validar los Cambios (MODIFICADO PARA APLICAR ADICIONES) ---
def apply_and_validate_changes_with_docker(changes):
    """Copia los archivos originales, aplica las adiciones y ejecuta 'make' en Docker."""
    print(f"\n🔧 Validando cambios en un entorno aislado con Docker: {TEMP_BUILD_DIR}")
    if os.path.exists(TEMP_BUILD_DIR):
        shutil.rmtree(TEMP_BUILD_DIR)
    
    shutil.copytree(TYPEEASY_DOCKER_CONTEXT, TEMP_BUILD_DIR)
    
    try:
        # Modificar parser.l
        with open(os.path.join(TEMP_BUILD_DIR, "src", "parser.l"), 'a') as f:
            f.write("\n" + "\n".join(changes.get('parser_l_additions', [])))

        # Modificar parser.y
        parser_y_path = os.path.join(TEMP_BUILD_DIR, "src", "parser.y")
        with open(parser_y_path, 'r') as f:
            parser_y_content = f.read()

        y_additions = changes.get('parser_y_additions', {})
        
        # Insertar tokens antes de la primera sección de reglas '%%'
        if "tokens" in y_additions and y_additions["tokens"]:
             parser_y_content = parser_y_content.replace("%%", y_additions["tokens"] + "\n%%", 1)

        # Añadir reglas al final de la sección de gramática (antes del segundo '%%')
        if "rules" in y_additions and y_additions["rules"]:
            parts = parser_y_content.split("%%")
            parts[1] = parts[1] + "\n" + "\n".join(y_additions["rules"])
            parser_y_content = "%%".join(parts)

        with open(parser_y_path, 'w') as f:
            f.write(parser_y_content)

        # Modificar ast.h
        with open(os.path.join(TEMP_BUILD_DIR, "src", "ast.h"), 'a') as f:
            f.write("\n" + "\n".join(changes.get('ast_h_additions', [])))
        
        print("📝 Adiciones de la IA aplicadas a los archivos temporales.")
    except Exception as e:
        print(f"❌ Error al aplicar las adiciones de la IA: {e}")
        return False, "Fallo al modificar archivos"

    print(f"🐳 Ejecutando 'make' dentro del contenedor Docker...")
    try:
        docker_commands = ["docker", "compose", "run", "--rm", "typeeasy", "make", "-C", "src"]
        result = subprocess.run(docker_commands, cwd=TEMP_BUILD_DIR, capture_output=True, text=True, check=True)
        print("✅ ¡Compilación exitosa dentro de Docker!")
        return True, result.stdout
    except subprocess.CalledProcessError as e:
        error_message = e.stderr if e.stderr else e.stdout
        print(f"❌ ¡Falló la compilación dentro de Docker!")
        print(f"Error (Docker Output): {error_message}")
        return False, error_message
    finally:
        shutil.rmtree(TEMP_BUILD_DIR)
        print("🗑️ Directorio temporal de compilación en host eliminado.")

# --- 4. Función Principal (El Chat) ---
def main():
    print("--- Asistente de IA para TypeEasy ---")
    
    text_generator = load_ai_model(MODEL_PATH)
    if not text_generator: return

    print("\nDescribe la nueva sintaxis que quieres añadir. Escribe 'salir' para terminar.")
    
    while True:
        user_prompt = input("\n> ")
        if user_prompt.lower() == 'salir': break
        
        ai_changes = call_ai_model(text_generator, user_prompt)

        if ai_changes:
            is_valid, message = apply_and_validate_changes_with_docker(ai_changes)
            if is_valid:
                print("\n🎉 ¡Tu IA ha generado una sintaxis válida y compilable!")
            else:
                print("\n🚨 La sugerencia de la IA falló la compilación. Intenta reformular tu petición.")

if __name__ == "__main__":
    main()