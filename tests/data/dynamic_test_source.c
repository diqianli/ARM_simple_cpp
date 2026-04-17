// dynamic_test_source.c - Dynamically linked test for FunctionalSim
// Tests: printf, malloc, free, strlen, strcpy, strcmp, memcpy, memset, puts

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    int a = 10;
    int b = 20;
    int c = a + b;
    printf("sum = %d\n", c);

    char *s = malloc(32);
    strcpy(s, "hello world");
    printf("s = %s, len = %lu\n", s, strlen(s));
    free(s);

    char buf[16];
    memset(buf, 'A', 15);
    buf[15] = '\0';
    printf("memset: %s\n", buf);

    puts("puts test ok");

    int cmp = strcmp("abc", "abd");
    printf("strcmp(abc,abd) = %d\n", cmp);

    return c;
}
