# 05 — The broadcast channel decodes synthetically and not on air

Status: needs-info
Blocked by: (none)

`make check-lte-mib` passes 289 checks and `make check-lte-dsp` recovers all
480 soft bits of a synthesised broadcast channel exactly, for one, two and four
antenna ports. Against the three live band 20 captures, no Master Information
Block decodes: no scrambling offset and no port hypothesis produces a parity
that fits.

The cell search above it is solid on the same samples -- cell 32 in every block
of `testfiles/lte_b20_pci32.bin`, cross-checked by two independent detectors --
so this is not a tuning or a timing-of-the-frame problem in any gross sense.

## Where it stops

**The cell-specific reference signals do not lock.** With the cell identity
known and the frame boundary the search reports, correlating the reference
signals of slot 1 symbol 0 against the sequence that identity implies gives a
coherence of 0.27 to 0.48, where the synthesised frame gives 0.95. Scanning the
symbol timing either way, and both candidate slot numbers, does not find a
position where it locks. Without a channel estimate the equalised resource
elements are not QPSK (0.06 to 0.16, against 1.0 synthetically), so the soft
bits handed to the decoder are noise and the parity has nothing to check.

So the question is narrow: **why do the reference signals not correlate?** The
plausible causes, none yet excluded:

- A convention in the reference-signal construction that the synthetic round
  trip shares and therefore cannot catch -- exactly the shape of fault that
  `04-the-conjugated-primary-sequence.md` turned out to be. The candidates are
  the sequence index for the central twelve (this code uses 104 to 115,
  derived from the widest bandwidth the standard allows inwards), the seed
  `c_init`, and the frequency shift each port takes.
- The frame boundary being right about the subframe and wrong about which
  half-frame it is, which would put slot 1 where slot 11 belongs and change
  the seed. The search reports the half-frame from the secondary sequence and
  it is consistent across blocks, but it has not been checked independently.
- Simply too weak a signal. The captures were taken with a short antenna
  indoors; the cell search survives it because a 128-sample correlation has
  processing gain that a twelve-point channel estimate does not.

## What a live session added

A live band 20 session on 2026-09-02 sat on cell 32 with a considerably
stronger signal than any capture: PSS 0.77 against 0.64, and SSS 0.94 with a
runner-up of 0.46 against 0.75 and 0.40. Over 9312 blocks in which a cell was
found, the broadcast channel never decoded.

That is worth more than it looks, because it is the third cause above being
tested. "Too weak" predicts that a signal half a dB stronger decodes
sometimes; this one is far more than half a dB stronger and decodes never. So
the weight moves onto the first two: a convention in the reference-signal
construction that the synthetic round trip shares and cannot catch, or a
half-frame the search has backwards.

The same session is where the parity's own false-pass rate stopped being
theoretical. The funnel read "messages 1", which looked like a success and was
not: twelve attempts a call and three calls a block is one chance pass expected
in about nine thousand cell detections, and nine thousand is what it had. A
message is now believed only when a second one agrees about the bandwidth, the
acknowledgement channel and the antenna count -- the things a cell cannot
change between frames. The funnel counts parities and messages separately, so
the difference is on the screen rather than in this file.

## Where it has been narrowed to, 2026-09-02

A day of measurement against `testfiles/lte_b20_pci32.bin` and a live session
on the same cell. Two things are now established rather than suspected.

**The reference signals are exactly where the code looks.** The proof needs no
sequence at all: average the power on each of the six subcarrier phases of a
symbol, over ten slots. At symbol 4 of each slot, phases 2 and 5 stand +4.4 dB
and +2.2 dB above the mean while the rest sit 3 to 8 dB below --

    symbol 4:   -3.0   -8.5   +4.4   -2.3   -4.8   +2.2   dB

-- and cell 32 gives `pci % 6 = 2`, with the second port three phases on at 5.
So the cell identity, the subcarrier shift rule, the frame boundary and the
symbol timing are all right. (Symbol 0 shows no such pattern because the
control channels fill it; symbol 4 is in the data region, which an idle cell
leaves empty, so only the reference signals remain. That is why the earlier
measurements at symbol 0 were inconclusive.)

**And the sequence is wrong.** At those very subcarriers, no hypothesis
correlates. What has been excluded, each averaged over ten symbols so the
noise floor sits near 0.26 rather than the 0.9 that twelve samples alone
reach:

| Swept | Range | Best |
| --- | --- | --- |
| subcarrier shift | all 6 | noise |
| sequence index | 0 to 438 | noise |
| cyclic-prefix bit | both | noise |
| slot numbering | +0 and +10 | noise |
| Gold generator convention | 16, being every combination of seed order, register end, tap end and output end | noise |
| seed multiplier | every affine function a*slot+b, a to 15, b to 255 | noise |

Two metrics were used and agree: a coherent average of the channel estimates,
and a differential one comparing neighbouring estimates, which is immune to
channel tilt and timing ramps the way the secondary-sequence detector is. The
code's own hypothesis scores 0.21 to 0.34 against a floor of 0.26.

