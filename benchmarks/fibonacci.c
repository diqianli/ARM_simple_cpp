/// Fibonacci benchmark - recursive, branch-heavy, data dependencies
/// Computes fib(25) using iterative method (avoids stack overflow on bare-metal)

typedef unsigned long long uint64;

volatile uint64 result;

void __attribute__((noinline)) compute_fibonacci(void) {
    uint64 a = 0, b = 1;
    for (int i = 0; i < 35; i++) {
        uint64 tmp = a + b;
        a = b;
        b = tmp;
    }
    result = a;
}

void _start(void) {
    compute_fibonacci();
    // Infinite loop to signal completion (simulator detects this)
    for (;;) {}
}
