/// Memory copy benchmark - pure memory bandwidth test
/// Copies blocks of data between arrays (small for simulator compatibility)

#define SIZE 64   // number of uint64 elements
#define ITERS 5
typedef unsigned long long uint64;

volatile uint64 result;

static uint64 src[SIZE];
static uint64 dst[SIZE];

static void __attribute__((noinline)) manual_memcpy(uint64* d, const uint64* s, int n) {
    for (int i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

void __attribute__((noinline)) memcpy_bench(void) {
    // Initialize source
    for (int i = 0; i < SIZE; i++) {
        src[i] = i * 7 + 13;
    }

    // Copy ITERS times
    for (int iter = 0; iter < ITERS; iter++) {
        manual_memcpy(dst, src, SIZE);
    }

    // Compute checksum
    uint64 sum = 0;
    for (int i = 0; i < SIZE; i++) {
        sum += dst[i];
    }
    result = sum;
}

void _start(void) {
    memcpy_bench();
    for (;;) {}
}
