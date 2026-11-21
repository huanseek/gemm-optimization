/* block_shape.c -- 单核最后那 17%：换块的形状能拿回多少？
 *
 * v5 的账（N=1024，122.6 GFLOPS = 峰值的 82.7%）：
 *
 *   每 1 个 k，4 行 × 2 个向量 = 8 条 FMA
 *   加载：4 个 A 广播 + 2 个 B 向量 = 6 次
 *   FMA ：8 ÷ 2 条/周期 = 4 周期
 *   加载：6 ÷ 2 条/周期 = 3 周期     ← 只差一点就顶上了
 *
 * 也就是说 FMA 那条线快满了，但加载已经用到 75%。
 * 加宽 i 或 j 的块，能让每个 FMA 摊到更少的加载：
 *
 *   形状        累加器   每 k 的加载   FMA   加载/FMA
 *   4 行 × 16    8        4A + 2B = 6    8     0.75      ← v5 现在这个
 *   6 行 × 16   12        6A + 2B = 8   12     0.67
 *   4 行 × 24   12        4A + 3B = 7   12     0.58      ← 最划算
 *   6 行 × 24   18        6A + 3B = 9   18     0.50      ← 18 个累加器，溢出
 *
 * 探针只测前三个。每个都做同样多的乘加，只改块的形状。
 *
 * 注意：三个都用 KBLOCK=128。j 块 24 对 N=1024 有尾巴（1024 = 42×24 + 16），
 * 尾巴用朴素循环补，只跑一次，不影响对比。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <windows.h>
#include <malloc.h>

typedef float data_t;
#define KBLOCK 128
static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* ================= 4 行 × 16（两个向量）= 8 个累加器  = v5 现在这个 ================= */
static void shape_4x16(int N, const data_t *A, const data_t *B, data_t *C) {
    memset(C, 0, sizeof(data_t) * (size_t)N * N);
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4) {
            int j = 0;
            for (; j + 16 <= N; j += 16) {
                __m256 c0a = _mm256_setzero_ps(), c0b = c0a, c1a = c0a, c1b = c0a;
                __m256 c2a = c0a, c2b = c0a, c3a = c0a, c3b = c0a;
                for (int k = kk; k < kend; k++) {
                    __m256 b0 = _mm256_loadu_ps(&B[k*N+j]);
                    __m256 b1 = _mm256_loadu_ps(&B[k*N+j+8]);
                    __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                    __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                    __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                    __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                    c0a = _mm256_fmadd_ps(a0,b0,c0a); c0b = _mm256_fmadd_ps(a0,b1,c0b);
                    c1a = _mm256_fmadd_ps(a1,b0,c1a); c1b = _mm256_fmadd_ps(a1,b1,c1b);
                    c2a = _mm256_fmadd_ps(a2,b0,c2a); c2b = _mm256_fmadd_ps(a2,b1,c2b);
                    c3a = _mm256_fmadd_ps(a3,b0,c3a); c3b = _mm256_fmadd_ps(a3,b1,c3b);
                }
                #define PUT(r, p, v) _mm256_storeu_ps(&C[(i+r)*N+j+p], _mm256_add_ps(_mm256_loadu_ps(&C[(i+r)*N+j+p]), v))
                PUT(0,0,c0a); PUT(0,8,c0b); PUT(1,0,c1a); PUT(1,8,c1b);
                PUT(2,0,c2a); PUT(2,8,c2b); PUT(3,0,c3a); PUT(3,8,c3b);
                #undef PUT
            }
            for (; j < N; j++)                                  /* j 尾巴 */
                for (int k = kk; k < kend; k++) {
                    data_t b = B[k*N+j];
                    C[(i+0)*N+j] += A[(i+0)*N+k]*b; C[(i+1)*N+j] += A[(i+1)*N+k]*b;
                    C[(i+2)*N+j] += A[(i+2)*N+k]*b; C[(i+3)*N+j] += A[(i+3)*N+k]*b;
                }
        }
    }
}

