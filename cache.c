/* cache.c -- 实测各级 cache 的容量（指针追逐）
 *
 * 做法：把 n 个指针串成一个环（索引经过随机打乱，破坏预取器），
 * 沿着环一跳一跳地走，测每跳平均多少纳秒。工作集从小往大扫，
 * 每跨过一个 cache 的容量，延迟会跳一档。
 *
 * 为什么必须用 -O0 编：这类探针全是相互依赖的加载，编译器优化会直接
 * 把整个追逐循环删掉（-O2 删干净，-O1 删掉大半）。实测过：
 *   -O2  -> 2e6 次跳只花了 100 ns，显然是假的
 *   -O0  -> 2e6 次跳花 2.4 ms，16 KB 时 1.22 ns/跳 = 5.6 周期，这才是 L1 延迟
 *
 * 为什么要测：日志里 L1 = 48 KB / L2 = 1.25 MB / L3 = 24 MB 都是查来的规格，
 * 而 v3 的分块公式整个压在 L2 = 1.25 MB 上。规格和实物对不上，后面的账就全错。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <malloc.h>

static volatile void *g_sink;      /* 防止追逐循环被优化掉 */

static double now_sec(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* 返回每跳的纳秒数。失败返回 -1。 */
static double chase(size_t bytes) {
    size_t n = bytes / sizeof(void*);
    if (n < 64) return -1;

    void **a = (void**)_aligned_malloc(n * sizeof(void*), 64);
    size_t *idx = (size_t*)malloc(n * sizeof(size_t));
    if (!a || !idx) { free(idx); _aligned_free(a); return -1; }

    for (size_t i = 0; i < n; i++) idx[i] = i;
    /* 费雪-耶茨打乱，破坏预取器的规律性 */
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(rand() % (int)(i + 1));
        size_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (size_t i = 0; i < n; i++)
        a[idx[i]] = (void*)&a[idx[(i + 1) % n]];

    /* 预热：走半圈，把这一级填满 */
    void **cur = (void**)&a[idx[0]];
    for (size_t i = 0; i < n / 2; i++) cur = (void**)*cur;

    /* 每个元素至少走两次；小工作集也要有足够的跳数才测得准 */
    size_t iters = n * 2;
    if (iters < 2000000) iters = 2000000;

    /* 每个工作集测三次，取最短的那次 —— 干扰只会让读数变长 */
    double best = 1e30;
    for (int rep = 0; rep < 3; rep++) {
        double t0 = now_sec();
        for (size_t i = 0; i < iters; i++) cur = (void**)*cur;
        double t1 = now_sec();
        if (t1 - t0 < best) best = t1 - t0;
    }

    g_sink = (void*)cur;
    double ns = best / (double)iters * 1e9;
    free(idx);
    _aligned_free(a);
    return ns;
}

int main(void) {
    static const size_t sizes[] = {
        16u<<10,  32u<<10,  48u<<10,  64u<<10,  96u<<10,  128u<<10,
        256u<<10, 512u<<10, 768u<<10, 1024u<<10, 1280u<<10,
        /* 1.25 ~ 2.5 MB 这一段加密，L2 的拐点在这里 */
        1536u<<10, 1664u<<10, 1792u<<10, 1920u<<10, 2048u<<10, 2304u<<10,
        3u<<20,   4u<<20,   6u<<20,   8u<<20,   12u<<20,
        16u<<20,  20u<<20,  24u<<20,  32u<<20,  48u<<20
    };
    int cnt = (int)(sizeof(sizes)/sizeof(sizes[0]));

    srand(12345);
    printf("\n=== cache hierarchy probe (pointer chasing) ===\n\n");
    printf("  working set     ns/hop    cycles @4.63GHz\n");
    printf("  --------------------------------------------\n");

    for (int i = 0; i < cnt; i++) {
        double ns = chase(sizes[i]);
        if (ns < 0) { printf("  %10.0f KB     failed\n", sizes[i] / 1024.0); continue; }
        printf("  %10.0f KB   %6.2f       %6.1f\n",
               sizes[i] / 1024.0, ns, ns * 4.63);
    }
    printf("\n  A step up in latency marks the capacity of one level.\n");
    printf("  Expect: flat in L1 (~5 cycles) -> step at L2 -> step at L3 -> plateau at DRAM.\n\n");
    return 0;
}
