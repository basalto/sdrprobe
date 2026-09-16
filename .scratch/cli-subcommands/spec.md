# A subcommand for the front door

## What this is

`sdrprobe <command> [options]`, where the command names **which frontend**:

| command | what it opens |
| --- | --- |
| `sdrprobe` | the raylib window, exactly as today |
| `sdrprobe web` | the Viewer link, and a browser pointed at it |
| `sdrprobe server` | the Viewer link, and nothing else |

`web` is `server` plus a browser. That is the only difference between them.

## The rule for what earns a command, and its honest edge

**A command names the frontend; flags configure what it does.** Window,
browser, socket. Everything else this program can be asked -- a band, a
capture, a dwell, a technology -- is a flag, because none of it changes who is
looking.

The rule has one edge worth stating rather than discovering later:
`--headless --decode` prints a report to stdout, and under this rule stdout is
a fourth frontend. So the rule predicts a `sdrprobe report` (or `decode`)
eventually. **This does not build it.** Three commands, and the rule written
down, so whoever wants the fourth has something to argue against rather than a
precedent to guess at.

The failure mode to avoid is a half-migrated command line where some modes are
commands and some are flags for no reason anyone can state. The rule above is
what stops that; a command added for any other reason is what starts it.

## Nothing is removed

`--headless`, `--serve` and `--serve-port` keep working and keep their
meaning. The command is sugar that sets them, and the flags remain what every
script, check and skill drives. There are **173 references to `--headless` or
`--serve` across 52 files** -- `tests/pipelines.sh`, `scripts/screens.sh`,
`scripts/serve_cost.sh`, `scripts/artifact_sweep.sh`, four ADRs, three
skills, `README.md`, `TROUBLESHOOTING.md`, `AGENTS.md` and `CLAUDE.md` -- and
breaking them buys nothing this feature needs.

Under ADR-0016 that makes this **MINOR**: the command line gains a way in and
loses none.

## Feasibility, checked rather than assumed

- **A bare word at `argv[1]` is unambiguous today.** `parse_options()` is one
  flat loop from `i = 1` and its final `else` is `return -1`, so every
  argument that is not a known `--flag` is already a hard refusal. There are
  no positional arguments to collide with, and no existing invocation can
  change meaning.
- **`xdg-open` is present** (`/usr/bin/xdg-open`, with chromium behind it).
- **The dispatch is one branch**: `if (options.headless) run_headless(); else
  run_gui();` in `main()`.
- **`check-options` already has the shape**: 908 lines driven through a
  `parse_line("--arfcn 73", &options)` helper, so a command's cases are one
  more string each.

## A bug this falls out of, and fixes

**`--serve` does not imply `--headless` and does not refuse without it.**
`./sdrprobe --serve` opens a window, serves nothing, and binds no socket --
reproduced live, twice, this session. Its own help text says
`--serve  headless: serve the Scope's view model ...`, which is a promise the
parser does not keep, and the only reason it has not been reported is that
every existing caller writes `--headless --serve` together out of habit.

`sdrprobe server` cannot be built without deciding what `--serve` alone means,
so the fix belongs here rather than in a ticket of its own.

## Tickets

1. `issues/01-the-command-word.md` -- parsing, dispatch, help, and the
   `--serve` fix. Useful on its own.
2. `issues/02-open-the-browser.md` -- what `web` adds over `server`. The only
   genuinely new systems code, and the only part that can hurt a long-running
   server.
