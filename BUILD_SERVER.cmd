@echo off
setlocal
cd /d "%~dp0"
where x86_64-w64-mingw32-gcc >nul 2>nul
if errorlevel 1 (
  echo x86_64-w64-mingw32-gcc was not found.
  echo Compile portable_server.c with MinGW-w64 on Linux, or use an equivalent Windows C toolchain.
  pause
  exit /b 1
)
x86_64-w64-mingw32-gcc -O2 -Wall -o GPT_Image_Server.exe portable_server.c -lwinhttp -lws2_32 -lshell32
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo Build succeeded: GPT_Image_Server.exe
pause
