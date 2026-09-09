# 07 - The calibration state into the struct that already holds it

Status: resolved, 2026-09-09
Opened by the audit in `06-struct-app-carve-out.md`, 2026-09-09.

`struct calibration cal` exists in `app.h` and is read by two files. Sixteen
calibration fields sit **outside** it, in `struct app`:

| field | readers |
| --- | --- |
| `calibration_open` | 4 |
| `calibration_status` | 4 |
| `calibration_technology` | 3 |
| `calibration_expected_hz` | 2 |
| `auto_drift_check` | 2 |
| `drift_health`, `drift_notice`, `drift_phase` | 2 each |
| `gsm_cal_valid`, `gsm_cal_ppm` | 2 each |
| `gsm_cal_arfcn` | 2 |
| `cal_lte_band`, `cal_lte_scanning`, `lte_cal_valid`, `lte_cal_earfcn`, `lte_cal_ppm` | 1 each -- `overlay_calibration.c` |

`CLAUDE.md` already tells anyone working here to "reach for `app->cal.*`
rather than adding a `calibration_*` field back to `struct app`". The struct
does not follow its own advice.

Ticket 01 walked past this. It took `cal_return_sample_rate` -- and that field
was *retired* rather than moved, because one lease token holds a frequency and
a rate together -- while the sixteen beside it were left where they were. That
is the right outcome for one field and it is why nobody counted the rest.

## Local hypothesis

All sixteen can move into `struct calibration` with no behaviour change. The
five `lte_cal_*` / `cal_lte_*` fields have a single reader --
`overlay_calibration.c` -- so those are free; the other eleven are shared with
`overlay_settings.c` (the health indicator and the drift toggle),
`sdrprobe.c` (the frame loop), `overlay_scan.c` and `view_gsm.c`.

It is false if any of them is read while the overlay is *closed* for a reason
that is not the drift check -- which is the one part of calibration that runs
in the background, so that is where to look first. `calibration_open` itself
has four readers and is the field that decides whether anything else runs, so
it is the one to move last and the one most likely to want to stay.

## What to check afterwards

Nothing new should be needed: `check-calibration` covers the lock gate and the
machine that fills its buffer, and `check-layout` covers the overlay's
geometry. If a field turns out to need a check to move safely, that is a
finding worth recording rather than a reason to leave it.

The screenshot pair matters more than usual here, because the calibration
overlay is a screen this program has shipped broken twice: `--view calibration`
and `--view calibration --calibrate lte`, before and after (the `screenshot`
skill has both recipes).

## Not in scope

- Changing what calibration decides. `calibration_gate.h` and ADR-0004 stand.
- The `drift_*` trio becoming a state machine of its own. It may want to be
  one; that is a separate question from where the fields live.


## What was done

Sixteen fields moved into `struct calibration`, the redundant `calibration_` /
`cal_` / `_cal_` prefixes dropped on the way in, and the two that were already
inside renamed to match (`gsm_cal_expected_hz` -> `gsm_expected_hz`,
`gsm_cal_tune_hz` -> `gsm_tune_hz`) so the struct reads as one thing:

| was | is |
| --- | --- |
| `calibration_open` | `cal.open` |
| `calibration_technology` | `cal.technology` |
| `calibration_expected_hz` | `cal.expected_hz` |
| `calibration_status` | `cal.status` |
| `auto_drift_check` | `cal.auto_drift` |
| `gsm_cal_valid` / `_ppm` / `_arfcn` | `cal.gsm_valid` / `_ppm` / `_arfcn` |
| `lte_cal_valid` / `_earfcn` / `_ppm` | `cal.lte_valid` / `_earfcn` / `_ppm` |
| `cal_lte_band` / `cal_lte_scanning` | `cal.lte_band` / `cal.lte_scanning` |
| `drift_health` / `_notice` / `_phase` | `cal.drift_health` / `_notice` / `_phase` |

