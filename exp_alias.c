/* ============================================================================
 *  对照实验：cache 组冲突（aliasing）—— padding 能救回多少？
 *
 *  【这个文件是干什么的】
 *
 *    gemm.c 里的 v0 在 N=1024 时崩溃（0.95 GFLOPS，而 N=512 时是 3.66）。
 *    有两个假设都能解释它：
 *
 *      A. 容量不足 —— 工作集 12 MB，超过 L2 的 1.25 MB
 *      B. 组冲突   —— 行跨度 4096 B 正好是 2 的幂，1024 行挤进 L1 的同一组
 *
 *    这个实验用来把 A 和 B 拆开：
 *
 *      ★ 算法一个字不改，只把行跨度从 N 改成 N + PAD ★
 *
 *    数据总量几乎没变（只多了 PAD×N 个 float，占 0.1%）。
 *    所以如果性能恢复 → 不可能是容量问题 → 是 B（组冲突）。
 *
 *  【已知结果】N=1024
 *      v0       跨度 N      2103.30 ms     1.021 GFLOPS
 *      v0+pad   跨度 N+4     465.93 ms     4.609 GFLOPS     ← 4.5×
 *
 *  【编译运行】
 *      gcc -O2 -o exp_alias.exe exp_alias.c
 *      exp_alias.exe 1024          （参数 = 规模 N）
 *      exp_alias.exe 1024 3        （第二个参数 = 重复次数）
 *
 *  ★ 改 PAD 试别的值：改下面的 #define，重新编译。
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- 唯一需要改的参数：每行末尾多留几个 float ---------- */
#define PAD 4

/* ---------- 平台相关的计时 + 对齐内存分配 ---------- */
#ifdef _WIN32
#  include <windows.h>
#  include <malloc.h>
#  include <io.h>
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
#  include <unistd.h>
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

typedef float data_t;

/* ============================================================================
 *  对照组 A：v0 原样 —— 跨度 = N
 *  ★ 这是基线，不要动它。改了就没法对比了。
 * ==========================================================================*/
void gemm_v0(int N, const data_t *A, const data_t *B, data_t *C) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            data_t s = 0.0f;
            for (int k = 0; k < N; k++)
                s += A[(size_t)i * N + k] * B[(size_t)k * N + j];
            C[(size_t)i * N + j] = s;
        }
    }
}

/* ============================================================================
 *  实验组 B：v0 + padding —— 跨度 = N + PAD
 *  算法与上面完全相同，唯一的差别就是行跨度。
 * ==========================================================================*/
void gemm_v0_pad(int N, const data_t *A, const data_t *B, data_t *C) {
    const int S = N + PAD;              /* ★ 唯一的跨度定义 */
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            data_t s = 0.0f;
            for (int k = 0; k < N; k++)
                s += A[(size_t)i * S + k] * B[(size_t)k * S + j];
            C[(size_t)i * S + j] = s;
        }
    }
}

/* ============================================================================
 *  正确性自检：随机抽查 64 格，直接算点积对比
 *  （多一个 stride 参数，因为两组用的跨度不一样）
 * ==========================================================================*/
static int verify(int N, int stride, const data_t *A, const data_t *B,
                  const data_t *C, int verbose) {
    int bad = 0;
    double maxerr = 0.0;
    for (int t = 0; t < 64; t++) {
        int i = rand() % N, j = rand() % N;
        double s = 0.0;
        for (int k = 0; k < N; k++)
            s += (double)A[(size_t)i * stride + k] * (double)B[(size_t)k * stride + j];
        double got = (double)C[(size_t)i * stride + j];
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
 *  机器速度探针（同 gemm.c）
 * ==========================================================================*/
static volatile long long g_probe_sink;

static double cpu_probe(void) {
    long long x = 1;
    const long long n = 30000000LL;
    double t0 = now_sec();
    for (long long i = 0; i < n; i++)
        x = x * 6364136223846793005LL + 1442695040888963407LL;
    double t1 = now_sec();
    g_probe_sink = x;
    return (double)n / (t1 - t0) / 1e9;
}

/* ============================================================================
 *  计时 + 出数
 * ==========================================================================*/
typedef void (*gemm_fn)(int, const data_t *, const data_t *, data_t *);

static int stdout_is_tty(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout));
#else
    return isatty(fileno(stdout));
#endif
}

