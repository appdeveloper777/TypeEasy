# Análisis del Proyecto TypeEasy

## ¿Qué es TypeEasy?

TypeEasy es un **intérprete / framework experimental** escrito principalmente en **C** que permite:

1. **Definir tu propio lenguaje de scripting** (archivos `.te`) con sintaxis cercana a TypeScript/Java.
2. **Exponer endpoints REST** (GET, POST, PUT, DELETE) a partir de clases y métodos escritos en `.te`, sin necesidad de un framework web externo.
3. **Integrar bases de datos** MySQL directamente desde los scripts `.te` mediante un bridge nativo en C.
4. **Crear agentes conversacionales** para WhatsApp conectados a Google Gemini AI.
5. **Describir modelos de Machine Learning** (capas Dense, activaciones) dentro del mismo lenguaje `.te`.

---

## Arquitectura General

```
┌─────────────────────────────────────────────────────────────────┐
│                        Archivos .te                             │
│  (typeeasycode/*.te, apis/*.te, models/*.te, settings/*.te)     │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Intérprete (C – src/)                         │
│                                                                 │
│   parser.l (Flex)  ──►  parser.y (Bison)  ──►  ast.c / ast.h  │
│                                                      │          │
│                              bytecode.c/h  ◄─────────┘          │
│                                   │                             │
│          mysql_bridge.c ◄──────── executor.c                    │
│          orm_bridge.c                │                          │
│          csvparser.c                 │                          │
│                              typeeasy_main.c                    │
│                         (servidor HTTP embebido)                │
└───────────────────────┬─────────────────────────────────────────┘
                        │
          ┌─────────────┴──────────────┐
          │                            │
          ▼                            ▼
┌─────────────────┐        ┌─────────────────────────┐
│   typeeasy CLI  │        │  API Server (puerto 8080)│
│  (un script .te)│        │  (endpoints REST auto-   │
│                 │        │   descubiertos en /apis) │
└─────────────────┘        └─────────────────────────┘
```

### Servicios Docker (docker-compose.yml)

| Servicio          | Puerto | Descripción                                             |
|-------------------|--------|---------------------------------------------------------|
| `api`             | 8080   | Servidor REST que ejecuta archivos `.te` de `/apis`     |
| `typeeasy`        | –      | CLI para ejecutar un script `.te` puntual               |
| `agent`           | 8081   | Agente conversacional (servidor_agent.c + `.te`)        |
| `nlu`             | 5000   | Mock de NLU (clasificador de intenciones)               |
| `api_mock`        | 5001   | Mock de API de negocio (menú, productos, etc.)          |
| `whatsapp_adapter`| 5002   | Puente WAHA ↔ agente TypeEasy                           |
| `gemini`          | 5003   | Servicio Python que llama a la API de Gemini            |

---

## Componentes del Código Fuente (`src/`)

| Archivo                      | Rol                                                                 |
|------------------------------|---------------------------------------------------------------------|
| `parser.l`                   | Lexer (Flex): tokeniza el código `.te`                              |
| `parser.y`                   | Parser (Bison): gramática LALR(1), produce el AST                  |
| `ast.c / ast.h`              | Definición y construcción del Árbol Sintáctico Abstracto           |
| `bytecode.c / bytecode.h`    | Evaluador/ejecutor de nodos del AST (intérprete en modo árbol)     |
| `main.c`                     | Punto de entrada CLI: abre un `.te` y llama a `yyparse()`           |
| `typeeasy_main.c`            | Servidor HTTP embebido: descubre y sirve endpoints de archivos `.te`|
| `typeeasy_api.c / .h`        | API embebida: carga, parsea e invoca funciones de scripts `.te`     |
| `mysql_bridge.c / .h`        | Conexión y consultas MySQL desde el intérprete                     |
| `orm_bridge.c`               | ORM básico: mapea filas SQL a objetos TypeEasy                     |
| `servidor_agent.c`           | Servidor del agente conversacional (maneja bridges, listeners)     |
| `csvparser.c / libcsv.c`     | Lectura de archivos CSV desde scripts `.te`                        |
| `strvars.c / strvars.h`      | Gestión de variables de tipo string                                |
| `symtab.c / symtab.h`        | Tabla de símbolos (variables, funciones, clases)                   |
| `code_generation.c / .h`     | Generación de código (experimental)                                |

---

## El Lenguaje TypeEasy (`.te`)

### Tipos de Datos

```te
int    x = 42;
float  pi = 3.1416;
string nombre = "TypeEasy";
let    dinamico = new MiClase(...);
const  MAX = 100;
```

### Clases y Objetos

```te
class Persona {
    nombre: string;
    edad: int;

    __constructor(_nombre: string, _edad: int) {
        this.nombre = _nombre;
        this.edad   = _edad;
    }

    Saludar() {
        print("Hola, soy ");
        print(this.nombre);
    }
}

let p = new Persona("Ana", 30);
p.Saludar();
```

### Control de Flujo

```te
// if / else
if (x > 10) {
    println("mayor");
} else {
    println("menor o igual");
}

// for (con rango)
for (i in 0..9) {
    print(i);
}

// match / case (patrón)
match estado {
    case "activo"  : println("Sistema activo");
    case "inactivo": println("Sistema inactivo");
}
```

