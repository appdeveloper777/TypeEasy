# 06b_xlsx — Lectura de archivos Excel (.xlsx)

TypeEasy puede leer hojas Excel con la misma sintaxis `from "archivo.xlsx", Class;`
que ya se usa para CSV. Por debajo, el intérprete:

1. Detecta la extensión `.xlsx` / `.xlsm`.
2. Abre el archivo como ZIP, descomprime `xl/sharedStrings.xml` y la primera
   hoja (`xl/worksheets/sheet1.xml`) con zlib.
3. Materializa un CSV en memoria (RFC 4180) y lo entrega al lector CSV existente.
4. A partir de ahí todo funciona igual: LINQ (`sumBy`, `where`, `orderBy`),
   modo columnar (`as dataframe`), iteración con `for`, `.length`, etc.

## Archivos

| Archivo | Descripción |
|---|---|
| `productos.xlsx` | Datos de ejemplo (columnas `nombre`, `precio`). |
| `crear_productos_xlsx.py` | Genera/regenera `productos.xlsx` con `openpyxl`. |
| `leer_excel.te` | Ejemplo básico — lee, recorre e imprime. |
| `leer_excel_dataframe.te` | Mismo dataset cargado como DataFrame (columnar). |

## Probarlo

```bash
# (opcional) regenerar el .xlsx
python crear_productos_xlsx.py

# desde el host con el binario de la imagen Docker:
docker compose run --rm typeeasy /typeeasycode/examples/06b_xlsx/leer_excel.te
```

## Limitaciones intencionales

- Solo se lee la **primera hoja** del libro.
- Celdas tipo fecha vienen como **serial numérico OADate**; convertir
  manualmente en el script si se necesita ISO-8601.
- Sin evaluación de fórmulas — se usa el valor cacheado `<v>` que escribió
  Excel/LibreOffice al guardar.
- Tipos soportados: string (compartida / inline), numérico, booleano (0/1).
