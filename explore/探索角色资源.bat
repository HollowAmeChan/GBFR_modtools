@echo off
chcp 65001 >nul 2>&1
echo --- GBFR 角色资源探索 ---
echo 将 .minfo 文件拖入此窗口，或直接传入路径
echo.
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0explore_char.ps1" "%~1"
echo --- done (exit %ERRORLEVEL%) ---
pause