`struct app` is **70 fields, down from 85**. `calibration_open` moved after
all -- the ticket guessed it would want to stay, and `help.open` is the
precedent that settles it: the precedence chain reads it through
`input_state_now()` and does not care where it lives. `settings_open` and
`scan_open` are now the only two overlay flags still loose, and `scan_open` is
ticket 08's.

**The boundary had been running through the middle of coherent pairs**, which
is the tell that it was never chosen: `drift_health_prev` was inside
`struct calibration` while `drift_health` was outside it, and
`gsm_cal_expected_hz` was inside while `gsm_cal_valid`, `gsm_cal_ppm` and
`gsm_cal_arfcn` were outside.

## The fault the hypothesis found

The hypothesis was "all sixteen move with no behaviour change, and it is false
if any is read while the overlay is closed for a reason that is not the drift
check". **It is false, for `calibration_status`, and the reason is a real
fault rather than a naming problem.**

`retune_receiver()` is *the* retune in this program -- the survey, GSM, LTE,
FM, ADS-B, the band scan and the calibration overlay all go through it -- and
every one of its five failure messages was written into `calibration_status`
and prefixed "Calibration":

    "Calibration requires a live RTL-SDR receiver"
    "Receiver rejected calibration tuning or PPM correction"
    "Could not read back calibration tuning"
    "Calibration acquisition failed; restored previous tuning"

`retune_receiver_at_rate()`, `receiver_borrow()` and `start_scan()` did the
same. So when a survey step would not tune, the reason was written to the
calibration overlay's status line -- a screen the operator was not on, saying
"Calibration" about something that had nothing to do with calibration -- while
the survey showed its own message and the actual reason was never seen.

**`view_lte.c` already knew.** It reads the buffer by hand to answer why
1.92 MS/s was refused:

```c
snprintf(app->lte.session.status, ...,
         "The receiver would not move to 1.92 MS/s: %.100s",
         app->receiver_error);          /* app->calibration_status, before */
```

That line is the evidence the buffer was doing two jobs, and it is the only
place the second job was ever consumed.

So the reason has its own name: **`app->receiver_error`**, written by the three
receiver entry points, with the false "Calibration" prefix gone and the
frequency and ppm in the message where they were missing. It stays in
`struct app` because it *is* a handoff -- one writer, and whichever screen
asked is the reader.

**Two paths were depending on the shared buffer and are now explicit.**
`start_calibration()` and `start_lte_calibration()` returned -1 from a failed
borrow or retune **without writing a status at all**, and the headless report
printed whatever the retune had left in the same buffer. That worked by
accident. Both quote `receiver_error` deliberately now, and the headless
refusal reads

    Could not tune to 957.200 MHz: Tuning requires a live receiver: a
    capture holds one frequency

where it used to read "Calibration requires a live RTL-SDR receiver".

**PATCH, not MINOR** (ADR-0016, read against the command line, the headless
reports and the file formats): no flag changed, no format changed, and the
screens are identical. The one output that moved is a refusal message that was
mislabelled, and a wrong answer was never the contract. 0.46.1.

## Measured

- `make check` green, 17781 checks in 55 suites; `check-calibration` and `check-layout` unchanged.
- Both calibration arrangements screenshotted before and after. The **4G one
  is byte-identical**; the 2G one differs only in the live waterfall's top
  rows, with every field, button and the status line identical.
- A live GSM calibration still locks: `--headless --calibrate gsm --arfcn 113`
  gave 843 measurements, centre -35.82 ppm, spread 0.05, `locked 1`.
- The capture refusal above, which is the improved message.

## What is left for someone else

- **`settings_open` should follow `cal.open` into `struct settings_panel`**,
  and `scan_open` into `struct band_scan` with ticket 08. Then all four overlay
  flags are nested and `input_state_now()` is the only thing that knows the
  set.
- **The other callers could quote `receiver_error` now that it exists.** The
  survey says "The receiver would not tune to %.4f MHz." and drops the reason;
  so do the FM and ADS-B paths. Not done here because the survey's message
  belongs to `survey_session`, which does not see `struct app` by design --
  passing the reason in is a change to that seam and wants its own thought.
