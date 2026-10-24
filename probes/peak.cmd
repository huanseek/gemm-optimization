@echo off
rem ==========================================================
rem  peak.cmd -- measure the real single-core FMA ceiling.
rem  Works on any x86 machine with AVX2 + FMA.
rem  usage:  peak.cmd            (default 500 million iterations)
rem          peak.cmd 200000000  (shorter run)
rem ==========================================================
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=500000000"

echo === Build: gcc -O2 -mavx2 -mfma -Wall -o peak.exe peak.c ===
gcc -O2 -mavx2 -mfma -Wall -o peak.exe peak.c
if errorlevel 1 (
  echo.
  echo ########## BUILD FAILED ##########
  pause
  exit /b 1
)

peak.exe %N%
echo.
echo   Read it like this:
echo     8 accumulators  = the ceiling (expect ~140 on this laptop)
echo     4 accumulators  = should be half  (stuck on FMA latency)
echo     16 accumulators = should be half  (spilled out of the 16 YMM registers)
echo.
pause
