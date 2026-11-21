@echo off
rem  locate.cmd -- which logical CPU is a P-core and which is an E-core
rem
rem  OMP_PROC_BIND does not work here (libgomp says affinity not supported),
rem  but Windows can still pin a whole PROCESS. OpenMP threads inherit that.
rem  So: run one thread, pin it to a single logical CPU, and look at GFLOPS.
rem     P-core  -> about 120
rem     E-core  -> about 50
rem
rem  We only check 8 of the 20 CPUs. That is enough to find where the boundary is.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"
set "OMP_NUM_THREADS=1"

echo === build ===
gcc -O3 -mavx2 -mfma -fopenmp -Wall -o gemm.exe gemm.c
if errorlevel 1 ( echo BUILD FAILED & pause & exit /b 1 )

echo.
echo  cpu   mask      v6 GFLOPS
echo  --------------------------
call :one 0  0x1
call :one 3  0x8
call :one 6  0x40
call :one 9  0x200
call :one 12 0x1000
call :one 14 0x4000
call :one 16 0x10000
call :one 18 0x40000

echo.
echo   ~120 means P-core, ~50 means E-core. Note where it flips.
echo.
pause
exit /b 0

:one
start /affinity %2 /wait /b gemm.exe 1024 1 > "temp_pin.txt" 2>nul
set "LINE="
for /f "tokens=*" %%L in ('findstr /C:"v6" "temp_pin.txt"') do set "LINE=%%L"
if "%LINE%"=="" ( echo  %1  %2   failed ) else ( echo  %1  %2   %LINE% )
del "temp_pin.txt" 2>nul
exit /b 0
