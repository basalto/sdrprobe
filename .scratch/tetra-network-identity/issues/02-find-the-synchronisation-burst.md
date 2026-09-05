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

**2026-09-06 — and then the document turned up, so the ticket got its original
answer too.**

ETSI publishes its deliverables free of charge; EN 300 392-2 V3.8.1 (2016-08)
downloads from etsi.org with a browser user-agent. So the correlator this
ticket asked for could be written after all, and it is the strongest evidence
in the whole effort.

Table 9.9 puts the synchronization training sequence at bits 215 to 252 of the
510-bit synchronization continuous downlink burst -- symbols 108 to 126 of the
255 -- and equation (9.11) gives the 38 bits. On the capture:

| chunk | matched | position in the 255-symbol slot |
| --- | --- | --- |
| 0 | **19 of 19** | 188 |
| 1 | **19 of 19** | 143 |
| 2 | **19 of 19** | 98 |
| 3 | **19 of 19** | 53 |
| 4 | **19 of 19** | 8 |
| 5 | **19 of 19** | 218 |
| 6 | **19 of 19** | 173 |
| 7 | **19 of 19** | 128 |

Every symbol, every chunk. And the position walks by exactly 45 symbols per
chunk and wraps cleanly through 255 -- 8 to 218 is -45 modulo 255 -- which is
the chunk stride against the slot length and could not be regular if the timing
were slipping.

Empty spectrum reaches 12 to 14 of 19, never 16.

**This also found a bug the round trip could not.** The phase transition table
was guessed before the document was to hand, and the guess had dibits 2 and 3
swapped: table 5.1 gives B(2k-1),B(2k) of 1,0 as -pi/4 and 1,1 as -3pi/4, and
the guess had those the other way round. Every synthetic test passed anyway,
every time, because the encoder and the decoder shared the mistake -- which is
precisely what ticket 01's header warned would happen and why it refused to
claim the mapping. With the table corrected the sequence matches 19 of 19; with
the old one it could not have.

The match threshold moved too, and from measurement rather than taste. Thirteen
of nineteen is one in a hundred thousand per position, which sounded ample and
is not: a block holds a few thousand positions, and an empty channel produced a
fourteen. Sixteen costs nothing, since the real signal gives nineteen.
