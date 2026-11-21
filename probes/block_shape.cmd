@echo off
rem  block_shape.cmd -- can a wider block squeeze the last 17% out of one core?
rem  Three shapes, same multiply-add count, only the block shape changes.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"

echo === Build: gcc -O3 -mavx2 -mfma -Wall -o block_shape.exe block_shape.c ===
gcc -O3 -mavx2 -mfma -Wall -o block_shape.exe block_shape.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

block_shape.exe %N%
echo   How to read it:
echo     4x16 = v5 today,  8 accumulators
echo     6x16 = 12 accumulators, fewer A loads per FMA
echo     4x24 = 12 accumulators, fewest loads per FMA
echo.
echo   If 4x24 wins clearly, the load port was the last limit, and v6 can take it.
echo   If nothing beats 4x16, single core is done at about 83 percent of peak.
echo.
pause