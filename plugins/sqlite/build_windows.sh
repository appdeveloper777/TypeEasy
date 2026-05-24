#!/usr/bin/env bash
set -e
export PATH="/c/msys64/mingw64/bin:$PATH"
cd "$(dirname "$0")"
echo "[sqlite] compiling libte_sqlite.dll..."
gcc -shared -O2 -Wall -Wno-unused-parameter \
    -I../../src \
    sqlite_plugin.c \
    -lsqlite3 \
    -o libte_sqlite.dll
echo "[sqlite] done:"
ls -la libte_sqlite.dll
