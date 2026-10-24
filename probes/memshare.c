/* memshare.c -- 访存到底占多少时间？（第二版）
 *
 * 第一版的错误：
 *   1. loadB / none 用单个累加器，形成串行依赖链（FMA 延迟 4 周期），
 *      测出来的是依赖链，不是访存。
 *   2. full / loadC 没有 k 分块，实际测的是 v1 的形状，不是 v3。
 *
 * 这一版的改法：
 *   - 所有变体 j 方向展开 8 个向量（64 个 float），用 8 个独立累加器，
 *     每个累加器每 4 个周期才被碰一次，延迟完全藏住。
 *   - 所有变体都带 k 分块（KBLOCK=128），和 v3 的形状一致。
 *   - 四个变体的乘加次数完全相同，只有访存不同。
 *
 *   full    读 B + 读 C + 写 C      （= v3）
 *   loadB   只读 B
 *   loadC   只碰 C
 *   none    不访存（纯计算上界）
 */
#include <stdio.h>
#include <stdlib.h>
#include <immintrin.h>
#include <windows.h>
#include <malloc.h>

typedef float data_t;
#define KBLOCK 128
static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* 8 宽展开、4 个向量的内层主体（一次处理 32 个 float）。
 * MULT 宏负责"算"，由各变体自己决定算完往哪放。 */

#define BODY_LOADS8(pi, pk, pj)  \
    __m256 b0 = _mm256_loadu_ps(&B[(pk)*N + (pj) +  0]); \
    __m256 b1 = _mm256_loadu_ps(&B[(pk)*N + (pj) +  8]); \
    __m256 b2 = _mm256_loadu_ps(&B[(pk)*N + (pj) + 16]); \
    __m256 b3 = _mm256_loadu_ps(&B[(pk)*N + (pj) + 24]);

/* ---------- full：读 B、读 C、写 C ---------- */
static void full(int N, const data_t *A, const data_t *B, data_t *C) {
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i < N; i++)
            for (int k = kk; k < kend; k++) {
                __m256 av = _mm256_set1_ps(A[i*N+k]);
                for (int j = 0; j + 32 <= N; j += 32) {
                    __m256 b0 = _mm256_loadu_ps(&B[k*N+j+ 0]);
                    __m256 b1 = _mm256_loadu_ps(&B[k*N+j+ 8]);
                    __m256 b2 = _mm256_loadu_ps(&B[k*N+j+16]);
                    __m256 b3 = _mm256_loadu_ps(&B[k*N+j+24]);
                    __m256 c0 = _mm256_loadu_ps(&C[i*N+j+ 0]);
                    __m256 c1 = _mm256_loadu_ps(&C[i*N+j+ 8]);
                    __m256 c2 = _mm256_loadu_ps(&C[i*N+j+16]);
                    __m256 c3 = _mm256_loadu_ps(&C[i*N+j+24]);
                    c0 = _mm256_fmadd_ps(av, b0, c0);
                    c1 = _mm256_fmadd_ps(av, b1, c1);
                    c2 = _mm256_fmadd_ps(av, b2, c2);
                    c3 = _mm256_fmadd_ps(av, b3, c3);
                    _mm256_storeu_ps(&C[i*N+j+ 0], c0);
                    _mm256_storeu_ps(&C[i*N+j+ 8], c1);
                    _mm256_storeu_ps(&C[i*N+j+16], c2);
                    _mm256_storeu_ps(&C[i*N+j+24], c3);
                }
            }
    }
}

/* ---------- loadB：只读 B ---------- */
static void loadB(int N, const data_t *A, const data_t *B, data_t *C) {
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    __m256 a4 = a0, a5 = a0, a6 = a0, a7 = a0;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i < N; i++)
            for (int k = kk; k < kend; k++) {
                __m256 av = _mm256_set1_ps(A[i*N+k]);
                for (int j = 0; j + 64 <= N; j += 64) {
                    a0 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+ 0]), a0);
                    a1 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+ 8]), a1);
                    a2 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+16]), a2);
                    a3 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+24]), a3);
                    a4 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+32]), a4);
                    a5 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+40]), a5);
                    a6 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+48]), a6);
                    a7 = _mm256_fmadd_ps(av, _mm256_loadu_ps(&B[k*N+j+56]), a7);
                }
            }
    }
    __m256 s = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a0,a1), _mm256_add_ps(a2,a3)),
                             _mm256_add_ps(_mm256_add_ps(a4,a5), _mm256_add_ps(a6,a7)));
    float buf[8]; _mm256_storeu_ps(buf, s);
    g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
    C[0] = g_sink;
}

