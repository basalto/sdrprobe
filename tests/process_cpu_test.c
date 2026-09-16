#include "check.h"

#include "process_cpu.h"

/*
 * The arithmetic only, against synthetic samples -- process_cpu_sample_now()
 * is the one syscall-touching half nothing here can pin, but the percentage
 * it feeds is a pure function of two (wall, cpu) pairs and belongs to a
 * check that needs no process to run.
 */

static void test_half_the_wall_time_is_fifty_percent(void) {
    struct process_cpu_sample earlier = { 10.0, 1.0 };
    struct process_cpu_sample later = { 12.0, 2.0 }; /* 1.0 cpu s / 2.0 wall s */

    check_close("half the wall time on CPU is 50%",
               process_cpu_percent(&earlier, &later), 50.0, 1e-9);
}

static void test_idle_process_is_zero_percent(void) {
    struct process_cpu_sample earlier = { 10.0, 1.0 };
    struct process_cpu_sample later = { 15.0, 1.0 }; /* no CPU time spent */

    check_close("no CPU time over five wall seconds is 0%",
               process_cpu_percent(&earlier, &later), 0.0, 1e-9);
}

static void test_zero_elapsed_wall_time_refuses_rather_than_divides(void) {
    struct process_cpu_sample same = { 10.0, 1.0 };

    check_close("identical samples report 0%, not a division by zero",
               process_cpu_percent(&same, &same), 0.0, 1e-9);
}

static void test_wall_time_going_backwards_also_refuses(void) {
    struct process_cpu_sample earlier = { 10.0, 1.0 };
    struct process_cpu_sample later = { 9.0, 1.5 }; /* a clock oddity */

    check_close("a negative wall interval reports 0%, not a negative percent",
               process_cpu_percent(&earlier, &later), 0.0, 1e-9);
}

static void test_a_multithreaded_process_can_exceed_100_percent(void) {
    struct process_cpu_sample earlier = { 0.0, 0.0 };
    struct process_cpu_sample later = { 1.0, 1.8 }; /* two busy threads */

    check_close("1.8 CPU seconds in 1 wall second is 180%, uncapped",
               process_cpu_percent(&earlier, &later), 180.0, 1e-9);
}

static void test_a_cpu_time_glitch_does_not_go_negative(void) {
    struct process_cpu_sample earlier = { 10.0, 5.0 };
    struct process_cpu_sample later = { 11.0, 4.9 }; /* cpu time "shrank" */

    check_close("a cpu-time regression is floored at 0%, not negative",
               process_cpu_percent(&earlier, &later), 0.0, 1e-9);
}

static void test_sample_now_reports_something_plausible(void) {
    struct process_cpu_sample a, b;
    int i;
    volatile long spin = 0;

    check_int("sampling this process succeeds", process_cpu_sample_now(&a), 0);
    for (i = 0; i < 20000000; i++)
        spin += i; /* burn a little real CPU time between samples */
    check_int("sampling it again succeeds", process_cpu_sample_now(&b), 0);
    check_true("the spin loop actually ran", spin > 0);
    check_true("wall time moved forward", b.wall_seconds >= a.wall_seconds);
    check_true("cpu time did not go backwards", b.cpu_seconds >= a.cpu_seconds);
    check_true("the percentage over a real sample is never negative",
              process_cpu_percent(&a, &b) >= 0.0);
}

int main(void) {
    test_half_the_wall_time_is_fifty_percent();
    test_idle_process_is_zero_percent();
    test_zero_elapsed_wall_time_refuses_rather_than_divides();
    test_wall_time_going_backwards_also_refuses();
    test_a_multithreaded_process_can_exceed_100_percent();
    test_a_cpu_time_glitch_does_not_go_negative();
    test_sample_now_reports_something_plausible();
    return check_report("a process's own CPU cost against wall time");
}
