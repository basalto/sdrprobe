# 01 - The command word

Status: needs-triage

## What to build

`sdrprobe [command] [options]`, where `command` is `web`, `server`, or absent.
Absent is the window and must stay byte-identical to today.

A command is recognised only at `argv[1]`, and only when it does not begin
with `-`. Anywhere else it is what it is today: an unknown argument, refused.
A word that is not a known command is refused **by name** -- `unknown command
"serv"` rather than the bare usage dump an unknown flag gets, because a
mistyped command is the one case where the program knows what the reader was
reaching for.

## What each command sets

| command | `headless` | `serve` | `open_browser` |
| --- | --- | --- | --- |
| (none) | 0 | 0 | 0 |
| `server` | 1 | 1 | 0 |
| `web` | 1 | 1 | 1 |

That is the whole of it: the command sets flags a caller could have set, and
everything downstream is unchanged. `open_browser` is ticket 02's; this ticket
records it and nothing reads it yet.

## The `--serve` fix

`--serve` sets `options->serve` and nothing else, so `./sdrprobe --serve`
opens a window and binds no socket -- the dispatch is `if (options.headless)`
and nothing there consults `serve`. Two ways to close it:

- **`--serve` implies `--headless`.** One line, nothing to update, and it
  makes the flag do what its own help says.
- **`--serve` without `--headless` is refused.** Louder, and it would break
  no caller in the repository, since every one of the 173 references writes
  both.

**Recommended: imply.** A flag whose help text says "headless:" should not
need a second flag to be headless, and a refusal buys a diagnostic for a
mistake nobody in the tree makes. Either way `check-options` pins it, and
either way `sdrprobe server` is then exactly `--serve`.

## Decisions this ticket has to take

**A command with a flag that contradicts it.** `sdrprobe web --headless` is
harmless and should be accepted -- the command sets what the flag asks for.
`sdrprobe web --view lte` is not: the Viewer serves the Scope and pins
`TAB_SCOPE` / `VIEW_SPECTRUM` in `viewer_session_run()`, so `--view` is a
request the program will not honour. **Refuse it**, by the same standard that
made `--ppm` refuse alongside the startup form: a silently ignored request is
worse than a refused one. The same question applies to `--screenshot`,
`--analysis` and `--calibrate` alongside `web`/`server`, and the answer should
be the same for all of them, decided once and listed in the check.

**`--serve-port` with `server`/`web`** is the natural pairing and is accepted
unchanged.

## Tasks

- [ ] Add `enum start_command` (or a plain int) and one field to
      `struct options`; parse `argv[1]` before the loop and start the loop at
      the first flag.
- [ ] Refuse an unrecognised command by name, and refuse a command anywhere
      but first.
- [ ] Make `--serve` imply `--headless` (or refuse; see above) and say so in
      its help text.
- [ ] Decide, once, which flags are incompatible with a serving command and
      refuse them with a sentence naming the command.
- [ ] Rewrite `usage()` to lead with the three commands before the flag list.
- [ ] Extend `check-options`: each command's flag mapping, the unknown
      command, a command in second position, `--serve` alone, and every
      incompatible-flag refusal.
- [ ] Update `README.md` and `TROUBLESHOOTING.md` to show the commands as the
      way in, with the flags still documented beneath.
- [ ] Bump MINOR in `src/version.h` (ADR-0016: the command line gained a way
      in and lost none).

## Acceptance criteria

- [ ] `sdrprobe` with no arguments reaches the window exactly as before, and
      `make screens` is unchanged.
- [ ] `sdrprobe server` listens on 127.0.0.1:8765 and opens no window.
- [ ] `./sdrprobe --serve` (no `--headless`) either listens or refuses --
      never opens a window and serves nothing.
- [ ] `--headless --serve` keeps working unchanged; `tests/pipelines.sh`,
      `scripts/serve_cost.sh` and `scripts/screens.sh` need no edits.
- [ ] An unknown command names itself in the refusal.
- [ ] `check-options` covers every row of the table above and every refusal.
- [ ] `make check` passes.

## Not in scope

- Opening a browser (ticket 02).
- A command for the headless reports (`--decode`, `--survey`, `--calibrate`).
  The spec says why the rule predicts one and why this does not build it.
- Removing or renaming any existing flag.
