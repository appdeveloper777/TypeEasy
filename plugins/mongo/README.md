# libte_mongo — MongoDB driver para TypeEasy

Plugin nativo de ejemplo. Demuestra el modelo de paquetes instalables vía
`load_native(...)`.

## Compilar

```bash
docker build -f plugins/mongo/Dockerfile -t te-mongo-builder .
docker run --rm -v "$PWD/plugins/mongo:/out" te-mongo-builder
# Resultado: plugins/mongo/libte_mongo.so
```

## Instalar

```bash
tools/te-install/te-install mongo --from plugins/mongo/libte_mongo.so
```

Esto copia el `.so` a `~/.te/packages/mongo/libte_mongo.so`.

## Usar desde un .te

```ts
load_native("mongo");

let conn = mongo_connect("mongodb://host.docker.internal:27017/meri");
let r    = mongo_query(conn, "usuarios", { "activo": 1 }, "json");
println(r);
mongo_close(conn);
```

## API

| Builtin | Args | Retorno |
|---|---|---|
| `mongo_connect(uri)` | string URI mongodb:// | int slot (≥0 ok, -1 error) |
| `mongo_query(slot, coll, filter, fmt)` | int, string, object/JSON, "json"\|"xml" | string (JSON array o XML) |
| `mongo_close(slot)` | int | 0 |

## Publicar (futuro registry)

1. CI compila `libte_mongo.so` en GitHub Actions.
2. Sube el `.so` como asset de la Release `v1.0.0`.
3. PR al repo `typeeasy/registry` añadiendo entrada en `index.json`:
   ```json
   "mongo": {
     "latest": "1.0.0",
     "versions": {
       "1.0.0": {
         "url": "https://github.com/<org>/te-mongo/releases/download/v1.0.0/libte_mongo.so",
         "sha256": "...",
         "abi": 1
       }
     }
   }
   ```
4. Una vez merged, los usuarios podrán `te-install mongo` sin `--from`.
