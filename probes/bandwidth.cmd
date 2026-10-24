@echo off
rem  bandwidth.cmd -- what is this machine's actual memory bandwidth?
rem  read uses 8 independent vector accumulators (a single one would be
rem  latency-bound and read about a third of the truth).
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

echo === Build: gcc -O2 -mavx2 -Wall -o bandwidth.exe bandwidth.c ===
gcc -O2 -mavx2 -Wall -o bandwidth.exe bandwidth.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

bandwidth.exe
echo   How to read it:
echo     read      the cleanest number - this is the memory ceiling
echo     copy      read + write, counted together
echo     write     normal stores, so the real DRAM traffic is twice this
echo     stream    inflated (write buffer absorbs it) - upper bound only
echo.
echo   Close everything else first. My own processes inflate every number.
echo.
pause