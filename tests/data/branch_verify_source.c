/**
 * branch_verify_source.c - Branch prediction verification
 *
 * Compile with -O0 to prevent branch optimization.
 * Uses volatile and function args to prevent constant folding.
 *
 * Pattern 1: B.GT TAKEN (arg1=10 > 5)
 * Pattern 2: B.GT NOT TAKEN (arg2=3 < 100)
 * Pattern 3: B unconditional TAKEN (skip NOP)
 */

void _start(void) __attribute__((noreturn));
void _start(void) {
    register long x19 asm("x19");
    register long x20 asm("x20");
    register long x21 asm("x21");
    register long x22 asm("x22");
    register long x23 asm("x23");
    register long x24 asm("x24");
    register long x25 asm("x25");
    register long x26 asm("x26");

    /* Use volatile to prevent constant folding */
    volatile long v1 = 10;
    volatile long v2 = 3;
    volatile long v3 = 5;
    volatile long v4 = 100;

    x19 = v1;  /* 10 */
    x20 = v2;  /* 3 */

    /* --- Pattern 1: conditional branch TAKEN (10 > 5) --- */
    if (x19 > v3) {
        asm volatile("mul %0, %1, %2" : "=r"(x23) : "r"(x19), "r"(x20));
    } else {
        asm volatile("sub %0, %0, #1" : "+r"(x21));
        asm volatile("sub %0, %0, #1" : "+r"(x22));
    }

    /* --- Pattern 2: conditional branch NOT TAKEN (3 < 100) --- */
    if (x20 > v4) {
        asm volatile("orr %0, %0, #1" : "+r"(x25));
    } else {
        asm volatile("and %0, %1, %2" : "=r"(x24) : "r"(x19), "r"(x20));
    }

    /* --- Pattern 3: EOR + unconditional branch (skip NOP) --- */
    asm volatile("eor %0, %1, %2" : "=r"(x26) : "r"(x24), "r"(x23));
    asm volatile("b .+8");
    asm volatile("nop");
    asm volatile("add %0, %0, #1" : "+r"(x26));

    /* Barrier + halt */
    asm volatile("dmb ish" ::: "memory");
    x19 = 0;
    while (1) { asm volatile("" ::: "memory"); }
    __builtin_unreachable();
}
