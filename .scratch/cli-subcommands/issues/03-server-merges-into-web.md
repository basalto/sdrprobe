# 03 - `server` merges into `web`

Status: resolved, 2026-09-27
Blocked by: 02 (done)

## The problem

Ticket 02 shipped `web` as `server` plus a browser, and wrote down what that
really was:

> `sdrprobe server` is then exactly `sdrprobe web --no-browser`

and `browser_wanted()` carried the same sentence as a comment. So the
program had **two command words for one command and a flag**, which costs a
reader the question "which of these takes `--no-browser`?" -- a question with
no useful answer, since both did.

`enum start_command` had four values and one of them was a duplicate wearing
a default.

## What changed

Three command words, and the browser is the default:

    sdrprobe                  the window
    sdrprobe headless [flags] no window and no link; prints to stdout
    sdrprobe web [flags]      serves the browser Viewer, and opens one at it

`--no-browser` (or `SDRPROBE_NO_BROWSER`) suppresses it. The browser leads
because somebody typing `web` on a machine with a display is asking to look
at something; `browser_wanted()` already declined on a machine with neither
`DISPLAY` nor `WAYLAND_DISPLAY`, so the default is safe where it would
otherwise be a child process failing invisibly.

**`server` still parses**, and the way it parses is the point:

```c
} else if (strcmp(argv[1], "server") == 0) {
    options->command = COMMAND_WEB;
    options->no_browser = 1;
```

It is not a second command handled alongside the first, and not a lookup
table entry that something downstream could come to treat differently. The
word *sets the flag, at the point it is read*, and from the next line on
nothing in the program can tell which spelling asked. `COMMAND_SERVER` is
gone.

## How it is pinned

`check-options` asserts the equivalence **by comparing the whole struct**:

```c
check_true("server is web --no-browser, field for field",
           memcmp(&as_word, &as_flag, sizeof(as_word)) == 0);
```

Field-by-field assertions would have proved the fields somebody thought to
name. A `memcmp` is the only form of this check that notices the two
spellings picking up a difference in a field nobody thought about -- which is
exactly the failure a merge like this can introduce.

Verified live as well, both spellings over the same capture: byte-identical
output, down to the `Not opening a browser: --no-browser.` line.

## Why this was PATCH and not MAJOR

A command word disappearing sounds like a break. Nothing broke: every
invocation that worked before produces a byte-identical `struct options`
now. Nothing was gained either. What changed is the help text and one enum
value, and neither is a contract. `src/version.h` records the reasoning
beside the numbers.

## Callers updated

`scripts/web_layout.mjs`, `scripts/viewer_client.py`, `README.md`,
`TROUBLESHOOTING.md`, `CLAUDE.md` and the `web-view` skill now spell it
`web --no-browser`, so the canonical form is what a reader meets.

**`--no-browser` is not optional in those**, and that is a real trap rather
than tidiness: `check-web-layout` drives its own chromium, and it runs on a
desktop where `DISPLAY` is set -- so a bare `web` there would have opened a
second browser on every gate run. The old `server` spelling hid that by
having no browser to open.

`docs/adr/0027-*.md` keeps `sdrprobe server` in its record of what was
verified at the time, which is what an ADR is for.

## Left open

The binary from `.scratch/layer-boundaries/issues/04-*` is called
`sdrprobe-server`, and `server` is no longer the word for what it does --
it is the **no-window build**, and it runs `headless` as much as `web`.
`sdrprobe-server web --no-browser` reads oddly. Not renamed here, because
the alternative (`sdrprobe-headless`) collides with the `headless` command
word in the same way. Worth a decision, not worth guessing at.
