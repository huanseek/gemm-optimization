@echo off
rem  cache.cmd -- measure the real capacity of each cache level.
rem  Pointer chasing over a random permutation ring.
rem  Compiled with -O0 on purpose: at -O2 GCC deletes the whole chase loop.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

echo === Build: gcc -O0 -Wall -o cache.exe cache.c ===
gcc -O0 -Wall -o cache.exe cache.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

cache.exe
echo.
echo   How to read it:
echo     flat ~4-5 cycles      -> still in L1
echo     first step up         -> past L1   (expect near 48 KB)
echo     second step up        -> past L2   (expect near 1.25 MB)
echo     third step up         -> past L3   (expect near 24 MB)
echo.
echo   Close everything else first. Background load inflates every number.
echo.
pause