/* ================= 6 行 × 16 = 12 个累加器 ================= */
static void shape_6x16(int N, const data_t *A, const data_t *B, data_t *C) {
    memset(C, 0, sizeof(data_t) * (size_t)N * N);
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 6 <= N; i += 6) {
            int j = 0;
            for (; j + 16 <= N; j += 16) {
                __m256 c0a=_mm256_setzero_ps(), c0b=c0a, c1a=c0a, c1b=c0a, c2a=c0a, c2b=c0a;
                __m256 c3a=c0a, c3b=c0a, c4a=c0a, c4b=c0a, c5a=c0a, c5b=c0a;
                for (int k = kk; k < kend; k++) {
                    __m256 b0 = _mm256_loadu_ps(&B[k*N+j]);
                    __m256 b1 = _mm256_loadu_ps(&B[k*N+j+8]);
                    __m256 a0=_mm256_set1_ps(A[(i+0)*N+k]), a1=_mm256_set1_ps(A[(i+1)*N+k]);
                    __m256 a2=_mm256_set1_ps(A[(i+2)*N+k]), a3=_mm256_set1_ps(A[(i+3)*N+k]);
                    __m256 a4=_mm256_set1_ps(A[(i+4)*N+k]), a5=_mm256_set1_ps(A[(i+5)*N+k]);
                    c0a=_mm256_fmadd_ps(a0,b0,c0a); c0b=_mm256_fmadd_ps(a0,b1,c0b);
                    c1a=_mm256_fmadd_ps(a1,b0,c1a); c1b=_mm256_fmadd_ps(a1,b1,c1b);
                    c2a=_mm256_fmadd_ps(a2,b0,c2a); c2b=_mm256_fmadd_ps(a2,b1,c2b);
                    c3a=_mm256_fmadd_ps(a3,b0,c3a); c3b=_mm256_fmadd_ps(a3,b1,c3b);
                    c4a=_mm256_fmadd_ps(a4,b0,c4a); c4b=_mm256_fmadd_ps(a4,b1,c4b);
                    c5a=_mm256_fmadd_ps(a5,b0,c5a); c5b=_mm256_fmadd_ps(a5,b1,c5b);
                }
                #define PUT(r, p, v) _mm256_storeu_ps(&C[(i+r)*N+j+p], _mm256_add_ps(_mm256_loadu_ps(&C[(i+r)*N+j+p]), v))
                PUT(0,0,c0a); PUT(0,8,c0b); PUT(1,0,c1a); PUT(1,8,c1b);
                PUT(2,0,c2a); PUT(2,8,c2b); PUT(3,0,c3a); PUT(3,8,c3b);
                PUT(4,0,c4a); PUT(4,8,c4b); PUT(5,0,c5a); PUT(5,8,c5b);
                #undef PUT
            }
            for (; j < N; j++)
                for (int k = kk; k < kend; k++) {
                    data_t b = B[k*N+j];
                    for (int r = 0; r < 6; r++) C[(i+r)*N+j] += A[(i+r)*N+k]*b;
                }
        }
        for (int i = (N/6)*6; i < N; i++)                       /* i 尾巴 */
            for (int k = kk; k < kend; k++)
                for (int j = 0; j < N; j++) C[i*N+j] += A[i*N+k]*B[k*N+j];
    }
}

/* ================= 4 行 × 24（三个向量）= 12 个累加器 ================= */
static void shape_4x24(int N, const data_t *A, const data_t *B, data_t *C) {
    memset(C, 0, sizeof(data_t) * (size_t)N * N);
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4) {
            int j = 0;
            for (; j + 24 <= N; j += 24) {
                __m256 c0a=_mm256_setzero_ps(), c0b=c0a, c0c=c0a, c1a=c0a, c1b=c0a, c1c=c0a;
                __m256 c2a=c0a, c2b=c0a, c2c=c0a, c3a=c0a, c3b=c0a, c3c=c0a;
                for (int k = kk; k < kend; k++) {
                    __m256 b0 = _mm256_loadu_ps(&B[k*N+j]);
                    __m256 b1 = _mm256_loadu_ps(&B[k*N+j+8]);
                    __m256 b2 = _mm256_loadu_ps(&B[k*N+j+16]);
                    __m256 a0=_mm256_set1_ps(A[(i+0)*N+k]), a1=_mm256_set1_ps(A[(i+1)*N+k]);
                    __m256 a2=_mm256_set1_ps(A[(i+2)*N+k]), a3=_mm256_set1_ps(A[(i+3)*N+k]);
                    c0a=_mm256_fmadd_ps(a0,b0,c0a); c0b=_mm256_fmadd_ps(a0,b1,c0b); c0c=_mm256_fmadd_ps(a0,b2,c0c);
                    c1a=_mm256_fmadd_ps(a1,b0,c1a); c1b=_mm256_fmadd_ps(a1,b1,c1b); c1c=_mm256_fmadd_ps(a1,b2,c1c);
                    c2a=_mm256_fmadd_ps(a2,b0,c2a); c2b=_mm256_fmadd_ps(a2,b1,c2b); c2c=_mm256_fmadd_ps(a2,b2,c2c);
                    c3a=_mm256_fmadd_ps(a3,b0,c3a); c3b=_mm256_fmadd_ps(a3,b1,c3b); c3c=_mm256_fmadd_ps(a3,b2,c3c);
                }
                #define PUT(r, p, v) _mm256_storeu_ps(&C[(i+r)*N+j+p], _mm256_add_ps(_mm256_loadu_ps(&C[(i+r)*N+j+p]), v))
                PUT(0,0,c0a); PUT(0,8,c0b); PUT(0,16,c0c);
                PUT(1,0,c1a); PUT(1,8,c1b); PUT(1,16,c1c);
                PUT(2,0,c2a); PUT(2,8,c2b); PUT(2,16,c2c);
                PUT(3,0,c3a); PUT(3,8,c3b); PUT(3,16,c3c);
                #undef PUT
            }
            for (; j < N; j++)
                for (int k = kk; k < kend; k++) {
                    data_t b = B[k*N+j];
                    C[(i+0)*N+j] += A[(i+0)*N+k]*b; C[(i+1)*N+j] += A[(i+1)*N+k]*b;
                    C[(i+2)*N+j] += A[(i+2)*N+k]*b; C[(i+3)*N+j] += A[(i+3)*N+k]*b;
                }
        }
    }
}

