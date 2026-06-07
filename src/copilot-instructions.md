# 🧩 Guía de Estilo — Plugins Kapital (FacturaScripts)

Estas instrucciones son OBLIGATORIAS para todo código generado por Copilot.

---

## 1. 🚫 PROHIBICIONES
Copilot NO debe:
- Crear datos de prueba, fixtures, ejemplos ni sandbox.
- Crear logs, trazas, debug (`var_dump`, `print_r`, `console.log`, `error_log`, etc.).
- Crear carpetas o archivos de prueba (`tests/`, `demo_*`, `tmp_*`, `sandbox`, etc.).
- Crear archivos SQL para crear tablas (las tablas se crean automáticamente desde los XML al limpiar caché).
- Cambiar estructura de carpetas, namespaces o nombres existentes.
- Reindentar o embellecer código sin orden explícita.
- Cambiar sintaxis RainTPL.
- Introducir dependencias externas (JS, PHP, CSS).
- Modificar diseño visual o CSS existente.

---

## 2. ✅ REGLAS GENERALES
- Estructura obligatoria: **XML → Modelo → Controlador → Vista**.
- No inventar ni renombrar campos.
- Mantener nombres, IDs y jerarquía HTML.
- Comentarios en **español técnico**.
- Indentación: **4 espacios** (sin tabs).
- Tablas y campos: minúsculas.
- Clases: `StudlyCaps`.

---

## 3. 🌧️ RainTPL (sintaxis exacta)
Correcto:
{if="$condicion"}
{loop="$lista"}
{include="header"}

Incorrecto:
{ if = " $condicion" }
{ loop = " $lista" }


---

## 4. 📄 XML
- Define tablas completas.
- Cada columna con `comment`.
- IDs = `int`, códigos = `varchar`.
- Sin `text` salvo observaciones.
- Debe coincidir con el modelo PHP.

---

