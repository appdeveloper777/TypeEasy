# 14 — bool, datetime, uuid (v1.0.0)

Tres tipos nuevos en TypeEasy:

| Tipo       | Almacenamiento              | Literales / builtins                          |
|------------|-----------------------------|-----------------------------------------------|
| `bool`     | int 0/1 con etiqueta `BOOL` | `true`, `false`                               |
| `datetime` | string ISO-8601 UTC         | `now()`, `date_parse`, `date_format`, `date_add`, `date_diff` |
| `uuid`     | string canónico v4          | `uuid_v4()`, `uuid_valid(s)`                  |

## Ejecutar

```bash
docker compose run --rm typeeasy examples/14_types_bool_datetime_uuid/01_bool.te
docker compose run --rm typeeasy examples/14_types_bool_datetime_uuid/02_datetime.te
docker compose run --rm typeeasy examples/14_types_bool_datetime_uuid/03_uuid.te
```

## Builtins de datetime

- `now()` → string ISO-8601 UTC, p.ej. `"2026-05-19T17:42:09Z"`.
- `now_epoch()` → int (segundos desde 1970).
- `date_parse(iso)` → int (epoch). Devuelve 0 si el formato no es válido.
- `date_format(epoch, fmt)` → string. `fmt` es `strftime` (`%Y-%m-%d`, `%H:%M:%S`, ...).
- `date_add(iso, unit, n)` → string ISO. `unit` ∈ `{seconds, minutes, hours, days}`.
- `date_diff(a, b, unit)` → int. Resultado = `(a - b) / unit`.

## Builtins de uuid

- `uuid_v4()` → string `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` (RFC 4122 v4).
- `uuid_valid(s)` → int (1 si tiene formato válido, 0 si no).

## JSON

- `json_stringify(bool)` emite `true`/`false`, no `1`/`0`.
- `datetime` y `uuid` se serializan como strings (`"..."`).
