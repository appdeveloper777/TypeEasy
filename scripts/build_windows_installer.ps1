param(
    [string]$Version = "0.0.1",
    [string]$SourceDir = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$IssPath = Join-Path $RepoRoot "installer/windows/TypeEasy.iss"

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $RepoRoot "dist/windows/TypeEasy-$Version-win64"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $RepoRoot "dist/windows"
}

if (-not (Test-Path $IssPath)) {
    throw "No se encontro el script de Inno Setup: $IssPath"
}

$BinaryPath = Join-Path $SourceDir "bin/typeeasy-bin.exe"
if (-not (Test-Path $BinaryPath)) {
    throw "No se encontro el binario en el paquete: $BinaryPath"
}

$Iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $Iscc) {
    throw "No se encontro 'iscc'. Instala Inno Setup y vuelve a ejecutar."
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

& $Iscc.Source `
    $IssPath `
    "/DAppVersion=$Version" `
    "/DSourceDir=$SourceDir" `
    "/DOutputDir=$OutputDir"

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup finalizo con codigo $LASTEXITCODE"
}

Write-Host "Instalador generado en: $OutputDir"
