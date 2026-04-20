/// Linked list traversal benchmark - pointer-chasing, irregular memory access
/// Uses an array to simulate a linked list (16 nodes)

#define NODES 16
typedef unsigned long long uint64;

volatile uint64 result;

typedef struct Node {
    uint64 value;
    uint64 next;  // index of next node
} Node;

static Node nodes[NODES];

void __attribute__((noinline)) linkedlist_traverse(void) {
    // Build linked list: 0 -> 1 -> 2 -> ... -> NODES-1 -> 0 (circular)
    for (int i = 0; i < NODES; i++) {
        nodes[i].value = i * i;
        nodes[i].next = (i + 1) % NODES;
    }

    // Traverse the list 3 full cycles
    uint64 sum = 0;
    uint64 current = 0;
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int i = 0; i < NODES; i++) {
            sum += nodes[current].value;
            current = nodes[current].next;
        }
    }
    result = sum;
}

void _start(void) {
    linkedlist_traverse();
    for (;;) {}
}
