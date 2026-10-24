@echo off
rem ==========================================================
rem  run_exp.cmd -- build and run the controlled experiment
rem  usage:  run_exp.cmd 1024      compile and run N=1024
rem          run_exp.cmd 1024 3    also set repeat count
rem          run_exp.cmd 0         compile only
rem ==========================================================
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "N=%~1"
if "%N%"=="" set "N=1024"
set "REPS=%~2"
if "%REPS%"=="" set "REPS=3"

echo === Build: gcc -O2 -Wall -o exp_alias.exe exp_alias.c ===
gcc -O2 -Wall -o exp_alias.exe exp_alias.c
if errorlevel 1 (
  echo.
  echo ########## BUILD FAILED ##########
  pause
  exit /b 1
)
echo Build OK.
echo.

if "%N%"=="0" exit /b 0

exp_alias.exe %N% %REPS%
echo.
pause
