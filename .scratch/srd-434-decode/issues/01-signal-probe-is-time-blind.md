# `signal_probe` is time-blind, and says "no carrier" when it means "not here yet"

Status: done

## The fault

Three separate fixed windows in the signal-probing path all look at a prefix
of the buffer they are given:

| Constant | Where | Window at 2 MS/s |
| --- | --- | --- |
| `SIGNAL_COARSE_PAIRS 65536` | `src/signal_probe.h` | first 32.8 ms |
| `SIGNAL_BURST_SAMPLES 262144` | `src/signal_probe.h` | first 131 ms |
| `MAX_PAIRS 4000000` | `scripts/signal_report.c` | first 2.00 s |

A transmitter that speaks once per button press is in none of them.

## What it cost

`testfiles/srd_remote_control_ook_a.bin` holds **four** transmissions at 46.8 to
57.2 dB over the local floor. `make probe-signal` reported `no carrier` at
every frequency asked about, with envelope statistics indistinguishable from
noise, and the capture was written off as empty. It was re-recorded
unnecessarily.

The coarse search is the visible half: given 32.8 ms of noise it returned
`+201983.6 Hz`, which is the **bottom edge of the search window it was
handed** -- the signature of a search that found nothing and pinned -- and the
fine stage then refined that noise peak to a decimal place.

`make probe-ook` demonstrates that nothing is wrong with the measurements
themselves. Handed the right 0.8 seconds of the same file, the same three
functions read *a modulated carrier, 53.7 dB over its floor*; handed an
equal-length window at t = 0 they read *no carrier, -4.3 dB*. 58 dB apart,
same code, same file.

## What is not wrong

`SIGNAL_BURST_SAMPLES` has a stated justification in the header:

> a burst pattern is a property of the signal that a longer look does not
> change

That is true of Mode S and of everything else this program decodes. The
constant is correct for its stated purpose. The fault is that **the
justification silently does not extend to a class of signal the program can
now be pointed at**, and nothing in the output distinguishes "measured, and
there is nothing" from "did not look where it is".

Raising the constants is therefore the wrong fix on its own: it makes every
shipping measurement slower to serve a case most callers do not have.

## What to do

Three parts, and the first is the one that matters.

1. **Say which case it is.** A caller that gets `no carrier` cannot currently
   tell "nothing here" from "nothing in the prefix I looked at". Report the
   window actually examined, or a flag, so an absence is falsifiable. This is
   the same objection ADR-0012 makes about decisions made where no check can
   reach them.

2. **Let a caller choose the window.** `signal_find_carrier()` and
   `signal_find_bursts()` should take an offset, or accept that the caller
   seeks first. `probe-ook` currently does the seeking outside them, which
   works and is the evidence that this is all that is needed.

3. **`MAX_PAIRS` in `scripts/signal_report.c` is a separate and simpler
   fault** -- a diagnostic silently truncating a 5 s capture to 2 s with no
   message. At minimum it should say so. `PAIRS_SIGNAL` already exists to
   limit the look deliberately; the *default* being a silent cap is what is
   wrong.

## Check

Whatever is built must be reachable without a receiver. The capture
above are the fixture: a transmission at a known offset, a known distance into
the file, and a known number of them.

Note that neither capture is committed to `testfiles/` yet -- see ticket 02.

## What was built

`signal_find_activity()` in `src/signal_probe.{h,c}` -- a whole-buffer scan in
2 ms chunks that accumulates power per chunk in the channel of interest and
reports **where** the band is busy: an offset, a length, how far the busy run
stands over the buffer's 25th percentile, how many busy runs there were, and a
duty. It keeps no samples, so it costs one pass and a sort.

`scripts/signal_report.c` calls it before measuring, narrows onto the window it
returns, and prints a `looked at:` line in **every** case -- which is part 1 of
this ticket and the half that matters. An absence is now falsifiable: the
report says whether it was measured over a narrowed window, over the whole
buffer because the band is uniform, or over a buffer too short to narrow.

Part 2 was answered by **not** putting the seek inside `signal_find_carrier()`.
A caller measuring a continuous carrier should not pay for a whole-buffer scan,
and a caller comparing a signal against its controls has to be able to say
which window each answer came from. `probe-ook` already seeked outside them and
that was the evidence.

Part 3: `MAX_PAIRS` is a **ceiling rather than a default**. `load()` sizes from
`ftell()` and prints a note if the ceiling ever bites. `PAIRS_SIGNAL` is still
the deliberate limit.

## The threshold, measured from both ends

`SIGNAL_ACTIVITY_BUSY_DB` is **12.0**. Over a 100 kHz band, `over_floor_db`
reads:

| Buffer | dB over the 25th percentile | Verdict |
| --- | --- | --- |
| SRD remote control transmissions | **32.1, 35.9** | busy |
| genuinely empty controls | 6.8, 7.0, 7.7 | uniform |
| `adsb_modes1.bin` (Mode S) | 8.8 - 10.0 | uniform |
| `carrier_75000_bare.bin` + GSM | 2.9 - 3.9 | uniform |

12.0 sits 4.3 dB above the loudest measured noise and 20 dB under the quietest
real transmission, and leaves every existing caller bit-for-bit unchanged.

**6.0 dB was tried first and was wrong.** It narrowed empty controls onto a
2 ms sliver of noise, whose envelope statistics then refused for want of
samples -- a *worse* control than the bug this ticket is about. A window
shorter than `SIGNAL_COARSE_PAIRS` is therefore padded around its centre and
clamped to the buffer, which makes the asymmetry safe: a false positive lands
on an arbitrary 32.8 ms of noise, which is what the prefix was anyway, while a
false negative returns the whole original fault.

## Neither constant was raised

`SIGNAL_COARSE_PAIRS` and `SIGNAL_BURST_SAMPLES` are untouched. The fix was to
choose the window, not to widen the caps -- raising them would slow every
shipping measurement to serve a case most callers do not have, and the header's
stated justification for the burst cap remains correct for the signals it was
written about.

## Verification

`tests/signal_probe_test.c` gains four cases and needs no receiver: a
transmission placed after the prefix is found where the prefix search cannot
do as well, a level buffer reports `uniform`, a short transmission is padded to
at least `SIGNAL_COARSE_PAIRS`, and the refusals.

On the two committed fixtures:

| Capture | Asked at | Before | After |
| --- | --- | --- | --- |
| `srd_remote_control_ook_a.bin` | +616679 Hz | `no carrier` | **58.9 dB**, 0.780 - 1.630 s, 4 busy runs |

`carrier_75000_bare.bin` still reads *a bare carrier, 46.0 dB, standing 0.923*
over `the whole buffer, level throughout`, which is the invariant CLAUDE.md
pins. `make check`: **20314 checks in 62 suites, no failures**.