**Also ruled out: signal strength.** The live session locked PSS at 0.91 and
SSS at 0.93 with 5562 cells in 5637 blocks, far stronger than any capture, and
the broadcast never decoded. And **the frequency refinement is not the cause**:
reference-signal correlation is the same with the coarse offset as the refined
one (0.314 against 0.315), and sweeping the offset over +-3 kHz finds no peak
-- which also means the refinement is currently refining nothing, and the
parts-per-million figure the view shows is the coarse estimator's scatter.

## The pilots do lock -- four identities away

The search that finally caught something swept the cell identity and the
sequence index **together**, judged on ten symbols so the noise ceiling sat
near 0.43 rather than the 0.9 that twelve samples reach on their own. It found
a lock, and then the same lock in two more captures:

| capture | the sync signals say | the pilots' sequence matches | score |
| --- | --- | --- | --- |
| lte_b20_pci32.bin | cell 32 | cell **28** | 0.979 |
| earfcn 6300 | cell 160 | cell **156** | 0.818 |
| earfcn 6400 | cell 406 | cell **402** | 0.903 |

**Minus four, every time.** At sequence index 104 and slot offset 0 -- both
exactly what the code uses -- so the index derivation and the frame boundary
are confirmed correct, not merely unrefuted.

The positions and the sequence disagree about the identity, and both
disagreements are solid:

- The **positions** say the reported identity is right. Cell 32 predicts pilot
  phases 2 and 5 at symbol 4; those are the two that stand +4.4 dB and +2.2 dB
  above the mean, while cell 28's predicted phases 1 and 4 are the two
  *lowest*, at -8.5 dB and -4.8 dB. Cells 160 and 406 predict their observed
  phases exactly too.
- The **sequence** says the seed is built from four less.

Written as arithmetic, the seed the air carries is (2N - 7)(1024A + 1) where
the code produces (2N + 1)(1024A + 1). Nothing in the standard has that shape,
which is why this reads as a symptom rather than the fault itself.

## Also excluded since

- **The Gold generator.** Written a second time as the standard writes it --
  two arrays indexed by n rather than two shift registers -- and the two agree
  bit for bit across eight seeds including the ones actually used. It was the
  strongest suspect and it is clear.
- **The slot numbering.** Swept 0 to 19 jointly with the identity; offset 0
  wins, so the frame boundary from the half-frame decision is right.
- **The subcarrier window.** Slid by -2 to +2; every offset is either the noise
  floor or a relabelling of the same lock.
- **The Master Information Block under both stories.** Positions, pilot
  sequence and scrambling each independently set to the reported identity or
  four less, across three port counts and five frames: nothing decodes. Then
  the same again through the production path rather than a probe -- src/lte_dsp.c
  patched to seed the reference signals four lower and the ordinary
  lte_pbch_soft_bits and lte_mib_decode run over it -- with the same result.
  So whatever the -4 is, substituting it does not by itself repair the chain,
  and something downstream of the channel estimate is wrong as well.

## What is left

Whatever is wrong is not a parameter of the construction as this code models
it. The remaining candidates, in the order worth trying:

1. **Explain the four -- but not by doubting the identity.** That check is
   now done and the identity survives it. The minus-four cell needs an N_ID_2
   of 1, 0 and 0 in the three captures, and the primary sequence scores those
   roots at 0.316, 0.319 and 0.357 against a noise floor of about 0.35, while
   the reported roots score 0.796, 0.622 and 0.595. The primary sequence
   rejects the minus-four identity outright, three times, and the pilot
   positions back the reported one three times. So the identity and the
   positions are right and the **seed formula** is the fault: the air carries
   the sequence of (2N - 7)(1024A + 1) where the code makes (2N + 1)(1024A + 1).
   Nothing in the standard as this code models it has that shape, which means
   the model is wrong somewhere the code cannot see -- and the way to settle
   that is a published test vector for c_init, not another sweep.
2. **Read the sequence rather than searching for it.** Attempted, and the
   recovery is too noisy to convict: the differential points cluster at 0.5 to
   0.7 where four clean corners would be 1.0, so several of the twelve digits
   are wrong and a comparison at 3 of 11 proves nothing. Averaging the
   recovery over many slots, rather than reading each slot alone, would fix
   that -- the sequence differs per slot, but the *channel* does not, and it
   is the channel that is limiting the read.
3. **Check the seed against a published vector.** The generator agrees with
   itself in two spellings, which is exactly the assurance the conjugated
   primary sequence also had. The seed formula around it has never been
   checked against anything external at all.

## What would settle it

A stronger capture -- outdoors, or with a better antenna -- separates the last
cause from the first two in one measurement. If the reference signals lock at
higher signal and the message decodes, nothing is wrong with the code. If they
still do not, the fault is a convention, and the way to find it is the one that
worked before: a reference-free measurement of the received symbol's own
structure, rather than a search over what the code thinks it should be.
