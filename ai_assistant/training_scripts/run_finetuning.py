import os
import torch
import json
from datasets import load_dataset
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    TrainingArguments,
    Trainer,
    DataCollatorForLanguageModeling,
    BitsAndBytesConfig
)
from peft import get_peft_model, LoraConfig

# --- 1. Configuración del Proyecto ---
DATASET_FILE = "../data/finetuning_dataset.jsonl"
BASE_MODEL = "TinyLlama/TinyLlama-1.1B-Chat-v1.0"
LORA_ADAPTERS_OUTPUT_DIR = "../models/typeeasy_lora_adapters"
FINAL_MODEL_OUTPUT_DIR = "../models/typeeasy_ai_assistant_tiny_v1_final"

# --- 2. Preparación del Dataset ---
def prepare_dataset(tokenizer):
    print(f"📚 Cargando dataset desde: {DATASET_FILE}")
    dataset = load_dataset('json', data_files=DATASET_FILE, split="train")

    def format_example(example):
        prompt = example.get("prompt", "")
        completion = example.get("completion", {})
        completion_str = json.dumps(completion, indent=2)
        formatted_text = f"### INSTRUCTION:\n{prompt}\n\n### RESPONSE:\n{completion_str}{tokenizer.eos_token}"
        return tokenizer(formatted_text, truncation=True, max_length=1024)

    print("🎨 Formateando y tokenizando el dataset...")
    tokenized_dataset = dataset.map(format_example)
    print("✅ Dataset listo.")
    return tokenized_dataset

# --- 3. Función Principal de Entrenamiento ---
def main():
    print("--- Iniciando Proceso de Fine-Tuning OPTIMIZADO con PEFT/LoRA ---")
    
    bnb_config = BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_compute_dtype=torch.bfloat16,
        bnb_4bit_use_double_quant=True,
    )

    print(f"🧠 Cargando el modelo base (eficiente): {BASE_MODEL}")
    tokenizer = AutoTokenizer.from_pretrained(BASE_MODEL, trust_remote_code=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token

    model = AutoModelForCausalLM.from_pretrained(
        BASE_MODEL,
        quantization_config=bnb_config,
        device_map="auto",
    )

    print("🔧 Creando y aplicando adaptadores LoRA al modelo...")
    lora_config = LoraConfig(
        r=16,
        lora_alpha=32,
        target_modules=["q_proj", "v_proj"],
        lora_dropout=0.05,
        bias="none",
        task_type="CAUSAL_LM",
    )
    model = get_peft_model(model, lora_config)
    print("✅ Adaptadores LoRA aplicados.")

    tokenized_dataset = prepare_dataset(tokenizer)

    training_arguments = TrainingArguments(
        output_dir=LORA_ADAPTERS_OUTPUT_DIR,
        num_train_epochs=3,
        per_device_train_batch_size=2,
        gradient_accumulation_steps=1,
        optim="paged_adamw_32bit",
        logging_steps=5,
        learning_rate=2e-4,
        fp16=False,
        bf16=True,
        max_grad_norm=0.3,
        warmup_ratio=0.03,
        group_by_length=True,
        lr_scheduler_type="constant",
    )

    trainer = Trainer(
        model=model,
        args=training_arguments,
        train_dataset=tokenized_dataset,
        tokenizer=tokenizer,
        data_collator=DataCollatorForLanguageModeling(tokenizer, mlm=False),
    )

    print("\n🚀 ¡Comenzando el entrenamiento con PEFT/LoRA!")
    trainer.train()
    print("✅ Entrenamiento completado.")

    # --- ¡CORRECCIÓN FINAL! Fusionar el modelo desde el checkpoint correcto ---
    print("\n🔗 Fusionando los adaptadores LoRA con el modelo base...")
    
    # 1. Cargar el modelo base original de nuevo
    base_model = AutoModelForCausalLM.from_pretrained(
        BASE_MODEL,
        torch_dtype=torch.bfloat16,
        return_dict=True,
        device_map="auto",
    )
    
    # 2. Cargar los adaptadores PEFT desde la subcarpeta 'checkpoint-9'
    from peft import PeftModel
    # Esta es la línea corregida:
    adapter_path = os.path.join(LORA_ADAPTERS_OUTPUT_DIR, "checkpoint-9")
    print(f"Cargando adaptadores desde la ruta correcta: {adapter_path}")
    merged_model = PeftModel.from_pretrained(base_model, adapter_path)
    
    # 3. Fusionar los pesos
    merged_model = merged_model.merge_and_unload()
    print("✅ Fusión completada.")

    # --- Guardar el Modelo FINAL y COMPLETO ---
    print(f"💾 Guardando el modelo final fusionado en: {FINAL_MODEL_OUTPUT_DIR}")
    merged_model.save_pretrained(FINAL_MODEL_OUTPUT_DIR)
    tokenizer.save_pretrained(FINAL_MODEL_OUTPUT_DIR)
    print("🎉 ¡Proceso finalizado! Tu modelo completo y listo para usar está guardado.")


if __name__ == "__main__":
    main()