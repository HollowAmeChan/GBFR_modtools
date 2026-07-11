@echo off
chcp 65001 >nul 2>&1
if "%~1"=="" (
    echo Drag a .minfo file onto this script to run.
    pause & exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_step1_parse_mmat.ps1" "%~1"
echo.
echo --- done (exit %ERRORLEVEL%) ---
pause
