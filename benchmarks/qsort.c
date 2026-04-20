/// Quick sort benchmark - branch misprediction, memory operations
/// Sorts an array of 16 integers (kept small for simulator compatibility)

#define SIZE 16
typedef unsigned long long uint64;

volatile uint64 result;

static uint64 arr[SIZE];

static void swap(uint64* a, uint64* b) {
    uint64 tmp = *a;
    *a = *b;
    *b = tmp;
}

static int partition(uint64* a, int lo, int hi) {
    uint64 pivot = a[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        if (a[j] < pivot) {
            i++;
            swap(&a[i], &a[j]);
        }
    }
    swap(&a[i+1], &a[hi]);
    return i + 1;
}

static void quicksort(uint64* a, int lo, int hi) {
    if (lo < hi) {
        int p = partition(a, lo, hi);
        quicksort(a, lo, p - 1);
        quicksort(a, p + 1, hi);
    }
}

void __attribute__((noinline)) sort_bench(void) {
    // Initialize with reverse-sorted array (worst case for quicksort)
    for (int i = 0; i < SIZE; i++) {
        arr[i] = SIZE - i;
    }

    quicksort(arr, 0, SIZE - 1);

    result = arr[0] + arr[SIZE-1];
}

void _start(void) {
    sort_bench();
    for (;;) {}
}