typedef void (*fn_t)(int, const data_t*, const data_t*, data_t*);

/* 正确性自检：随机抽 48 格和直接点积比 */
static int check(const char *label, int N, const data_t *A, const data_t *B,
                 const data_t *C, fn_t f) {
    data_t *D = (data_t*)_aligned_malloc((size_t)N*N*sizeof(data_t), 64);
    memset(D, 0, (size_t)N*N*sizeof(data_t));
    f(N, A, B, D);
    srand(999);
    int bad = 0;
    for (int t = 0; t < 48; t++) {
        int i = rand() % N, j = rand() % N;
        double ref = 0;
        for (int k = 0; k < N; k++) ref += (double)A[i*N+k] * (double)B[k*N+j];
        double got = (double)D[i*N+j];
        if (ref != 0 && (got-ref)/ref > 1e-3) bad++;
    }
    printf("    %-9s check = %s\n", label, bad ? "MISMATCH" : "OK");
    _aligned_free(D);
    return bad;
}

int main(int argc, char **argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 1024;
    size_t sz = (size_t)N * N * sizeof(data_t);
    data_t *A = (data_t*)_aligned_malloc(sz, 64);
    data_t *B = (data_t*)_aligned_malloc(sz, 64);
    data_t *C = (data_t*)_aligned_malloc(sz, 64);
    if (!A || !B || !C) { printf("alloc failed\n"); return 1; }
    srand(12345);
    for (size_t k = 0; k < (size_t)N*N; k++) {
        A[k] = (data_t)(rand()%1000)/1000.0f;
        B[k] = (data_t)(rand()%1000)/1000.0f;
        C[k] = 0.0f;
    }

    static const char *names[3] = { "4x16", "6x16", "4x24" };
    fn_t fns[3] = { shape_4x16, shape_6x16, shape_4x24 };
    const char *accs[3] = { "8", "12", "12" };

    printf("\n=== block shape sweep   N = %d   KBLOCK = %d ===\n\n", N, KBLOCK);
    for (int i = 0; i < 3; i++) check(names[i], N, A, B, C, fns[i]);

    /* 交错跑：每轮三个都跑，机器漂移对三个影响一样 */
    double best[3];
    for (int i = 0; i < 3; i++) { best[i] = 1e30; fns[i](N, A, B, C); }
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < 3; i++) {
            double t0 = now_sec(); fns[i](N, A, B, C); double t1 = now_sec();
            if (t1 - t0 < best[i]) best[i] = t1 - t0;
        }

    printf("\n  shape    accumulators   time(ms)    GFLOPS    vs 4x16\n");
    printf("  ---------------------------------------------------------\n");
    for (int i = 0; i < 3; i++) {
        double g = 2.0*N*N*(double)N/best[i]/1e9;
        printf("  %-8s %8s     %8.2f  %8.2f    %5.2fx\n",
               names[i], accs[i], best[i]*1000.0, g, best[0]/best[i]);
    }
    printf("\n  If 4x24 wins, the load port was the last limit.\n");
    printf("  If nothing beats 4x16, single core is done at about 83%% of peak.\n\n");
    return 0;
}
