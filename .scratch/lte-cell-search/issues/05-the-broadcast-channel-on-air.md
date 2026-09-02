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

## What would settle it

A stronger capture -- outdoors, or with a better antenna -- separates the last
cause from the first two in one measurement. If the reference signals lock at
higher signal and the message decodes, nothing is wrong with the code. If they
still do not, the fault is a convention, and the way to find it is the one that
worked before: a reference-free measurement of the received symbol's own
structure, rather than a search over what the code thinks it should be.
