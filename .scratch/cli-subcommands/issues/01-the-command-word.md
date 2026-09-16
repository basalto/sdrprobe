# 01 - The command word

Status: resolved, 2026-09-16

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

- [x] Add `enum start_command` (or a plain int) and one field to
      `struct options`; parse `argv[1]` before the loop and start the loop at
      the first flag.
- [x] Refuse an unrecognised command by name, and refuse a command anywhere
      but first.
- [x] Make `--serve` imply `--headless` (or refuse; see above) and say so in
      its help text.
- [x] Decide, once, which flags are incompatible with a serving command and
      refuse them with a sentence naming the command.
- [x] Rewrite `usage()` to lead with the three commands before the flag list.
- [x] Extend `check-options`: each command's flag mapping, the unknown
      command, a command in second position, `--serve` alone, and every
      incompatible-flag refusal.
- [x] Update `README.md` and `TROUBLESHOOTING.md` to show the commands as the
      way in, with the flags still documented beneath.
- [x] Bump MINOR in `src/version.h` (ADR-0016: the command line gained a way
      in and lost none).

## Acceptance criteria

- [x] `sdrprobe` with no arguments reaches the window exactly as before, and
      `make screens` is unchanged.
- [x] `sdrprobe server` listens on 127.0.0.1:8765 and opens no window.
- [x] `./sdrprobe --serve` (no `--headless`) either listens or refuses --
      never opens a window and serves nothing.
- [x] `--headless --serve` keeps working unchanged; `tests/pipelines.sh`,
      `scripts/serve_cost.sh` and `scripts/screens.sh` need no edits.
- [x] An unknown command names itself in the refusal.
- [x] `check-options` covers every row of the table above and every refusal.
- [x] `make check` passes.

## Not in scope

- Opening a browser (ticket 02).
- A command for the headless reports (`--decode`, `--survey`, `--calibrate`).
  The spec says why the rule predicts one and why this does not build it.
- Removing or renaming any existing flag.

## Done, 2026-09-16

`argv[1]` is recognised before the parse loop and only there: `web` and
`server` set `command`, the loop's own start index moves to `argv[2]`, and
anything else that does not begin with `-` is refused with
`options->unknown_command` pointed at it -- `main()` prints
`unknown command "serv"` ahead of the usage dump, which is the one refusal
in this parser that now says what the reader meant rather than only what it
did not accept.

**The two flags stay authoritative.** `command == COMMAND_SERVER` or
`COMMAND_WEB` sets `serve`, and `serve` sets `headless` -- both post-loop,
which is what let `--serve` gain the same implication without colliding
with `--headless`'s own duplicate-flag guard regardless of which order a
caller writes them in. `server` is exactly `--headless --serve` on the
fields every other check in this file already reads, checked directly:
`test_the_command_word()` asserts the two spellings agree.

**The bug is fixed as a side effect of needing it fixed.** `./sdrprobe
--serve` used to open a window, bind no socket and serve nothing -- its own
help text said "headless:" and the parser did not enforce it. It could not
be left that way and still have `sdrprobe server` mean anything, since
`server` has no separate code path to fall back on.

**The incompatible-flag question turned out mostly already answered.**
`--view` and `--screenshot` already refuse alongside `--headless`, so
implying `headless` from `serve` closes both for free -- no new check
needed beyond confirming it, which `test_the_command_word()` does.
`--analysis` did not have an existing refusal (it is a harmless no-op under
plain `--headless` and stays one there) and gets one specific to `serve`:
the Viewer serves only the Scope, so asking for a decode view's analysis
arrangement is a request the link cannot honour. And `serve` joins the same
"its own run" exclusion `lte_chain` and `calibrate` already enforce against
each other -- `calibrate`, `survey`, `decode`, `lte-scan` and `lte-chain`
each refuse alongside it now, closing a gap that predates this ticket:
`sdrprobe --headless --calibrate gsm --arfcn 73 --serve` was accepted
before, and which of the two runs actually won depended on `run_headless()`'s
own if-chain order rather than anything the command line said.

**One claim caught before it was written down wrong.** A first version of
the "nothing needed here" list included `--startup`, reasoning from the
same pattern as `--view`/`--screenshot`. Tried live: `sdrprobe server
--startup` does not refuse, it runs -- `startup_form_wanted()` declines the
form at runtime rather than `parse_options()` refusing the flag, a
pre-existing and unrelated no-op this ticket has no business changing. The
comment says so now instead of the false claim.

**Five mutations, all caught**: dropping the `serve` -> `headless`
implication, dropping the `command` -> `serve` implication, dropping the
`--analysis` refusal, dropping `lte_chain` from the exclusion list, and
letting a command be recognised anywhere instead of only `argv[1]`.
`check-options`: 348 checks, all passing on the first run before any
mutation.

**Verified live**, receiver attached: `sdrprobe server` and `sdrprobe web`
both bind `127.0.0.1:8765` and log the same listening line `--headless
--serve` always has; `sdrprobe server --file testfiles/gsm_arfcn_69.bin`
serves a capture; `sdrprobe serv` refuses and names itself; `sdrprobe server
--view lte`, `--screenshot`, `--analysis` and `--calibrate gsm --arfcn 113`
each refuse. No leaked process in any case.

`src/version.h` MINOR bumped to 61 (ADR-0016): the command line gained a way
in and lost none. `usage()` leads with the three commands; `README.md` and
`TROUBLESHOOTING.md` introduce them without rewriting every existing
`--headless --serve` recipe, each of which is demonstrating a specific flag
combination rather than the way in.

`make check`: 21707 checks in 77 suites, no failures.
