# 08 - Two verified GSM cells, one crystal, twenty ppm apart

Status: resolved, 2026-09-13 -- the cause is the transmitter, and the fix is
the agreement gate rather than a better single-channel statistic.

Found while measuring ticket 06's fold on air, 2026-09-12. It is not caused by
that change -- the same disagreement is there in the pre-fold binary -- and it
is the largest open risk in the startup calibration feature, because the scan
picks the reference and the operator is offered whatever it picked.

## The measurement

R820T, site home-sala-estar, +32 ppm applied throughout, consecutive runs of
`--calibrate gsm --arfcn N --calibrate-seconds 25`.

| channel | runs | centre_ppm | suggested |
| --- | --- | --- | --- |
| ARFCN 113, 957.6 MHz | 5 | -3.29, -2.85, -2.13, -3.74, -2.45 | +34 to +36 |
| ARFCN 63, 947.6 MHz | 4 | -24.73, -24.41, -17.76, -14.36 | +46 to +57 |

Both are **real GSM base stations**: ARFCN 63 produces a parity-valid
synchronisation burst, BSIC 42, so the verification pass added in ticket 06
confirms it rather than rejecting it. A base station's carrier is held to about
0.05 ppm, so they cannot both be measuring this crystal correctly.

## What the distributions say

Within one run, at one block each:

- **ARFCN 63 is unimodal and tight** -- 47 blocks at -14, 46 at -15, 12 at
  -13, and a handful of outliers. sem 0.08.
- **ARFCN 113 is bimodal** -- a main mode at -3/-4/-5 holding about three
  quarters of the blocks, and a second cluster at -46 to -51 holding about a
  fifth. The median rejects the second cluster, which is exactly what it is
  for, and the reported sem is 0.13 to 0.33.

So the *tighter-looking* measurement is the one that disagrees with everything
else known about this receiver, and the messier one is the one that agrees.
**A within-run spread does not separate them**: 63 gave 0.63 to 2.96 and 113
gave 0.56 to 2.64 over the same runs. Nor does the gate -- both lock.

## Why 113 is believed and 63 is not

Not from the measurement, which is the uncomfortable part. From outside it:

- `CLAUDE.md` independently records **GSM ARFCN 113 gave -31.3 ppm** against an
  LTE cell's -32.5 in an earlier session with no correction applied. With +32
  applied now, -3 is the same crystal to within a few ppm of genuine drift.
  ARFCN 63's answer would put the crystal at +46 to +57, which nothing in the
  repository's history agrees with.
- `CLAUDE.md` also records 113 as the only one of the three committed GSM
  captures whose cell is still on air -- so it is the channel this site's
  history is actually about.
- **ARFCN 63's centre moved 10 ppm across four runs** in one session (-24.7,
  -24.4, -17.8, -14.4) while 113 stayed within 1.6 ppm across five runs
  interleaved with them. Whatever 63 is measuring is not stationary, and a
  crystal that drifted 10 ppm in twenty minutes would have moved 113 too.

## What is not known

Why. The obvious explanations were checked and do not hold:

- **Not a false FCCH.** That was the first hypothesis -- a coherent line that
  is not a broadcast carrier's tone -- and the SCH verification refutes it:
  BSIC 42 decodes with valid parity, so a GSM base station is transmitting
  there. (It was first written up as a *refarmed* carrier; there is no GSM
  refarming here, and the hypothesis was refuted on its own terms anyway.)
- **Not a search-window edge artifact.** `GSM_FCCH_SEARCH_HALF_HZ` is 50 kHz
  and -14 ppm at 947.6 MHz is -13.6 kHz, well inside it.
- **Not the duplicate-residual bug** ticket 06 found: these are post-fix runs,
  one residual per block.

**Co-channel cells were the next hypothesis and are refuted.** With no
refarming, dense reuse makes two base stations on one ARFCN the obvious
candidate -- two FCCHs at slightly different frequencies, a tone detector
averaging between them, a constant bias, and a run-to-run wander as fading
decides which dominates. It fits everything. It is also wrong: 25 s of
`--decode` gives

    ARFCN 63   284 SCH decodes, all BSIC 42 (NCC 5, BCC 2)
    ARFCN 113  227 SCH decodes, all BSIC 38 (NCC 4, BCC 6)

One identity each, no second one anywhere. (And ARFCN 113's BSIC 38 / NCC 4 /
BCC 6 is exactly what `gsm_arfcn_113.bin` is pinned to in `check-gsm-dsp`, so
that channel is the same cell the corpus was recorded from -- which is worth
having as corroboration that 113 is the trustworthy one here.)

