# Fuzzing — front-end de TypeEasy

Fuzzing guiado por cobertura (libFuzzer + AddressSanitizer) del front-end:
lexer + parser Bison + construcción del AST + `free_ast()`. Es donde se
esconden los "segfaults intermitentes en paths no cubiertos por los tests":
entrada maliciosa/malformada que ejercita producciones de la gramática y
constructores de nodos que la suite curada nunca alcanza.

## Componentes

- `fuzz_parser.c` — harness libFuzzer. `LLVMFuzzerTestOneInput` envuelve los
  bytes en un `FILE*` (`fmemopen`), llama `parse_file()`, libera el AST y resetea
  el estado global entre iteraciones.
- `typeeasy.dict` — diccionario de tokens del lenguaje para que el mutador
  construya entrada sintácticamente interesante más rápido.
- `seeds/` — corpus semilla curado (programas válidos pequeños, alta cobertura).
- `corpus/` — corpus de trabajo (generado; ignorado por git).
- `artifacts/` — reproducers de crashes (generado; ignorado por git).

## Correr local (requiere clang; usa Docker)

```bash
# Smoke de 60s dentro del contenedor de toolchain (gcc:13.2.0 + clang):
docker run --rm -v "$PWD":/work -w /work gcc:13.2.0 bash -c '
  apt-get update -qq && apt-get install -y -qq clang flex bison libssl-dev \
    default-libmysqlclient-dev libcurl4-openssl-dev libpq-dev freetds-dev \
    libsqlite3-dev libomp-dev >/dev/null
  bash scripts/run_fuzz.sh 60
'
```

En Windows, anteponer `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'` al `docker run`.

## Reproducir un crash

```bash
src/fuzz_parser tests/fuzz/artifacts/crash-<sha1>
```

El binario imprime el stack trace de ASan y la entrada que lo disparó.

## Por qué solo-parse (sin `interpret_ast`)

Interpretar entrada de fuzz arbitraria llamaría al runtime (IO de archivos,
HTTP, conexiones a BD, `system()`) y colgaría o tocaría el host. El path
parser + AST + `free_ast` es autocontenido, determinista, y es justo donde
aparecen los bugs de memoria (UAF / overflow / truncación LLP64).

## CI

El job `fuzz-smoke` de `.github/workflows/lang-tests.yml` construye el fuzzer y
corre un smoke acotado en cada push/PR. Cualquier crash falla el job y sube el
reproducer como artifact.
