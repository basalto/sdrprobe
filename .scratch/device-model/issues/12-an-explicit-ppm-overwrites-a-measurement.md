# 12 - An explicit --ppm overwrites a measured calibration

Status: **resolved 2026-09-12.** `--ppm` applies for the run and writes
nothing; `--claim-calibration` beside it stores the correction, which is what
that flag already meant for a legacy value and is how a headless
`--calibrate` result is saved. A run whose ppm differs from the stored one
says so on stderr, because silence is what made the overwrite invisible. The
decision is `installation_records_ppm()` rather than an `if` beside `main()`,
and `check-installation` pins all four cases. v0.50.0: the command line's
behaviour changed (ADR-0016).
Found 2026-09-12, while running ticket 10's uncorrected sweeps.

`--ppm 0` destroyed this site's calibration. Before:

    calibration 77771111153705700 32 home-sala-estar

after an afternoon of `make probe-tone ... APPLIED_TONE=0`:

    calibration 77771111153705700 0 home-sala-estar

and the program said so on the next start -- `Restored +0 ppm` where it had
said `Restored +32 ppm` that morning. The value was restored by hand from the
ticket that measured it, which is the only reason it is not lost.

## It is deliberate, which is why it needs a decision rather than a patch

`src/sdrprobe.c:2811`:

> An explicit --ppm outranks everything and is recorded against wherever and
> whatever we now are.

Outranking for the run is right. **Recording is the part to reopen**, on this
repository's own argument: ADR-0018 keeps the correction per receiver and site
because it is a *measurement* against whatever reference a place offers, and
`--claim-calibration` exists precisely because claiming one is meant to be
**the operator's explicit act**. A bare `--ppm` performs that act with no act.

## Why it bites here rather than in theory

The whole method of `10-*` is the uncorrected sweep: a clock-coherent tone
reads its exact nominal only when no correction is applied, so every probe in
that campaign passes `--ppm 0`. `scripts/tone_probe.sh` -- written for that
ticket, and the documented way to ask the question -- passes it on every
`APPLIED_TONE=0` invocation, which its own usage block advertises:

    make probe-tone FREQ_TONE=150M APPLIED_TONE=0   # uncorrected sweep

**So the tool built to measure the clock erases the clock measurement, every
time it is used in the mode it was built for.** Nothing warns, and the damage
is visible only as a line in a config file and one word of startup output that
looks like every other startup.

The loss is not small. A calibration is an on-air measurement against a
reference that a given site happens to offer -- ticket 08 and
`docs/cellular-frequency-correction.md` -- and at 32 ppm it is 4.1 kHz at
132 MHz, which is the entire discriminator `reading_origin.h` runs on. Zeroing
it does not merely lose precision: `reading_origin_for()` refuses outright
when the crystal error is zero, because the two hypotheses become the same
frequency. **Every coherence verdict silently becomes unavailable**, and a
sweep that would have said `clocked-here` says `unexplained` instead -- which
is exactly the flag ticket 10 is about, so the fault forges evidence for the
question it interferes with.

## What is not yet known

**Whether a check could have caught it.** `check-installation` covers what a
measurement belongs to; nothing here asserts what a *command line* may do to a
stored one, and the persistence happens in `sdrprobe.c` beside `main` rather
than in `installation.c` -- the half ADR-0012 says no check reaches. The
property to pin is one line: a run that names a ppm does not change what is
stored unless the operator asked for that.

**What the right rule is**, which is the triage question:

- **Apply without recording** unless `--claim-calibration` is given. Matches
  the existing explicit-act design and costs nothing to anyone; a scripted
  uncorrected sweep becomes safe by default. Recommended.
- Record only a *non-zero* ppm. Cheaper and wrong in the same way for anybody
  who legitimately measures a correction of zero.
- Warn on the overwrite and record anyway. Leaves scripted use broken, since
  nobody reads stderr from a loop.

## Workaround until then

Point `HOME` at a throwaway directory holding a copy of the config, which is
what the 2026-09-12 passes did:

    export HOME=<scratch>/fakehome    # config copy lives under .config/sdrprobe


## Comments

**Resolved 2026-09-12, and the ticket's recommended fix was almost wrong.**

It proposed "apply without recording unless `--claim-calibration` is given",
which is what landed -- but the reasoning underneath it missed that **`--ppm`
persisting was the only scriptable way to store a calibration at all**.
`--calibrate` measures and prints; it does not commit, and the GUI overlay's
Apply was the only writer besides this. Removing the write outright, which is
what "a per-run override should not persist" invites, would have left a
headless calibration with nowhere to go.

So the fix is not "stop writing" but "write when asked", and
`--claim-calibration` is the act because it already meant exactly this for an
unowned legacy value: make this correction mine. The legacy claim is now
skipped when a ppm is given, so the two jobs cannot both fire and disagree.

Both paths verified on air rather than only in the unit:

    --ppm 0                      Using +0 ppm for this run. <serial> at
                                 "home-sala-estar" stays calibrated +32 ppm
                                 (--claim-calibration to replace it).
                                 -> config unchanged
    --ppm 25 --claim-calibration Claimed +25 ppm for <serial> at "..."
                                 -> config now 25

And `scripts/artifact_sweep.sh` lost the `HOME` redirection it was born with,
which existed only to survive this bug.