**That count is itself the sharpest evidence, and it points at the
measurement.** 284 parity-valid synchronisation bursts in 25 s means the SCH
path demodulates that carrier cleanly **using the nominal offset it is handed**
-- `gsm_sch_decode()` takes `expected_hz - centre_hz` and nothing corrects it
by 13 kHz. A carrier really sitting 13.3 kHz from nominal would rotate the
differential detector by 0.30 rad a symbol at 270.833 kBd, which is not what a
284-for-284 decode looks like.

So the carrier is where it should be and **the FCCH tone measurement on ARFCN
63 is the thing that is 13 kHz out**. Two estimators on one carrier disagree,
and the one with a parity check behind it is the one to believe -- which is
the `dsp-validation` argument exactly.

Candidates now worth testing, none tested: the tone detector locking to a
modulation product or a spectral line of the burst structure rather than the
all-zeros burst; an interferer within 50 kHz of that channel's FCCH frequency
that is absent near 113's; the detector's window interacting with this cell's
burst timing.

## Why it matters

`scan_select_bcch()` takes the **loudest** channel carrying a tone, and 63 is
louder here. So the startup form's default answer at this site is the wrong
one, and it presents it as `locked` with a BSIC beside it. The only thing
standing between that and a filed correction is the operator reading the panel
and declining to press Continue.

## What to do about it

`app.h` already states the doctrine, and it is the right one: *"Two references
measuring one crystal: when they agree the correction is worth trusting, and
when they do not that is the most useful thing either of them has said."*

So the fix is **not** a better single-channel heuristic -- the data above says
no single-run statistic separates these two. It is to measure a second
verified candidate and report agreement or disagreement, with the verdict
saying which. That doubles the startup cost on the GSM path, from about 23 s
to about 40, and it is worth it: the alternative is a confident wrong number.

Until then, and this is the honest interim position, **the startup form must
not be treated as unattended**. It already requires a click, shows the channel,
the BSIC and the correction, and files nothing without one.

## The diagnostic was run, and it changes the diagnosis

Each channel measured at two applied corrections, same session, 25 s each:

| channel | `--ppm 0` | `--ppm 32` | suggested |
| --- | --- | --- | --- |
| ARFCN 113 | -34.67 (sem 0.05, spread 0.41) | -2.10 (sem 0.13, spread 1.06) | **35, 34** |
| ARFCN 63 | -48.03 (sem 0.78, spread 4.43) | -18.49 (sem 0.42, spread 3.40) | **48, 50** |

**Both channels are internally consistent under a change of applied
correction** -- each suggests the same crystal error at 0 ppm as at 32. So the
measurement is linear and the fault is not in how the correction is applied.

What the 2x2 shows instead is that **ARFCN 63 carries a constant offset of
about -14 ppm, or -13.3 kHz at 947.6 MHz, on top of the same crystal error
113 measures.** Take 113's answer as the crystal, +35:

- expected residual at `--ppm 0` is -35; 63 reads **-48**, a bias of -13
- expected residual at `--ppm 32` is -3; 63 reads **-18.5**, a bias of -15.5

The same bias at both, which a disagreement between two crystals could not
produce and a fixed frequency error does. So this is **not two cells
disagreeing about a crystal**. It is one crystal, and a tone measurement on
ARFCN 63 that is about 13 kHz away from where the FCCH is.

That is a much sharper question: what is the detector locking onto at
947.6 MHz + 67.708 kHz - 13.3 kHz, given that a parity-valid synchronisation
burst decodes on the same carrier? A leaking neighbour, a spur, or a spectral
line of the modulation rather than the all-zeros burst. `probe-signal` at 947.6 MHz + 67.708 kHz with controls either side is the
next measurement -- it will say whether there is one line there or two, and
how far off it sits -- and `probe-tone` will say whether whatever is there is
coherent with this receiver's own clock rather than with a transmitter.

## A discriminator this does expose

In the cleanest pair of runs the **spread** separates them where nothing else
did: 3.40 and 4.43 for ARFCN 63 against 0.41 and 1.06 for ARFCN 113, three to
ten times wider. That is what a tone measurement sitting on the wrong line
looks like next to one sitting on an FCCH.

It is **not** enough to build a threshold on. Earlier, noisier runs of the
same two channels gave overlapping spreads (63: 0.63-2.96, 113: 0.56-2.64),
so a constant chosen from these four runs would be chosen from the quiet ones
and fail on the others. `does-it-help` is the discipline this needs -- measure
where the constant breaks, over more than one afternoon -- and until that is
done, agreement between two references remains the honest gate rather than a
spread threshold.


## Resolved: one tone each, and ARFCN 63's channel really is low

`make probe-fcch` was written for this (see below) and sweeps
`gsm_fcch_detect()` across a channel with a **narrow** search at each step,
where the shipping detector takes one winner over +/-50 kHz.

