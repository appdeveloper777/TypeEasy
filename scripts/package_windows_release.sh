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
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/examples" "$PKG_DIR/cli/templates" "$PKG_DIR/plugins/sqlite" "$PKG_DIR/vscode"

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

# VS Code extension (.vsix) so users can run `te ext install` without the repo.
# Reuse a prebuilt one if provided ($TYPEEASY_VSIX), else build it (needs npx).
VSIX_DST="$PKG_DIR/vscode/typeeasy-debug.vsix"
if [[ -n "${TYPEEASY_VSIX:-}" && -f "$TYPEEASY_VSIX" ]]; then
  cp -f "$TYPEEASY_VSIX" "$VSIX_DST"
  echo "VS Code extension incluida (prebuilt): $VSIX_DST"
elif command -v npx >/dev/null 2>&1; then
  if bash "$ROOT_DIR/scripts/build_vscode_vsix.sh" "$VSIX_DST" >/dev/null 2>&1; then
    echo "VS Code extension incluida (build): $VSIX_DST"
  else
    echo "WARN: no se pudo construir el .vsix; el paquete no incluira la extension VS Code" >&2
    rmdir "$PKG_DIR/vscode" 2>/dev/null || true
  fi
else
  echo "WARN: npx no encontrado; el paquete no incluira la extension VS Code (te ext install no funcionara offline)" >&2
  rmdir "$PKG_DIR/vscode" 2>/dev/null || true
fi

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

# Conector SQL Server (FreeTDS) — OPCIONAL, para que el paquete "no pese mucho".
#   TE_WITH_SQLSERVER=0  -> NO bundlear las DLLs de FreeTDS/GnuTLS (paquete liviano).
#   auto/1 (default)     -> si typeeasy.exe fue compilado con FreeTDS, ldd ya las
#                           copio arriba; aqui solo reforzamos por si ldd fallo.
# Estas DLLs son exclusivas del conector SQL Server (libsybdb + cadena GnuTLS) y
# pesan varios MB. El instalador (TypeEasy.iss) las ofrece como componente
# desmarcable "Conector SQL Server" para que el usuario decida en tiempo de install.
TE_WITH_SQLSERVER="${TE_WITH_SQLSERVER:-auto}"
SQLSERVER_DLL_GLOBS=(libsybdb*.dll libgnutls*.dll libhogweed*.dll libnettle*.dll \
                     libtasn1*.dll libp11-kit*.dll libidn2*.dll libunistring*.dll libgmp*.dll)
case "$TE_WITH_SQLSERVER" in
  0|off|no|false|OFF|NO|FALSE)
    echo "TE_WITH_SQLSERVER=$TE_WITH_SQLSERVER: removiendo DLLs de SQL Server del paquete (mas liviano)"
    for glob in "${SQLSERVER_DLL_GLOBS[@]}"; do
      rm -f "$PKG_DIR"/bin/$glob 2>/dev/null || true
    done
    ;;
  *)
    if [[ -n "$MSYS_BIN" ]]; then
      copied_any=0
      for glob in "${SQLSERVER_DLL_GLOBS[@]}"; do
        for src in "$MSYS_BIN"/$glob; do
          if [[ -f "$src" ]]; then
            cp -f "$src" "$PKG_DIR/bin/"
            copied_any=1
          fi
        done
      done
      if [[ "$copied_any" == "1" ]]; then
        echo "Conector SQL Server: DLLs de FreeTDS/GnuTLS incluidas (componente opcional en el instalador)."
      else
        echo "Conector SQL Server: FreeTDS no presente en $MSYS_BIN; paquete sin sqlserver_* (usa stub)."
      fi
    fi
    ;;
esac

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

# Plugins nativos (load_native("sqlite") -> plugins/sqlite/libte_sqlite.dll).
# El plugin DEBE reconstruirse del mismo commit que el .exe: un libte_sqlite.dll
# viejo tiene otra ABI del host y hace que sqlite_query/sqlite_exec devuelvan
# []/0 en silencio. Por eso forzamos rebuild y FALLAMOS si no se puede (mejor
# no publicar instalador que publicar uno con plugin stale/ausente).
SQLITE_DLL="$ROOT_DIR/plugins/sqlite/libte_sqlite.dll"
rm -f "$SQLITE_DLL"   # forzar rebuild fresco, ABI-matched con este .exe
if ! command -v gcc >/dev/null 2>&1; then
  echo "ERROR: gcc no encontrado; no se puede construir el plugin sqlite (libte_sqlite.dll)." >&2
  exit 1
fi
echo "Compilando plugin sqlite (amalgamation)..."
if ! bash "$ROOT_DIR/plugins/sqlite/build_windows.sh"; then
  echo "ERROR: build del plugin sqlite falló. Abortando para no publicar un instalador" >&2
  echo "       con el .exe nuevo y un libte_sqlite.dll viejo/ausente." >&2
  exit 1
fi
if [[ ! -f "$SQLITE_DLL" ]]; then
  echo "ERROR: build del plugin sqlite no produjo libte_sqlite.dll." >&2
  exit 1
fi
cp "$SQLITE_DLL" "$PKG_DIR/plugins/sqlite/libte_sqlite.dll"
cp "$ROOT_DIR/plugins/sqlite/README.md" "$PKG_DIR/plugins/sqlite/" 2>/dev/null || true
echo "Plugin sqlite incluido (rebuild fresco):"
ls -lh "$PKG_DIR/plugins/sqlite/"

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
