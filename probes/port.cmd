@echo off
rem  port.cmd -- how wide is this core's L1? read vs write.
rem  Run this from the project folder:  probes\port.cmd
rem  Two numbers only, load and copy. Everything fits in L1.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

echo === Build: gcc -O3 -mavx2 -mfma -Wall -o port.exe port.c ===
gcc -O3 -mavx2 -mfma -Wall -o port.exe port.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

port.exe
echo.
echo   Expect roughly:
echo     load  ~ 2.0 vectors/cycle   (64 B/cycle read)
echo     copy  ~ 1.0 vectors/cycle   (32 B/cycle write is the limit)
echo.
echo   Close everything else first. Run it twice and compare.
echo.
pause