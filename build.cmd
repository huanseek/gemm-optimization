@echo off
rem ==========================================================
rem  build.cmd -- used by VS Code tasks. No pause at the end.
rem  usage:  build.cmd 1024   compile and run N=1024
rem          build.cmd 0      compile only
rem ==========================================================
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"
set "REPS=%~2"
if "%REPS%"=="" set "REPS=3"

rem  Compile standard, fixed 2026-10-11. See the experiment log, section 0.
rem    -O3     : the compiler vectorizes on its own at -O3 (it does not at -O2)
rem    -mavx2  : 256-bit vectors (ymm, 8 floats)
rem    -mfma   : fused multiply-add
gcc -O3 -mavx2 -mfma -Wall -o gemm.exe gemm.c
if errorlevel 1 (
  echo.
  echo ===== BUILD FAILED =====
  exit /b 1
)

if "%N%"=="0" (
  echo Build OK.
  exit /b 0
)

gemm.exe %N% %REPS%