// Minimal test: MUL -> dependent ADD (back-to-back wakeup check)
int main() {
    volatile int a = 3, b = 4, c;
    c = a * b;    // MUL
    c = c + 1;    // ADD (depends on MUL result)
    return c;
}
