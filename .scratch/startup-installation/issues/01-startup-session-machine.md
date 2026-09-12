# 01 - The machine that sequences scan, measure, gate and file

Status: resolved, 2026-09-12

The startup sequence is: find a reference, measure the crystal against it,
wait for the gate, and hand the answer to whoever is filing it. That is a
state machine, and the obvious place to write it is inside the overlay that
draws it, which is the mistake this ticket exists to not make.

`src/survey_session.{c,h}` is the shape to copy, including both of its
refusals. Read it and `.scratch/survey-to-decoder` before starting.

## Local hypothesis

The whole sequence can be decided by a module that has never seen `struct
app`, raylib, a file or a receiver -- so every transition in it is reachable
by a check that needs no window and no hardware (ADR-0012).

It is false if some step genuinely needs the receiver in hand. The way to find
out is to write the machine first and see what it has to ask for.

## What to build

`src/startup_session.{c,h}`, `startup_` prefix.

**The phases.** One enum, and the names are the report:

```
STARTUP_IDLE
STARTUP_SCAN_GSM      /* scan_plan.h's walk, looking for a BCCH */
STARTUP_MEASURE_GSM   /* an ARFCN was found; FCCH residuals */
STARTUP_SCAN_LTE      /* no BCCH anywhere; fall back to a band scan */
STARTUP_MEASURE_LTE   /* a cell was chosen; PSS residuals */
STARTUP_LOCKED        /* the gate opened; a correction is on offer */
STARTUP_FAILED        /* nothing to measure, or the budget ran out */
STARTUP_SKIPPED       /* the operator said so */
```

**What it asks for, and never does.** The machine does not touch the receiver.
It publishes a *request* -- a centre frequency, a sample rate, and whether the
tuning has changed since the last one -- and the adapter tunes and reports
back whether the tuner moved or refused. This is `survey_session_retuned()`'s
arrangement and it is what lets the check drive a whole scan with no dongle.

**What it consumes.** A `struct startup_block`, the seam: the block's
measurements, not `struct app`. Whatever the GSM scan step and the calibration
tracker actually need -- and no more, because the seam is what keeps this
checkable.

**What it decides.**

- when a GSM scan step is settled, when it is done probing, and when the walk
  is over -- delegated to `scan_plan.h`, not restated;
- the fall-through: `scan_select_bcch()` non-zero goes to `STARTUP_MEASURE_GSM`,
  zero goes to `STARTUP_SCAN_LTE`. **Not `scan_select_strongest()`**; the spec
  says why, and a check must pin it with a band that has power and no tone;
- the budget, per phase, so a scan that finds nothing and a measurement that
  will not lock are told apart in the report;
- whether the form may be committed -- true in `LOCKED`, `FAILED` and
  `SKIPPED`, false while anything is running. The overlay reads this rather
  than deciding it;
- the verdict and its reason, in the same vocabulary `--calibrate` already
  prints: `locked`, `no-cell`, `too-few-measurements`, `sem-too-wide`,
  `timeout`. A new word here is a second vocabulary for one outcome.

**What it refuses.**

- It does not apply a correction. It offers `suggested_ppm` and the source it
  came from; ticket 04 files it.
- It does not read or write a file. `installation_commit()` stays the one
  writer.
- It does not include `app.h`, `raylib.h`, `rtl-sdr.h` or `stdio.h` beyond
  what a header needs.

## The trap that has to be pinned

**A step's settle is timed from the tuning, not from the request.** This is
the fault the survey extraction shipped: a retune flushes the pipeline and
costs about a tenth of a second, so a settle timed from when the tuning was
*asked for* expires before the blocks it is meant to discard have arrived, and
the scan folds the previous step's signal into this step's answer. Green
everywhere, because no capture retunes.

The machine must start a step's clock when the adapter reports the tuner
moved, and the check must assert it by driving a retune that reports late.

**And a step is over on its own clock once it has heard something.** Returning
early on "no block" costs a block a step. `update_survey()` is called every
frame with `spectrum_updated` as a *parameter* rather than as a guard, for
exactly this reason; do the same here.

## Acceptance criteria

- `make check-startup-session` passes and is listed in `CHECK_UNITS`.
- `tests/startup_session_test.c` links `-lm` alone -- no raylib, no librtlsdr.
- Every phase and every transition between them is reached by a test,
  including: a GSM band with a BCCH; a GSM band with power and no BCCH falling
  through to LTE; an LTE scan that finds no cell; a measurement that times out
  with each of the three gate reasons; and Skip from each running phase.
- A test asserts the settle is timed from the tuning, by reporting the retune a
  frame late and checking the step does not advance early.
- `startup_session.h` is in `APP_HDR`.

## Not in scope

- Drawing anything (ticket 03) or filing anything (ticket 04).
- The headless adapter (ticket 06).


## What was built

`src/startup_session.{c,h}` and `tests/startup_session_test.c`,
`check-startup-session` in `CHECK_UNITS`, **100 checks**. Links `-lm` alone
beside `gsm_dsp.c`, `lte_dsp.c` and `sdr_dsp.c`; no raylib, no librtlsdr, no
`struct app`.

Two faults the check caught on its first run, and they are different kinds:

- **`startup_session_may_commit()` was written as the three finished phases**
  -- LOCKED, FAILED, SKIPPED -- which leaves a session that never began
  (IDLE) uncommittable, so a form whose Continue is gated on a calibration
  nobody started would never open again. Every skip condition in ticket 04
  lands exactly there. It is `!startup_session_running()` now.
- **The FCCH detector's return was tested inverted** (`== 0 && detected`
  where it returns 1 on detection). The scan path happened to read
  `fcch.confidence` instead and so worked, which is how the two halves
  disagreed with a green build.

And one **wrong claim** written into the check itself, which is the shape this
repository keeps warning about: `test_the_measured_correction` asserted that a
tone reading 20 ppm high suggests **+20**. It suggests **-20** --
`sdr_dsp_corrected_ppm()` is `current - residual`, which is the feedback sign
that drives the residual to zero, and applying the residual itself would
double the error. The code was right; the claim was corrected and now says
why.

One thing moved to make this possible: **`GSM900_BASE_HZ` and
`GSM900_ARFCN_SPACING_HZ` lived in `app.h`**, so any module wanting the
channel grid had to include the whole of the application's state to get it.
They are in `gsm_dsp.h` now, beside `gsm_downlink_hz()`, which is the same
arithmetic with the bounds checked.

Deliberate deviations from the ticket, both recorded in the header:

- The LTE fall-back **stops at the first channel whose identity repeats**
  rather than walking the whole raster. It is looking for a reference, not
  taking an inventory, and `lte_scan_candidate()`'s order tries whole
  megahertz first precisely because that is where carriers are centred.
- The GSM measurement **refuses the centroid fallback** that the calibration
  overlay allows. The overlay serves an operator watching a chart; this files
  a correction unattended, and a centroid residual is a different measurement
  of a different thing (ADR-0004).
