@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-HulkPC-Probe.ps1" -GameDir "%~1"
if errorlevel 1 (echo Installation was not completed.)
pause
