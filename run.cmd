@echo off
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"
set "REPS=%~2"
if "%REPS%"=="" set "REPS=3"

echo === Build: gcc -O2 -o gemm.exe gemm.c ===
gcc -O2 -Wall -o gemm.exe gemm.c
if errorlevel 1 (
  echo.
  echo ########## BUILD FAILED ##########
  echo Fix all errors above first, then run this again.
  pause
  exit /b 1
)

echo Build OK.
echo.
gemm.exe %N% %REPS%
echo.
pause