# 04 - When it runs, what it commits, and the cases with no calibration

Status: resolved, 2026-09-12

The machine (01), the health indicator (02) and the form (03) joined to the
program.
This is the ticket where the provenance rules are either honoured or quietly
broken, so it is mostly a list of cases and what each one does.

## When it runs

A plain windowed receiver launch. Every one of these skips it:

| condition | why |
| --- | --- |
| `--headless` | there is no window to draw it in |
| `--file` | a recording's correction is already in its samples |
| `--duration` | a timed run is a scripted run |
| `--view <anything>` | the operator named the screen they want |
| `--ppm` | the correction for this run has been stated (see below) |
| `--site` (or `SDRPROBE_SITE`) | ticket 04b: the form has nothing left to ask |
| `--no-startup`, `startup_prompt 0` | asked not to |

`--view startup` is the one exception that opens it deliberately.

**`--ppm` suppressing it is a provenance rule, not a convenience.** The
distinction between applying a correction and recording one was bought with
`.scratch/device-model/issues/12-*`: `--ppm 0` overwrote a measured +32 twice
in one afternoon, and because `reading_origin_for()` refuses outright when the
crystal error is zero, every coherence verdict silently became `unexplained`.
A startup calibration that measured a crystal and offered to overwrite an
explicit `--ppm` would be that confusion in a new place.

## What it commits, and when

On **Continue**, in this order:

1. the form's fields into `app->config` and `app->installation` -- site,
   antenna, receiver label, via `config_remember_site()` and
   `config_remember_antenna()` so a name typed once is offerable next time;
2. the correction, if the gate opened: `retune_receiver()` with the suggested
   ppm, then `installation_record_ppm()` and **one** `installation_commit()`.
   This is what the Apply PPM button already does at `overlay_calibration.c:755`
   -- call the same path, do not write a second one;
3. the FCCH bookkeeping, when and only when the source was
   `CALIBRATION_SOURCE_FCCH`: `gsm_valid`, `gsm_arfcn`, `gsm_expected_hz`,
   `gsm_tune_hz`, `gsm_ppm`, `drift_health = CAL_HEALTH_GOOD`. Miss this and
   the drift re-check never runs for the whole session, silently;
4. the fallback band for this site, if the LTE path was taken
   (`startup_band`);
5. the health dots, so the verdict stands for the rest of the session: the
   correction, the reference it came from, and the channel (ticket 02);
6. the tuning and rate the form asked for, and the lease given back.

**Not before.** The crystal does not depend on the site, so the scan may start
while the operator is still typing -- but a correction is filed by receiver
*and* site (ADR-0018), so filing waits for a committed site. A measurement
that locked against a site the operator then changed is filed against the new
one, which is correct: the crystal is the same crystal, and the site is
whichever one they say they are at.

## The cases with no calibration

| case | what the form does |
| --- | --- |
| no GSM BCCH and no reachable LTE band | says so, Continue enabled at once |
| `installation_identified()` false | measures, but says the result cannot be filed until a label is given |
| the receiver refuses 1.92 MS/s | the LTE fallback is unavailable; say which, quoting `app->receiver_error` |
| the gate never opens | the verdict carries the reason, and nothing is filed |

**A failed calibration files nothing and applies nothing.** A correction that
did not pass the gate is wrong by however far the estimate had not settled,
and ADR-0004 exists because one was once accepted that should not have been.

## Gain

Applied when changed, and **the calibration restarts**. A band scan's cell
list is a function of the gain it ran at, so a cell found at one gain and
measured at another is two measurements wearing one label. The form says it
restarted rather than doing it quietly.

## Acceptance criteria

- `make check-options` covers every skip condition above, each as its own case.
- `make check-pipelines` is unchanged and still passes -- no capture run and no
  headless run may reach this overlay.
- Every screenshot recipe in the `screenshot` skill still works untouched.
- `make check` passes.

## The measurement this ticket needs, which no check can make

**No capture retunes**, so nothing in `make check` exercises a scan step, a
settle, or a lease handed back. The survey extraction was byte-identical on
both capture surveys and byte-identical on screen while the settle that throws
away stale blocks was disabled, and what caught it was one line the program
prints about itself on a live sweep.

So, on air, against the current binary:

1. a cold launch where GSM 900 has a BCCH -- record the ARFCN chosen, the
   residual sequence, the verdict, and the wall-clock from launch to Continue;
2. the same at a site or a band where `scan_select_bcch()` returns 0, so the
   LTE fallback runs;
3. `--view gsm --duration 20` immediately afterwards, confirming the restored
   correction is the one just measured and that no form appeared;
4. the drift re-check firing 300 s later, from a tab that is **not** the GSM
   view, and the dot changing state there;
5. the same four runs again with `--debug-log`, and the trace read back
   without the window -- which is ticket 07's acceptance criterion and is best
   measured on the same sessions rather than on four more.

Paste the numbers into this ticket. A verdict with no sequence behind it is
the thing `--calibrate` prints every measurement to avoid.


## What was built

The rules are `startup_form_wanted()` in `options.c`, pure and covered by
`check-options`; the commit is `startup_commit()` in `overlay_startup.c`,
calling the same `installation_record_ppm()` / `installation_commit()` path
the Apply PPM button uses. `check-pipelines` is unchanged and passes.

## The measurement, taken on air

`./sdrprobe --view startup --duration 150 --debug-log ...`, R820T, site
home-sala-estar, +32 ppm restored at startup.

| what | reading |
| --- | --- |
| GSM scan | 16 steps, **14.9 s**, chose **ARFCN 113** at FCCH confidence 1.00 |
| measurement | 122 FCCH residuals in 8.1 s |
| centre | **-3.29 ppm**, sem **0.07**, spread 0.54 |
| suggestion | **+35 ppm** (from the +32 in force) |
| verdict | `locked`, **23.0 s from launch** |
| afterwards | the lease returned the receiver to 1090 MHz |

Three things that reading establishes and no check could:

- **The predicted cost was right.** 12.8 s of plan plus the retune latency;
  steps came 0.89 s apart against the plan's 0.80, and the difference is the
  tuner, which is exactly what the settle rule exists to absorb.
- **ARFCN 113 is the one cell still on air here**, which `CLAUDE.md` records
  independently -- so `scan_select_bcch()` chose the only channel that could
  have been calibrated against.
- **The robust centre earned its place twice in one run.** Residuals 5 and 12
  came back at -49.55 and -52.02 ppm; the median/MAD centre did not move
  (-3.87 to -3.82). A mean would have followed them.

Still to measure on air: the fall-through to LTE at a site with no BCCH, and
the drift re-check firing 300 s later from a tab that is not the GSM view.
