# ============================================================================
# typeeasy.ps1 — PowerShell wrapper for TypeEasy (Windows)
# Mirrors cli/typeeasy (bash) subcommands.
# ============================================================================
param(
    [Parameter(Position=0)][string]$Command = "help",
    [Parameter(Position=1, ValueFromRemainingArguments=$true)][string[]]$Rest
)

$ErrorActionPreference = "Stop"
$VERSION = "0.1.0"

# --- Locate binary + templates ---
$TYPEEASY_BIN = $env:TYPEEASY_BIN
if (-not $TYPEEASY_BIN) { $TYPEEASY_BIN = "C:\Program Files\TypeEasy\typeeasy-server.exe" }
if (-not (Test-Path $TYPEEASY_BIN)) {
    $local = Join-Path $PSScriptRoot "..\bin\typeeasy_api.exe"
    if (Test-Path $local) { $TYPEEASY_BIN = $local }
}

$TEMPLATES_DIR = $env:TYPEEASY_TEMPLATES
if (-not $TEMPLATES_DIR) {
    foreach ($c in @("C:\Program Files\TypeEasy\templates",
                     (Join-Path $PSScriptRoot "templates"))) {
        if (Test-Path $c) { $TEMPLATES_DIR = $c; break }
    }
}

function Die($msg) { Write-Error "[typeeasy] ERROR: $msg"; exit 1 }
function Info($msg) { Write-Host "[typeeasy] $msg" }

function Require-Project {
    if (-not (Test-Path ".\typeeasy.toml")) {
        Die "Not in a TypeEasy project (no typeeasy.toml). Run 'typeeasy new <name>' first."
    }
}

function Toml-Get($section, $key, $file = ".\typeeasy.toml") {
    if (-not (Test-Path $file)) { return $null }
    $inSection = $false
    foreach ($line in Get-Content $file) {
        if ($line -match "^\s*\[$section\]\s*$") { $inSection = $true; continue }
        if ($line -match "^\s*\[")               { $inSection = $false; continue }
        if ($inSection -and $line -match "^\s*$key\s*=\s*(.+?)\s*$") {
            return $Matches[1].Trim('"')
        }
    }
    return $null
}

function Render-Template($src, $dst, $vars) {
    if (-not (Test-Path $src)) { Die "Template not found: $src" }
    $content = Get-Content -Raw $src
    foreach ($k in $vars.Keys) {
        $content = $content -replace [regex]::Escape("{{$k}}"), [string]$vars[$k]
    }
    Set-Content -NoNewline -Path $dst -Value $content
}

# --- Subcommands ---

function Cmd-New($name) {
    if (-not $name) { Die "Usage: typeeasy new <project-name>" }
    if (Test-Path $name) { Die "Directory '$name' already exists." }
    Info "Creating project '$name' ..."
    $src = Join-Path $TEMPLATES_DIR "project"
    if (-not (Test-Path $src)) { Die "Project templates not found at: $src" }

    Copy-Item -Recurse $src $name
    $vars = @{ PROJECT_NAME = $name }
    Get-ChildItem -Recurse -File -Filter "*.tmpl" $name | ForEach-Object {
        $out = $_.FullName -replace '\.tmpl$', ''
        Render-Template $_.FullName $out $vars
        Remove-Item $_.FullName
    }
    Info "Project '$name' created."
    Write-Host ""
    Write-Host "  cd $name"
    Write-Host "  typeeasy gen resource producto"
    Write-Host "  typeeasy serve"
}

function Cmd-Gen($kind, $name) {
    if (-not $kind -or -not $name) { Die "Usage: typeeasy gen <resource|endpoint> <name>" }
    Require-Project
    $nameCap = $name.Substring(0,1).ToUpper() + $name.Substring(1)
    $vars = @{
        NAME     = $name
        NAME_CAP = $nameCap
        TABLE    = "${name}s"
        DB_HOST  = (Toml-Get database host) ?? "127.0.0.1"
        DB_PORT  = (Toml-Get database port) ?? "3306"
        DB_USER  = (Toml-Get database user) ?? "typeeasy"
        DB_PASS  = (Toml-Get database pass) ?? "typeeasy"
        DB_NAME  = (Toml-Get database name) ?? "meri"
    }
    switch ($kind) {
        "resource" {
            $outTe  = ".\apis\${name}_endpoint.te"
            $stamp  = Get-Date -Format "yyyyMMddHHmmss"
            $outSql = ".\migrations\${stamp}_create_$($vars.TABLE).sql"
            if (Test-Path $outTe) { Die "$outTe already exists." }
            New-Item -ItemType Directory -Force ".\apis","\migrations" | Out-Null
            Render-Template (Join-Path $TEMPLATES_DIR "resource\endpoint.te.tmpl")   $outTe  $vars
            Render-Template (Join-Path $TEMPLATES_DIR "resource\migration.sql.tmpl") $outSql $vars
            Info "Created: $outTe"
            Info "Created: $outSql"
        }
        "endpoint" {
            $outTe = ".\apis\${name}_endpoint.te"
            if (Test-Path $outTe) { Die "$outTe already exists." }
            New-Item -ItemType Directory -Force ".\apis" | Out-Null
            Render-Template (Join-Path $TEMPLATES_DIR "endpoint\endpoint.te.tmpl") $outTe $vars
            Info "Created: $outTe"
        }
        default { Die "Unknown generator: $kind (use 'resource' or 'endpoint')" }
    }
}

