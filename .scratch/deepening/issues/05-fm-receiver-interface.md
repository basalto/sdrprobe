# 05 - Close the FM receiver's interface

Status: **wontfix, 2026-09-09** -- on the inventory this ticket asked for.
The premise is false: the interface is already closed but for one field, and
one accessor closes it. That accessor is worth adding; the module this ticket
proposes is not worth building.

`struct fm_pilot` (`fm_dsp.h:84-110`) exposes about twenty fields -- loop
phase and frequency, both correlator arms, the resonator's coefficients and
delay line, three smoother constants, the lock phasor, the error. Callers and
the analysis charts read them directly, so any change to the loop is an
interface change. `struct fm_rds_front` and `struct fm_audio` each own a
pilot, so a station being listened to with RDS shown runs two loops on the
same 19 kHz.

`view_fm.c` is 1 483 lines with 7 inline `y +=` and the chunked bit
accumulation at `:273`.

## The deepened module

One FM receiver whose interface is a block in and named readouts out: pilot
lock and coherence, pilot ppm, the multiplex spectrum, RDS soft bits, stereo
flag, audio frames. One pilot loop, one resonator, shared by the RDS front
and the audio. The loop's internals become implementation.

## Why speculative

- The analysis mode *wants* internals on screen -- the timing scores, the
  matched-filter constellation, the pilot's coherence -- so they must become
  deliberate readouts rather than disappear. Which fields the charts actually
  read has to be inventoried first.
- The resonator moved stereo separation from 23 dB to 65 (CLAUDE.md). Merging
  the two pilots changes what the audio's loop sees; the separation has to be
  re-measured on `fm_rds_tsf.bin` and the three stations the pilot thresholds
  were set against, before and after, and the `does-it-help` skill says how.
- `check-fm-dsp` asserts on some of those internals today (163 checks). Which
  of them are claims about the interface and which about the implementation
  decides what survives.

## First step

Inventory: every read of a `struct fm_pilot` field outside `fm_dsp.c`, by
file and line, sorted into "readout the view needs" and "internal the test
happens to check". That list is what makes this `ready-for-agent` or
`wontfix`.

## Depends on

Nothing, but it feeds ticket 02's FM session, so doing it first makes that
session's interface smaller.


## The inventory, 2026-09-09

Every mention of `struct fm_pilot` outside `fm_dsp.c` and `fm_dsp.h`, in the
whole tree -- `src/`, `tests/`, `scripts/`:

| file | line | what it touches |
| --- | --- | --- |
| `src/view_fm.c` | 336 | `fm_pilot_locked(&fm->session.front.pilot)` |
| `src/view_fm.c` | 368 | `fm_pilot_hz(...)` |
| `src/view_fm.c` | 374 | `fm_pilot_ppm(...)` |
| `src/view_fm.c` | **377** | `fm->session.front.pilot.coherence` |
| `src/view_fm.c` | **379** | `fm->session.front.pilot.coherence` |
| `src/view_fm.c` | 510 | `fm_pilot_locked(...)` |
| `tests/fm_dsp_test.c` | 21 sites | `fm_pilot_init` / `_feed` / `_locked` / `_hz` / `_ppm` only |

**One field is read directly, `coherence`, twice, in one function** -- the
signal panel's coherence row and the colour it is drawn in. Everything else in
the program and *everything* in the 163-check suite goes through the five
functions the header declares. `struct fm_rds_front` is likewise reached only
for its `.pilot`; `struct fm_audio` gives up `decimate` and `sample_rate` to
`view_fm.c:141-142` and `decimate` to five places in the suite.

So the three premises this ticket rests on are all false:

- *"Callers and the analysis charts read them directly, so any change to the
  loop is an interface change."* One field, in one panel. The loop phase, both
  correlator arms, the resonator's coefficients and delay line, the three
  smoother constants, the lock phasor and the error -- about twenty fields --
  have **no reader anywhere outside the module**. Changing the loop is already
  an implementation change.
- *"`check-fm-dsp` asserts on some of those internals today."* It asserts on
  none of them. The suite is written entirely against the accessors, which is
  the thing this ticket was worried about losing.
- *"The analysis mode wants internals on screen."* The timing offset, the
  axis and the constellation come from `struct fm_session` (`fm_session.h`),
  not from the pilot. The only pilot internal on screen is the coherence.

## What is worth doing, and it is not this ticket

Add `fm_pilot_coherence()` beside `fm_pilot_locked()`, `_hz()` and `_ppm()`,
and `struct fm_pilot` has no reader outside its module at all. That is a
five-line change, and it is the whole of the "close the interface" work this
ticket describes.

The rest -- one receiver object, one pilot loop shared by the RDS front and
the audio -- is a different proposal and should not inherit this one's
justification. It has no reader problem to solve, and the reason to be
cautious about it is unchanged and is written above: the resonator moved
stereo separation from 23 dB to 65, and merging the two loops changes what the
audio's loop sees. There is a real cost (two loops on one 19 kHz tone, per
station) and no measured symptom. If somebody wants it, it needs its own spec
and a `does-it-help` measurement of the separation before and after; it does
not need this ticket, whose stated reason for existing does not hold.

`view_fm.c` being 1 483 lines with 7 inline `y +=` is true and is a different
complaint, belonging with `panel_rows.h`.
