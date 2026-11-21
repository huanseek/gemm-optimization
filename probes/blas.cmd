@echo off
rem  blas.cmd -- compare against OpenBLAS in the same sitting
rem
rem  All text in this file must stay pure ASCII.
rem  cmd.exe reads .cmd files in the ANSI codepage; UTF-8 Chinese turns into
rem  garbage and can even split a line into bogus commands.
rem
rem  Why everything runs back to back: the machine drifts. Ten minutes apart,
rem  the same code can differ by 5 percent, and then the ratio means nothing.
rem
rem  Why we set the thread count by hand: BLAS is initialised the moment numpy
rem  is imported. Changing the environment afterwards has no effect, so each
rem  process measures exactly one thread count.
cd /d "%~dp0"
set "PY=C:\Users\Lenovo\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
if not exist "%PY%" set "PY=python"

echo ==================================================
echo   PART 1  OpenBLAS, 1 thread
echo ==================================================
set "OMP_NUM_THREADS=1"
set "OPENBLAS_NUM_THREADS=1"
"%PY%" blas_bench.py 1

echo.
echo ==================================================
echo   PART 2  OpenBLAS, 20 threads
echo ==================================================
set "OMP_NUM_THREADS=20"
set "OPENBLAS_NUM_THREADS=20"
"%PY%" blas_bench.py 20

echo.
echo ==================================================
echo   PART 3  my own gemm, right after, same conditions
echo ==================================================
cd ..
call run.cmd 1024 3

echo.
echo   How to read it:
echo     PART 1, N=1024   vs   gemm v5 (single core)
echo     PART 2, N=1024   vs   gemm v6 (20 threads)
echo.
echo   Within 1.05x is a tie. If it is worse than that, first check the two
echo   cpu probe lines inside the gemm run. Over 5 percent apart means rerun.
echo   Also skip any row whose spread is over 10 percent.
echo.
pause
