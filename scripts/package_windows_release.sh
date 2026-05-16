#!/usr/bin/env bash
# Empaqueta TypeEasy para distribucion Windows sin Docker.
# Uso:
#   bash scripts/package_windows_release.sh 0.0.1

set -euo pipefail

VERSION="${1:-0.0.1}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="$ROOT_DIR/src/typeeasy.exe"
OUT_DIR="$ROOT_DIR/dist/windows"
PKG_DIR="$OUT_DIR/TypeEasy-${VERSION}-win64"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "ERROR: no existe $BIN_PATH" >&2
  echo "Compila primero: bash scripts/build_native_windows.sh" >&2
  exit 1
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/examples" "$PKG_DIR/cli/templates"

# Binary is renamed to typeeasy-bin.exe so the smart .cmd wrapper takes
# precedence on PATH (Windows PATHEXT default puts .EXE before .CMD).
cp "$BIN_PATH" "$PKG_DIR/bin/typeeasy-bin.exe"

# Smart dispatcher wrappers (Rails subcommands -> bash CLI, flags/files -> interp)
cp "$ROOT_DIR/installer/windows/typeeasy.cmd" "$PKG_DIR/bin/typeeasy.cmd"
cp "$ROOT_DIR/installer/windows/te.cmd"       "$PKG_DIR/bin/te.cmd"

# Rails-style bash CLI + templates (invoked by wrappers via Git Bash)
cp "$ROOT_DIR/cli/typeeasy" "$PKG_DIR/cli/typeeasy"
cp "$ROOT_DIR/cli/te"       "$PKG_DIR/cli/te"
chmod +x "$PKG_DIR/cli/typeeasy" "$PKG_DIR/cli/te"
cp -r "$ROOT_DIR/cli/templates/." "$PKG_DIR/cli/templates/"

for doc in README.md BUILD.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    cp "$ROOT_DIR/$doc" "$PKG_DIR/"
  fi
done

if [[ -f "$ROOT_DIR/typeeasycode/crear_const_variable.te" ]]; then
  cp "$ROOT_DIR/typeeasycode/crear_const_variable.te" "$PKG_DIR/examples/"
fi
if [[ -f "$ROOT_DIR/typeeasycode/endpoint.te" ]]; then
  cp "$ROOT_DIR/typeeasycode/endpoint.te" "$PKG_DIR/examples/"
fi

if command -v ldd >/dev/null 2>&1; then
  # ldd en MSYS2 imprime: "libfoo.dll => /mingw64/bin/libfoo.dll (0xADDR)"
  # Solo copiamos DLLs que vivan en el arbol de MSYS2/MINGW (no las de WINDOWS\System32).
  mapfile -t DLLS < <(
    ldd "$BIN_PATH" \
      | awk '/=> \// { print $3 }' \
      | grep -iE '\.dll$' \
      | grep -iE '/(mingw64|msys64|usr|clang64|ucrt64)/' \
      | sort -u
  )

  echo "DLLs detectadas para bundlear:"
  for dll in "${DLLS[@]}"; do
    if [[ -f "$dll" ]]; then
      echo "  $dll"
      cp -f "$dll" "$PKG_DIR/bin/"
    fi
  done
fi

# Fallback / refuerzo: copiar explicitamente las DLLs criticas que sabemos que typeeasy.exe necesita.
# Si ldd fallo o no las detecto, esto evita "no se encontro libXXX.dll" en la maquina del usuario.
MSYS_BIN_CANDIDATES=(
  "/mingw64/bin"
  "/c/msys64/mingw64/bin"
  "/d/a/_temp/msys64/mingw64/bin"
)
MSYS_BIN=""
for cand in "${MSYS_BIN_CANDIDATES[@]}"; do
  if [[ -d "$cand" ]]; then MSYS_BIN="$cand"; break; fi
done

if [[ -n "$MSYS_BIN" ]]; then
  echo "Copiando DLLs criticas desde $MSYS_BIN"
  for dll in libwinpthread-1.dll libmariadb.dll libgcc_s_seh-1.dll libstdc++-6.dll \
             libssl-3-x64.dll libcrypto-3-x64.dll zlib1.dll libiconv-2.dll; do
    if [[ -f "$MSYS_BIN/$dll" ]]; then
      cp -f "$MSYS_BIN/$dll" "$PKG_DIR/bin/"
    fi
  done
fi

# Override: si el caller (CI o build local) construyo una libmariadb.dll
# contra OPENSSL via scripts/build_mariadb_openssl_dll.sh, reemplazamos la
# de MSYS2 (Schannel) por esa. Schannel falla contra los NLB de AWS
# (TiDB Cloud, PlanetScale, Aiven) con SEC_E_DECRYPT_FAILURE.
if [[ -n "${LIBMARIADB_OPENSSL_DLL:-}" && -f "$LIBMARIADB_OPENSSL_DLL" ]]; then
  echo "Reemplazando libmariadb.dll (Schannel) con build OpenSSL: $LIBMARIADB_OPENSSL_DLL"
  cp -f "$LIBMARIADB_OPENSSL_DLL" "$PKG_DIR/bin/libmariadb.dll"
  ls -lh "$PKG_DIR/bin/libmariadb.dll"
elif [[ -n "${LIBMARIADB_OPENSSL_DLL:-}" ]]; then
  echo "WARNING: LIBMARIADB_OPENSSL_DLL=$LIBMARIADB_OPENSSL_DLL no existe, se mantiene la de MSYS2 (Schannel)" >&2
fi

echo "Contenido final de bin/:"
ls -lh "$PKG_DIR/bin/"

cat > "$PKG_DIR/README-WINDOWS.txt" <<EOF
TypeEasy Windows Package v${VERSION}

Estructura:
- bin/typeeasy-bin.exe   (intérprete C)
- bin/typeeasy.cmd       (CLI Rails-style, dispatcher)
- bin/te.cmd             (alias corto)
- cli/typeeasy           (bash script — requiere Git Bash)
- cli/te                 (alias bash)
- cli/templates/         (scaffolds para 'typeeasy new')
- examples/

Uso rápido (PowerShell o CMD):

  CLI Rails-style (requiere Git Bash instalado):
    typeeasy new mi-app
    cd mi-app
    typeeasy gen resource producto
    typeeasy serve --dev

  Script directo (no requiere Git Bash):
    typeeasy .\\examples\\crear_const_variable.te

  Servidor HTTP embebido (--api):
    typeeasy --api .\\examples\\endpoint.te --port 9000
    # luego en otra consola:
    curl http://localhost:9000/ping

El alias 'te' funciona idéntico a 'typeeasy'.

Flags útiles (intérprete):
  --api <archivo.te>     Levanta servidor HTTP con los endpoints del .te
  --port <p>             Puerto (default 8080)
  --host <h>             Host bind (default 0.0.0.0)
  --help                 Lista todas las opciones

Subcomandos Rails-style:
  new <nombre>           Crea proyecto nuevo
  gen resource <nombre>  Genera endpoint CRUD + migración SQL
  gen endpoint <nombre>  Genera endpoint vacío
  serve [--dev|--prod]   Levanta servidor local
  migrate                Aplica migraciones SQL
  console                REPL del intérprete

Nota: los subcomandos Rails-style (new/gen/serve/...) requieren Git Bash
instalado: https://git-scm.com/download/win

Esta versión es v${VERSION}.
EOF

echo "Paquete generado en: $PKG_DIR"
