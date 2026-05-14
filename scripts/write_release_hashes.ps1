param(
    [string]$Version = "0.0.1"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutDir = Join-Path $RepoRoot "dist/windows"
$PkgDir = Join-Path $OutDir "TypeEasy-$Version-win64"
$ZipPath = Join-Path $OutDir "TypeEasy-$Version-win64.zip"
$InstallerPath = Join-Path $OutDir "TypeEasy-Setup-v$Version.exe"
$HashesPath = Join-Path $OutDir "SHA256SUMS-$Version.txt"

if (-not (Test-Path $PkgDir)) {
    throw "No se encontro el directorio de paquete: $PkgDir"
}

if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Compress-Archive -Path (Join-Path $PkgDir "*") -DestinationPath $ZipPath -Force

$Targets = @($ZipPath, $InstallerPath) | Where-Object { Test-Path $_ }
if ($Targets.Count -eq 0) {
    throw "No hay artefactos para calcular hash en: $OutDir"
}

$Lines = foreach ($File in $Targets) {
    $Hash = (Get-FileHash -Algorithm SHA256 -Path $File).Hash.ToLowerInvariant()
    "$Hash  $(Split-Path -Leaf $File)"
}

$Lines | Set-Content -Path $HashesPath -NoNewline:$false

Write-Host "Hashes generados en: $HashesPath"
