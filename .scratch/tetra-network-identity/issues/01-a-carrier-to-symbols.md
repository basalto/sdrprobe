# 01 — One carrier to symbols, and the proof it is TETRA

Status: ready-for-agent

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
