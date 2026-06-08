#!/usr/bin/env bash
# ============================================================================
# build_vscode_vsix.sh — Package the TypeEasy VS Code extension into a .vsix.
# ----------------------------------------------------------------------------
# The .vsix produced here is the artifact shipped inside the Windows installer
# and the Linux .deb/tarball so that a brand-new user can install the
# extension (syntax highlighting + native debugger) with a single command:
#
#     te ext install
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
OUT="${1:-$ROOT_DIR/dist/typeeasy-debug.vsix}"

if [[ ! -d "$EXT_DIR" ]]; then
  echo "ERROR: extension source not found at $EXT_DIR" >&2
  exit 1
fi
if ! command -v npx >/dev/null 2>&1; then
  echo "ERROR: 'npx' not found in PATH. Install Node.js (https://nodejs.org)." >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"

# Resolve OUT to an absolute path: vsce runs after `cd "$EXT_DIR"`, so a
# relative -o would be created inside the extension dir instead of the CWD.
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"

echo "Packaging VS Code extension from: $EXT_DIR"
( cd "$EXT_DIR" && npx --yes @vscode/vsce package \
    --allow-missing-repository --no-yarn --skip-license -o "$OUT" >/dev/null )

echo "VSIX generated: $OUT"
