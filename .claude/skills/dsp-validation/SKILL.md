---
name: dsp-validation
description: Establish that a DSP result is true and not merely self-consistent. Use when changing gsm_dsp, adsb_dsp, lte_dsp or a decode chain, when a decode is wrong or has regressed, or before writing a check that pins a real-capture answer.
---

# Believing a decode

A decode that locks is a **hypothesis**, and a confident one is not better
evidence than a weak one -- the two most expensive faults in this repository
both presented as a clean lock on a wrong answer. What settles it is
**corroboration**: a second measurement that does not share the first one's
code path.

## What a round trip cannot prove

`make check` builds a signal and reads it back. Both directions call the same
functions, so the round trip proves the code agrees with itself. **A convention
both directions share is invisible to it**, however many checks are green.

Twice now that hole has swallowed months:

- The LTE primary sequence generated the conjugate of the standard's. Roots 29
  and 34 are each other's conjugate, so every cell was found with correct
  timing and reported N_ID_2 one step off. 516 checks passed.
  (`.scratch/lte-cell-search/issues/04-the-conjugated-primary-sequence.md`)
- The GSM SCH read its 25 information bits as four contiguous fields where
  TS 44.018 scatters them. Self-consistent with the encoder, so the round trip
  passed while every non-constant field came out wrong.
  (`docs/adr/0011-sch-frame-number-joint-trellis.md`)

Read whichever is nearer your change before starting. They are short, and they
are the same shape as the fault you are about to look for.

## Steps

1. **Name what would be self-confirming.** Before measuring anything, say which
   evidence would agree with your change *because* of your change -- a round
   trip, a detector reading a sequence its own generator built, a check whose
   expected value you took from a run of the code. Set those aside as unable to
   testify.

2. **Run `make check`.** Necessary, never sufficient. It catches the regression
   you did not intend; it cannot catch the convention you got wrong.

3. **Corroborate on a real capture.** Find one measurement that reaches the
   same conclusion without sharing the path under test -- see below for what
   qualifies. Done when a real signal and an independent measurement agree.

4. **When two measurements disagree, establish which one moves.** This is the
   step that cost a day. The reasoning that flipped the primary sequence's sign
   was correct that the two sides were inconsistent and correct about the size
   of the gap; it was wrong about *which side* to move, and moving the wrong
   one made a broken detector agree with a newly broken generator. Two wrongs
   agreeing is indistinguishable from two rights agreeing, from the inside.
   Work outward from whichever fact is hardest to doubt, and move the side that
   no independent measurement supports.

5. **Check the convention against something outside this repository** whenever
   a constant, a sign, a bit order or a field layout is in question. A named
   clause of the standard, or a reference implementation: srsRAN
   (`lib/src/phy/sync/pss.c` settled the sign in minutes), dump1090 for Mode S
   (`docs/dump1090-reference.md`, since that source is not vendored here).
   Quote the lines you relied on in the commit message.

6. **Then pin it.** A check written after the fact must assert what the
   independent measurements agreed on, never a value read off a run of the code
   under test -- that is how `check-lte-dsp` and `check-pipelines` spent days
   asserting cell 32 and lending a wrong answer real-signal authority.

## What counts as independent

- **A measurement with no model in it.** Where the energy sits, how often
  something repeats. The secondary sequence was located, before its content
  could be read, by energy confined to the central 62 subcarriers 17 dB above
  the rest, and by agreeing 0.8 across a whole frame against 0.3 across half
  a frame -- which no other signal does.
- **A reference-free form of the same test.** Each subcarrier times the
  conjugate of its neighbour needs no channel estimate, so it cannot inherit
  a channel-estimate fault. Prefer the differential form of a metric whose
  coherent form assumes something you have not established.
- **Diversity of captures.** The three GSM captures carry three different
  BCCs on purpose -- 3, 0 and 6, inside BSICs 59, 56 and 38. The BCC picks the
  training sequence every normal burst is found by, so a hardcoded one passes
  `gsm_arfcn_69.bin` and fails `gsm_arfcn_113.bin`. One capture agreeing is one
  measurement.
- **Agreement over time.** `probe-gsm-chain` reports `BSIC agreement: 31/31`
  and the frame-number gap per block. A field that is constant in the world and
  varies across blocks is decoding wrong, whatever its parity says.
- **Physical plausibility.** A tuning error is a property of the dongle, so it
  should be the same ppm at every frequency and within what the part can drift.
  An offset that changes with the answer is a symptom, not a measurement.

## A search needs its noise floor first

A brute-force sweep always returns a winner. Before believing one, know what
the best score looks like when there is nothing to find -- run the same search
against noise or a shuffled input.

Several searches here over ~400k hypotheses scored 12 samples each, hit a
ceiling near 0.9 by chance alone, and returned confident wrong answers.
Averaging over ten symbols dropped the floor to ~0.43 and the real match stood
clear. Raise the evidence per hypothesis until the floor is visibly below a
true match; a margin over the runner-up is worth more than an absolute score,
which is why `lte_cell_search` carries `LTE_SSS_MIN_MARGIN` as well as a
threshold.

The same arithmetic applies to parity. 16 bits accept 1 in 65536 by chance, and
36 MIB attempts a block over a long session make a false pass a certainty
rather than a hypothetical -- `lte_mib_same_cell` exists because of it. Repeat
before reporting.

## Which tool answers which question

| Question | Reach for |
| --- | --- |
| Where in the chain does it break? | `make probe-gsm-chain`, `probe-adsb-chain`, `probe-lte-chain FILE_LTE=...` -- a block-by-block walk, ending in a stated conclusion |
| What exactly did it decode? | `--headless --decode --once`, exact and untruncated |
| Did the change help or hurt? | the same headless run before and after, on the same capture |
| Does it still fit the block budget? | `make bench-dsp`, against 65.5 ms a block (68.3 for LTE at 1.92 MS/s) |
| Does it draw correctly? | the `screenshot` skill -- for looking, not for reading values off |

`--headless` file playback is lossless and unpaced, so a scripted decode sees
every block and gives the same answer twice. That repeatability is what makes a
before-and-after comparison mean anything.
