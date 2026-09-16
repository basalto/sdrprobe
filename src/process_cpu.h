#ifndef PROCESS_CPU_H
#define PROCESS_CPU_H

/*
 * This process's own CPU cost against wall time -- the "server CPU"
 * ticket 08's Health panel shows, and nothing else. No `struct app`, no
 * raylib: a fact about the OS process, read the same way whether this is
 * running headless under `--serve` or under a window.
 *
 * Split into a sample (the impure half, one syscall each) and a pure
 * percentage over two samples, on the same principle CLAUDE.md states for
 * drawing and deciding: the arithmetic is worth a check that needs no
 * process to run, and `process_cpu_sample_now()` is the only part that
 * cannot be one.
 */

struct process_cpu_sample {
    double wall_seconds;
    double cpu_seconds; /* user + system, this process, every thread --
                           getrusage(RUSAGE_SELF) sums them on Linux, so
                           this already includes the acquisition worker,
                           not only the serving loop. */
};

/* Reads the current wall clock and this process's cumulative CPU time.
   Returns 0 on success, -1 if the underlying syscalls fail (out is then
   left zeroed, so a caller that ignores the return value gets 0%, not
   garbage). */
int process_cpu_sample_now(struct process_cpu_sample *out);

/*
 * The percentage of wall time spent on CPU between two samples of the
 * same process. Returns 0.0 rather than dividing by (near) zero when the
 * two samples are not far enough apart in wall time to mean anything --
 * this is the caller's job to avoid by spacing samples out, but a
 * defensive 0 is cheaper than a NaN reaching a Viewer. Not capped at
 * 100: a multi-threaded process can spend more than one wall-second of
 * CPU time per wall-second, and reporting that honestly is the point.
 */
double process_cpu_percent(const struct process_cpu_sample *earlier,
                           const struct process_cpu_sample *later);

#endif
