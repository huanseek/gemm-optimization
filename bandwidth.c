/* bandwidth.c -- 这台机器的内存带宽到底多少？
 *
 * 为什么重要：之前算 v0 的账时假设内存峰值 40~51 GB/s，据此判定
 * "18.7 GB/s 远没打满，所以不是带宽瓶颈"。如果实际峰值只有 18~20，
 * 那个判断就要反过来。
 *
 * 第一版的错误：read 用 for (i) s += a[i]，一个累加器 → 加法依赖链
 * 4 周期一次，测出来的是依赖链不是带宽。这一版 read 用 8 个独立的
 * 向量累加器，把延迟完全藏住。
 *
 * 三个测试：
 *   read   8 个向量累加器，只读
 *   copy   memcpy，读 + 写
 *   write  普通写（有 RFO，有效带宽只有一半）
 *   stream 非临时写（绕过 cache，直接写内存）
 *
 * 每个测 5 次，打印最短、最长和中位数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <windows.h>
#include <malloc.h>

typedef float data_t;
#define NELEM (32u<<20)          /* 每块 128 MB */
#define REPS  5

static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static void report(const char *name, double *t, double bytes) {
    qsort(t, REPS, sizeof(double), cmp_d);
    printf("  %-8s best %7.2f   median %7.2f   worst %7.2f   GB/s\n",
           name, bytes/t[0]/1e9, bytes/t[REPS/2]/1e9, bytes/t[REPS-1]/1e9);
}

int main(void) {
    size_t bytes = (size_t)NELEM * sizeof(data_t);
    data_t *a = (data_t*)_aligned_malloc(bytes, 64);
    data_t *b = (data_t*)_aligned_malloc(bytes, 64);
    if (!a || !b) { printf("alloc failed\n"); return 1; }
    for (size_t i = 0; i < NELEM; i++) a[i] = (data_t)(i & 1023);
    memset(b, 0, bytes);

    printf("\n=== memory bandwidth   each buffer = %.0f MB ===\n\n", bytes/1048576.0);

    double t[REPS];

    /* ---- read：8 个独立向量累加器，一次读 64 个 float ---- */
    for (int r = 0; r < REPS; r++) {
        __m256 s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
        __m256 s4 = s0, s5 = s0, s6 = s0, s7 = s0;
        double t0 = now_sec();
        for (size_t i = 0; i + 64 <= NELEM; i += 64) {
            s0 = _mm256_add_ps(s0, _mm256_loadu_ps(&a[i+ 0]));
            s1 = _mm256_add_ps(s1, _mm256_loadu_ps(&a[i+ 8]));
            s2 = _mm256_add_ps(s2, _mm256_loadu_ps(&a[i+16]));
            s3 = _mm256_add_ps(s3, _mm256_loadu_ps(&a[i+24]));
            s4 = _mm256_add_ps(s4, _mm256_loadu_ps(&a[i+32]));
            s5 = _mm256_add_ps(s5, _mm256_loadu_ps(&a[i+40]));
            s6 = _mm256_add_ps(s6, _mm256_loadu_ps(&a[i+48]));
            s7 = _mm256_add_ps(s7, _mm256_loadu_ps(&a[i+56]));
        }
        double t1 = now_sec();
        __m256 z = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(s0,s1), _mm256_add_ps(s2,s3)),
                                 _mm256_add_ps(_mm256_add_ps(s4,s5), _mm256_add_ps(s6,s7)));
        float buf[8]; _mm256_storeu_ps(buf, z);
        g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
        t[r] = t1 - t0;
    }
    report("read", t, (double)bytes);

    /* ---- copy：读 + 写 ---- */
    for (int r = 0; r < REPS; r++) {
        double t0 = now_sec();
        memcpy(b, a, bytes);
        double t1 = now_sec();
        t[r] = t1 - t0;
    }
    report("copy", t, 2.0 * (double)bytes);

    /* ---- write：普通写（有 read-for-ownership）---- */
    for (int r = 0; r < REPS; r++) {
        double t0 = now_sec();
        for (size_t i = 0; i < NELEM; i++) b[i] = (data_t)(i & 7);
        double t1 = now_sec();
        t[r] = t1 - t0;
    }
    report("write", t, (double)bytes);

    /* ---- stream：非临时写，绕过 cache ---- */
    for (int r = 0; r < REPS; r++) {
        double t0 = now_sec();
        for (size_t i = 0; i + 8 <= NELEM; i += 8)
            _mm256_stream_ps(&b[i], _mm256_set1_ps((float)(i & 7)));
        _mm_sfence();
        double t1 = now_sec();
        t[r] = t1 - t0;
    }
    report("stream", t, (double)bytes);

    printf("\n  read is the cleanest number; copy counts read plus write.\n");
    printf("  write has RFO, so the real DRAM traffic is twice this figure.\n");
    printf("  stream reads high: non-temporal stores can sit in the write buffer,\n");
    printf("  and sfence only orders them, it does not force them into DRAM.\n\n");

    _aligned_free(a); _aligned_free(b);
    return 0;
}
