@echo off
rem  memshare.cmd -- how much of the time does memory access cost?
rem  All four variants do the same N^3 multiply-adds; only the memory differs.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"

echo === Build: gcc -O3 -mavx2 -mfma -Wall -o memshare.exe memshare.c ===
gcc -O3 -mavx2 -mfma -Wall -o memshare.exe memshare.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

memshare.exe %N%
echo.
echo   Calibration:  full should land near the v3 number (~51 GFLOPS).
echo                 none should land near the measured peak (~140 GFLOPS).
echo   Memory share = (full_time - none_time) / full_time
echo.
pause