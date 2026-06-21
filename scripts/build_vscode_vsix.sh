#!/usr/bin/env bash
# ============================================================================
# build_vscode_vsix.sh — Package the TypeEasy VS Code extension into a .vsix.
# ----------------------------------------------------------------------------
# The .vsix produced here is the artifact shipped inside the Windows installer
# and the Linux .deb/tarball so that a brand-new user can install the
# extension (syntax highlighting + LSP + native debugger) with one command:
#
#     te ext install
#
# The packaging step bundles the Language Server (tools/typeeasy-lsp/) INSIDE
# the extension directory at tools/typeeasy-vscode/lsp/, runs `npm install`
# in both packages so vscode-languageclient + vscode-languageserver* travel
# with the .vsix, and only then invokes `vsce package`. Without this, the
# packaged extension would silently fail to start the LSP and the user would
# get no go-to-definition / find-references / hover on .te files.
#
# Usage:
#   bash scripts/build_vscode_vsix.sh [output.vsix]
#
# Default output: dist/typeeasy-debug.vsix
# Requires: node + npx (for @vscode/vsce). 'code' CLI is NOT required to build.
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXT_DIR="$ROOT_DIR/tools/typeeasy-vscode"
LSP_DIR="$ROOT_DIR/tools/typeeasy-lsp"
OUT="${1:-$ROOT_DIR/dist/typeeasy-debug.vsix}"

if [[ ! -d "$EXT_DIR" ]]; then
  echo "ERROR: extension source not found at $EXT_DIR" >&2
  exit 1
fi
if [[ ! -d "$LSP_DIR" ]]; then
  echo "ERROR: LSP source not found at $LSP_DIR" >&2
  exit 1
fi
if ! command -v npx >/dev/null 2>&1; then
  echo "ERROR: 'npx' not found in PATH. Install Node.js (https://nodejs.org)." >&2
  exit 1
fi
if ! command -v npm >/dev/null 2>&1; then
  echo "ERROR: 'npm' not found in PATH. Install Node.js (https://nodejs.org)." >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"

BUNDLED_LSP="$EXT_DIR/lsp"
cleanup() { rm -rf "$BUNDLED_LSP"; }
trap cleanup EXIT

echo "=== Bundling LSP -> $BUNDLED_LSP ==="
rm -rf "$BUNDLED_LSP"
mkdir -p "$BUNDLED_LSP"
# Copy LSP source (server.js + package.json + lockfile). Skip node_modules; we
# install fresh inside the bundled folder to ensure production-only deps.
cp "$LSP_DIR/server.js" "$BUNDLED_LSP/server.js"
cp "$LSP_DIR/package.json" "$BUNDLED_LSP/package.json"
[[ -f "$LSP_DIR/package-lock.json" ]] && cp "$LSP_DIR/package-lock.json" "$BUNDLED_LSP/package-lock.json"

echo "=== npm install (production) in bundled LSP ==="
( cd "$BUNDLED_LSP" && npm install --omit=dev --no-audit --no-fund --silent )

echo "=== npm install (production) in extension ==="
( cd "$EXT_DIR" && npm install --omit=dev --no-audit --no-fund --silent )

# Guard: the .vsix is useless for go-to-definition/references unless the
# language-server + language-client runtime deps actually travel inside it.
# Fail the build loudly here rather than silently shipping a navigation-less
# extension (which is exactly what happened before this check existed).
for dep in \
  "$BUNDLED_LSP/node_modules/vscode-languageserver/package.json" \
  "$BUNDLED_LSP/node_modules/vscode-languageserver-textdocument/package.json" \
  "$EXT_DIR/node_modules/vscode-languageclient/package.json"; do
  if [[ ! -f "$dep" ]]; then
    echo "ERROR: required runtime dependency missing after npm install: $dep" >&2
    echo "       The packaged extension would not provide .te navigation." >&2
    exit 1
  fi
done
if [[ ! -f "$BUNDLED_LSP/server.js" ]]; then
  echo "ERROR: bundled LSP server.js missing at $BUNDLED_LSP/server.js" >&2
  exit 1
fi

echo "=== Packaging VS Code extension from: $EXT_DIR ==="
( cd "$EXT_DIR" && npx --yes @vscode/vsce package \
    --allow-missing-repository --no-yarn --skip-license -o "$OUT" >/dev/null )

echo "VSIX generated: $OUT"
ls -lh "$OUT"
