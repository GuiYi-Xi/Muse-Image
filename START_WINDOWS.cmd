@echo off
setlocal
cd /d "%~dp0"
if not exist "GPT_Image_Server.exe" goto missingexe
if not exist "index.html" goto missinghtml
"%~dp0GPT_Image_Server.exe"
if errorlevel 1 goto failed
exit /b 0
:missingexe
echo ERROR: GPT_Image_Server.exe not found.
echo Please extract the ZIP completely before starting.
pause
exit /b 1
:missinghtml
echo ERROR: index.html not found.
echo Please extract the ZIP completely before starting.
pause
exit /b 1
:failed
echo ERROR: The local server failed to start.
echo Check whether ports 8765-8785 are occupied or blocked.
pause
exit /b 1
