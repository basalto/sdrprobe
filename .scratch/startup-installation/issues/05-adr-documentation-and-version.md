# 05 - ADR-0024, the documentation, and the version

Status: resolved, 2026-09-12

## ADR-0024 -- the session begins at a known installation

What it records, and the alternatives it rejects:

- the program does not reach a view, on a plain windowed receiver launch,
  until the site is known and a calibration has been attempted;
- **the verdict is a standing fact, not an event**: it lives in the health
  indicator ADR-0006 already established, and nothing in this program
  dismisses itself on a timer;
- the overlay is **modal**, because `start_calibration()` holds the tuner and
  any non-modal arrangement draws a frequency the operator did not choose;
- **GSM first, LTE on fall-through** -- and the ADR must record the reason
  honestly. Not "GSM is more precise": both sources pass the same gate at the
  same tolerance and the one on-air comparison has them within about a ppm.
  The reasons are that only an FCCH-backed calibration enables the drift
  re-check (ADR-0006), that an FCCH is a tone with no modulo ambiguity where a
  PSS phase wraps every 15 kHz, and that finding a GSM reference costs 12.8 s
  against roughly 170;
- a fifth overlay target between Help and Settings, extending ADR-0008's
  enums rather than adding a flag;
- what it supersedes: nothing. It sits beside ADR-0004 (the gate), ADR-0006
  (the drift check), ADR-0008 (overlays), ADR-0012 (reachable decisions) and
  ADR-0018 (whose crystal), and it cites each.

## CLAUDE.md and AGENTS.md

`CLAUDE.md` gains the new check in the build list (`check-startup-session`),
the new log keywords beside the `--debug-log` passage it already has, and a
short passage in the application section saying what
the startup sequence is and how a script avoids it. `AGENTS.md` gains the
per-file and per-field detail: every field on the form, every key and button,
and the four environment variables.

**Say the cost out loud.** A cold launch at a site with no GSM BCCH spends
about three minutes on an LTE band scan before the operator reaches anything.
That belongs in the documentation as a number, not discovered.

## The audits

Both are one line each and both have caught real faults here:

```sh
for h in $(ls src/*.h | xargs -n1 basename); do \
    grep -q "SRC)/$h" Makefile || echo "MISSING: $h"; done
```

```sh
for r in $(grep -oE '^check-[a-z0-9-]+:' Makefile | tr -d ':' | sort -u); do \
    case "$r" in check|check-dsp|check-touched|check-pipelines) continue;; esac; \
    grep -q "$r\b" <(sed -n '/^CHECK_UNITS=/,/^$/p' Makefile) || echo "NOT GATED: $r"; \
done
```

`startup_session.h` and `startup_layout.h` must appear in `APP_HDR`, and the
new rules in `CHECK_UNITS`. `check-signal-probe` existed,
passed, and was never run by the gate; four headers were listed only by their
own check rule, so editing one rebuilt the check and not the binary, which is
how a screenshot came back showing wording that had already been changed.

## The version

MINOR (ADR-0016), read against the command line, the headless reports and the
file formats -- not against the screens. A new overlay alone would not be it;
`--no-startup`, `--view startup`, the four environment variables and the
`startup_prompt` / `startup_band` config keys are. The config format gains
keys and loses none, and an older build keeps unknown lines verbatim, so
nothing there is breaking.

Three numbers in `src/version.h`. It is in `APP_HDR`, which it was not for a
while, and the failure was quiet in the worst way.

## Acceptance criteria

- ADR-0024 written, and every ADR it cites checked to see whether it needs a
  cross-reference back.
- Both audits print nothing.
- `make check` passes, and `make CC=clang check` too -- a second
  implementation is the only thing that catches a side effect in an argument
  list, which is a fixture that depends on the compiler.


## What was built

`docs/adr/0024-the-session-begins-at-a-known-installation.md`; the startup and
logging passages in `CLAUDE.md`; **v0.53.0** (MINOR: `--no-startup`,
`--view startup`, four environment variables, new log keywords; the config
format gains keys and loses none).

Both audits print nothing.
