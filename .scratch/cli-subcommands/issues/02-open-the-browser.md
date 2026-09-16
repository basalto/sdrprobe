# 02 - Open the browser

Status: resolved, 2026-09-16
Blocked by: 01 (done)

## What to build

The one thing `web` does that `server` does not: after the Viewer link is
listening, point a browser at it.

Small in code and the only part of this feature that can hurt. Everything
below is a way a convenience has taken down the thing it was convenient for.

## Where it goes

After `viewer_link_open()` returns 0 and before the loop, in
`viewer_session_run()` -- **not** before the bind. `viewer_link_open()` is
what binds and listens; a browser started ahead of it races the listener and
the reader gets a connection refused on the first load, which looks exactly
like the program being broken.

## What it must not do

- **Must not block.** `system("xdg-open ...")` runs a shell and waits. On a
  machine where the handler is slow to start, that is the whole server
  stalled before its first block.
- **Must not leave a zombie.** A long-running server that never reaps the
  child accumulates one for its lifetime. Double-fork, or handle `SIGCHLD`
  -- and note that `acquisition.c` already blocks `SIGINT`/`SIGTERM` around
  `pthread_create`, so whatever is done here has to be stated against that
  rather than assumed independent of it.
- **Must not fire with nowhere to draw.** With neither `DISPLAY` nor
  `WAYLAND_DISPLAY` set, skip it and say so on stderr. `xdg-open` on a
  headless box is a child that fails in a way nobody sees.
- **Must not take the server down.** A browser that will not start is a
  message, not an exit: the link is listening and the URL is on stderr, which
  is all a reader needs to open it themselves.
- **Must not fire twice**, on a reconnect or a retune.

## Suppressing it

`--no-browser`, and `SDRPROBE_NO_BROWSER` beside the four variables
`options_apply_environment()` already reads. The precedence is the one already
written down: a flag beats a variable beats the config file, and an empty
variable is not a value.

`sdrprobe server` is then exactly `sdrprobe web --no-browser`, which is worth
asserting in the check: two spellings of one behaviour that cannot drift.

## Testability, and where it stops

**The decision is checkable and the act is not.** Whether a browser should
open -- command, `--no-browser`, the environment variable, and whether a
display exists -- is a pure function over `struct options` and a looked-up
environment, and `check-options` reaches it the way it already reaches
`startup_form_wanted()`. A check can drive every row of that table with no
process and no display.

Spawning is one function in a file of its own (`browser_open(url)`), and no
check here starts a browser. Say that in the header rather than leaving a
reader to notice: what is proved is *when* it fires, not *that* the browser
appeared. This is the same line `.scratch/testability` draws everywhere else,
and it is where it falls here.

## Tasks

- [x] Add `--no-browser` and `SDRPROBE_NO_BROWSER`.
- [x] Add a pure `browser_wanted(options, env_lookup)` and check every row.
- [x] Add `browser_open(url)` in its own translation unit: non-blocking, no
      zombie, no shell, stdout and stderr to `/dev/null`.
- [x] Call it once, after `viewer_link_open()` succeeds, and report on stderr
      what it did -- opened, skipped for no display, skipped by request, or
      failed.
- [x] Assert `server` and `web --no-browser` agree, in the check.
- [x] Say in `README.md` how to stop it opening.

## Acceptance criteria

- [x] `sdrprobe web` opens a browser at `http://127.0.0.1:<port>/` after the
      link is listening, never before.
- [x] `sdrprobe web --no-browser` and `sdrprobe server` are indistinguishable.
- [x] With no `DISPLAY` and no `WAYLAND_DISPLAY`, `web` serves and says why it
      did not open anything.
- [x] A browser that cannot start leaves the server running and the URL on
      stderr.
- [x] No zombie after a browser exits -- checked by running `web` for a
      minute and looking, since no unit check can see this.
- [x] `make check` passes and `check-options` covers the decision table.

## Not in scope

