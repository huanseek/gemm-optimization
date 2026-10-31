/* v4cost.c -- v4 的三样访存各占多少，用比值说话
 *
 * 为什么用比值：跑的时候机器上还有别的活（编辑器、后台进程），绝对值不可靠。
 * 但同一轮里把五个变体都跑一遍，机器漂移对它们的影响是一样的 —— 比值就稳。
 *
 * 五个变体，全部是 v4 的形状（i 每 4 行一组、kblock=128、j 方向 8 宽向量），
 * 乘加次数相同，只有访存不同：
 *
 *   full      读 B + 读 C + 写 C     （= v4）
 *   no_B      ——   + 读 C + 写 C
 *   no_loadC  读 B +         写 C     （赋值而不是累加）
 *   no_storeC 读 B + 读 C            （结果累积到寄存器）
 *   none      ——       ——      ——
 *
 * 纪律：
 *   - 五轮交错跑，每轮五个变体各一次，每个变体取五轮里最短的那次。
 *   - no_storeC / none 用 8 个独立累加器，否则会卡在加法依赖链上（半速）。
 *   - 每个 .c 都开 -O3 -mavx2 -mfma 编。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <windows.h>
#include <malloc.h>

typedef float data_t;
#define KBLOCK 128
#define ROUNDS 5
static volatile float g_sink;

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* ---- full：读 B + 读 C + 写 C ---- */
static void v_full(int N, const data_t *A, const data_t *B, data_t *C) {
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4)
            for (int k = kk; k < kend; k++) {
                __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                for (int j = 0; j + 8 <= N; j += 8) {
                    __m256 bv = _mm256_loadu_ps(&B[k*N+j]);
                    _mm256_storeu_ps(&C[(i+0)*N+j], _mm256_fmadd_ps(a0, bv, _mm256_loadu_ps(&C[(i+0)*N+j])));
                    _mm256_storeu_ps(&C[(i+1)*N+j], _mm256_fmadd_ps(a1, bv, _mm256_loadu_ps(&C[(i+1)*N+j])));
                    _mm256_storeu_ps(&C[(i+2)*N+j], _mm256_fmadd_ps(a2, bv, _mm256_loadu_ps(&C[(i+2)*N+j])));
                    _mm256_storeu_ps(&C[(i+3)*N+j], _mm256_fmadd_ps(a3, bv, _mm256_loadu_ps(&C[(i+3)*N+j])));
                }
            }
    }
}

/* ---- no_B：读 C + 写 C，B 用常量 ---- */
static void v_noB(int N, const data_t *A, const data_t *B, data_t *C) {
    const __m256 bv = _mm256_set1_ps(1e-7f);
    (void)B;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4)
            for (int k = kk; k < kend; k++) {
                __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                for (int j = 0; j + 8 <= N; j += 8) {
                    _mm256_storeu_ps(&C[(i+0)*N+j], _mm256_fmadd_ps(a0, bv, _mm256_loadu_ps(&C[(i+0)*N+j])));
                    _mm256_storeu_ps(&C[(i+1)*N+j], _mm256_fmadd_ps(a1, bv, _mm256_loadu_ps(&C[(i+1)*N+j])));
                    _mm256_storeu_ps(&C[(i+2)*N+j], _mm256_fmadd_ps(a2, bv, _mm256_loadu_ps(&C[(i+2)*N+j])));
                    _mm256_storeu_ps(&C[(i+3)*N+j], _mm256_fmadd_ps(a3, bv, _mm256_loadu_ps(&C[(i+3)*N+j])));
                }
            }
    }
}

/* ---- no_loadC：读 B + 写 C（赋值，不读旧值）---- */
static void v_noload(int N, const data_t *A, const data_t *B, data_t *C) {
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4)
            for (int k = kk; k < kend; k++) {
                __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                for (int j = 0; j + 8 <= N; j += 8) {
                    __m256 bv = _mm256_loadu_ps(&B[k*N+j]);
                    _mm256_storeu_ps(&C[(i+0)*N+j], _mm256_mul_ps(a0, bv));
                    _mm256_storeu_ps(&C[(i+1)*N+j], _mm256_mul_ps(a1, bv));
                    _mm256_storeu_ps(&C[(i+2)*N+j], _mm256_mul_ps(a2, bv));
                    _mm256_storeu_ps(&C[(i+3)*N+j], _mm256_mul_ps(a3, bv));
                }
            }
    }
}

