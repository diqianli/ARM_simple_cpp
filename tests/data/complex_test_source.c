/**
 * complex_test_source.c - Complex ARM64 test with loops and data dependencies
 *
 * Produces a rich visualization with many pipeline stages, dependencies,
 * and cache interactions. Includes nested loops, chain dependencies,
 * and memory access patterns.
 *
 * Compile: aarch64-elf-gcc -static -O1 -nostdlib -o complex_test_aarch64 complex_test_source.c
 */

void _start(void) __attribute__((noreturn));
void _start(void) {
    register long x0  asm("x0");
    register long x1  asm("x1");
    register long x2  asm("x2");
    register long x3  asm("x3");
    register long x4  asm("x4");
    register long x5  asm("x5");
    register long x6  asm("x6");
    register long x7  asm("x7");
    register long x8  asm("x8");
    register long x9  asm("x9");
    register long x10 asm("x10");
    register long x19 asm("x19");
    register long x20 asm("x20");
    register long x21 asm("x21");
    register long x22 asm("x22");
    register long x23 asm("x23");
    register long x24 asm("x24");
    register long x25 asm("x25");
    register long x26 asm("x26");
    register long x27 asm("x27");
    register long x28 asm("x28");

    /* Initialize */
    x0 = 0; x1 = 1; x2 = 2; x3 = 0; x4 = 0; x5 = 0;
    x6 = 0; x7 = 0; x8 = 0; x9 = 0; x10 = 0;

    long data[32];
    for (int i = 0; i < 32; i++) data[i] = i + 1;

    /*
     * Loop 1: Fibonacci-like chain (10 iterations)
     * Creates long RAW dependency chains: each iteration depends on previous
     */
    x19 = 0; x20 = 1; x21 = 0;
    for (int i = 0; i < 10; i++) {
        asm volatile("add %0, %1, %2" : "=r"(x21) : "r"(x19), "r"(x20));
        asm volatile("mov %0, %1" : "=r"(x19) : "r"(x20));
        asm volatile("mov %0, %1" : "=r"(x20) : "r"(x21));
    }

    /*
     * Loop 2: Accumulation with memory (8 iterations)
     * Mix of compute and load/store, creates cache traffic
     */
    x22 = 0; x23 = 0;
    for (int i = 0; i < 8; i++) {
        asm volatile("ldr %0, [%1, %2]" : "=r"(x24) : "r"(data), "r"(x23));
        asm volatile("add %0, %1, %2" : "=r"(x22) : "r"(x22), "r"(x24));
        asm volatile("str %0, [%1, %2]" :: "r"(x22), "r"(data), "r"(x23));
        asm volatile("add %0, %0, #8" : "=r"(x23));
    }

    /*
     * Loop 3: Parallel computation (16 iterations)
     * Independent chains that issue in parallel
     */
    x24 = 1; x25 = 1; x26 = 1; x27 = 1;
    for (int i = 0; i < 16; i++) {
        asm volatile("mul %0, %0, %1" : "=r"(x24) : "r"(x2));
        asm volatile("mul %0, %0, %1" : "=r"(x25) : "r"(x3));
        asm volatile("add %0, %0, %1" : "=r"(x26) : "r"(x1));
        asm volatile("sub %0, %0, %1" : "=r"(x27) : "r"(x0));
    }

    /*
     * Loop 4: Bitwise operations (12 iterations)
     */
    x19 = 0xFFFF; x20 = 0;
    for (int i = 0; i < 12; i++) {
        asm volatile("and %0, %1, %2" : "=r"(x21) : "r"(x19), "r"(x19));
        asm volatile("orr %0, %0, %1" : "=r"(x20) : "r"(x21));
        asm volatile("eor %0, %1, %2" : "=r"(x22) : "r"(x19), "r"(x20));
        asm volatile("lsl %0, %1, #1" : "=r"(x23) : "r"(x22));
        asm volatile("lsr %0, %1, #2" : "=r"(x24) : "r"(x23));
    }

    /*
     * Loop 5: Mixed ALU with branches (8 iterations)
     * Conditional branches create pipeline bubbles
     */
    x19 = 100; x20 = 0;
    for (int i = 0; i < 8; i++) {
        asm volatile("cmp %0, #50" :: "r"(x19));
        asm volatile("b.gt 2f");
        asm volatile("add %0, %0, #10" : "=r"(x20));
        asm volatile("b 3f");
        asm volatile("2:");
        asm volatile("sub %0, %0, #5" : "=r"(x20));
        asm volatile("3:");
        asm volatile("sub %0, %0, #7" : "=r"(x19));
    }

    /* Floating point section */
    double d0, d1, d2, d3, d4;
    d1 = 1.0; d2 = 2.0; d4 = 0.0;
    for (int i = 0; i < 8; i++) {
        asm volatile("fadd d0, d1, d2");
        asm volatile("fsub d3, d0, d1");
        asm volatile("fmul d4, d3, d2");
        asm volatile("fmadd d1, d4, d2, d0");
    }

    /* Memory barriers */
    asm volatile("dmb ish" ::: "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");

    /* Terminate */
    x0 = 42;
    while (1) { asm volatile("" ::: "memory"); }
    __builtin_unreachable();
}
