# 04 — A conjugated primary sequence, and why nothing caught it

Status: resolved
Blocked by: (none)

The first live captures found their cell immediately -- a correlation of 0.62
to 0.80, sharp to the sample, repeating every 5 ms exactly as the standard says
-- and then failed to read the secondary sequence under any hypothesis: either
cyclic prefix, any timing within a symbol either way, any frequency offset
within a few kilohertz, and finally an exhaustive search over every degree-5
recurrence and every pair of shifts. Nothing above chance.

## What it was

`lte_pss_sequence` generated the conjugate of the standard's Zadoff-Chu
sequence. For a punctured length-63 Zadoff-Chu, conjugation maps root u to root
63 - u, and the three roots LTE uses are 25, 29 and 34: **29 and 34 are each
other's conjugate**. So the detector found every cell, with correct timing and
a coherent channel, and reported N_ID_2 1 where the truth was 2 and 2 where the
truth was 1. Since the secondary sequence's scrambling is seeded by N_ID_2,
every one of the 336 candidates was built wrong, and the search found nothing
because there was nothing to find.

Root 25's conjugate is 38, which no cell uses, so N_ID_2 0 was invisible
altogether. That is the half of the fault that had no symptom at all.

## Why the checks passed

`check-lte-dsp` builds a frame and reads it back, and the builder calls the
same `lte_pss_sequence` the reader does. Both conjugated, both agreed, 516
checks green. This is the same shape of hole as the SCH field layout that
survived until 2026-08-30 (see `docs/adr/0011`): **a round trip cannot check a
convention that both directions share.**

The capture is now in `testfiles/` and `check-lte-dsp` asserts its cell
identity, which is a check the conjugated code cannot pass.

## How it was found

Working outward from what could not be doubted:

1. The symbol one before the primary sequence had the synchronisation
   signature -- energy confined to the central 62 subcarriers, 17 dB above the
   rest -- so it was the secondary sequence and it was in the right place.
2. It repeated with agreement 0.8 across a whole frame and only 0.3 across half
   a frame, which is precisely how the secondary sequence behaves and nothing
   else does.
3. So the sequence model was wrong, not the position. A reference-free search
   -- each subcarrier times the conjugate of its neighbour, which needs no
   channel estimate at all -- over every degree-5 recurrence and every shift
   found the standard's own two polynomials at 0.915, with a scrambling shift
   of 2 where the primary sequence had claimed 1.

One step apart, on the one pair of roots that are conjugates. That named it.

## The fix was wrong, and that is the more useful lesson

Flipping the sign of the exponent made the secondary-sequence detector start
locking, so it looked right. It was not. Checked afterwards against
3GPP 36.211 and against srsRAN's `lib/src/phy/sync/pss.c`, which is
unambiguous:

    const float root_value[] = {25.0, 29.0, 34.0};
    root_idx = N_id_2;
    int sign = -1;
    arg = (float)sign * M_PI * root_value[root_idx] *
          ((float)i * ((float)i + 1.0)) / 63.0;

A **negative** sign, and the roots indexed directly by N_ID_2 -- which is what
this file had in the first place. The reasoning that led to the flip was sound
right up to its last step: the secondary sequence really did want an N_ID_2 one
step from what the primary sequence reported, and the two really were
inconsistent. The wrong conclusion was *which of them* to move.

What the flip actually did was hide a second fault. Roots 29 and 34 are
conjugates, so flipping the sign swaps N_ID_2 1 and 2 -- and it made a broken
secondary detector agree with a now-broken primary one. Two wrongs agreeing is
indistinguishable from two rights agreeing, from the inside, which is the same
trap as the round trip.

The independent witness that settled it is the cell's own reference signals.
They have nothing in common with either synchronisation signal, and they name
a cell whose N_ID_2 is what the **negative** sign reports. See `issues/05`.

## What changed

- The sign of the exponent in `lte_pss_sequence`: flipped, then flipped back,
  and now carrying a comment naming the reference rather than the reasoning.
- The secondary-sequence detector is now the reference-free differential one
  that found the fault, because it also turned out to work far better on air:
  the live captures score 0.75 that way against 0.44 for the channel-referenced
  method, which is indistinguishable from noise.
- `check-lte-dsp` reads the live capture and asserts its identity.

## Comments

**2026-09-02 — resolved.** The primary sequence was never conjugated. 36.211
and srsRAN's `pss.c` both give the negative exponent this file started with,
`lte_pss_sequence` carries it today, and the flip was reverted in 1d260c3.

What the secondary sequence was missing was the *integer* part of the frequency
offset. A phase measures an offset only modulo one subcarrier, and this dongle
runs about -36 ppm -- two whole subcarriers at 796 MHz -- so the secondary
sequence and the reference signals were reading subcarriers two places from
where they should, while the primary sequence still locked at 0.8 with all of
it present. `lte_cell_search` now sweeps integer offsets and re-finds the
primary peak with the offset removed, because a frequency error moves a
Zadoff-Chu correlation as well as weakening it. Cell 28 reads off the capture
and off the air (54a409d).

The title stays wrong on purpose: the misdiagnosis is what the file is for, and
`.claude/skills/dsp-validation/SKILL.md` now carries the lesson to where
somebody will meet it before repeating it.
