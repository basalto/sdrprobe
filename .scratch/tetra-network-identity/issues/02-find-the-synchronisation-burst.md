# 02 — Find the synchronisation burst

Status: resolved
Blocked by: 01

A TETRA downlink is continuous, so there is no silence to find a burst against
-- the structure is entirely in the symbols. The synchronisation burst carries
a known training sequence, and correlating for it is what turns a stream of
dibits into frames and timeslots.

This is the closest thing here to `gsm_dsp`'s FCCH and SCH: a known sequence,
found by correlation, that establishes where everything else is. The difference
is that GSM's frequency correction burst is a pure tone and can be found by
looking at the spectrum alone; TETRA has nothing of the kind, so correlation is
the only way in.

## What must be checkable

- The training sequence correlates to a sharp peak on synthetic symbols at a
  known offset, and its runner-up is far below -- the same shape the LTE
  primary sequence is checked with, and for the same reason.
- On the real capture, bursts land on a **56.67 ms** grid and hold that cadence
  across the whole three seconds. A correlator that finds a peak per burst but
  cannot keep the cadence has found noise that fits.
- The peak survives the frequency offset ticket 01 measures, since that is what
  a real capture has.

## Comments

**2026-09-05 — resolved, and not the way this ticket proposed.**

The ticket asked for a correlator against the synchronisation burst's training
sequence. That sequence is a constant out of ETSI EN 300 392-2 and **the
document is not available here**. Transcribing a 38-bit pattern from memory is
precisely the failure this repository has met twice -- a wrong constant
correlates perfectly with an encoder that shares it and finds nothing on air --
so it was not transcribed and no correlator was written against a guess.

The grid was measured from the signal instead. A burst structure means some
symbol positions repeat every period and the rest do not, and that shape is
findable with nothing transcribed at all. `tetra_burst_find()` searches lags
for the fundamental period and reports how each position within it behaves.

### What it finds, on three seconds at 392.640 MHz

| | period | repeat | runner-up | fixed | varying |
| --- | --- | --- | --- | --- | --- |
| the carrier, chunk 0 | **255** | 0.811 | 0.363 | 163 | 51 |
| chunk 1 | **255** | 0.799 | 0.366 | 181 | 58 |
| chunk 2 | **255** | 0.805 | 0.367 | 181 | 57 |
| chunk 3 | **255** | 0.797 | 0.362 | 179 | 56 |
| +700 kHz, empty | none | 0.26 | -- | -- | -- |
| `fm_rds_tsf.bin` | none | 0.28-0.51 | -- | -- | -- |

**255 symbols is one TETRA timeslot**, 14.167 ms at 18 000 symbols per second,
and it comes out the same in every chunk while the controls find nothing and
their best lag wanders between 201 and 225. A separate run over the whole
capture put the strongest lag anywhere in 200 to 1200 at exactly 255 (0.744),
with one TETRA frame at 1020 next (0.547) and every neighbouring lag near
chance.

About 180 of the 255 positions are fixed and about 55 vary, which is a burst
with a large static part -- consistent with a lightly loaded control carrier
sending the same filler in most slots. That mixture is the finding: a profile
flat at 0.25 is noise and a profile flat at 1.0 is a stuck receiver.

### Two mistakes, both caught by measurement rather than by thinking

**The first profile was flat and meaningless.** Symbols were stitched from
chunks of 2323, and 2323 is 9x255 + 28, so the phase reference shifted by 28
every chunk and the histogram smeared every burst position together. It showed
"0 of 255 positions fixed", which is what a burst structure does *not* look
like, and it nearly became a finding. Measured within chunks and aligned, it is
181 of 255. The header now warns that this needs contiguous symbols.

**The finder picked a harmonic.** Anything with a period of 255 repeats just as
well at 510, 765 and 1020, and on the synthetic check those tied to within a
thousandth -- it chose 1020. It now takes the smallest lag that matches as well
as the best, and measures the runner-up only over lags that are *not* multiples
of it. On real air the harmonics are weaker, so this would not have shown up
there; relying on that would have been relying on the signal being interesting.

### What ticket 03 needs, and does not have

The channel coding is all constants from the same unavailable document: the
scrambling, the RCPC puncturing pattern, the interleaving, and the block layout
within the 510 bits. None of it can be measured out of the signal the way the
grid was, because every one of them is a convention rather than a structure. 03
is therefore **needs-info**, blocked on the standard rather than on work.

What can be done without it, if anything is wanted next: the *boundary* of the
burst within the 255 is visible in the profile as the pattern of fixed and
varying positions, and a longer capture would sharpen it. That is not a decoder,
but it would narrow what the layout can be.
