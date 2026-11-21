# -*- coding: utf-8 -*-
"""blas_bench.py -- 拿现成的 BLAS 当参照物

用法（一次只测一个线程数）：
    python blas_bench.py 1
    python blas_bench.py 20

为什么一次只测一个：
    BLAS 在 numpy 被 import 的那一刻就已经初始化好了，之后再改环境变量没用。
    所以线程数必须在 import numpy 之前设进环境变量 —— 也就是说，一个进程只能测一个线程数。
"""
import os, sys, time, io, contextlib

threads = sys.argv[1] if len(sys.argv) > 1 else "1"
for k in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
          "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ[k] = threads

import numpy as np

# 抓一下 numpy 背后是哪个 BLAS。只要那一行，别把整份配置打出来。
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    try:
        np.show_config()
    except Exception:
        pass
config = buf.getvalue()
blas = ""
for line in config.splitlines():
    if "openblas configuration" in line.lower():
        blas = line.split(":", 1)[1].strip().strip('",')
        break

print("threads : %s" % threads)
print("numpy   : %s" % np.__version__)
print("BLAS    : %s" % (blas if blas else "(not recognised, see np.show_config)"))
print()
print("  N         time(ms)      GFLOPS")
print("  -------------------------------")

REPS = 7
for N in (1024, 2048):
    a = np.random.rand(N, N).astype(np.float32)
    b = np.random.rand(N, N).astype(np.float32)
    best = 1e9
    for _ in range(REPS):
        t0 = time.perf_counter()
        c = a @ b
        t1 = time.perf_counter()
        if t1 - t0 < best:
            best = t1 - t0
    print("  %-8d %8.2f   %9.2f" % (N, best * 1000, 2.0 * N**3 / best / 1e9))
    del a, b, c
print()