**The first sweep measured the wrong thing and that is worth recording.** It
printed the detector's *confidence*, which reads **0.96 to 0.997 at every
probe position across the whole channel** -- GMSK is constant-envelope and
continuous-phase, so any narrow slice of an occupied carrier looks coherent.
Swept, the confidence is flat and says nothing about where the tone is. The
shipping detector works because it takes the **strongest** line, so amplitude
is the quantity that decides. (Which also disposes of an earlier
straw: the 0.979-against-0.996 confidence gap between the two channels means
nothing.)

By amplitude, both channels show a single clean peak:

| | peak amplitude | at | from nominal |
| --- | --- | --- | --- |
| ARFCN 113 | 0.3021 | +465374 Hz | **-2334 Hz** (-2.46 ppm) |
| ARFCN 63 | 0.3875 | +450282 Hz | **-17426 Hz** (-18.39 ppm) |

and the roll-off either side is consistent with each peak being that channel's
FCCH: 113's falls to 0.16 by +468 kHz and 0.11 by +474, and 63's sweep returns
no detection at all above +453 kHz. **One tone each, not two.** So the
detector is not preferring the wrong line, and the hypothesis that this was a
selection fault is refuted.

ARFCN 113's tone sits where the receiver's own residual (-2.5 to -3.4 ppm
against the applied +32) puts it. ARFCN 63's sits 14.6 kHz lower than that --
**15 ppm of transmitter**, after the receiver's share is removed.

Three independent instruments now agree on it: the calibration residual, the
chain probe's stage-2 carrier refinement (-17153 Hz against 113's -2670), and
this sweep. The cause is a property of that transmitter -- a small cell on a
poor reference, or a frequency-translating repeater, are the candidates, and
neither is ours to fix.

## What follows for the program

**No single-channel statistic separates these**, and the data says so rather
than the author: the within-run spread overlaps, the coherence is flat, the
gate locks on both, and the SCH decodes 284 times on the bad one. A threshold
on "how far the tone may be from nominal" cannot work either, because on an
**uncalibrated** receiver a tone legitimately sits tens of kHz out -- that is
the whole reason calibration exists.

So the fix is the one `app.h` already states: *"Two references measuring one
crystal: when they agree the correction is worth trusting, and when they do
not that is the most useful thing either of them has said."* The startup
search measures a **second** verified channel and requires agreement. That
needs no new threshold on the signal, only one on the disagreement, and it
handles the calibrated and uncalibrated cases identically.

## The tool

`scripts/fcch_probe.c` behind `make probe-fcch`, per the repository's own rule
that a scratch script written more than once is a tool -- this question came
up twice in one session, and `signal_probe` cannot answer it: inside an
occupied 200 kHz carrier everything is modulated energy, so
`signal_find_carrier()` reports "a modulated carrier" wherever it is pointed,
which is true and useless.

```sh
make probe-fcch FILE_FCCH=captures/x.bin RATE_FCCH=2000000 CARRIER_FCCH=400000
```

**Pass `RATE_FCCH`**: an empty `make` variable vanishes from the argument list
rather than becoming an empty argument, so leaving it out shifts every
positional after it -- the first run of this read 4 M pairs "at 0.400 MS/s".

## And four diagnostics that did not compile

`probe-gsm-chain` was needed here and failed to build:
`device_profile_rtlsdr()` gained a tuner argument in commit cb03dfa and four
scripts were never updated -- `gsm_chain_probe.c`, `adsb_chain_probe.c`,
`dsp_bench.c`, `survey_threshold_probe.c`. Every *test* was updated, because
tests are in `CHECK_UNITS` and scripts are not. All four are fixed.

That is the same shape as `check-signal-probe` existing and never being gated:
**no `probe-*` or `bench-*` target is built by `make check`**, so they rot
silently and are found only when somebody reaches for one mid-diagnosis. A
compile-only sweep of the scripts would cost seconds and belongs in the gate.


## Follow-on: it is not two channels, it is a trend

The agreement gate's first on-air run measured a **third** channel, ARFCN 17
(BSIC 10, 938.4 MHz), at +71 ppm -- against 63's +51 and 113's +35. Ordered by
frequency and monotonic, about -1.8 ppm per megahertz.

That looked like it reframed this ticket's conclusion -- three base stations
do not drift together in a line ordered by tuning, and the receiver is common
to all three measurements. **It did not: issue 09 measured the receiver and
exonerated it.** The trend was an artefact of the sweep that found it, ARFCN
113 repeats to 272 Hz across 400 kHz of tuning, and ARFCN 63 repeats to 71 Hz.
So "ARFCN 63's transmitter is 15 ppm low" stands as this ticket measured it,
and ARFCN 17 turns out not to be an FCCH at all.

The gate stands regardless: whatever the cause, no single-channel measurement
can see it, and refusing two references that disagree is right in both
readings.
