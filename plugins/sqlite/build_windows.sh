#!/usr/bin/env bash
# Build libte_sqlite.dll for Windows (MSYS2 MINGW64).
#
# Statically links the SQLite amalgamation (sqlite3.c) so the produced DLL
# has no runtime dependency on libsqlite3-0.dll. The Windows installer only
# needs to ship libte_sqlite.dll itself.
#
# Usage:
#   bash plugins/sqlite/build_windows.sh
#
# Env overrides:
#   SQLITE_VERSION   default "3530100" (3.53.1)
#   SQLITE_YEAR      default "2026"
#   AMALGAMATION_URL fully override the download URL
set -euo pipefail

export PATH="/c/msys64/mingw64/bin:$PATH"
# Asegurar TMP/TEMP escribibles para gcc en Windows (evita
# "Cannot create temporary file in C:\Windows\").
mkdir -p /c/temp 2>/dev/null || true
export TMP="${TMP:-C:/temp}"
export TEMP="${TEMP:-C:/temp}"
export TMPDIR="${TMPDIR:-C:/temp}"
cd "$(dirname "$0")"

SQLITE_VERSION="${SQLITE_VERSION:-3530100}"
SQLITE_YEAR="${SQLITE_YEAR:-2026}"
AMALGAMATION_ZIP="sqlite-amalgamation-${SQLITE_VERSION}.zip"
AMALGAMATION_URL="${AMALGAMATION_URL:-https://sqlite.org/${SQLITE_YEAR}/${AMALGAMATION_ZIP}}"
AMALGAMATION_DIR="sqlite-amalgamation-${SQLITE_VERSION}"

if [[ ! -f "$AMALGAMATION_DIR/sqlite3.c" ]]; then
    echo "[sqlite] downloading amalgamation: $AMALGAMATION_URL"
    rm -f "$AMALGAMATION_ZIP"
    if command -v curl >/dev/null 2>&1; then
        curl -fL -o "$AMALGAMATION_ZIP" "$AMALGAMATION_URL"
    else
        powershell -NoProfile -Command "Invoke-WebRequest -Uri '$AMALGAMATION_URL' -OutFile '$AMALGAMATION_ZIP'"
    fi
    unzip -o "$AMALGAMATION_ZIP" >/dev/null
    rm -f "$AMALGAMATION_ZIP"
fi

echo "[sqlite] compiling libte_sqlite.dll (static amalgamation)..."
gcc -shared -O2 -Wall -Wno-unused-parameter \
    -I../../src \
    -I"$AMALGAMATION_DIR" \
    -DSQLITE_THREADSAFE=1 \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    sqlite_plugin.c \
    "$AMALGAMATION_DIR/sqlite3.c" \
    -static-libgcc \
    -o libte_sqlite.dll
echo "[sqlite] done:"
ls -la libte_sqlite.dll
