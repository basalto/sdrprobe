# 01 — One carrier to symbols, and the proof it is TETRA

Status: resolved

Take a 25 kHz channel out of a 2 MS/s block and turn it into dibits: matched
filter, timing recovery, pi/4-DQPSK differential demodulation.

This ticket also closes the spec's open question. The symbol-rate line says
TETRA at a factor of three to five above its own floor, which is suggestive and
not proof. A demodulator that produces a **clean eight-point differential
constellation at 18 000 symbols per second** is proof, because nothing else in
that allocation would.

## Where the difficulty is

**The timing.** 18 000 does not divide 2 000 000 -- it is 111.11 samples per
symbol -- so there is no whole decimation, which is the trick `fm_dsp` uses and
the reason its comment says a station's rates are all whole multiples of the
pilot. Here the loop has to interpolate. `gsm_dsp` already does sub-phase
timing for the same reason (`.scratch/sch-frame-number/issues/01`), and that is
the nearest thing to copy.

Decimating to about 100 kS/s first is fine and cheap; the fractional part
remains either way.

**pi/4-DQPSK is differential, so absolute phase never matters.** That is a gift
-- no carrier phase recovery -- and a trap: the frequency offset still rotates
every symbol by a constant, so a residual offset appears as a fixed rotation of
the whole constellation and quietly turns every dibit into a different dibit.
The receiver is about 35 ppm out at this site, which is 14 kHz at 392 MHz and
nearly a whole symbol period of phase per symbol. It must be measured and
removed, not assumed away.

## What must be checkable

- A synthetic round trip: dibits to pi/4-DQPSK at 18 ksym/s and back, at
  several frequency offsets and timing phases, with no receiver involved.
- **The real capture reaching a constellation with structure**, measured rather
  than looked at: the eight differential phase points, each within a stated
  angle of its ideal, and an error-vector magnitude reported so a regression
  shows up as a number. `captures/` is gitignored, so the ticket carries the
  command to re-record.
- That the demodulator refuses a sample rate it cannot work from, the way
  `lte_` functions refuse anything but 1.92 MS/s (ADR-0014).

## What this must not become

A constellation on a screen and nothing else. The measurement is the
deliverable; drawing it is ticket 06's business.

## Comments

**2026-09-05 — resolved. `src/tetra_dsp.{c,h}`, checked by `check-tetra-dsp`.**

### It is TETRA

The spec left this open at a factor of three to five, which was not enough. A
demodulator settles it at a factor of sixty. Three seconds at 392.640 MHz, in
six chunks of 0.13 s:

| tuned to | lock | rms error | timing phase across the six chunks |
| --- | --- | --- | --- |
| the carrier | **0.80 - 0.83** | 0.079 - 0.089 rad | 0.322, 0.400, 0.485, 0.564, 0.645, 0.730 |
| +700 kHz, empty | 0.006 - 0.014 | 0.449 rad | 0.207, 0.897, 0.975, 0.096, 0.029, 0.252 |
| `fm_rds_tsf.bin` | 0.012 - 0.034 | 0.448 rad | random |

The FM control matters as much as the empty one: it is a strong real signal
that is not pi/4-DQPSK, and it locks no better than noise.

**The timing phase is the best evidence and it was not the evidence looked
for.** It advances monotonically -- 0.322 to 0.730 over 0.65 s -- where both
controls scatter. Noise cannot produce a monotonic drift. And the rate of it
is 0.628 symbols per second against 18 000, which is **35 ppm**: the site's
configured crystal correction is 35 ppm, measured by an entirely different
route (the calibration, and the 1.6 MHz comb in `.scratch/receiver-comb/`).
Two unrelated measurements of the same crystal agreeing is the corroboration a
round trip cannot give (`dsp-validation`).

The fine frequency offset is likewise steady at -203 to -237 Hz where the
controls jump by kilohertz between chunks.

### What was built

Coarse offset from the channel's power centroid, then downconvert, decimate by
a whole 20 to 100 kS/s, root-raised-cosine matched filter, Oerder-Meyr timing
from the symbol-rate line in the squared magnitude, cubic interpolation to the
symbol instants, differential demodulation, and the fine offset from the fourth
power of the phase steps -- every legal step is an odd multiple of pi/4, so
four times any of them is -1 whatever was sent, the data cancels itself and
what is left is the rotation.

Two things the checks caught, both of them my claims rather than the code:

- **0.95 lock on a clean signal was asserted on no evidence and failed.** The
  floor of this implementation is about 0.05 radians of error -- 2.9 degrees on
  a constellation spaced 90 apart -- from the boxcar decimation's tilt, eight
  symbols of filter span, the interpolator and a 0.006-symbol timing bias. It
  costs no dibits. The bar is now set from the measurement, and the number that
  matters was never that one: it is the gap to noise at 0.35.
- **Linear interpolation was not good enough** at 5.56 samples per symbol,
  which showed as error that was *worst* when the timing was on a sample --
  the signature of an interpolator, not of a timing estimate. Cubic fixed it.
  The apparent asymmetry that pointed there turned out to be different random
  sequences between calls; a direct sweep of the timing showed the estimate
  tracks truth to 0.006 of a symbol at every phase.

### For ticket 02

The timing drifts 0.08 of a symbol per 0.13 s chunk, so anything that tracks
bursts across a capture has to re-estimate at least that often. It is
open-loop and cheap, so re-estimating per chunk is the obvious answer.

The dibit *mapping* is still unverified and must not be believed until a parity
check passes over real symbols. What is established is the shape -- four
distinct odd multiples of pi/4 -- and that is all ticket 02 needs to correlate
for a training sequence.

**2026-09-06 — the mapping this ticket refused to claim was wrong, and is now
fixed.**

The header said the dibit mapping must not be believed until a parity check
passed over real symbols. It should not have been: EN 300 392-2 table 5.1 gives
1,0 as -pi/4 and 1,1 as -3pi/4, and the guess here had those two swapped. Every
synthetic round trip passed regardless, because both sides shared it.

Corrected, and confirmed the only way it could be -- the standard's 38-bit
synchronization training sequence now matches 19 of 19 on air, in every chunk
of the capture. See ticket 02.