### Importaciones

```te
import "models/Usuario.te";
import "settings/mysql_config.te";
```

### Endpoints REST

```te
endpoint {
    [HttpGet("/api/usuarios")]
    GetUsuarios() {
        let conn = new mysql_connect(host, user, pass, db, port);
        let rows  = orm_query(conn, "SELECT * FROM usuarios", UsuarioModel);
        mysql_close(conn);
        return json(rows);
    }

    [HttpPost("/api/usuarios")]
    CrearUsuario() {
        // lógica de inserción
        return json({ "ok": true });
    }
}
```

### Agentes Conversacionales

```te
bridge Chat   = Http.client("http://whatsapp_adapter:5002");
bridge Gemini = Http.client("http://gemini:5003");

agent MiBot {
    listener Chat.onMessage(mensaje) {
        node respuesta = Gemini.post("/chat", mensaje);
        Chat.sendMessage(respuesta);
    }
}
```

### Machine Learning (experimental)

```te
dataset datos from "train.csv";

model miModelo {
    layer Dense(128, relu);
    layer Dense(10, softmax);
}

train(miModelo, datos, epochs=25);
predict(miModelo, nueva_entrada);
```

---

## Flujo de Ejecución

1. **Lexer** (`parser.l`): el código fuente `.te` se divide en tokens (palabras clave, literales, operadores).
2. **Parser** (`parser.y`): los tokens se reducen siguiendo la gramática LALR(1), construyendo nodos `ASTNode`.
3. **Evaluación** (`bytecode.c`): el árbol se recorre en profundidad; cada nodo ejecuta su operación (asignación, llamada a función, retorno de valor, etc.).
4. **Bridges nativos**: cuando el script llama a `mysql_connect`, `orm_query`, `Http.client`, etc., se despacha a las implementaciones C correspondientes.
5. **Servidor HTTP** (`typeeasy_main.c`): escanea el directorio `/app/apis`, parsea cada `.te`, registra sus endpoints y los sirve vía HTTP con `libmicrohttpd`.

---

## Tecnologías Usadas

| Tecnología          | Uso                                             |
|---------------------|-------------------------------------------------|
| **C (GCC)**         | Núcleo del intérprete, servidor HTTP, bridges   |
| **Flex (Lex)**      | Análisis léxico del lenguaje `.te`              |
| **Bison (YACC)**    | Análisis sintáctico / gramática LALR(1)         |
| **libmicrohttpd**   | Servidor HTTP embebido en C                     |
| **libmysqlclient**  | Conexión nativa a MySQL desde C                 |
| **Python 3**        | Servicio Gemini AI, mock NLU, adaptador WAHA    |
| **Docker / Compose**| Orquestación de todos los servicios             |
| **Google Gemini AI**| Motor de lenguaje natural para el chatbot       |
| **WAHA / Meta API** | Integración con WhatsApp                        |

---

## Casos de Uso Principales

1. **Prototipado rápido de APIs**: escribe un archivo `.te` con un `endpoint` y obtienes una API REST funcionando en segundos.
2. **DSL por dominio**: define un mini-lenguaje adaptado a tu negocio (facturación, logística, inventario) sin depender de un framework pesado.
3. **Chatbot WhatsApp empresarial**: conecta Gemini AI a WhatsApp con menos de 30 líneas de código `.te`.
4. **Scripts de automatización**: sustituye bash/Python para tareas simples con una sintaxis más legible y tipada.
5. **Aprendizaje de compiladores**: el proyecto es una referencia didáctica de cómo construir un intérprete completo con Flex + Bison + AST en C.

---

## Estructura de Directorios

```
TypeEasy/
├── src/                    # Código fuente del intérprete (C, Flex, Bison)
├── typeeasycode/           # Scripts de ejemplo y producción (.te)
│   ├── apis/               # Endpoints REST (.te)
│   ├── models/             # Modelos de datos (.te)
│   └── settings/           # Configuración de BD, etc. (.te)
├── ai_assistant/           # Entrenamiento y datos para el asistente AI
├── tools/                  # Servicios auxiliares (nlu_mock, api_mock, etc.)
├── docs/                   # Documentación adicional
├── docker-compose.yml      # Orquestación de servicios
├── .env.example            # Variables de entorno de ejemplo
└── README.md               # Guía de inicio rápido
```

---

## Resumen Ejecutivo

TypeEasy es un **intérprete escrito en C** que implementa un lenguaje de scripting propio (`.te`) con sintaxis familiar para desarrolladores de TypeScript/Java. Su propósito central es **reducir la fricción** entre la idea de negocio y el código funcional: en pocas líneas se pueden definir clases, consultar una base de datos MySQL, exponer un endpoint REST o crear un chatbot para WhatsApp conectado a IA generativa.

El proyecto es de código abierto (MIT), funciona completamente con Docker y está orientado tanto a desarrolladores que quieran prototipar rápidamente como a quienes deseen aprender a construir intérpretes y compiladores desde cero en C.
