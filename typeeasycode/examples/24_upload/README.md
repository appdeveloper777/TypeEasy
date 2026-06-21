# 24_upload — Upload de archivos CSV / XLSX a un endpoint

TypeEasy permite recibir archivos `.csv` y `.xlsx` desde un cliente HTTP y
materializarlos en una lista tipada con la **misma sintaxis** que ya se usa
para leer desde disco:

```ts
let productos = from "request:body", Producto;
```

La pseudo-URI `"request:body"` instruye al intérprete a leer el archivo
desde el cuerpo del request en curso, en vez de desde un archivo en disco.

## Detección automática

- **XLSX**: el sniffer reconoce la firma ZIP (`PK\x03\x04`) al inicio del
  payload y aplica el convertidor XLSX → CSV en memoria.
- **CSV**: cualquier otro contenido se trata como CSV UTF-8.
- **multipart/form-data**: si el `Content-Type` indica multipart, el
  intérprete extrae el primer file part (típico para uploads desde
  navegador o Postman) y aplica la detección sobre esos bytes. Si no hay
  ningún file part, vuelve a tratar el body completo como el archivo.

## Probarlo

```bash
# Levantar la imagen Docker como API:
docker compose run --rm -p 9101:9101 typeeasy \
    --api /typeeasycode/examples/24_upload/subir_archivo.te --port 9101

# (en otra terminal) Upload CSV crudo:
curl -X POST --data-binary @typeeasycode/examples/24_upload/productos.csv \
     -H "Content-Type: text/csv" \
     http://localhost:9101/api/productos

# Upload XLSX crudo:
curl -X POST --data-binary @typeeasycode/examples/06b_xlsx/productos.xlsx \
     -H "Content-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" \
     http://localhost:9101/api/productos

# Upload como multipart (como lo manda un <form> de navegador):
curl -X POST -F "archivo=@typeeasycode/examples/06b_xlsx/productos.xlsx" \
     http://localhost:9101/api/productos

# Variante columnar (DataFrame) — solo agregados:
curl -X POST -F "archivo=@typeeasycode/examples/06b_xlsx/productos.xlsx" \
     http://localhost:9101/api/productos/resumen
```

Respuesta esperada de `/api/productos`:

```json
{"total":6,"productos":[{"nombre":"Lapiz","precio":2},...]}
```

Respuesta esperada de `/api/productos/resumen`:

```json
{"total":6,"suma_precios":223,"max":120,"promedio":37.166666666666664}
```

## Límite de tamaño

El servidor API descarta cuerpos mayores que `TYPEEASY_MAX_BODY` bytes
(default **1 MiB**). Si necesitas subir archivos más grandes, exporta la
variable con un valor mayor antes de levantar el server:

```bash
TYPEEASY_MAX_BODY=$((16*1024*1024)) typeeasy --api ... --port 9101
```
