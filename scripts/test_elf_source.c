/**
 * test_elf_source.c - ARM64 综合指令测试程序
 *
 * 用于测试 ARM CPU 仿真器对各类 ARMv8 指令的支持。
 * 覆盖：整数 ALU、Load/Store、分支、浮点、SIMD、内存屏障。
 *
 * 编译方式：
 *   aarch64-linux-gnu-gcc -static -O2 -nostdlib -o test_elf_aarch64 test_elf_source.c
 *   或使用 scripts/compile_test_elf.sh
 */

/* 避免 libgcc 依赖，手动提供 _start */
void _start(void) __attribute__((noreturn));
void _start(void) {
    /* === 整数 ALU 指令 === */
    long a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p;
    long q, r, s, t, u, v, w, x, y;

    /* 寄存器别名 */
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
    register long x11 asm("x11");
    register long x12 asm("x12");
    register long x13 asm("x13");
    register long x14 asm("x14");
    register long x15 asm("x15");
    register long x16 asm("x16");
    register long x17 asm("x17");
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

    /* 初始化寄存器值 */
    x0 = 1; x1 = 2; x2 = 3; x3 = 4; x4 = 5; x5 = 6;
    x6 = 7; x7 = 8; x8 = 9; x9 = 10; x10 = 11; x11 = 12;
    x12 = 13; x13 = 14; x14 = 15; x15 = 16; x16 = 17; x17 = 18;
    x19 = 19; x20 = 20; x21 = 21; x22 = 22; x23 = 23; x24 = 24;
    x25 = 25; x26 = 26; x27 = 27; x28 = 28;

    /* ADD, SUB, MUL, AND, ORR, EOR */
    asm volatile("add %0, %1, %2" : "=r"(x0) : "r"(x1), "r"(x2));
    asm volatile("sub %0, %1, %2" : "=r"(x3) : "r"(x4), "r"(x5));
    asm volatile("mul %0, %1, %2" : "=r"(x6) : "r"(x7), "r"(x8));
    asm volatile("and %0, %1, %2" : "=r"(x9) : "r"(x10), "r"(x11));
    asm volatile("orr %0, %1, %2" : "=r"(x12) : "r"(x13), "r"(x14));
    asm volatile("eor %0, %1, %2" : "=r"(x15) : "r"(x16), "x17");

    /* LSL, LSR, MOV */
    asm volatile("lsl %0, %1, #3" : "=r"(x19) : "r"(x20));
    asm volatile("lsr %0, %1, #4" : "=r"(x21) : "r"(x22));
    asm volatile("mov %0, %1" : "=r"(x23) : "r"(x24));

    /* CMP, CSEL */
    asm volatile("cmp %0, %1" :: "r"(x0), "r"(x25));
    asm volatile("csel %0, %1, %2, eq" : "=r"(x26) : "r"(x0), "r"(x1));

    /* === Load/Store 指令 === */
    long mem[8] = {0};
    asm volatile("ldr %0, [%1, #16]" : "=r"(x27) : "r"(x28), "m"(mem[2]));
    asm volatile("str %0, [%1, #24]" :: "r"(x27), "r"(x28), "m"(mem[3]));
    asm volatile("ldp %0, %1, [%2, #32]" : "=r"(x0), "=r"(x1) : "r"(x28), "m"(mem[4]), "m"(mem[5]));
    asm volatile("stp %0, %1, [%2, #48]" :: "r"(x2), "r"(x3), "r"(x28), "m"(mem[6]), "m"(mem[7]));

    /* === 分支指令 === */
    asm volatile("b 2f");  /* forward branch */
    asm volatile("nop");
    asm volatile("2:");
    asm volatile("cbz %0, 2f" :: "r"(x0));  /* conditional branch */
    asm volatile("b.ne 2f");
    asm volatile("2:");

    /* === 浮点指令 (NEON) === */
    double d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12;
    d1 = 1.0; d2 = 2.0; d4 = 4.0; d5 = 3.0;
    d6 = 2.0; d7 = 3.0; d8 = 4.0;
    d9 = 1.0; d10 = 2.0; d11 = 3.0; d12 = 4.0;

    asm volatile("fadd %d0, %d1, %d2" : "=w"(d0) : "w"(d1), "w"(d2));
    asm volatile("fsub %d3, %d4, %d5" : "=w"(d3) : "w"(d4), "w"(d5));
    asm volatile("fmul %d6, %d7, %d8" : "=w"(d6) : "w"(d7), "w"(d8));
    asm volatile("fmadd %d9, %d10, %d11, %d12" : "=w"(d9) : "w"(d10), "w"(d11), "w"(d12));

    /* === SIMD 指令 === */
    float v0[4], v1[4], v2[4], v3[4], v4[4], v5[4], v6[4], v7[4], v8[4], v9[4];
    for (int i = 0; i < 4; i++) { v1[i] = 1.0f; v2[i] = 2.0f; v4[i] = 4.0f; v5[i] = 3.0f; v7[i] = 3.0f; v8[i] = 4.0f; v10 = 11; }

    asm volatile("add v0.4s, v1.4s, v2.4s" : "=w"(*v0) : "w"(*v1), "w"(*v2));
    asm volatile("sub v3.4s, v4.4s, v5.4s" : "=w"(*v3) : "w"(*v4), "w"(*v5));
    asm volatile("mul v6.4s, v7.4s, v8.4s" : "=w"(*v6) : "w"(*v7), "w"(*v8));
    asm volatile("dup v9.4s, w10" : "=w"(*v9) : "r"(x10));

    /* === 内存屏障 === */
    asm volatile("dmb ish" ::: "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");

    /* === 终止：设置返回值 42 并死循环（无 OS 环境） === */
    x0 = 42;
    while (1) {
        asm volatile("" ::: "memory"); /* 防止编译器优化掉循环 */
    }
    __builtin_unreachable();
}
