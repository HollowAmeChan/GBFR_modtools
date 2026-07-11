@echo off
chcp 65001 >nul 2>&1
if "%~1"=="" (
    echo Drag a .gts file onto this script to run.
    pause & exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_step2_extract_tex.ps1" "%~1"
pause >nul
