@echo off
rem  v4cost.cmd -- which memory stream is the tightest in v4?
rem  Four variants, same multiply-add count, one memory stream removed each time.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"

echo === Build: gcc -O3 -mavx2 -mfma -Wall -o v4cost.exe v4cost.c ===
gcc -O3 -mavx2 -mfma -Wall -o v4cost.exe v4cost.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

v4cost.exe %N%
echo   How to read it:
echo     full / no_store   = what WRITING C costs
echo     full / no_load    = what READING C costs
echo     the biggest ratio is the tightest resource
echo.
echo   Close everything else first.
echo.
pause