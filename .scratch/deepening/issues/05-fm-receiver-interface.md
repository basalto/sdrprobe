# 05 - Close the FM receiver's interface

Status: needs-triage

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
