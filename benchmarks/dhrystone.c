/// Dhrystone-like benchmark - classic CPU performance benchmark
/// Adapted for bare-metal ARM (no libc)

typedef unsigned long long uint64;
typedef long long int64;

volatile uint64 result;

// Dhrystone core variables
typedef struct {
    int64 ident1, ident2;
    int64 int1, int2, int3;
    int64 arr1[5], arr2[5];
    char ch1, ch2;
} Rec;

static int64 globally;
static Rec record;

static int64 func1(Rec* r, int64 val) {
    r->int1 += val;
    r->int2 += r->int1;
    r->int3 = r->int2 - r->int1;
    r->int1 = r->int3 + val;
    return val;
}

static int64 func2(int64 a, int64 b) {
    int64 c = a + b;
    globally = a;
    return c;
}

static int64 func3(int64* p) {
    *p = *p * 2;
    return *p;
}

void __attribute__((noinline)) dhrystone(void) {
    int64 i;
    globally = 0;
    record.ident1 = 1;
    record.ident2 = 2;
    record.int1 = 3;
    record.int2 = 4;
    record.int3 = 5;
    record.ch1 = 'A';
    record.ch2 = 'B';

    for (i = 0; i < 50; i++) {
        func1(&record, func2(record.int1, record.int2));
        func3(&record.int3);
        record.arr1[0] = record.int1;
        record.arr2[0] = record.int2;
        record.arr1[record.int3 & 3] = record.int1 + record.int2;
        if (record.ch1 == 'A') {
            record.ch1 = 'B';
        } else {
            record.ch1 = 'A';
        }
    }

    result = record.int1 + record.int2 + record.int3;
}

void _start(void) {
    dhrystone();
    for (;;) {}
}
