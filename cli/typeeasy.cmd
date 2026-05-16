@echo off
REM ============================================================================
REM typeeasy.cmd — Windows launcher for the TypeEasy CLI (uses Git Bash)
REM
REM Install: copy this file (or add this folder) to your PATH so that you can
REM run `typeeasy` from anywhere.
REM
REM Requires: Git for Windows (https://git-scm.com/download/win).
REM Explicitly avoids WSL's bash, which can't read Windows-style paths.
REM ============================================================================
setlocal enabledelayedexpansion

set "CLI_DIR=%~dp0"
REM Strip trailing backslash
if "%CLI_DIR:~-1%"=="\" set "CLI_DIR=%CLI_DIR:~0,-1%"

set "CLI_SCRIPT=%CLI_DIR%\typeeasy"
set "TYPEEASY_TEMPLATES=%CLI_DIR%\templates"

REM --- Find Git Bash (NOT WSL bash) ---
set "GITBASH="
if exist "%ProgramFiles%\Git\bin\bash.exe"        set "GITBASH=%ProgramFiles%\Git\bin\bash.exe"
if exist "%ProgramFiles(x86)%\Git\bin\bash.exe"   set "GITBASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if exist "%LocalAppData%\Programs\Git\bin\bash.exe" set "GITBASH=%LocalAppData%\Programs\Git\bin\bash.exe"

if "%GITBASH%"=="" (
    echo [typeeasy] ERROR: Git Bash not found.
    echo                   Install Git for Windows: https://git-scm.com/download/win
    exit /b 1
)

REM --- Convert Windows path to POSIX (C:\foo\bar  -^>  /c/foo/bar) ---
set "POSIX_SCRIPT=%CLI_SCRIPT::=%"
set "POSIX_SCRIPT=/!POSIX_SCRIPT:\=/!"

set "POSIX_TEMPLATES=%TYPEEASY_TEMPLATES::=%"
set "POSIX_TEMPLATES=/!POSIX_TEMPLATES:\=/!"

REM --- Invoke Git Bash with POSIX path + forward all arguments ---
"%GITBASH%" -c "TYPEEASY_TEMPLATES='%POSIX_TEMPLATES%' '%POSIX_SCRIPT%' %*"
endlocal
