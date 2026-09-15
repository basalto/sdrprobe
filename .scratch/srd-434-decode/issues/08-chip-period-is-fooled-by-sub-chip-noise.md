# 08 — The chip period is fooled by sub-chip noise runs

Status: done

## What

`srd_chip_period()` picks the first significant mode of a run-length
histogram, and guards against noise with an **absolute** floor: runs shorter
than 20 us are ignored. That floor was chosen for the 500 us chips of the OOK
remote control, where 20 us is a twenty-fifth of a chip. It is a third of a chip at the
64 us the 2-FSK remote uses, and it does not scale with anything.

## The measurement

`make probe-srd` over `testfiles/srd_remote_control_fsk.bin`, transmission
0 — a 369.7 ms signal at +147.3 kHz reading 36.6 dB over its floor, roughly
30 dB weaker than the remote's own bursts and classified 2-FSK:

```
    8192 runs                      <- the SRD_MAX_RUNS cap, hit
         0-   9 us  4001
        10-  19 us  3000
        20-  29 us   743
        30-  39 us   243
        ...
       480- 489 us    14
       490- 499 us    12
       500- 509 us    23
       510- 519 us     7
        ...
       970- 979 us     3
       980- 989 us    12
       990- 999 us     1
      1000-1009 us     4
    srd_chip_period() -> 23.01 us
```

Seven thousand runs under 20 us are the discriminator crossing its threshold
on noise. They are excluded from the histogram, but the tail of the same
population is not: the 20-29 us bucket holds 743 of them and becomes the first
significant mode, so the answer is **23.01 us**.

The signal it is looking at has a 1T population at ~500 us and a 2T
population at ~1000 us — the signature of a **500 us Manchester chip**, which
is what `srd_frame.h` documents for the OOK remote control. So the pick is out by a
factor of twenty-two on a transmitter whose real period is sitting in the same
histogram.

The chip-period sweep in the same report shows the pick is not even a local
optimum: violations fall monotonically from 51.1% at 10 us to 6.4% at 90 us,
so nothing about the chosen period is a minimum of anything.

## Why it matters now

It used to be invisible. `srd_extract_frames()`'s generic path kept only the
longest unbroken Manchester stretch and a wrong chip period rarely produced
one, so a bad period read as silence. Ticket 07 made the path report **every**
frame, and transmission 0 now yields three short frames that survive the
all-0x00/0xFF filter and are not frames of anything.

A false decode is worse than a missed one in this module, because the whole
point of the SRD view is to say what an unknown transmitter is sending.

## What to build

Three things, in order of how much they are worth:

1. **Scale the noise floor to the signal, not to a constant.** The existing
   20 us is doing the right job with the wrong units. A fraction of the
   *dominant* run length, or a two-pass estimate (mode, then re-bin with a
   floor derived from it), both express "shorter than a chip can be".
2. **Report the cap.** 8192 runs was `SRD_MAX_RUNS`, hit silently. A truncated
   run stream is a different measurement from a complete one and nothing said
   so.
3. **Refuse rather than guess.** A histogram whose mode sits in the bottom
   bucket and whose sweep is monotonic has not found a chip period. Returning
   0.0 there is already the documented "not found" contract and would have
   suppressed all three spurious frames.

## Acceptance

- [x] The 500 us population of transmission 0 is what `srd_chip_period()`
      returns for it, or it returns 0.0 and reports why — not 23 us.
- [x] The OOK remote control's 500 us and the 2-FSK remote's 64.2 us are both still
      recovered, on the real captures, unchanged.
- [x] A hardware-free check covers a run stream padded with sub-chip noise at
      two different chip periods, so the floor cannot be re-fixed to a
      constant that only suits one of them.
- [x] `make probe-srd` reports when the run cap bit.
- [x] `check-pipelines` frame counts on both committed corpora are unchanged.

## Blocked by

None — ticket 07's extractor work is done and this is what it exposed.

## Not in scope

- Changing the modulation classifier. Whether transmission 0 is really 2-FSK
  is a separate question and `SRD_FSK_FREQ_SPREAD_MIN_HZ` has its own measured
  justification.
- Identifying what transmission 0 is. It is a neighbour, not the subject.

---

## Comments

**2026-09-15 — done, and the fix is not a better floor.**

The floor is gone. Every run is binned now, **every** significant mode is a
candidate rather than the first one, and the candidates are scored by a
question the old code never asked: *does this period explain the signal?*
`srd_chip_coverage()` is that question — the fraction of a run stream's
**time** spent in runs of one or two chips, which is all a Manchester coder
can emit. Time and not count is the whole of it: a threshold crossing on
noise is a short run and there can be thousands, so by count they are the
dominant population and by duration they are a rounding error.

**The threshold was measured from both ends**, over the 39 transmissions in
the three SRD remote control captures, each at whatever period the recovery picked for it:

