// Minimal test: DIV -> dependent ADD (back-to-back wakeup check)
int main() {
    volatile int a = 100, b = 3, c;
    c = a / b;    // DIV (udiv in ARM)
    c = c + 1;    // ADD (depends on DIV result)
    return c;
}
