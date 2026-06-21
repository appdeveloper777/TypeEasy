#!/usr/bin/env bash
# Installs the TypeEasy VS Code extension (syntax highlighting + debugger)
# into the user's local VS Code. Run once after cloning.
set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
EXT_DIR="$SCRIPT_DIR/../tools/typeeasy-vscode"

if ! command -v code >/dev/null 2>&1; then
  echo "ERROR: 'code' CLI not found in PATH."
  echo "Open VS Code -> Ctrl+Shift+P -> 'Shell Command: Install code command in PATH'"
  exit 1
fi

cd "$EXT_DIR"
echo "Packaging extension..."
npx --yes @vscode/vsce package --allow-missing-repository --no-yarn --skip-license -o /tmp/typeeasy-debug.vsix >/dev/null
echo "Installing extension..."
# Uninstall first so VS Code always picks up the new build even if the
# version number did not change (avoids stale-extension caching).
code --uninstall-extension typeeasy.typeeasy-debug >/dev/null 2>&1 || true
code --install-extension /tmp/typeeasy-debug.vsix --force
rm -f /tmp/typeeasy-debug.vsix
echo
echo "Done. Restart VS Code to activate TypeEasy syntax highlighting."
