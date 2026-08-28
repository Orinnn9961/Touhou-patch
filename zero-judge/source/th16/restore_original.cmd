@echo off
setlocal
set "TARGET=%~1"
if not defined TARGET set "TARGET=%~dp0th16.exe"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_th16.ps1" -Action Restore -GameExe "%TARGET%"
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Restore failed. See the error above.
pause
exit /b %RESULT%
