@echo off
chcp 65001 >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0GBFR_WorkspaceBuilder.ps1" "%~1"
