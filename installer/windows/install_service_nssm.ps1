# Instala TypeEasy --api-dir como servicio de Windows usando NSSM.
# Requisitos: NSSM en PATH (https://nssm.cc) y TypeEasy ya instalado.
#
# Uso (PowerShell elevada):
#   .\install_service_nssm.ps1 -ApisDir "C:\TypeEasy\apis" -Port 9001
#   .\install_service_nssm.ps1 -ApisDir "C:\TypeEasy\apis" -Port 9002
#
# Para escalar: instalar varias instancias en puertos distintos y poner
# un IIS / nginx / Azure Front Door delante haciendo load balancing.

param(
    [Parameter(Mandatory=$true)]
    [string]$ApisDir,

    [int]$Port = 9001,

    [string]$BindHost = "127.0.0.1",

    [string]$TypeEasyExe = "C:\Program Files\TypeEasy\typeeasy-server.exe",

    [string]$ServiceName = ""
)

if (-not (Test-Path $TypeEasyExe)) {
    Write-Error "No se encontro typeeasy-server.exe en: $TypeEasyExe"
    exit 1
}
if (-not (Test-Path $ApisDir)) {
    Write-Error "No se encontro el directorio de endpoints: $ApisDir"
    exit 1
}
if (-not (Get-Command nssm -ErrorAction SilentlyContinue)) {
    Write-Error "NSSM no esta en el PATH. Descargar de https://nssm.cc"
    exit 1
}

if ([string]::IsNullOrEmpty($ServiceName)) {
    $ServiceName = "TypeEasyAPI_$Port"
}

Write-Host "Instalando servicio: $ServiceName" -ForegroundColor Cyan

$logDir = "C:\ProgramData\TypeEasy\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$args = "--api-dir `"$ApisDir`" --host $BindHost --port $Port"

nssm install $ServiceName $TypeEasyExe $args
nssm set $ServiceName AppStdout "$logDir\$ServiceName.out.log"
nssm set $ServiceName AppStderr "$logDir\$ServiceName.err.log"
nssm set $ServiceName AppRotateFiles 1
nssm set $ServiceName AppRotateBytes 10485760
nssm set $ServiceName Start SERVICE_AUTO_START
nssm set $ServiceName AppRestartDelay 2000

Start-Service $ServiceName
Get-Service $ServiceName

Write-Host ""
Write-Host "Servicio instalado y corriendo en http://${BindHost}:${Port}" -ForegroundColor Green
Write-Host "Logs: $logDir"
Write-Host ""
Write-Host "Comandos utiles:"
Write-Host "  Restart-Service $ServiceName"
Write-Host "  Stop-Service    $ServiceName"
Write-Host "  nssm remove     $ServiceName confirm   # desinstalar"
