/* 实测 FMA 吞吐上界：只做 FMA，完全不碰内存。
 * 换累加器个数（4 / 8 / 16），看哪一个能打满。
 * 理论：Golden Cove 每周期 2 条 256 位 FMA，每条 8 float × 2 FLOP = 16 FLOP
 *       所以峰值 = 主频 × 32 FLOP/周期
 * 依赖链：FMA 延迟 4 周期，所以要 8 个独立累加器才填得满（8 ÷ 2 = 4）。 */
#include <stdio.h>
#include <stdlib.h>
#include <immintrin.h>
#include <windows.h>

static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

#define SINK(a,b,c,d) do { \
    __m256 s_ = _mm256_add_ps(_mm256_add_ps((a),(b)), _mm256_add_ps((c),(d))); \
    float buf_[8]; _mm256_storeu_ps(buf_, s_); \
    g_sink = buf_[0]+buf_[1]+buf_[2]+buf_[3]+buf_[4]+buf_[5]+buf_[6]+buf_[7]; \
} while (0)

/* 4 个累加器：4 条 ÷ 2 条/周期 = 2 周期 < 延迟 4 → 卡在延迟上，只有一半吞吐 */
static double run4(long long n) {
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    const __m256 b = _mm256_set1_ps(1.0f), c = _mm256_set1_ps(1e-7f);
    double t0 = now_sec();
    for (long long i = 0; i < n; i++) {
        a0 = _mm256_fmadd_ps(a0, b, c);
        a1 = _mm256_fmadd_ps(a1, b, c);
        a2 = _mm256_fmadd_ps(a2, b, c);
        a3 = _mm256_fmadd_ps(a3, b, c);
    }
    double t1 = now_sec();
    SINK(a0, a1, a2, a3);
    return t1 - t0;
}

/* 8 个累加器：8 ÷ 2 = 4 周期 = 延迟 4 → 刚好填满 */
static double run8(long long n) {
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    __m256 a4 = a0, a5 = a0, a6 = a0, a7 = a0;
    const __m256 b = _mm256_set1_ps(1.0f), c = _mm256_set1_ps(1e-7f);
    double t0 = now_sec();
    for (long long i = 0; i < n; i++) {
        a0 = _mm256_fmadd_ps(a0, b, c);
        a1 = _mm256_fmadd_ps(a1, b, c);
        a2 = _mm256_fmadd_ps(a2, b, c);
        a3 = _mm256_fmadd_ps(a3, b, c);
        a4 = _mm256_fmadd_ps(a4, b, c);
        a5 = _mm256_fmadd_ps(a5, b, c);
        a6 = _mm256_fmadd_ps(a6, b, c);
        a7 = _mm256_fmadd_ps(a7, b, c);
    }
    double t1 = now_sec();
    SINK(a0, a1, a2, a3);
    SINK(a4, a5, a6, a7);
    return t1 - t0;
}

/* 16 个累加器：超过饱和点，应该和 8 个一样快 */
static double run16(long long n) {
    __m256 a[16];
    for (int k = 0; k < 16; k++) a[k] = _mm256_setzero_ps();
    const __m256 b = _mm256_set1_ps(1.0f), c = _mm256_set1_ps(1e-7f);
    double t0 = now_sec();
    for (long long i = 0; i < n; i++) {
        for (int k = 0; k < 16; k++) a[k] = _mm256_fmadd_ps(a[k], b, c);
    }
    double t1 = now_sec();
    for (int k = 0; k < 16; k += 4) SINK(a[k], a[k+1], a[k+2], a[k+3]);
    return t1 - t0;
}

int main(int argc, char **argv) {
    long long n = (argc > 1) ? (long long)atol(argv[1]) : 300000000LL;
    printf("\n=== peak FMA throughput   %lld iterations ===\n\n", n);
    printf("  accumulators   best(ms)      GFLOPS     FMA/cycle @4.7GHz\n");
    printf("  ---------------------------------------------------------\n");

    double best;
    int flops;

    best = 1e30;
    for (int r = 0; r < 3; r++) { double t = run4(n); if (t < best) best = t; }
    flops = 4 * 8 * 2;
    printf("       4        %9.1f   %10.2f   %8.2f\n",
           best * 1000.0, (double)n * flops / best / 1e9, (double)n / best / 4.7e9 * 4);

    best = 1e30;
    for (int r = 0; r < 3; r++) { double t = run8(n); if (t < best) best = t; }
    flops = 8 * 8 * 2;
    printf("       8        %9.1f   %10.2f   %8.2f\n",
           best * 1000.0, (double)n * flops / best / 1e9, (double)n / best / 4.7e9 * 8);

    best = 1e30;
    for (int r = 0; r < 3; r++) { double t = run16(n); if (t < best) best = t; }
    flops = 16 * 8 * 2;
    printf("      16        %9.1f   %10.2f   %8.2f\n",
           best * 1000.0, (double)n * flops / best / 1e9, (double)n / best / 4.7e9 * 16);

    printf("\n  (sink = %g)\n\n", (double)g_sink);
    return 0;
}
