@echo off
setlocal
set "TARGET=%~1"
if not defined TARGET set "TARGET=%~dp0th11.exe"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_th11.ps1" -Action Apply -GameExe "%TARGET%"
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Patch failed. See the error above.
pause
exit /b %RESULT%
