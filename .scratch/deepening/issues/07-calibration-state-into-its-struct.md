# 07 - The calibration state into the struct that already holds it

Status: ready-for-agent
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
