@echo off
rem  psweep.cmd -- fix the mask, sweep the threads.
rem
rem  Idea: pin the whole process to one group of cores, then vary how many
rem  threads share exactly those cores. With the mask fixed, nothing can drift
rem  to the other group, so the curve has one variable only.
rem
rem    Part A: mask = P cores only (0x0FFF). 12 is the right count, more is over-subscribed.
rem            The curve should climb, flatten at 12, then fall if 12 really is the ceiling.
rem    Part B: mask = E cores only (0xFF000). Only 8 fit. This is the E-core ceiling.
rem
rem  Assumes locate.cmd said 0-11 = P and 12-19 = E. Change the masks if not.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"
set "MASK_P=0x0FFF"
set "MASK_E=0xFF000"

echo === build ===
gcc -O3 -mavx2 -mfma -fopenmp -Wall -o gemm.exe gemm.c
if errorlevel 1 ( echo BUILD FAILED & pause & exit /b 1 )

echo.
echo ================= PART A: P cores only =================
for %%T in (1 2 3 4 5 6 8 10 12 14 16 20) do call :run "P" %%T %MASK_P%

echo.
echo ================= PART B: E cores only =================
for %%T in (1 2 4 8) do call :run "E" %%T %MASK_E%

echo.
echo   Part A: if the curve turns round after 12, then 12 logical P cores
echo           (6 physical + hyperthreads) really is the ceiling.
echo   Part B: the 8-thread row is what the E cores can do at best.
echo.
pause
exit /b 0

:run
set "OMP_NUM_THREADS=%2"
start /affinity %3 /wait /b gemm.exe 1024 3 > "temp_s.txt" 2>nul
echo.
echo ---- %1 only, %2 threads ----
findstr /C:"v6" /C:"best " "temp_s.txt"
del "temp_s.txt" 2>nul
exit /b 0
