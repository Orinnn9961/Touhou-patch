@echo off
setlocal

rem The managed development shell can contain both Path and PATH. MSBuild's
rem environment importer rejects that duplicate, so normalize it first.
set "Path="
set "PATH="
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

set "COOP_CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"%COOP_CMAKE%" -S "%~dp0." -B "%~dp0build-gui" -G "Visual Studio 17 2022" -A Win32
if errorlevel 1 exit /b %errorlevel%

"%COOP_CMAKE%" --build "%~dp0build-gui" --config Release
exit /b %errorlevel%
