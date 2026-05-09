# Installs the TypeEasy VS Code extension (syntax highlighting + debugger).
# Run once after cloning.  Usage:  powershell -ExecutionPolicy Bypass -File scripts\install_vscode_extension.ps1
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ExtDir = Join-Path $ScriptDir "..\tools\typeeasy-vscode"

if (-not (Get-Command code -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: 'code' CLI not in PATH." -ForegroundColor Red
    Write-Host "Open VS Code -> Ctrl+Shift+P -> 'Shell Command: Install code command in PATH'"
    exit 1
}

Push-Location $ExtDir
try {
    Write-Host "Packaging extension..."
    npx --yes @vscode/vsce package --allow-missing-repository --no-yarn --skip-license -o $env:TEMP\typeeasy-debug.vsix | Out-Null
    Write-Host "Installing extension..."
    code --install-extension $env:TEMP\typeeasy-debug.vsix --force
    Remove-Item $env:TEMP\typeeasy-debug.vsix -ErrorAction SilentlyContinue
    Write-Host "`nDone. Restart VS Code to activate TypeEasy syntax highlighting." -ForegroundColor Green
}
finally {
    Pop-Location
}
