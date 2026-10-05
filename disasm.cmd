@echo off
rem ==========================================================
rem  disasm.cmd -- dump gemm.exe into human-readable assembly
rem
rem  usage:  double-click it, or run  disasm.cmd  in a terminal
rem          disasm.cmd exp_alias    (dump exp_alias.exe instead)
rem
rem  It writes asm.txt and opens it in Notepad.
rem  Press Ctrl+F and search for the function you want to read,
rem  for example:   gemm_v1
rem ==========================================================
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=gemm"

if not exist "%TARGET%.exe" (
  echo.
  echo  %TARGET%.exe not found.
  echo  Build it first, for example:   run.cmd 1024
  echo.
  pause
  exit /b 1
)

echo Disassembling %TARGET%.exe ...
objdump -d -Mintel "%TARGET%.exe" > asm.txt
if errorlevel 1 (
  echo.
  echo ########## DISASSEMBLE FAILED ##########
  pause
  exit /b 1
)

echo Done -^> asm.txt
echo.
echo In Notepad: press Ctrl+F and search for   gemm_v1
echo.
start notepad asm.txt
