#!/usr/bin/env bash
# Build libte_sqlite.so for Linux (no host libsqlite3 dependency).
#
# Statically embeds the SQLite amalgamation (sqlite3.c) into the .so so the
# produced library has no runtime dependency on libsqlite3.so. The .deb /
# tarball can ship libte_sqlite.so standalone.
#
# Usage:
#   bash plugins/sqlite/build_linux.sh
#
# Env overrides:
#   SQLITE_VERSION   default "3530100" (3.53.1)
#   SQLITE_YEAR      default "2026"
#   AMALGAMATION_URL fully override the download URL
set -euo pipefail

cd "$(dirname "$0")"

SQLITE_VERSION="${SQLITE_VERSION:-3530100}"
SQLITE_YEAR="${SQLITE_YEAR:-2026}"
AMALGAMATION_ZIP="sqlite-amalgamation-${SQLITE_VERSION}.zip"
AMALGAMATION_URL="${AMALGAMATION_URL:-https://sqlite.org/${SQLITE_YEAR}/${AMALGAMATION_ZIP}}"
AMALGAMATION_DIR="sqlite-amalgamation-${SQLITE_VERSION}"

if [[ ! -f "$AMALGAMATION_DIR/sqlite3.c" ]]; then
    echo "[sqlite] downloading amalgamation: $AMALGAMATION_URL"
    rm -f "$AMALGAMATION_ZIP"
    curl -fL -o "$AMALGAMATION_ZIP" "$AMALGAMATION_URL"
    unzip -o "$AMALGAMATION_ZIP" >/dev/null
    rm -f "$AMALGAMATION_ZIP"
fi

echo "[sqlite] compiling libte_sqlite.so (static amalgamation)..."
gcc -shared -fPIC -O2 -Wall -Wno-unused-parameter \
    -I../../src \
    -I"$AMALGAMATION_DIR" \
    -DSQLITE_THREADSAFE=1 \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    sqlite_plugin.c \
    "$AMALGAMATION_DIR/sqlite3.c" \
    -lpthread -ldl -lm \
    -o libte_sqlite.so
echo "[sqlite] done:"
ls -la libte_sqlite.so