## 5. 🧠 MODELOS PHP
- `namespace` correcto.
- Extiende `ModelClass`.
- Métodos obligatorios:
```php
public static function primaryColumn(): string
public static function tableName(): string
Sin lógica compleja.

6. 🎮 CONTROLADORES
Prefijos: List o Edit.

No romper flujo base de FacturaScripts.

Sin lógica innecesaria.

Íconos FontAwesome coherentes.

7. 🎨 VISTAS
No cambiar jerarquía DOM.

No cambiar clases ni IDs.

Mantener compatibilidad con Chosen.js y diseño Kapital.

Formularios limpios y responsivos.

8. 🧾 DEBUG
Solo si el usuario lo pide explícitamente.

**Proceso obligatorio de debugging (en este orden):**

1. **MODELO (SQL)**: 
   - Imprimir la sentencia SQL generada con `echo` antes del `return $this->db->exec($sql);`
   - Copiar y ejecutar esa sentencia directamente en la BD
   - Verificar si hay error SQL (longitud de campos, tipos de datos, etc.)

2. **CONTROLADOR (Variables POST)**:
   - Usar `print_r($_POST);` o `echo '<pre>'; print_r($variable); echo '</pre>';`
   - Verificar que las variables correctas llegan desde la vista
   - Validar tipos de datos y valores recibidos

3. **VISTA (JavaScript)**:
   - Usar `console.log()` para verificar valores antes de enviar AJAX
   - Validar que los selectores DOM obtienen los valores correctos

**Herramientas:**
- PHP: `echo`, `print_r()`, `var_dump()`
- JavaScript: `console.log()`, `alert()`
- SQL: Imprimir query completo

Una vez resuelto:

Eliminar todo debugging: `echo`, `die()`, `print_r()`, `var_dump()`, `console.log()`.

9. ✍️ AUTORÍA OBLIGATORIA
Usar:

@author Jonathan Veintimilla <proyectos@kapitalcompany.com>
Solo en:

Archivos nuevos completos.

Funciones nuevas.

Bloques complejos.

NO usar en:

Cambios pequeños.

Campos XML.

10. 🔧 FUNCIONES BASE
Antes de crear funciones:
Revisar base/fs_functions.php.

Si existe → usarla.

Si es reutilizable → crearla ahí con @author.

Si es específica → crearla en el modelo o controlador.

11. 🗃️ SQL
SQL con saltos de línea legibles.

Strings y fechas: var2str().

Números: intval() o floatval().

Índices: cualquier índice nuevo debe crearse en parametrizacion_mysql.php.

NO definir CREATE INDEX en los XML de tablas.

En los XML solo usar restricciones compatibles con FacturaScripts: PRIMARY KEY, FOREIGN KEY y UNIQUE.

12. 📦 JSON (controladores)
Formato estándar:

echo json_encode([
    'success' => true,
    'data' => $resultado,
    'mensaje' => 'Operación completada'
]);
13. 🧩 COMPORTAMIENTO DE COPILOT
Copilot debe:

No crear archivos no solicitados.

No alterar diseño.

No inventar campos.

Respetar estructura Kapital.

Usar español técnico.

Respetar esta guía antes de generar código.

14. ❓ CUANDO FALTE INFO
Preguntar:

¿Campo obligatorio o no?

¿Es reutilizable?

¿Formato de fecha?

¿Debe validarse RUC?

15. 🎯 OBJETIVO
Todo módulo nuevo debe generar:


1 Modelo
1 carpeta table que va dentro de la carpeta modelo para que ahí guarde los .xml 

1 Controlador

1 Vista
alineados entre sí y con estas reglas.

---

## 16. 💾 RESTAURACIÓN DE BASE DE DATOS
Para restaurar una base de datos desde terminal:

```bash
mysql -u JonathanV -p [nombre_base_destino] < /var/www/Bases/[nombre_archivo.sql]
```

**Parámetros:**
- Usuario: `JonathanV`
- Directorio de respaldos: `/var/www/Bases`
- Carpeta temporal: `tmp/CycowghORMX0UEnfuN38/`
- **Bases de datos disponibles:**
  - `developer` = `jonathanv_developer`
  - `developer2` = `jonathanv_developer2`
  - `developer3` = `jonathanv_developer3`

**Proceso completo:**
1. Preguntar: ¿Cuál es el nombre del archivo de respaldo SQL?
2. Preguntar: ¿En qué base de datos lo subo? (developer, developer2 o developer3)
3. Ejecutar comando: `mysql -u JonathanV -p [base_destino] < /var/www/Bases/[archivo.sql]`
4. Usuario ingresa contraseña
5. Esperar confirmación del usuario: ¿Está ok?
6. Si usuario confirma "sí" → Actualizar `config.php`:
   - Modificar `FS_DB_NAME` con el nombre de la base destino
7. Abrir archivos para edición manual del usuario:
   - `tmp/CycowghORMX0UEnfuN38/config2.ini`
   - `tmp/CycowghORMX0UEnfuN38/enabled_plugins.list`
8. Usuario copia y pega manualmente los cambios necesarios
9. Confirmar finalización del proceso

---

## 17. 🔧 PARAMETRIZACIÓN
Las variables de configuración del sistema se acceden de dos formas según su origen:

### **admin_home.html → FS_**
Variables en `admin_home.html` con `$GLOBALS['config2']['nombre_variable']`

**CORRECTO:**
```php
if (FS_ORGANIC_GARDENS == 1) {
    // código
}
```

**INCORRECTO:**
```php
if (isset($this->kp_var->organic_gardens) and $this->kp_var->organic_gardens == '1') {
    // NO usar esta forma
}
```

### **admin_personalizaciones → kp_var**
Variables en `admin_personalizaciones` se acceden con `$this->kp_var->`

**CORRECTO:**
```php
if (isset($this->kp_var->variable_personalizada) && $this->kp_var->variable_personalizada == 1) {
    // código
}
```

**Regla:**
- **admin_home.html** → Constantes `FS_NOMBRE_VARIABLE`
- **admin_personalizaciones** → Objeto `$this->kp_var->nombre_variable`
- Comparar con entero: `== 1` o `== 2`
- Para `kp_var` siempre usar `isset()` antes de comparar
- **NO usar `defined()` para constantes FS_** → Las constantes FS_ siempre están definidas, usar directamente `FS_NOMBRE == 1`

**INCORRECTO:**
```php
if (defined('FS_LAVANDERIA') && FS_LAVANDERIA == 1) {
    // NO usar defined() para FS_
}
```

**CORRECTO:**
```php
if (FS_LAVANDERIA == 1) {
    // código
}
```

---

## 18. 🏗️ INSTANCIACIÓN DE OBJETOS/MODELOS
Al instanciar modelos en PHP, usar la forma simple sin namespace completo.

**CORRECTO:**
```php
$agencia = new agencia_transporte();
$origen = new origenesdestinos();
$cliente = new cliente();
```

**INCORRECTO:**
```php
$agencia = new \FacturaScripts\model\agencia_transporte();
$origen = new \FacturaScripts\model\origenesdestinos();
```

**Regla:**
- Usar siempre `new nombre_clase()` sin namespace
- No usar `\FacturaScripts\model\` ni otros namespaces completos
- Mantener código limpio y coherente con el estilo Kapital

---

## 19. ➕ CREACIÓN DE NUEVOS CAMPOS
Al agregar nuevos campos a un modelo existente, seguir **SIEMPRE** estos 5 pasos:

### **Paso 1: XML (tabla)**
Definir el campo en `/plugins/[plugin]/model/table/[tabla].xml`:
```xml
<column>
    <name>nombre_campo</name>
    <type>double precision</type>
    <null>YES</null>
    <default>0</default>