static double bench(const char *name, gemm_fn f, int N, int stride,
                    const data_t *A, const data_t *B, data_t *C, int reps) {
    int tty = stdout_is_tty();
    if (tty) { printf("  %-10s  N=%-5d  ...", name, N); fflush(stdout); }

    f(N, A, B, C);                              /* 预热 */

    if (!verify(N, stride, A, B, C, 0)) {
        if (tty) printf("\r");
        printf("  %-10s  N=%-5d  %10s   %10s   [*** WRONG RESULT - no perf ***]\n",
               name, N, "-", "N/A");
        verify(N, stride, A, B, C, 1);
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
    if (tty) printf("\r");
    printf("  %-10s  N=%-5d  %10.2f ms   %10.3f GFLOPS   [OK]            \n",
           name, N, best * 1000.0, gflops);
    return gflops;
}

int main(int argc, char **argv) {
    int N    = (argc > 1) ? atoi(argv[1]) : 1024;
    int reps = (argc > 2) ? atoi(argv[2]) : 3;
    if (N < 64) { printf("N must be >= 64\n"); return 1; }
    if (N >= 2048 && reps > 1) reps = 1;

    const int    stride = N + PAD;
    const size_t sz     = (size_t)N * N * sizeof(data_t);
    const size_t sz_pad = (size_t)N * stride * sizeof(data_t);

    data_t *A  = (data_t *)xmalloc(sz);
    data_t *B  = (data_t *)xmalloc(sz);
    data_t *C  = (data_t *)xmalloc(sz);
    data_t *Ap = (data_t *)xmalloc(sz_pad);
    data_t *Bp = (data_t *)xmalloc(sz_pad);
    data_t *Cp = (data_t *)xmalloc(sz_pad);
    if (!A || !B || !C || !Ap || !Bp || !Cp) {
        printf("allocation failed (N too large?)\n");
        return 1;
    }

    srand(12345);                    /* 固定种子，两组用同一份数据 */
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            data_t a = (data_t)(rand() % 1000) / 1000.0f;
            data_t b = (data_t)(rand() % 1000) / 1000.0f;
            A[(size_t)i * N + j]      = a;
            B[(size_t)i * N + j]      = b;
            Ap[(size_t)i * stride + j] = a;   /* 同样的数据，放进带 padding 的布局 */
            Bp[(size_t)i * stride + j] = b;
        }
    }

    printf("\n=== controlled experiment: change stride only   N=%d  PAD=%d  best of %d ===\n",
           N, PAD, reps);
    printf("    cpu probe = %.3f G iter/s   (low = CPU throttled, discard this run)\n\n",
           cpu_probe());
    printf("  version        size          time          perf            check\n");
    printf("  ---------------------------------------------------------------------\n");

    double g0 = bench("v0",     gemm_v0,     N, N,      A,  B,  C,  reps);
    double g1 = bench("v0+pad", gemm_v0_pad, N, stride, Ap, Bp, Cp, reps);

    printf("\n  N=%d  PAD=%d  →  %.3f  →  %.3f GFLOPS   (%.2fx)\n",
           N, PAD, g0, g1, (g0 > 0) ? g1 / g0 : 0.0);
    printf("  data size %d KB -> %d KB (basically unchanged)\n",
           (int)((sz * 3) / 1024), (int)((sz_pad * 3) / 1024));
    printf("\n");

    xfree(A);  xfree(B);  xfree(C);
    xfree(Ap); xfree(Bp); xfree(Cp);
    return 0;
}
