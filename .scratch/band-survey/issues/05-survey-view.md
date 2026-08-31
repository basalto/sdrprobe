# 05 — The view: sweep, select, characterise

Status: resolved
Blocked by: 01, 02, 03, 04

`src/view_survey.c` plus `struct survey_view` in `app.h`.

## Sweeping

A per-frame state machine like the GSM scan's, but cheaper per step because it
needs power only: retune, wait `SURVEY_SETTLE_SECONDS` (0.10), take one block's
spectrum, fold the usable middle of it (discard the outer 10% either side,
where the tuner's response rolls off) into the survey array, step on. Stop
button, progress in the header, and the entry tuning restored when the sweep
ends or the view is left -- `leave_gsm()` is the pattern.

Live receiver only: in file mode the view says so, as the GSM view does.

## Selecting

Clicking a candidate in the chart or the list retunes so the candidate sits
300 kHz off centre -- never on the DC spike, which is an artifact of the
receiver and would be measured as signal -- and characterises it over
`SURVEY_MEASURE_SECONDS` (2.0): `sdr_dsp_characterise_carrier` each block,
plus the two things one block cannot show:

- **duty**: the fraction of blocks in which the candidate stood above its floor
  by more than half its first-seen prominence. Continuous, intermittent, or
  bursty, with the fraction in the text.
- **stability**: the spread of the measured centre across those blocks, which
  separates a stable carrier from something drifting or hopping.

Then the band-plan line, with the "a frequency lookup, not a detection" note
under it, and an Inspect button when the entry names a decoder: it sets the
tuning and switches to that decode view, exactly as if the operator had gone
there and tuned it themselves.

## Comments

**Measured live.** A 600 MHz sweep took 375 steps at about 0.24 s each, so the
tuner's full span is roughly four minutes rather than the two and a half the
spec estimated -- the extra is the acquisition worker being stopped and started
by each retune. The FM band, 13 steps, is seconds.

**The two signals it was built to tell apart, it tells apart.** An FM carrier
at 94.3969 MHz measured continuous (31/31 blocks) and stable to +/- 1.0 kHz. A
GSM 900 downlink channel at 936.7628 MHz measured intermittent (12/31 blocks)
and +/- 25 kHz. Same view, same two seconds.

**Occupied bandwidth needed a guard.** At -20 dB below the peak the threshold
falls into the noise for anything under about 25 dB of prominence, and the
width then measures the floor. It is held 3 dB clear of the measured floor, and
`bandwidth_ref_db` reports the drop actually used so the panel can label the
figure with the truth rather than with the request.

**Up and Down select candidates.** Not in the ticket; added because the scale
keys mean nothing in this view and stepping a list is how two carriers get
compared without hunting for them with a pointer.
