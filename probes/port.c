/* port.c -- L1 的读带宽和写带宽，到底是多少
 *
 * 为什么要测这个：
 *   v4 的内层，每一个向量 FMA 都配一次 C 的读和一次 C 的写。
 *   如果 L1 一个周期只能写 32 字节（也就是一个 256 位向量），
 *   那 v4 就有一个硬天花板：1 条 FMA / 周期 -> 8 次乘加 / 周期 -> 约 74 GFLOPS。
 *   v4 实测 62 左右，从来没超过 74。
 *   而 v5 把 C 的写从内层整个拿掉了，同一套算法跑到 120.8。
 *
 * 所以这个探针只需要两个数：
 *   load  纯读，8 个独立累加器，测每秒读多少个 256 位向量
 *   copy  读一个、写一个，测每秒完成多少组
 *
 * 全部在 32 KB 里做（< 48 KB 的 L1），不碰内存。
 *
 * 注意：别想单独测「纯 store」。
 *   试过了 —— 往连续地址写同一个值，GCC 直接换成批量写，绕开 L1 写端口，
 *   测出来 12.74 条/周期那种不可能的数。copy 这一项就能替代它。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <windows.h>
#include <malloc.h>

#define GHZ      4.63
#define REPS     7
#define REPEAT   20000
#define NELEM    (((size_t)32u << 10) / 4)    /* 32 KB */

static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static void report(const char *name, double *t, double vecops) {
    qsort(t, REPS, sizeof(double), cmp_d);
    double best = t[0];
    double per_sec = vecops / best;
    double per_cyc = per_sec / (GHZ * 1e9);
    printf("  %-6s  %8.3f ms   %6.2f vectors/cycle   %6.1f bytes/cycle\n",
           name, best * 1000.0, per_cyc, per_cyc * 32.0);
}

int main(void) {
    float *a = (float*)_aligned_malloc(NELEM * sizeof(float), 64);
    float *b = (float*)_aligned_malloc(NELEM * sizeof(float), 64);
    if (!a || !b) { printf("alloc failed\n"); return 1; }
    for (size_t i = 0; i < NELEM; i++) { a[i] = 1.0f; b[i] = 2.0f; }

    const int NV = (int)(NELEM / 8);
    const int NHALF = (int)(NELEM / 2);          /* copy 只用一半，否则 a+b 装不进 L1 */
    double t[REPS];

    printf("\n=== L1 bandwidth probe   32 KB arrays, %d vectors, x%d passes ===\n\n", NV, REPEAT);

    /* ---------- load：8 个独立累加器，避免依赖链 ---------- */
    for (int r = 0; r < REPS; r++) {
        __m256 s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
        __m256 s4 = s0, s5 = s0, s6 = s0, s7 = s0;
        double t0 = now_sec();
        for (int rep = 0; rep < REPEAT; rep++)
            for (int i = 0; i + 64 <= (int)NELEM; i += 64) {
                s0 = _mm256_add_ps(s0, _mm256_loadu_ps(&b[i+ 0]));
                s1 = _mm256_add_ps(s1, _mm256_loadu_ps(&b[i+ 8]));
                s2 = _mm256_add_ps(s2, _mm256_loadu_ps(&b[i+16]));
                s3 = _mm256_add_ps(s3, _mm256_loadu_ps(&b[i+24]));
                s4 = _mm256_add_ps(s4, _mm256_loadu_ps(&b[i+32]));
                s5 = _mm256_add_ps(s5, _mm256_loadu_ps(&b[i+40]));
                s6 = _mm256_add_ps(s6, _mm256_loadu_ps(&b[i+48]));
                s7 = _mm256_add_ps(s7, _mm256_loadu_ps(&b[i+56]));
            }
        double t1 = now_sec();
        __m256 z = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(s0,s1), _mm256_add_ps(s2,s3)),
                                 _mm256_add_ps(_mm256_add_ps(s4,s5), _mm256_add_ps(s6,s7)));
        float buf[8]; _mm256_storeu_ps(buf, z);
        g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
        t[r] = t1 - t0;
    }
    report("load", t, (double)NV * REPEAT);

    /* ---------- copy：1 读 + 1 写。读的值来自内存，所以不会被优化成批量写 ---------- */
    for (int r = 0; r < REPS; r++) {
        __m256 v = _mm256_set1_ps(1.0f);
        double t0 = now_sec();
        for (int rep = 0; rep < REPEAT; rep++)
            for (int i = 0; i + 8 <= NHALF; i += 8)
                _mm256_storeu_ps(&a[i], _mm256_add_ps(_mm256_loadu_ps(&b[i]), v));
        t[r] = now_sec() - t0;
    }
    report("copy", t, (double)(NHALF / 8) * REPEAT);

    printf("\n  How to read:\n");
    printf("    load ~= 2.0 vectors/cycle  -> 64 bytes/cycle read\n");
    printf("    copy ~= 1.0 vectors/cycle  -> the store side is the limit:\n");
    printf("                                  32 bytes/cycle write\n");
    printf("    If copy is ~1.0, then v4 (1 store per FMA) caps at 8 FMA-lanes/cycle\n");
    printf("    = about 74 GFLOPS, and v4 measured 62, never above 74. Consistent.\n\n");

    _aligned_free(a); _aligned_free(b);
    return 0;
}
