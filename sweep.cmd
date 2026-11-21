@echo off
rem  sweep.cmd -- how many threads before the speedup flattens out
rem
rem  N=1024, not 2048. Reason: N=1024 needs 12 MB of working set, which fits
rem  inside the 24 MB L3. N=2048 needs 48 MB, so it spills to DRAM and every
rem  run gets a different page layout / bank state. That is why the big size
rem  looked unstable -- it was not the machine being flaky.
rem
rem  Only looks at v6 (OpenMP). Reps = 3, so each row also shows its spread.
rem  Threads are pinned, otherwise the OS decides which core gets what.
cd /d "%~dp0"
set "PATH=D:\c-compiler\mingw64\bin;%PATH%"

echo === Build: gcc -O3 -mavx2 -mfma -fopenmp -Wall -o gemm.exe gemm.c ===
gcc -O3 -mavx2 -mfma -fopenmp -Wall -o gemm.exe gemm.c
if errorlevel 1 (
  echo BUILD FAILED
  pause
  exit /b 1
)

set "OMP_PROC_BIND=true"
set "OMP_PLACES=threads"

echo.
echo ==== thread sweep, N=1024, best of 3 ====
echo.

for %%T in (1 2 3 4 5 6 8 12 16 20) do (
  set "OMP_NUM_THREADS=%%T"
  echo ---------- threads = %%T ----------
  gemm.exe 1024 3 | findstr /C:"v6" /C:"best " /C:"probe"
)

echo.
echo   Read it:
echo     v6 line  -^> the GFLOPS to compare across thread counts
echo     best line -^> spread. Over 10 percent means rerun that row.
echo     probe    -^> if it swings more than 5 percent, rerun that row.
echo.
echo   Where the curve bends is where the P-cores run out.
echo   If it keeps climbing past 12, the E-cores are still adding something.
echo.
pause