/* ---------- loadC：只碰 C ---------- */
static void loadC(int N, const data_t *A, const data_t *B, data_t *C) {
    const __m256 bv = _mm256_set1_ps(1e-7f);
    (void)B;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i < N; i++)
            for (int k = kk; k < kend; k++) {
                __m256 av = _mm256_set1_ps(A[i*N+k]);
                for (int j = 0; j + 32 <= N; j += 32) {
                    __m256 c0 = _mm256_loadu_ps(&C[i*N+j+ 0]);
                    __m256 c1 = _mm256_loadu_ps(&C[i*N+j+ 8]);
                    __m256 c2 = _mm256_loadu_ps(&C[i*N+j+16]);
                    __m256 c3 = _mm256_loadu_ps(&C[i*N+j+24]);
                    c0 = _mm256_fmadd_ps(av, bv, c0);
                    c1 = _mm256_fmadd_ps(av, bv, c1);
                    c2 = _mm256_fmadd_ps(av, bv, c2);
                    c3 = _mm256_fmadd_ps(av, bv, c3);
                    _mm256_storeu_ps(&C[i*N+j+ 0], c0);
                    _mm256_storeu_ps(&C[i*N+j+ 8], c1);
                    _mm256_storeu_ps(&C[i*N+j+16], c2);
                    _mm256_storeu_ps(&C[i*N+j+24], c3);
                }
            }
    }
}

/* ---------- none：纯计算 ---------- */
static void none(int N, const data_t *A, const data_t *B, data_t *C) {
    const __m256 bv = _mm256_set1_ps(1e-7f);
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    __m256 a4 = a0, a5 = a0, a6 = a0, a7 = a0;
    (void)B;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i < N; i++)
            for (int k = kk; k < kend; k++) {
                __m256 av = _mm256_set1_ps(A[i*N+k]);
                for (int j = 0; j + 64 <= N; j += 64) {
                    a0 = _mm256_fmadd_ps(av, bv, a0);
                    a1 = _mm256_fmadd_ps(av, bv, a1);
                    a2 = _mm256_fmadd_ps(av, bv, a2);
                    a3 = _mm256_fmadd_ps(av, bv, a3);
                    a4 = _mm256_fmadd_ps(av, bv, a4);
                    a5 = _mm256_fmadd_ps(av, bv, a5);
                    a6 = _mm256_fmadd_ps(av, bv, a6);
                    a7 = _mm256_fmadd_ps(av, bv, a7);
                }
            }
    }
    __m256 s = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a0,a1), _mm256_add_ps(a2,a3)),
                             _mm256_add_ps(_mm256_add_ps(a4,a5), _mm256_add_ps(a6,a7)));
    float buf[8]; _mm256_storeu_ps(buf, s);
    g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
    C[0] = g_sink;
}

typedef void (*fn_t)(int, const data_t*, const data_t*, data_t*);

static void run(const char *label, fn_t f, int N, const data_t *A, const data_t *B, data_t *C,
                double ref) {
    double best = 1e30;
    for (int r = 0; r < 5; r++) {
        double t0 = now_sec(); f(N, A, B, C); double t1 = now_sec();
        if (t1 - t0 < best) best = t1 - t0;
    }
    double g = 2.0*N*N*(double)N/best/1e9;
    printf("  %-8s %9.2f ms   %8.2f GFLOPS   %s\n",
           label, best*1000.0, g, ref > 0 ? "" : "");
}

int main(int argc, char **argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 1024;
    size_t sz = (size_t)N * N * sizeof(data_t);
    data_t *A = (data_t*)_aligned_malloc(sz, 64);
    data_t *B = (data_t*)_aligned_malloc(sz, 64);
    data_t *C = (data_t*)_aligned_malloc(sz, 64);
    srand(12345);
    for (size_t k = 0; k < (size_t)N*N; k++) {
        A[k] = (data_t)(rand()%1000)/1000.0f;
        B[k] = (data_t)(rand()%1000)/1000.0f;
        C[k] = 0.0f;
    }

    printf("\n=== memory share probe   N = %d   (all do N^3 multiply-adds) ===\n\n", N);
    printf("  variant     time        throughput\n");
    printf("  -----------------------------------\n");
    run("full",  full,  N, A, B, C, 0);
    run("loadB", loadB, N, A, B, C, 0);
    run("loadC", loadC, N, A, B, C, 0);
    run("none",  none,  N, A, B, C, 0);
    printf("\n");
    return 0;
}