function Cmd-Serve {
    Require-Project
    if (-not (Test-Path $TYPEEASY_BIN)) { Die "Server binary not found at $TYPEEASY_BIN" }
    $apis = (Toml-Get server apis_dir) ?? ".\apis"
    $port = (Toml-Get server port)     ?? "8080"
    $host = (Toml-Get server host)     ?? "127.0.0.1"
    Info "Serving $apis at http://${host}:${port} (hot-reload ON)"
    & $TYPEEASY_BIN --api-dir $apis --host $host --port $port --hotreload
}

function Cmd-Install {
    $mode = $Rest[0]
    if ($mode -ne "--nssm") { Die "Use: typeeasy install --nssm" }
    $script = Join-Path $PSScriptRoot "..\installer\windows\install_service_nssm.ps1"
    if (-not (Test-Path $script)) { Die "NSSM installer not found at $script" }
    $port = (Toml-Get server port) ?? "8080"
    $apis = (Toml-Get server apis_dir) ?? (Join-Path (Get-Location) "apis")
    & $script -ApisDir $apis -Port $port
}

# --- ext: install the VS Code extension (colors + debugger) ---
function Cmd-Ext {
    param([string]$Action = "install")
    if ($Action -and $Action -ne "install") { Die "Unknown ext command: $Action (try: typeeasy ext install)" }
    if (-not (Get-Command code -ErrorAction SilentlyContinue)) {
        Die "'code' CLI not found in PATH. Open VS Code -> Ctrl+Shift+P -> 'Shell Command: Install 'code' command in PATH', then retry."
    }
    $vsix = $env:TYPEEASY_VSIX
    if (-not ($vsix -and (Test-Path $vsix))) {
        $vsix = $null
        foreach ($dir in @((Join-Path $PSScriptRoot "..\vscode"),
                           "C:\Program Files\TypeEasy\vscode",
                           "C:\Program Files (x86)\TypeEasy\vscode")) {
            if (Test-Path $dir) {
                $found = Get-ChildItem -Path $dir -Filter *.vsix -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($found) { $vsix = $found.FullName; break }
            }
        }
    }
    if (-not $vsix) { Die "No bundled .vsix found. Reinstall TypeEasy (the package ships the extension)." }
    Info "Installing VS Code extension: $vsix"
    # Uninstall first so VS Code always picks up the new build even if the
    # version number did not change (avoids stale-extension caching).
    & code --uninstall-extension typeeasy.typeeasy-debug 2>$null
    & code --install-extension $vsix --force
    Info "Done. Restart VS Code to activate TypeEasy colors + debugger."
}

function Cmd-Help {
@"
typeeasy $VERSION — CLI for TypeEasy projects (Windows)

USAGE:
  typeeasy new <name>             Create a new project
  typeeasy gen resource <name>    Generate CRUD endpoint + migration
  typeeasy gen endpoint <name>    Generate empty endpoint
  typeeasy serve                  Run dev server (hot-reload)
  typeeasy ext install            Install the VS Code extension (colors + debugger)
  typeeasy install --nssm         Install as Windows Service via NSSM
  typeeasy version

ENVIRONMENT:
  TYPEEASY_BIN        Path to server binary
  TYPEEASY_TEMPLATES  Path to templates dir
"@ | Write-Host
}

# --- Dispatcher ---
switch ($Command) {
    "new"     { Cmd-New $Rest[0] }
    "gen"     { Cmd-Gen $Rest[0] $Rest[1] }
    "serve"   { Cmd-Serve }
    "ext"     { Cmd-Ext $Rest[0] }
    "install" { Cmd-Install }
    "version" { Write-Host "typeeasy $VERSION" }
    "help"    { Cmd-Help }
    default   { Die "Unknown command: $Command" }
}
