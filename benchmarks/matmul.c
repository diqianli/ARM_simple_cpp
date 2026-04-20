/// Matrix multiplication benchmark - memory-heavy, compute-intensive
/// 4x4 matrix multiply (kept small for simulator compatibility)

#define N 4
typedef unsigned long long uint64;

volatile uint64 result;

static uint64 A[N*N];
static uint64 B[N*N];
static uint64 C[N*N];

void __attribute__((noinline)) matmul(void) {
    // Initialize
    for (int i = 0; i < N*N; i++) {
        A[i] = i + 1;
        B[i] = (i + 1) * 2;
    }

    // Multiply
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            uint64 sum = 0;
            for (int k = 0; k < N; k++) {
                sum += A[i*N + k] * B[k*N + j];
            }
            C[i*N + j] = sum;
        }
    }

    result = C[0] + C[N*N-1];
}

void _start(void) {
    matmul();
    for (;;) {}
}