| | coverage |
|---|---|
| 38 of 39 — every real burst, wakeup and data, OOK at 500 us and 2-FSK at 64.2 us | 91.1% – 99.8% |
| 1 of 39 — transmission 0, the subject of this ticket | 24.1% |

`SRD_CHIP_COVERAGE_MIN` is **0.50**, the geometric middle of that gap (46.9%
to a decimal place), leaving a factor of 1.8 to the lowest true reading and
2.1 to the only false one. It is deliberately not near 0.9: nothing here has
measured a *correct* period on a marginal signal, and the two errors do not
cost the same — a missed decode is silence, a false one is this module
answering the only question it exists to answer, wrongly.

**Results, on the real captures:**

- Transmission 0: `refused: no candidate reached 50% of the time in one- or
  two-chip runs`, and the three spurious frames are gone. Not 23 us, and not
  500 us either — the honest answer for a burst whose discriminator output is
  mostly noise.
- The other 31 2-FSK SRD remote control transmissions: **64.10 – 64.34 us**, unchanged.
- `srd_remote_control_ook_a.bin`: **499.67 – 499.73 us**, unchanged.
- Assembled program, byte-for-byte unchanged on both corpora: 2-FSK SRD remote control 15 generic
  frames; `434a` 11 FULL, 9 REPEAT, 2 generic.

**A fixture was wrong and the new refusal found it.** `check-srd-session`'s
carry-rule test built a 750 us delimiter over 64 us chips, so the delimiter
was 11.7 chips where `srd_frame.h` defines it as 1.5 — a signal no
transmitter could send, which the coverage test correctly declined to believe
in. It had the wrong chips because the remote control's real geometry does not fit in a
65.5 ms block with 50 ms of silence after it; the block is a parameter of
`srd_session_feed()` rather than a constant, so the fixture uses a 100 ms one
and the remote control's own 500 us chips.

**Checks**, all in `check-srd-dsp`: `test_chip_period_survives_subchip_noise`
(both periods, same absolute noise, 4x the signal's run count),
`test_a_stream_of_noise_has_no_chip_period`, `test_chip_coverage_measure`.
Mutation-tested against both halves of the fix independently — taking the
first candidate instead of the best fails 6 assertions, removing the coverage
floor fails 1. One claim in them was wrong on first run and the code was
right: half the period explains the one-chip runs, which is half this
fixture's time, not none of it.

Full gate green: **20670 checks in 68 suites**.

## Not done

The **session** truncates silently too — `runs[2048]` per transmission and
`SRD_SESSION_STREAM_RUNS_MAX` 4096 — and nothing reports it. Only the probe
was in this ticket's acceptance. Worth a ticket of its own if a transmission
ever runs long enough to hit it.

## Comments

**2026-09-15 — the sweep's zero-violation rows are a trap, and the picker is
right.** Asked whether `srd_chip_period()` picks badly on
`srd_remote_control_ook_a.bin`: the sweep's own output invites that reading,
because at the picked 499.73 us the longest press scores 127 violations while
every period from 510 to 560 us scores **0** over 1085 bits at the same 94.7%
coverage. It does not. Run through `srd_extract_frames()`, all five periods
return the **same 12 frames** — 4 full, 8 repeat, byte-identical:

```
  499.73 us  coverage  94.7%  chips 2238  violations  127  frames 12 (full  4)
  510.00 us  coverage  94.7%  chips 2170  violations    0  frames 12 (full  4)
  520.00 us  coverage  94.7%  chips 2170  violations    0  frames 12 (full  4)
  540.00 us  coverage  94.7%  chips 2170  violations    0  frames 12 (full  4)
  560.00 us  coverage  94.7%  chips 2170  violations    0  frames 12 (full  4)
```

The mechanism is the delimiter. It is six runs of **1.5 chips** by design, so
at 499.73 us each quantises to 2 chips and reads as a same-state pair — a
violation — while at 510 us and above it absorbs into 1 and does not. The
extractor finds delimiters from the **runs**, never from the chips, so neither
quantisation reaches the decode.

Which means the violation count is not a measure of decode quality at all:
it swings 127 to 0 across periods whose output is identical. That is what
condemned `check-srd-dsp`'s old real-capture claim — `error_count * 10 <
bit_count`, which also counted the idle between frames in a 1.1 s press and
read 10.3%, 2.8%, 12.2% and 11.3% on the four presses. Replaced with the
longest unbroken legal stretch against a floor of two frames, which both
candidate periods clear (208 bits at the picked one, 1085 at 510) and every
wrong period fails (127 at 340-390 us, one or two under 200).

No change to `srd_chip_period()`. The harness was a one-off over the shipping
functions, not a second implementation; the sweep in `make probe-srd` already
prints every column above except the frame count, and adding that column is
the only thing here worth building if the question comes back.
