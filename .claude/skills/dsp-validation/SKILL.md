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
- **An implementation that shares no code.** numpy is installed here for
  this, and the boundary is in `AGENTS.md`: exploration and cross-checks only,
  never where an answer lives. It earns its place on exactly the faults
  nothing else catches -- where the arithmetic is right and the *sampling* is
  wrong, which a reader re-reading the code re-makes. It found a DFT scan
  returning a sidelobe (2 Hz steps over a record resolving 0.25 Hz) and it
  cross-checked `sdr_dsp_spectrum()` against off-bin tones, two tones, noise
  and a real capture -- agreement better than 0.01 dB above -100 dBFS, on a
  transform whose only fixture put a tone on bin 37 exactly. Note what this
  is *not*: a second C probe compiled from the same headers agrees by
  construction and is worth nothing here. Use `/usr/bin/python3`; the `python3`
  first on PATH is a mise shim without numpy.

## What a synthetic can and cannot stand in for

The round-trip hole above is about a signal and a decoder sharing a
*convention*. This is the other one, and it is not the same: a synthetic that
is perfectly self-consistent, decoded correctly, and models a case the world
does not produce. Nothing is inconsistent, so nothing warns.

**A synthetic proves the code does what the synthetic asked. It says nothing
about the air unless the synthetic is the air.**

The case that cost the most: a spectrum path was suspected of losing about
9 dB, because the DC-spike filter subtracts the sample mean and the
confirmation pass tunes onto its target. A synthetic put a carrier at exactly
0 Hz, where the sample mean *is* the carrier, and duly showed 9.3 dB going
missing. On air the same signal recorded at both tunings read **21.9 dB tuned
onto it and 20.1 dB tuned 300 kHz below** -- two decibels, in the other
direction. A carrier at the tuned frequency sits at the tuning error, hundreds
of hertz at least, which is many cycles in a block, so its mean is near
nothing and the filter never touches it. **A carrier at exactly zero is a
frequency the receiver cannot produce**, and a recommendation to change working
code was one command away from being acted on.

Two smaller ones from the same session. A "wide signal" stood in as an
amplitude-modulated carrier, so most of its energy was still in the carrier
and the row it produced was meaningless. And a burst-length fixture's
"training sequence" was `slot & 3`, periodic at four, so every lag that was a
multiple of four scored nearly as high as the true period and the finder
rightly refused to call it a finding.

### The questions

1. **Which parameter of the synthetic is at a value the world cannot take?**
   Exactly zero, exactly on a bin, exactly on a sample, exactly periodic,
   noiseless, drift-free. Every one of those is a place real hardware never
   sits, and each makes some statistic degenerate.
2. **Record the control before believing the synthetic.** Two captures and
   three minutes settled the DC question after a synthetic and an hour of
   reasoning had it backwards. If a claim is about the receiver's behaviour,
   the receiver is cheaper to ask than to model.
3. **Make the instrument fire on something known before trusting a null.**
   `probe-nbiot` exists for this and its `--self-test` is the pattern: a
   detector nobody has seen succeed is not a detector. An AIS null was worth
   nothing until the same code put a known TETRA carrier 16 dB clear of its
   own controls.
4. **Ask whether the statistic can see the thing at all.** A 99.9th percentile
   cannot see an event occupying 0.04% of a buffer, by construction, however
   loud it is. That is not a threshold to tune; it is the wrong statistic.

`make probe-signal` is the tool for the second and third: it measures a
capture at a signal *and at controls* in one command, which is the shape all
of this keeps needing.

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

## Is it *better*? That is a different skill

This establishes that a result is true. Whether a change *improves* anything
fails in its own ways -- measuring where the answer cannot show, comparing at
different gains, one draw of noise -- and `does-it-help` carries those.

## Which tool answers which question

| Question | Reach for |
| --- | --- |
| Where in the chain does it break? | `make probe-gsm-chain`, `probe-adsb-chain`, `probe-lte-chain FILE_LTE=...` -- a block-by-block walk, ending in a stated conclusion |
| What exactly did it decode? | `--headless --decode --once`, exact and untruncated |
| Did the change help or hurt? | the same headless run before and after, on the same capture |
| Does it still fit the block budget? | `make bench-dsp`, against 65.5 ms a block (68.3 for LTE at 1.92 MS/s) |
| Does it draw correctly? | the `screenshot` skill -- for looking, not for reading values off |
| Is something on air, or is that noise? | `make probe-signal FILE_SIGNAL=... AT_SIGNAL=... CONTROLS_SIGNAL=...` -- the same measurement at the signal and where nothing should be |

`--headless` file playback is lossless and unpaced, so a scripted decode sees
every block and gives the same answer twice. That repeatability is what makes a
before-and-after comparison mean anything.
