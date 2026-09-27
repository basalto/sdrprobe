#define _POSIX_C_SOURCE 200809L

#include "browser.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * The double fork, and why it is this rather than a SIGCHLD handler.
 *
 * A single fork()+exec() leaves a zombie until something reaps it, and this
 * process has nothing to reap it *with*: `viewer_session_run()`'s loop is
 * not going to sit in a `waitpid()` for a browser that may run for the rest
 * of the session. A `SIGCHLD` handler would work too, but it is one more
 * piece of process-wide signal state to keep straight against the
 * `SIGINT`/`SIGTERM` handling `install_signal_handlers()` already owns, for
 * a child this process has no further reason to hear from.
 *
 * The double fork sidesteps both. The immediate child forks once more and
 * exits at once, so the one blocking wait below is on a child that is
 * already on its way out by the time the wait call returns -- not on the
 * browser launcher, which by then has no parent at all and is init's to
 * reap whenever it exits, however long that is.
 *
 * The signal mask at the moment this forks is the ordinary, unblocked one:
 * `viewer_session_run()` is called from `run_headless()` after
 * `start_acquisition()` has already run its own transient
 * `SIGINT`/`SIGTERM` block-and-restore around its `pthread_create()` and
 * returned -- checked in the code rather than assumed. What *is* inherited
 * across the fork is `install_signal_handlers()`'s handler for both signals,
 * which is why the immediate child resets them to default before doing
 * anything else: a stray Ctrl-C in the microseconds before it exits should
 * do nothing, not run the parent's handler in a forked, about-to-exit
 * child.
 */
int browser_open(const char *url) {
    pid_t first;

    if (!url || !*url)
        return -1;

    first = fork();
    if (first < 0)
        return -1;
    if (first == 0) {
        pid_t second;

        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);

        second = fork();
        if (second == 0) {
            /*
             * The grandchild: reparented to init the instant this, its
             * parent, exits below, so nothing in this process tree ever
             * waits for it and nothing here ever reaps it.
             *
             * Its stdin, stdout and stderr are not this program's to spend
             * on a browser's own diagnostics -- and none of the three is
             * inherited past this: `execlp()` replaces the process image but
             * not its open file descriptors, so a browser or its launcher
             * finding a socket or a debug log on one of these three would be
             * this program's mistake, not xdg-open's.
             */
            int null_fd = open("/dev/null", O_RDWR);

            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO);
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                if (null_fd > STDERR_FILENO)
                    close(null_fd);
            }
            execlp("xdg-open", "xdg-open", url, (char *)NULL);
            /* xdg-open was not found, or could not run -- and there is
               nothing left to report it to: stdin/out/err are /dev/null
               now, and no parent is waiting on an exit status. Silent by
               construction, which is the alternative to silent by
               omission. */
            _exit(127);
        }
        /*
         * Whichever way the second fork went, this child's only remaining
         * job is to get out of the parent's one wait below as fast as
         * possible -- a failed second fork is not this function's failure
         * to report, since the first fork (the one the caller can see)
         * already succeeded and handed off.
         */
        _exit(0);
    }

    /*
     * The one wait in this function, and it is not a wait for the browser:
     * `first` exits within microseconds of the fork above, win or lose, so
     * this returns almost at once whatever xdg-open goes on to do.
     */
    waitpid(first, NULL, 0);
    return 0;
}