/* ---- no_storeC：读 B + 读 C，累积到寄存器 ---- */
static void v_nostore(int N, const data_t *A, const data_t *B, data_t *C) {
    __m256 s0a = _mm256_setzero_ps(), s0b = s0a, s1a = s0a, s1b = s0a;
    __m256 s2a = s0a, s2b = s0a, s3a = s0a, s3b = s0a;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4)
            for (int k = kk; k < kend; k++) {
                __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                for (int j = 0; j + 16 <= N; j += 16) {
                    __m256 b0 = _mm256_loadu_ps(&B[k*N+j]);
                    __m256 b1 = _mm256_loadu_ps(&B[k*N+j+8]);
                    s0a = _mm256_add_ps(s0a, _mm256_fmadd_ps(a0, b0, _mm256_loadu_ps(&C[(i+0)*N+j])));
                    s0b = _mm256_add_ps(s0b, _mm256_fmadd_ps(a0, b1, _mm256_loadu_ps(&C[(i+0)*N+j+8])));
                    s1a = _mm256_add_ps(s1a, _mm256_fmadd_ps(a1, b0, _mm256_loadu_ps(&C[(i+1)*N+j])));
                    s1b = _mm256_add_ps(s1b, _mm256_fmadd_ps(a1, b1, _mm256_loadu_ps(&C[(i+1)*N+j+8])));
                    s2a = _mm256_add_ps(s2a, _mm256_fmadd_ps(a2, b0, _mm256_loadu_ps(&C[(i+2)*N+j])));
                    s2b = _mm256_add_ps(s2b, _mm256_fmadd_ps(a2, b1, _mm256_loadu_ps(&C[(i+2)*N+j+8])));
                    s3a = _mm256_add_ps(s3a, _mm256_fmadd_ps(a3, b0, _mm256_loadu_ps(&C[(i+3)*N+j])));
                    s3b = _mm256_add_ps(s3b, _mm256_fmadd_ps(a3, b1, _mm256_loadu_ps(&C[(i+3)*N+j+8])));
                }
            }
    }
    __m256 z = _mm256_add_ps(
        _mm256_add_ps(_mm256_add_ps(s0a,s0b), _mm256_add_ps(s1a,s1b)),
        _mm256_add_ps(_mm256_add_ps(s2a,s2b), _mm256_add_ps(s3a,s3b)));
    float buf[8]; _mm256_storeu_ps(buf, z);
    g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
    C[0] = g_sink;
}

/* ---- none：不访存 ---- */
static void v_none(int N, const data_t *A, const data_t *B, data_t *C) {
    const __m256 bc = _mm256_set1_ps(1e-7f);
    __m256 s0a = _mm256_setzero_ps(), s0b = s0a, s1a = s0a, s1b = s0a;
    __m256 s2a = s0a, s2b = s0a, s3a = s0a, s3b = s0a;
    (void)B;
    for (int kk = 0; kk < N; kk += KBLOCK) {
        int kend = kk + KBLOCK; if (kend > N) kend = N;
        for (int i = 0; i + 4 <= N; i += 4)
            for (int k = kk; k < kend; k++) {
                __m256 a0 = _mm256_set1_ps(A[(i+0)*N+k]);
                __m256 a1 = _mm256_set1_ps(A[(i+1)*N+k]);
                __m256 a2 = _mm256_set1_ps(A[(i+2)*N+k]);
                __m256 a3 = _mm256_set1_ps(A[(i+3)*N+k]);
                for (int j = 0; j + 16 <= N; j += 16) {
                    s0a = _mm256_fmadd_ps(a0, bc, s0a);
                    s0b = _mm256_fmadd_ps(a0, bc, s0b);
                    s1a = _mm256_fmadd_ps(a1, bc, s1a);
                    s1b = _mm256_fmadd_ps(a1, bc, s1b);
                    s2a = _mm256_fmadd_ps(a2, bc, s2a);
                    s2b = _mm256_fmadd_ps(a2, bc, s2b);
                    s3a = _mm256_fmadd_ps(a3, bc, s3a);
                    s3b = _mm256_fmadd_ps(a3, bc, s3b);
                }
            }
    }
    __m256 z = _mm256_add_ps(
        _mm256_add_ps(_mm256_add_ps(s0a,s0b), _mm256_add_ps(s1a,s1b)),
        _mm256_add_ps(_mm256_add_ps(s2a,s2b), _mm256_add_ps(s3a,s3b)));
    float buf[8]; _mm256_storeu_ps(buf, z);
    g_sink = buf[0]+buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7];
    C[0] = g_sink;
}

typedef void (*fn_t)(int, const data_t*, const data_t*, data_t*);

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

    static const char *names[5] = { "full", "no_B", "no_loadC", "no_storeC", "none" };
    fn_t      fns[5] = { v_full, v_noB, v_noload, v_nostore, v_none };
    double    best[5];
    for (int i = 0; i < 5; i++) best[i] = 1e30;

    /* 预热一遍，把三个数组都过一遍 */
    for (int i = 0; i < 5; i++) fns[i](N, A, B, C);

    /* 交错跑：每轮五个都跑，机器漂移对五个影响一样 */
    for (int r = 0; r < ROUNDS; r++)
        for (int i = 0; i < 5; i++) {
            double t0 = now_sec(); fns[i](N, A, B, C); double t1 = now_sec();
            if (t1 - t0 < best[i]) best[i] = t1 - t0;
        }

    double tf = best[0];
    printf("\n=== v4 shape: what does each memory stream cost?   N = %d ===\n\n", N);
    printf("  variant      time(ms)   GFLOPS    speedup   time saved vs full\n");
    printf("  ---------------------------------------------------------------\n");
    for (int i = 0; i < 5; i++) {
        double g = 2.0*N*N*(double)N/best[i]/1e9;
        printf("  %-10s %9.2f  %8.2f    %5.2fx    %6.1f %%\n",
               names[i], best[i]*1000.0, g, tf/best[i], (tf-best[i])/tf*100.0);
    }
    printf("\n  full = 100%%.  Each row shows how much of full's time that stream costs.\n");
    printf("  If (no_B saving + no_loadC saving + no_storeC saving) > (none saving),\n");
    printf("  the three streams overlap -- they fight over the same hardware.\n\n");
    return 0;
}
