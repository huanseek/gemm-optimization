/* ============================================================================
 *  GEMM 优化项目 —— 骨架 v1
 *
 *  【编码提示】
 *    本文件是 UTF-8 编码，请用 VS Code 打开（Dev-C++ 5.11 对 UTF-8 支持很差，
 *    中文注释会显示成乱码）。所有 printf 输出都用英文，避免控制台编码问题。
 *
 *  【这个文件已经帮你处理好了三件脏活】
 *    1. 准确的计时（Windows 用 QPC，Linux 用 clock_gettime）
 *    2. GFLOPS 自动计算
 *    3. 正确性自检（随机抽查 64 个格子，和直接点积对比）
 *
 *  【你要做的只有一件事】
 *    实现 gemm_v1（把 i-j-k 换成 i-k-j）。
 *
 *  编译：gcc -O2 -o gemm.exe gemm.c
 *  运行：gemm.exe 1024        参数 = 矩阵规模 N
 *        gemm.exe 2048 1      第二个参数 = 重复次数（大 N 时用 1 省时间）
 *
 *  ★ 纪律：每一版跑完，把「版本 / N / 耗时 / GFLOPS / 瓶颈」填进
 *          E:\虚拟C盘\CSP冲刺日程表.xlsx 的「项目·GEMM」页
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- 平台相关的计时 + 对齐内存分配（64 字节，为后面 SIMD 准备） ---------- */
#ifdef _WIN32
#  include <windows.h>
#  include <malloc.h>
static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static void *xmalloc(size_t n) { return _aligned_malloc(n, 64); }
static void  xfree(void *p)    { _aligned_free(p); }
#else
#  include <time.h>
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static void *xmalloc(size_t n) {
    void *p = NULL;
    if (posix_memalign(&p, 64, n) != 0) return NULL;
    return p;
}
static void  xfree(void *p)    { free(p); }
#endif

typedef float data_t;          /* 先用 float：后面测 SIMD 时效果更直观 */

/* ============================================================================
 *  ★★ 编译期自检：这个二进制带优化吗？★★
 *
 *  没带 -O2 的话，v1 会慢 6 倍，你会得出一堆完全错误的结论。
 *  最常见的坑：VS Code 里那个 ▷「运行」三角按钮，默认用的是
 *      gcc -g <文件>            ← 没有 -O2！
 *  正确做法：run.cmd / build.cmd / Ctrl+Shift+B / F5
 * ==========================================================================*/
#ifdef __OPTIMIZE__
#  define BUILT_OPTIMIZED 1
#else
#  define BUILT_OPTIMIZED 0
#endif

/* ============================================================================
 *  v0：朴素 i-j-k          【已完成，作为基线】
 * ==========================================================================*/
void gemm_v0(int N, const data_t *A, const data_t *B, data_t *C) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            data_t s = 0.0f;
            for (int k = 0; k < N; k++)
                s += A[i * N + k] * B[k * N + j];
            C[i * N + j] = s;
        }
    }
}

/* ============================================================================
 *  v1：换序 i-k-j          【★ 留给你写，大概 8 行】
 *
 *  v0 为什么慢：
 *      最内层是 k 循环，访问 B[k*N + j] —— k 每加 1，地址跳 N 个元素。
 *      对 N=1024 的 float 矩阵是跳 4KB = 跨了 64 条 cache line。
 *      所以每算一个乘加就要拉一条新 cache line，命中率极低。
 *
 *  v1 要做的：
 *      最内层换成 j 循环，B[k*N + j] 随 j 连续递增，一条 cache line 能用 16 次。
 *
 *  骨架（先自己写一遍，卡住了再对照）：
 *
 *      memset(C, 0, sizeof(data_t) * (size_t)N * N);
 *      for (int i = 0; i < N; i++) {
 *          for (int k = 0; k < N; k++) {
 *              data_t a = A[i * N + k];          // 取到局部变量，别重复访存
 *              for (int j = 0; j < N; j++)
 *                  C[i * N + j] += a * B[k * N + j];
 *          }
 *      }
 *
 *  写错了没关系，程序会告诉你结果对不对。
 * ==========================================================================*/
void gemm_v1(int N, const data_t *A, const data_t *B, data_t *C) {
    memset(C, 0, sizeof(data_t) * (size_t)N * N);
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            for (int j = 0; j < N; j++) {
                C[i * N + j] += A[i * N + k] * B[k * N + j];
            }
        }
    }
}

/* ============================================================================
 *  正确性自检：随机抽查 64 个格子，直接算点积对比
 * ==========================================================================*/
static int verify(int N, const data_t *A, const data_t *B, const data_t *C, int verbose) {
    int bad = 0;
    double maxerr = 0.0;
    for (int t = 0; t < 64; t++) {
        int i = rand() % N, j = rand() % N;
        double s = 0.0;
        for (int k = 0; k < N; k++)
            s += (double)A[i * N + k] * (double)B[k * N + j];
        double got = (double)C[i * N + j];
        double err = fabs(got - s) / (fabs(s) + 1e-6);
        if (err > maxerr) maxerr = err;
        if (err > 1e-3) {
            bad++;
            if (verbose && bad <= 3)
                printf("        [!] C[%d][%d] expected %.4f, got %.4f\n", i, j, s, got);
        }
    }
    if (verbose) printf("        max relative error = %.2e\n", maxerr);
    return bad == 0;
}

