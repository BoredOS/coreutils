#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const unsigned long long iterations = 10000000ULL;
    volatile long long acc = 0;

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (unsigned long long i = 0; i < iterations; ++i) {
        acc += getpid();
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    double elapsed = (double)(ts_end.tv_sec - ts_start.tv_sec) +
                     (double)(ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;

    printf("%llu getpid() calls took %.6f seconds\n", iterations, elapsed);
    if (acc == 0) {
        return 1;
    }
    return 0;
}
