@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-HulkBridge.ps1" -GameDir "%~1"
if errorlevel 1 (echo HulkBridge installation was refused; no original game files changed.)
pause
