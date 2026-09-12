# The session begins at a known installation

## Status

accepted

## Context

Every number this program produces is keyed to the arrangement that produced
it: a site, an antenna, a receiver, and a crystal correction. ADR-0018 keys a
tuning correction by receiver and site; ADR-0022 keys a survey history by
receiver, site and antenna. Both exist because attaching one arrangement's
measurement to another is silent rather than wrong-looking.

Until now three of those four were *assumed* at startup and the fourth was
restored from a file. Nothing asked. The first thing that noticed a wrong
assumption was a survey saved under the wrong site, which is silent and
permanent.

## Decision

On a plain windowed receiver launch, the program does not reach a view until
the site is known and a calibration has been attempted.

### The form is modal, and that is not a preference

`start_calibration()` and `startup_session` both borrow the receiver for the
whole measurement and retune it to the reference carrier. There is no
arrangement in which a calibration runs "in the background" and the rest of
the program shows what its own controls say -- every view would be drawing the
calibration carrier. Modal is the only honest presentation of a receiver that
is busy.

While the form is up, the calibration reports into the form's own panel. There
is a whole screen to report into, and a popup over a modal form would be a
second window saying what the first one already says.

### GSM first, LTE on fall-through -- and not for the reason usually given

`startup_session` scans GSM 900 with `scan_plan.h`'s walk and takes
`scan_select_bcch()`. A non-zero ARFCN calibrates against its FCCH; zero falls
through to an LTE band scan.

**It is not because GSM is more precise.** Both sources pass through one gate,
`calibration_is_stable()`, which will not lock either until the standard error
of the centre is under `CALIBRATION_MAX_SEM_PPM` -- so a lock is a lock at the
same tolerance whichever reference produced it, and the one on-air comparison
in `CLAUDE.md` has them agreeing to about a ppm. Writing "GSM is more precise"
into the code would be a claim nobody measured.

The reasons that are established:

1. **Only an FCCH-backed calibration enables the drift re-check.**
   `update_drift_check()` returns immediately unless `cal.gsm_valid`, which is
   set only for `CALIBRATION_SOURCE_FCCH` (ADR-0006). The choice of reference
   decides whether the receiver watches its own crystal for the rest of the
   session -- a capability difference, not a precision one.
2. **An FCCH has no modulo ambiguity.** It is an unmodulated tone by
   construction. LTE's offset is a PSS *phase*, which wraps every 15 kHz and
   needs an integer-subcarrier sweep to recover.
3. **It is far cheaper.** At 2 MS/s `scan_plan_make()` covers the 24.8 MHz
   downlink in sixteen steps of 0.8 s -- 12.8 s, against minutes for a band of
   LTE channels.

The fall-through condition is **"no BCCH found", not "no GSM power found"**.
Choosing by power hands the calibration a channel with no tone in it, and it
then spends the whole budget failing to lock. `scan_choose()` sits beside
`scan_select_bcch()` and does exactly that, correctly, for the GSM view -- for
a chart, something to look at beats nothing -- and it is the wrong one to call
here.

**And a tone is not a broadcast carrier either.** `GSM_FCCH_SEARCH_HALF_HZ` is
50 kHz and the detector reports any coherent line inside that; a high
coherence says the line is steady, not that it is an FCCH. So the chosen
channel has to produce a parity-valid synchronisation burst before anything is
measured against it, and a candidate that cannot is dropped for the next best.
The gate is demonstrated firing on a synthetic bare tone in
`check-startup-session`; on air it has so far confirmed every channel it was
given, which is worth stating plainly, because a negative from a gate nobody
has seen fire is not a finding.

### The verdict is a standing fact, not an event

It lives in the health indicator ADR-0006 already established: a GSM lock
turns the GSM dot green with its ARFCN, an LTE lock turns the LTE dot green
with its EARFCN, and the hover says the correction, the reference and how long
ago. **Nothing in this program dismisses itself on a timer.** A self-erasing
notice was built and rejected: ten seconds later the crystal is still
corrected, still by that amount, still against that reference, and a surface
that erased itself could not answer "what am I corrected by?" at any later
moment.

### Saying what the form would ask is how it is bypassed

A run that already states its installation has nothing to be asked.
`startup_form_wanted()` in `options.c` is the whole rule, pure and checked:
`--headless`, `--file`, `--duration`, `--view`, `--ppm`, `--site` and
`--no-startup` each skip it. `SDRPROBE_SITE`, `SDRPROBE_ANTENNA`,
`SDRPROBE_RECEIVER_LABEL` and `SDRPROBE_NO_STARTUP` do the same from the
environment, for a launcher or a unit file that cannot reach the command line.

**A flag beats an environment variable beats the config file**, one rule, and
a value arriving by any of the three routes is remembered the same way -- a
site named once should be offerable next time however it was named. An empty
variable is not a value: a unit file that forgot to fill one in must leave the
config alone and must not suppress the form.

`--view startup` opens it anyway, for the reason `START_VIEW_CALIBRATION`
exists.

### `--ppm` suppressing it is a provenance rule

`.scratch/device-model/issues/12-*`: `--ppm 0` overwrote a measured +32 twice
in one afternoon, and because `reading_origin_for()` refuses outright when the
crystal error is zero, every coherence verdict silently became `unexplained`.
A startup calibration that measured a crystal and offered to overwrite an
explicit `--ppm` would be that confusion in a new place.

### A fifth overlay, not a fifth flag

ADR-0008 says extend the enums. `input_route()` gains `INPUT_TARGET_STARTUP`
between Help and Settings: Help stays outermost because it can be raised over
anything, and the form outranks Settings because Settings is reached through
it. `open` nests inside `struct startup_view` like `cal.open` and `help.open`.

### The deciding half is not in the drawing

`startup_session.{c,h}` has never seen `struct app`, raylib, a file or a
receiver. It says where it wants the tuning and at what rate; the adapter
obeys and reports back. It does not read or write files, and it applies
nothing -- it offers a correction and `installation_commit()` stays the one
writer. `survey_session.{c,h}` is the shape, and ADR-0012 is the rule.

## Consequences

- A cold launch costs 12.8 s of GSM scanning plus the gate's own settling
  where GSM 900 is on air. Where it is not, the LTE band scan is minutes:
  **Skip** releases the form at any moment, and a scripted run never sees it.
- `installation_commit()` is still the one writer, and filing waits for a
  committed site. A crystal does not depend on where it was measured, so the
  scan may start while the operator is still typing; a correction is filed by
  receiver **and** site, so it is filed against whatever the form says at
  Continue.
- Changing gain restarts the calibration. A band scan's cell list is a
  function of the gain it ran at, so a cell found at one gain and measured at
  another is two measurements wearing one label.
- A failed calibration files nothing and applies nothing (ADR-0004).
- The receiver label is on the form because a receiver with no identity cannot
  have a calibration profile at all (ADR-0018), and startup is where that is
  still fixable rather than discovered when the correction fails to file.
- `check-startup-session` reaches every transition with no window and no
  receiver, including the settle timed from the tuning -- which no capture can
  exercise, because nothing in `testfiles/` retunes.