</column>
```

### **Paso 2: Modelo (propiedad)**
Declarar e inicializar en `/plugins/[plugin]/model/[modelo].php`:
```php
/**
 * Descripción del campo
 * @var tipo
 */
public $nombre_campo;

// En el constructor __construct($data = FALSE):
if ($data) {
    $this->nombre_campo = isset($data['nombre_campo']) ? floatval($data['nombre_campo']) : 0;
} else {
    $this->nombre_campo = 0;
}
```

### **Paso 3: Modelo (método save) - CRÍTICO**
**OBLIGATORIO:** Agregar el campo en el SQL del método `save()`:

**UPDATE:**
```php
", campo_anterior = " . $this->var2str($this->campo_anterior) .
", nombre_campo = " . $this->var2str($this->nombre_campo) .
", campo_siguiente = " . $this->var2str($this->campo_siguiente) .
```

**INSERT (lista de columnas):**
```php
..., campo_anterior, nombre_campo, campo_siguiente)
```

**INSERT (lista de valores):**
```php
"," . $this->var2str($this->campo_anterior) .
"," . $this->var2str($this->nombre_campo) .
"," . $this->var2str($this->campo_siguiente)
```

### **Paso 4: Vista (input)**
Agregar el campo en `/plugins/[plugin]/view/[vista].html`:
```html
<div class="col-sm-2">
    <div class="form-group">
        <label>Nombre del Campo</label>
        <input class="form-control" type="number" step="any" name="nombre_campo" 
               value="{$fsc->agente->nombre_campo}" onkeyup="cambio(this);" />
    </div>
</div>
```

### **Paso 5: Controlador (procesamiento POST)**
Capturar y validar en `/plugins/[plugin]/controller/[controlador].php`:
```php
$this->modelo->nombre_campo = 0;
if(isset($_POST['nombre_campo']) && $_POST['nombre_campo'] != ''){
    $this->modelo->nombre_campo = floatval($_POST['nombre_campo']);
}
```

### **⚠️ ERRORES COMUNES:**
- ❌ Olvidar agregar el campo en el método `save()` del modelo (UPDATE e INSERT)
- ❌ No inicializar el campo en el constructor del modelo
- ❌ No validar con `isset()` antes de leer `$_POST`
- ❌ Usar tipo incorrecto de conversión (`intval()`, `floatval()`, `var2str()`)

### **✅ VERIFICACIÓN:**
Antes de terminar, confirmar:
1. ✓ Campo existe en XML
2. ✓ Propiedad declarada en modelo
3. ✓ Constructor inicializa el campo
4. ✓ **Campo incluido en UPDATE del save()**
5. ✓ **Campo incluido en INSERT del save() (columnas y valores)**
6. ✓ Input existe en vista
7. ✓ Controlador procesa el POST