- Choosing a browser, or a `--browser <path>`. `xdg-open` is the platform's
  own answer and a second mechanism needs a reason.
- Serving anything but the Scope, or opening a particular view in the browser.

## Done, 2026-09-16

`browser_wanted()` (options.h/.c) is the whole decision, pure over `struct
options` and one environment lookup for `DISPLAY`/`WAYLAND_DISPLAY` -- which
are not among the four questions `options_apply_environment()` answers and
were kept out of it rather than folded in. `--no-browser` and
`SDRPROBE_NO_BROWSER` resolve into `options->no_browser` the same way
`SDRPROBE_NO_STARTUP` resolves into `no_startup`, ahead of anything reading
it.

`browser_open()` is the one function that is not checked, in its own file
(`src/browser.c`/`.h`) with the reason said outright in both: starting a real
subprocess is not something a unit check can watch happen correctly. It is a
double fork, not a `SIGCHLD` handler, and the choice is stated against the
one thing in this codebase that could have collided with it:
`viewer_session_run()` is only ever reached, via `run_headless()`, after
`start_acquisition()`'s own transient `SIGINT`/`SIGTERM` block-and-restore
around its `pthread_create()` has already completed -- checked by reading
`run_headless()`'s call order rather than assumed -- so the fork happens
with the ordinary, unblocked mask. What *is* inherited across the fork is
`install_signal_handlers()`'s handler for both signals, which is why the
immediate child resets them to default before its own second fork, so a
stray Ctrl-C in the microseconds before it exits does nothing rather than
running the parent's handler in a forked, about-to-exit child.

Called once, right after the "Viewer link listening" line -- never before
the bind, which is the one ordering requirement this ticket named, and now
provably true since there is exactly one call site. It reports what it did
on the same stream: `Opening it in a browser.`, or one of two reasons it did
not (`--no-browser`, or no display) -- a failed `xdg-open` itself is silent
by construction, not by omission, since by the time it would fail its own
stdout and stderr are `/dev/null` and nothing is waiting on its exit status.

**check-options**: 364 checks (348 -> 364), green on the first run, table
covering every combination of command, `--no-browser`, the variable, an
empty variable, and both display variables together and apart -- and the
equivalence the flag exists for, `web --no-browser` against `server`,
asserted directly rather than trusted from two separate correct-looking
answers. Four mutations, all caught: dropping the `COMMAND_WEB` check,
dropping the `no_browser` check, `&&` for `||` on the two display variables,
and `SDRPROBE_NO_BROWSER` left unwired in `options_apply_environment()`.
(Two of the four mutations silently failed to apply on the first attempt --
a shell-escaping mistake of mine turned `&&` into `\&\&` in the search
string, so `.replace()` found nothing and the "mutated" build was actually
the original, passing for the wrong reason. Caught by the same rule this
session's `check-claims` amendment names: a check that passes on a mutation
you have not confirmed took effect is not evidence of anything.)

**Verified live**, receiver attached, this machine's own Wayland session:
`sdrprobe web` prints `Opening it in a browser.` and a new tab opens in the
already-running browser, connects, and streams (902 spectrum messages, 225
`receiver_state`, 58 `link_health` over about a minute); `sdrprobe server`
and `env -u DISPLAY -u WAYLAND_DISPLAY sdrprobe web` and `sdrprobe web
--no-browser` each print why they did not. `ps --ppid <server-pid>` at 2 s,
30 s and 60 s into the `web` run above shows **zero children each time** --
the double fork's whole point, and the one acceptance criterion this ticket
said no unit check could reach.

One side effect worth naming rather than leaving implicit: verifying `web`
live opened a real tab in the operator's own running browser, since a
display was genuinely present. That is the feature working, not a mistake,
but it is a real, visible effect on somebody's machine and the next person
verifying this should expect it rather than be surprised by it.

`make check`: 21723 checks in 77 suites, no failures -- re-run after the
`usage()`/README wording edits below, so the number covers the state this
ticket actually leaves behind.
