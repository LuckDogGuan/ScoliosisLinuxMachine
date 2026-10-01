#include "v3.h"
#include <time.h>
uint64_t v3_now(void) {
    struct timespec clock;
    if (clock_gettime(CLOCK_MONOTONIC, &clock)) return 0;
    return (uint64_t)clock.tv_sec * 1000 + (uint64_t)clock.tv_nsec / 1000000;
}
uint64_t wall_ms(void) {
    struct timespec clock;
    if (clock_gettime(CLOCK_REALTIME, &clock)) return 0;
    return (uint64_t)clock.tv_sec * 1000 + (uint64_t)clock.tv_nsec / 1000000;
}
