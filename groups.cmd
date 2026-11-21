@echo off
rem  groups.cmd -- run the three groups and compare
rem
rem  Needs that you already know the P/E boundary (run locate.cmd first).
rem  Default assumption: logical 0-11 = P cores, 12-19 = E cores.
rem  If locate.cmd said otherwise, change the two masks below.
rem
rem    P cores: 0x0FFF    (12 logical = 6 physical + hyperthreads)
rem    E cores: 0xFF000   (8 logical)
rem    all    : 0xFFFFF   (20 logical)
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

set "MASK_P=0x0FFF"
set "MASK_E=0xFF000"
set "MASK_A=0xFFFFF"

echo === build ===
gcc -O3 -mavx2 -mfma -fopenmp -Wall -o gemm.exe gemm.c
if errorlevel 1 ( echo BUILD FAILED & pause & exit /b 1 )

call :grp "P only,  6 threads " 6  %MASK_P%
call :grp "P only, 12 threads " 12 %MASK_P%
call :grp "E only,  8 threads " 8  %MASK_E%
call :grp "all,    20 threads " 20 %MASK_A%

echo.
echo   Now you have four clean numbers with nothing migrating around:
echo     P-only 6   -^> the honest multi-core ceiling of the P cores
echo     P-only 12  -^> how much hyperthreading helps AVX2  (probably not much)
echo     E-only 8   -^> what the E cores can do at all
echo     all 20     -^> what the OS default gives you
echo.
pause
exit /b 0

:grp
set "OMP_NUM_THREADS=%2"
echo.
echo ---------- %1  mask=%3 ----------
start /affinity %3 /wait /b gemm.exe 1024 3 > "temp_grp.txt" 2>nul
findstr /C:"v6" /C:"best " /C:"probe" "temp_grp.txt"
del "temp_grp.txt" 2>nul
exit /b 0
