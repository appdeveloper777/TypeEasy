@echo off
REM ============================================================================
REM typeeasy.cmd — Smart dispatcher for TypeEasy on Windows.
REM
REM Routes:
REM   typeeasy new|gen|serve|migrate|console|version|help [...]
REM       -> Rails-style CLI (bash script in ..\cli\typeeasy, runs in Git Bash)
REM
REM   typeeasy <archivo.te>   or any --flag
REM       -> direct interpreter (typeeasy-bin.exe in the same folder)
REM
REM This file lives at {app}\bin\typeeasy.cmd and is the entry point on PATH.
REM ============================================================================
setlocal enabledelayedexpansion

set "BIN_DIR=%~dp0"
if "%BIN_DIR:~-1%"=="\" set "BIN_DIR=%BIN_DIR:~0,-1%"
set "APP_DIR=%BIN_DIR%\.."
set "INTERP=%BIN_DIR%\typeeasy-bin.exe"

REM --- No args: show help from interpreter ---
if "%~1"=="" (
    "%INTERP%" --help
    exit /b %ERRORLEVEL%
)

REM --- Rails-style subcommands: dispatch to bash CLI ---
set "SUB=%~1"
if /I "%SUB%"=="new"     goto :cli
if /I "%SUB%"=="gen"     goto :cli
if /I "%SUB%"=="generate" goto :cli
if /I "%SUB%"=="serve"   goto :cli
if /I "%SUB%"=="docs"    goto :cli
if /I "%SUB%"=="migrate" goto :cli
if /I "%SUB%"=="console" goto :cli
if /I "%SUB%"=="install" goto :cli
if /I "%SUB%"=="deploy"  goto :cli
if /I "%SUB%"=="logs"    goto :cli
if /I "%SUB%"=="status"  goto :cli
if /I "%SUB%"=="start"   goto :cli
if /I "%SUB%"=="stop"    goto :cli
if /I "%SUB%"=="restart" goto :cli
if /I "%SUB%"=="version" goto :cli
if /I "%SUB%"=="help"    goto :cli

REM --- Everything else: passthrough to interpreter ---
"%INTERP%" %*
exit /b %ERRORLEVEL%

:cli
REM --- Locate Git Bash ---
set "GITBASH="
if exist "%ProgramFiles%\Git\bin\bash.exe"           set "GITBASH=%ProgramFiles%\Git\bin\bash.exe"
if exist "%ProgramFiles(x86)%\Git\bin\bash.exe"      set "GITBASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if exist "%LocalAppData%\Programs\Git\bin\bash.exe"  set "GITBASH=%LocalAppData%\Programs\Git\bin\bash.exe"

if "%GITBASH%"=="" (
    echo [typeeasy] ERROR: Git Bash no encontrado.
    echo                   Instala Git for Windows: https://git-scm.com/download/win
    echo                   Subcomandos Rails ^(new/gen/serve/etc.^) requieren Git Bash.
    echo                   Modo single-file directo SI funciona: typeeasy --api archivo.te
    exit /b 1
)

REM --- Resolve CLI script path ---
set "CLI_SCRIPT=%APP_DIR%\cli\typeeasy"
set "CLI_TEMPLATES=%APP_DIR%\cli\templates"
if not exist "%CLI_SCRIPT%" (
    echo [typeeasy] ERROR: no se encuentra %CLI_SCRIPT%
    echo                   La instalacion esta incompleta. Reinstala TypeEasy.
    exit /b 1
)

REM --- Convert Windows -> POSIX paths for Git Bash ---
set "POSIX_SCRIPT=%CLI_SCRIPT::=%"
set "POSIX_SCRIPT=/!POSIX_SCRIPT:\=/!"
set "POSIX_TEMPLATES=%CLI_TEMPLATES::=%"
set "POSIX_TEMPLATES=/!POSIX_TEMPLATES:\=/!"
set "POSIX_INTERP=%INTERP::=%"
set "POSIX_INTERP=/!POSIX_INTERP:\=/!"

REM --- Invoke with env vars + all args ---
"%GITBASH%" -c "TYPEEASY_BIN='%POSIX_INTERP%' TYPEEASY_TEMPLATES='%POSIX_TEMPLATES%' '%POSIX_SCRIPT%' %*"
exit /b %ERRORLEVEL%
