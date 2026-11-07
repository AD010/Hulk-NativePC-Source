@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
 echo ERROR: Visual Studio 2022 Build Tools with C++/LLVM are required.
 echo Install Visual Studio Build Tools and the LLVM Clang toolset.
 pause
 exit /b 1
)
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (echo ERROR: Visual Studio not found & pause & exit /b 1)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo ERROR: vcvars64 failed & pause & exit /b 1)
set "PATH=%VSPATH%\VC\Tools\Llvm\x64\bin;%PATH%"
where clang++ >nul 2>&1
if errorlevel 1 (echo ERROR: LLVM Clang is missing from VS installation & pause & exit /b 1)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BUILD_WINDOWS.ps1" %*
set "RC=%errorlevel%"
if not "%RC%"=="0" echo Build failed with code %RC%.
pause
exit /b %RC%