/* ============================================================================
 *  ★ 机器速度探针 —— 每次运行先跑一遍，判断"这次机器状态正常吗"
 *
 *  这是一串纯整数、有严格数据依赖的运算，几乎不碰内存。
 *  所以它的快慢只取决于 CPU 当时跑多快（主频），跟 cache 无关。
 *
 *  用法：同一个版本两次跑出的 GFLOPS 差很多时，先比这个探针值 ——
 *      探针值接近   → 机器状态一样，差异来自代码
 *      探针值差很多 → 机器状态不同（降频 / 后台占用），这组数据不能用，重测
 * ==========================================================================*/
static volatile long long g_probe_sink;   /* 只为了让编译器不能删掉探针循环 */

static double cpu_probe(void) {
    long long x = 1;
    const long long n = 30000000LL;
    double t0 = now_sec();
    for (long long i = 0; i < n; i++)
        x = x * 6364136223846793005LL + 1442695040888963407LL;
    double t1 = now_sec();
    g_probe_sink = x;                        /* 防止整个循环被优化掉 */
    return (double)n / (t1 - t0) / 1e9;      /* 每秒多少 G 次迭代 */
}

/* ============================================================================
 *  计时 + 出数
 * ==========================================================================*/
typedef void (*gemm_fn)(int, const data_t *, const data_t *, data_t *);

static double bench(const char *name, gemm_fn f, int N,
                    const data_t *A, const data_t *B, data_t *C, int reps) {
    /* 先报"我在跑"，否则大 N 时黑屏几分钟，看起来像卡死 */
    printf("  %-10s  N=%-5d  measuring ...\n", name, N);
    fflush(stdout);

    f(N, A, B, C);                              /* 预热：避免首次缺页污染计时 */

    /* ★ 纪律三：正确性优先于性能。
     *   结果不对，一律不测性能、不报 GFLOPS——
     *   因为一个算错的程序跑得再快，也只是一组假数据。            */
    if (!verify(N, A, B, C, 0)) {
        printf("  %-10s  N=%-5d  %10s   %10s   [*** WRONG RESULT - no perf ***]\n",
               name, N, "-", "N/A");
        verify(N, A, B, C, 1);
        return 0.0;
    }

    double best = 1e30;
    for (int r = 0; r < reps; r++) {
        double t0 = now_sec();
        f(N, A, B, C);
        double t1 = now_sec();
        if (t1 - t0 < best) best = t1 - t0;
    }

    double gflops = 2.0 * (double)N * N * N / best / 1e9;
    printf("  %-10s  N=%-5d  %10.2f ms   %10.3f GFLOPS   [OK]\n",
           name, N, best * 1000.0, gflops);
    return gflops;
}

int main(int argc, char **argv) {
    int N    = (argc > 1) ? atoi(argv[1]) : 1024;
    int reps = (argc > 2) ? atoi(argv[2]) : 3;
    if (N < 64) { printf("N must be >= 64\n"); return 1; }

    /* ★ N 大时自动把重复次数降到 1。
     *   N=2048 时 v0 一次就要几十秒，跑 4 遍（1 次预热 + 3 次计时）要三分钟，
     *   看起来就像卡死了。 */
    if (N >= 2048 && reps > 1) {
        printf("  [note] N=%d is slow; forcing reps=1 (was %d)\n", N, reps);
        reps = 1;
    }
    if (N >= 2048)
        printf("  [note] N=%d may take several MINUTES. Please wait.\n", N);

    size_t sz = (size_t)N * N * sizeof(data_t);
    data_t *A = (data_t *)xmalloc(sz);
    data_t *B = (data_t *)xmalloc(sz);
    data_t *C = (data_t *)xmalloc(sz);
    if (!A || !B || !C) { printf("allocation failed (N too large?)\n"); return 1; }

    srand(12345);                                /* 固定种子，保证可复现 */
    for (size_t i = 0; i < (size_t)N * N; i++) {
        A[i] = (data_t)(rand() % 1000) / 1000.0f;
        B[i] = (data_t)(rand() % 1000) / 1000.0f;
    }

    printf("\n=== GEMM benchmark   N=%d   best of %d runs ===\n", N, reps);

#if BUILT_OPTIMIZED
    printf("    build     = optimized (ok)\n");
#else
    printf("\n");
    printf("    **********************************************************\n");
    printf("    *** WARNING: this binary was built WITHOUT optimization ***\n");
    printf("    *** Numbers below are roughly 6x too slow for v1.      ***\n");
    printf("    *** Rebuild with one of these:                         ***\n");
    printf("    ***   run.cmd 1024        (double-click / terminal)    ***\n");
    printf("    ***   Ctrl+Shift+B        (VS Code build task)         ***\n");
    printf("    ***   F5                  (VS Code, uses launch.json)  ***\n");
    printf("    *** Do NOT use the plain Run triangle button.          ***\n");
    printf("    **********************************************************\n\n");
#endif

    printf("    cpu probe = %.3f G iter/s   (low = CPU throttled, discard this run)\n\n",
           cpu_probe());
    printf("  version        size          time          perf            check\n");
    printf("  ---------------------------------------------------------------------\n");
    bench("v0 i-j-k", gemm_v0, N, A, B, C, reps);
    bench("v1 i-k-j", gemm_v1, N, A, B, C, reps);
    printf("\n  >>> copy these two lines into the xlsx perf table\n\n");

    xfree(A); xfree(B); xfree(C);
    return 0;
}
