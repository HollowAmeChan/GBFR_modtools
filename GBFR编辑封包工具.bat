@echo off
start "" "%SystemRoot%\System32\wscript.exe" //nologo "%~dp0_lib\launch_workspace_builder.vbs" %*
exit /b
