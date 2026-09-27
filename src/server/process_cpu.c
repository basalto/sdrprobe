#define _POSIX_C_SOURCE 200809L

#include "process_cpu.h"

#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

static double timeval_seconds(struct timeval tv) {
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

int process_cpu_sample_now(struct process_cpu_sample *out) {
    struct rusage ru;
    struct timespec ts;

    memset(out, 0, sizeof(*out));
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return -1;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;
    out->wall_seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    out->cpu_seconds = timeval_seconds(ru.ru_utime) + timeval_seconds(ru.ru_stime);
    return 0;
}

double process_cpu_percent(const struct process_cpu_sample *earlier,
                           const struct process_cpu_sample *later) {
    double wall_elapsed = later->wall_seconds - earlier->wall_seconds;
    double cpu_elapsed = later->cpu_seconds - earlier->cpu_seconds;

    if (wall_elapsed <= 0.0)
        return 0.0;
    if (cpu_elapsed < 0.0)
        cpu_elapsed = 0.0; /* a clock oddity, not a negative CPU cost */
    return 100.0 * cpu_elapsed / wall_elapsed;
}
