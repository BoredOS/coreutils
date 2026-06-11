#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char **argv) {
    const unsigned long long iterations = 10000000ULL;
    volatile long long acc = 0;
    clock_t start = clock();
    for (unsigned long long i = 0; i < iterations; ++i) {
        acc += getpid();
    }
    clock_t end = clock();
    double elapsed = (double)(end - start) / (double)CLOCKS_PER_SEC;
    printf("%llu getpid() calls took %.6f seconds\n", iterations, elapsed);
    if (acc == 0) {
        return 1;
    }
    return 0;
}
