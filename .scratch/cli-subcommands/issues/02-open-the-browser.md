# 02 - Open the browser

Status: needs-triage
Blocked by: 01

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

- [ ] Add `--no-browser` and `SDRPROBE_NO_BROWSER`.
- [ ] Add a pure `browser_wanted(options, env_lookup)` and check every row.
- [ ] Add `browser_open(url)` in its own translation unit: non-blocking, no
      zombie, no shell, stdout and stderr to `/dev/null`.
- [ ] Call it once, after `viewer_link_open()` succeeds, and report on stderr
      what it did -- opened, skipped for no display, skipped by request, or
      failed.
- [ ] Assert `server` and `web --no-browser` agree, in the check.
- [ ] Say in `README.md` how to stop it opening.

## Acceptance criteria

- [ ] `sdrprobe web` opens a browser at `http://127.0.0.1:<port>/` after the
      link is listening, never before.
- [ ] `sdrprobe web --no-browser` and `sdrprobe server` are indistinguishable.
- [ ] With no `DISPLAY` and no `WAYLAND_DISPLAY`, `web` serves and says why it
      did not open anything.
- [ ] A browser that cannot start leaves the server running and the URL on
      stderr.
- [ ] No zombie after a browser exits -- checked by running `web` for a
      minute and looking, since no unit check can see this.
- [ ] `make check` passes and `check-options` covers the decision table.

## Not in scope

- Choosing a browser, or a `--browser <path>`. `xdg-open` is the platform's
  own answer and a second mechanism needs a reason.
- Serving anything but the Scope, or opening a particular view in the browser.
