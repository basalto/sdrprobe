# 01 — Find which stage loses band 8's broadcast channel

Status: resolved

The spec has the evidence and the two hypotheses nobody has tested. This is
about picking one and measuring it, not about writing a decoder.

## Where to start

`make probe-lte-chain` already walks the chain and prints every stage; what it
does not print is *why* the parity does not fit, because at that point there
is nothing left to print. Two things would say:

1. **The cyclic prefix decision and its margin.** The chain reports `normal`
   and reports it as a fact. It is a correlation with a runner-up, like every
   other decision here, and the runner-up is not shown. If extended scores
   nearly as well on this signal, that is the answer and it is one number away
   from being visible.
2. **Whether the four broadcast symbols are being sampled where they should
   be.** The frame boundary comes from the primary sequence, whose peak here
   is 0.583 against a runner-up of 0.479. A timing error of a few samples
   costs the broadcast channel and costs synchronisation almost nothing, so
   the two can disagree.

The honest first move is to make the chain print what it already knows and
looked at nothing new: the cyclic-prefix correlation both ways, and the
timing's peak against its neighbours.

## What must be checkable

- Whatever is added prints for the *working* capture too, so the two can be
  compared. `testfiles/lte_b20_pci28.bin` decodes 28 messages in 50 blocks and
  is the control: a number that looks alarming on band 8 means nothing until it
  has been read on a cell that works.
- If the cyclic prefix turns out to be the answer, the synthetic buffers in
  `tests/lte_dsp_test.c` can carry an extended-prefix case, which they do not
  today -- `build_buffer` passes 0 for `extended_cp` at every call site.

## What this must not become

A second decoder for a cell nobody can hear. If the answer turns out to be
that this cell is simply too weak for its broadcast channel at this antenna,
that is a finding and the ticket closes: `wontfix`, with the number that says
so. The point is to know which stage loses it, not to make it decode.

## Answer

Status: both hypotheses measured and both ruled out. The fault is not in
synchronisation, and it is not weakness. It tracks **N_ID_2 = 0**, which is the
one value of the three that no working cell here has ever exercised.

Measured 2026-09-06 on a fresh capture,
`captures/lte_earfcn3475_20260906-075233.bin` — 12 blocks, 12 with a cell, 0
with a message, the same shape as on air. `probe-lte-chain` now prints both
measurements for every block, on this capture and on the control.

### 1. The cyclic prefix — ruled out, and it had never been measured

The verdict was real but the comparison behind it was not. `lte_cell_search`
loops `for (k = 0; k < 2 && best_score < LTE_SSS_CONFIDENT; k++)`, so the
search stops at the first prefix clearing 0.72 — and this cell's secondary
sequence reads 0.83. **The extended hypothesis was never tried**, and the chain
printed `normal CP` as though it had been.

Now measured after the decision, where it cannot change it:

```
band 8  PCI 330   CP normal 0.82-0.84   extended 0.55-0.66
band 20 PCI 28    CP normal 0.74-0.83   extended 0.48-0.71   (decodes)
```

The same shape on both. The prefix is normal and the margin is comfortable.

### 2. The timing — ruled out, twice

The peak has no competitor: sidelobe 0.22-0.29 against peaks of 0.73-0.97,
where the control reads 0.15-0.36 against 0.60-0.87.

And directly, which needs no theory: sweeping the frame start across
`+-30` samples under all three port hypotheses, **no start fits**. The control
put through the same sweep decodes from **-9 to +8 samples**, an eighteen-sample
window. The timing is not merely right, it is nowhere near the edge on a cell
that works, and no timing at all rescues this one.

One number is still unexplained and is *not* a fault: the shift
`pss_refine_timing` applies is +26 here against +10 on the control, stable in
both. The offsets differ by only 14% (-31.8 against -28.0 kHz), so the offset
alone does not account for it, and a modular-inverse model of the Zadoff-Chu
root predicts 30 and 10 against the observed 10 and 26 — so that does not
account for it either. Given the sweep above it costs nothing; noted so nobody
chases it as a lead.

### 3. "Too weak" — not supported

Two measurements of the equalised broadcast-channel elements were built and
**neither separates the captures**:

- distance from the ideal QPSK points: 58-66% on the control, 49-64% here;
- fourth-power QPSK coherence: mean 0.198 (0.120-0.373) on the control, 0.172
  (0.020-0.377) here, twelve blocks each at one port.

Both are recorded, including the one discarded, because a number that cannot
tell the control from the fault is not evidence — and the first was nearly
reported as one. The elements here look like the elements that decode, so the
parity failing is something deterministic rather than noise.

### 4. What it does track

Three real cells, all measured this morning:

| cell            | N_ID_2 | PSS root | CRS shift | result                  |
|-----------------|--------|----------|-----------|-------------------------|
| PCI 28, band 20 | 1      | 29       | 4         | decodes                 |
| PCI 59, band 20 | 2      | 34       | 5         | 215 messages / 292      |
| PCI 330, band 8 | 0      | 25       | 0         | **0 messages / 219**    |

PCI 59 was found by `--lte-scan 20` and walked with `--lte-chain --earfcn 6300`.
It is a third identity, a third root and a third reference-signal shift, and it
decodes. The cell that fails is the only one with **N_ID_2 = 0**.

The spec ruled the reference-signal shift out because `tests/lte_dsp_test.c`
builds synthetic cells at shifts 0, 4 and 5 and decodes all of them. That
argument does not survive contact with this repository's own history: those
buffers are built by the same functions that read them, so they cannot check a
convention both sides share — the conjugated primary sequence, the scattered
GSM SCH layout, the pi/4-DQPSK phase table and the TETRA scrambler seed were
all green throughout. **Shift 0 has never been read off the air here.**

### 5. A fourth cell, and the band is exonerated

`--lte-scan 8` found a second cell on band 8: **PCI 190 at EARFCN 3625**
(942.5 MHz), N_ID_2 1, root 29, shift 4. It decodes **279 messages in 292
blocks**, the best of the four, at a tuning offset of -30.6 kHz against the
failing cell's -31.8 kHz.

| cell                 | band | N_ID_2 | root | shift | result               |
|----------------------|------|--------|------|-------|----------------------|
| PCI 28, EARFCN 6200  | 20   | 1      | 29   | 4     | decodes              |
| PCI 59, EARFCN 6300  | 20   | 2      | 34   | 5     | 215 messages / 292   |
| PCI 190, EARFCN 3625 | 8    | 1      | 29   | 4     | 279 messages / 292   |
| PCI 330, EARFCN 3475 | 8    | 0      | 25   | 0     | **0 messages / 219** |

So it is not the band, not the region of spectrum, not the antenna and not the
tuning error: a cell fifteen megahertz away, with an offset within 1.2 kHz of
this one's, decodes better than anything else here.

### 6. But the shift arithmetic reads correctly

`lte_crs_subcarriers()` computes `shift = pci % 6` and `first = (v + shift) % 6`
with `v` from `crs_shift()`, which matches 36.211 table 6.10.1.2-1: port 0 has
v = 0 at symbol 0 and 3 at symbol 4, port 1 the reverse, ports 2 and 3 at
symbol 1 with v = 3*(n_s mod 2) and 3 + 3*(n_s mod 2). Nothing special-cases a
shift of zero -- at shift 0, `first` is simply `v`.

That is an inspection against the standard rather than a round trip, which is
the only kind of check that can catch a shared convention, and it **weakens**
the shift hypothesis. It does not clear the rest of the N_ID_2 0 path.

### 7. The carrier does transmit a broadcast channel

Candidate 2 below is refuted. A PBCH carries the same coded block over 40 ms,
one quarter per radio frame, and the quarter is picked by the two low bits of
the frame number -- so subframe 0 of frame n and of frame n+4 carry the same
coded bits under the same scrambling, and frames one to three apart carry
different ones. Nothing else in LTE has a 40 ms period. That is a test of
whether a broadcast channel is *present* that needs no decoder, no
descrambling and no knowledge of the identity.

Correlating the soft bits at lags of one to five frames, averaged over every
block with a cell:

```
                              +1      +2      +3      +4      +5
band 8  PCI 330  subframe 0  -0.015  -0.015  +0.027  +0.145  +0.012
band 20 PCI 28   subframe 0  -0.003  -0.033  +0.008  +0.160  -0.016   (decodes)

band 8  PCI 330  subframe 7  +0.910  +0.902  +0.884  +0.865  +0.835
band 20 PCI 28   subframe 7  +0.275  +0.280  +0.271  +0.232  +0.255
```

Subframe 0 shows a peak at four frames and flat, near-zero near lags, on both
captures, and **band 8's is the same size as the one on the cell that
decodes**. So the carrier is transmitting a broadcast channel with the right
40 ms structure. It is not a repeater and not a sync-only transmitter, and the
message is there to be read.

Subframe 7 is the control and it is the part worth reading carefully. It has no
broadcast channel, and it correlates far *more* strongly than subframe 0 does
-- 0.84 to 0.91 on this cell -- because a lightly loaded subframe is nearly
identical frame to frame; the reference signals alone repeat. But it has no
maximum at four frames: on both captures +4 sits at or below its neighbours.
**The test is the peak and never the level**, and a control that correlates
three times higher than the signal while still showing no peak is what
establishes that.

(Subframe 5 was tried first and rejected: it gave an intermediate, partly
structured profile, and a control has to be unambiguous to be worth anything.)

### 8. The identity path, checked against other implementations

The remaining candidate was "something else in the N_ID_2 0 path". It is
largely ruled out too, and this section was done the way it should have been
done from the start -- against srsRAN and against measurement, not against
recollection of 36.211.

**The descrambling key is not wrong.** `lte_mib_decode()` takes the identity
separately from extraction, so the elements can be read once and every key
tried against them. All 504 identities, three port counts, four quarters and
the CRC masks -- roughly eighteen thousand attempts -- and **none fits**. A
secondary sequence that returns a confident wrong identity was a live
possibility (`lte_cell_search` sweeps integer offsets for exactly that reason);
it is not what is happening. The elements themselves are not a message.

**The reference-signal conventions are right, and one was settled by
experiment rather than by reading.** `pbch_subcarrier()` maps the 72 broadcast
subcarriers to 36 either side of DC, *skipping* DC, while the reference
exclusion two hundred lines away tests `index % 3`. Those two agree below DC
and differ by one residue class above it, which looked like a real bug. Four
builds settled it:

```
                              control PCI 28 (shift 4)   band 8 PCI 330 (shift 0)
skip DC, exclude on index      12 / 12 messages           0 / 12
skip DC, exclude on physical   11 / 12                    0 / 12
keep DC, exclude on index       0 / 12                    0 / 12
keep DC, exclude on physical    0 / 12                    0 / 12
```

So **skipping DC is required** -- not skipping it destroys the cell that works
-- and the index-space exclusion is at least as good as the physical one. The
convention is validated on air. Neither variant rescues band 8.

srsRAN corroborates the shape: `srsran_pbch_cp()` calls
`prb_cp_ref(&input, &output, cell.id % 3, 4, 4 * 6, put)`, and `prb_cp_ref`
computes `ref_interval = (SRSRAN_NRE / nof_refs) - 1` = 2, copying `offset`
elements then repeatedly skipping one and copying two. That is references every
third subcarrier starting at `cell.id % 3`, counted across the block -- the
same rule this file uses.

**The four-port combining is not it either.** All three cells that decode here
report two ports, so the four-port path had never been exercised on air, and
its element-to-port pairing is a convention a synthetic round trip cannot
check. Three pairings were tried on the failing capture -- pair 0 on ports
(0,1), (0,2) and (0,3) with the complements -- and none produces a message.

## What is now known, and what is left

A signal with the broadcast channel's 40 ms period is present in the right
place, and the 480 soft bits taken from it are not a message under **any**
identity, port count, frame start within a sample, subframe within a frame,
cyclic prefix, or reference-signal convention tried. Everything upstream of
the elements is measured and sound; the elements themselves are wrong, and
nothing tested so far explains why.

Note what section 7 does and does not establish. A 40 ms repetition proves a
40 ms-periodic transmission is there; it does **not** prove the extraction
picks the right resource elements, because a wrong extraction of a periodic
signal repeats just as faithfully. Presence was the question it was asked, and
presence is all it answers.

The channel estimate has now been looked at -- see section 9 -- and it is
sound. What it revealed instead is that this is a four-antenna-port cell.

### 9. It is a four-antenna-port cell, and that is the only path never proven

The channel estimate was the last thing never looked at, and looking at it
answered the question the whole ticket has been circling.

**The references are read correctly, and there are four of them.** A reference
symbol has unit magnitude, so dividing the received value by the expected one
preserves the magnitude whatever sequence is used -- which is why
`trace->channel_db` could never answer this and why it has to be phase. Taken
against the right sequence, neighbouring per-reference estimates differ by one
consistent rotation, the channel's delay; against the wrong one they differ
randomly. The coherence of that difference, averaged over every block:

```
                        port 0   port 1   port 2   port 3
band 20 PCI 28 (2 port)  0.729    0.901    0.411    0.326
band 8  PCI 330          0.805    0.746    0.867    0.918
```

Chance is about 0.30 -- there are twelve references per port per symbol, so
eleven differences. The control sits **at chance on ports 2 and 3**, exactly as
a two-port cell must. Band 8 is coherent on all four.

So **PCI 330 transmits on four antenna ports**, it is the only such cell here,
and its reference signals are being read correctly at shift 0. The channel
estimate is sound. Every cell that decodes -- 28, 59, 190 -- reports two ports.

**Which makes the four-port combining the only path in the chain never proven
against a real signal**, and its element-to-port pairing, conjugation and
output ordering are conventions a synthetic round trip cannot check, because
`build_buffer(101, 4, ...)` places the elements with the same function that
reads them.

### 10. Two measurements that grade rather than judge

`probe-lte-chain` now reports both, for every capture:

**Port hypothesis by 40 ms repetition.** Reading the elements the way the cell
transmits them makes the same coded block come back four frames later. It
picks correctly: the control peaks at the 2-port hypothesis (0.175 against
0.160 and 0.150) and band 8 at the 4-port one (0.162 against 0.145 and 0.141).
But note its limit -- **it tests determinism, not correctness.** A systematically
wrong extraction of a periodic signal repeats just as faithfully, so this
cannot distinguish "read right" from "read wrong the same way every time",
and band 8's peak is not evidence that its four-port extraction is correct.

**How close the decode gets.** "No parity fits" is one bit of information;
counting how many of the sixteen parity bits disagree, minimised over four
quarters and three masks, is a measurement:

```
                    best   mean        (a block carrying nothing: mean 4.80)
band 20 PCI 28
  1-port combining     0    0.0        <- decodes every block
  2-port combining     3    3.9
  4-port combining     3    4.7
band 8 PCI 330
  1-port combining     3    4.5
  2-port combining     4    5.1
  4-port combining     1    4.2
```

The null was simulated rather than assumed: the minimum over twelve draws from
Binomial(16, 0.5) has mean 4.80, `P(best <= 1)` is 0.0030 per block and 0.035
over twelve. **Band 8's soft bits are indistinguishable from carrying no
information** -- 4.2 against a null of 4.80, in the same range as the control's
two *wrong* hypotheses. The control's 1-port reads 0 in every block, which the
null gives with probability 0.00016.

A correction worth keeping: the single `best 1` looked like a near miss and was
reported as one for about a minute. Across blocks it is noise, and with six
hypothesis-capture pairs examined the chance of seeing at least one somewhere
is about one in five. One block's outlier is not a measurement.

### 11. Fixed: the Alamouti combiner recovered -conj(x1)

`alamouti()` computed

```c
x1 = conj(h1) * r0 - h0 * conj(r1);
```

Against 36.211 section 6.3.4.3 the space-frequency block code puts `x0` and
`-x1*` on the first element and `x1` and `x0*` on the second, so
`r0 = h0*x0 - h1*x1*` and `r1 = h0*x1 + h1*x0*`, and solving gives

```c
x1 = conj(h0) * r1 - h1 * conj(r0);
```

The two differ by more than arrangement: the expression that was there
evaluates to **-conj(x1)**, which inverts the real part and so flips one bit
of every second symbol -- 120 of the 480. srsRAN's
`srsran_predecoding_diversity_gen_` agrees with the standard
(`x1 += -h2 * conjf(r0) + conjf(h0) * r1`), and its four-port branch also
confirms the pairing this file already had: ports (0,2) on elements 4i and
4i+1, ports (1,3) on 4i+2 and 4i+3.

**Why it survived.** Two independent reasons, and it needed both. The
synthetic encoder in `tests/lte_dsp_test.c` transposed the same two terms, so
the round trip agreed perfectly. And the port hypotheses are tried 1, 2, 4 and
stop at the first that fits, so every two-port cell here decoded with
*single-port* combining and never reached the code at all. The transmit
diversity path had never once run to a successful decode.

The evidence was already on the page and misread. Section 10's table shows the
control's two-port combining at a mean of 3.9 parity bits wrong against a null
of 4.80 -- a two-port cell decoded with correct two-port combining must score
0, and it was recorded as "a wrong hypothesis giving noise, as expected".

**What it fixes.**

```
                      before            after
band 8  PCI 330       0 / 219 blocks    264 / 292 blocks   (on air)
band 20 PCI 28        158 / 175         158 / 175          (unchanged)

parity bits wrong, mean, on the captures
band 20 PCI 28  2-port combining   3.9  ->  0.0
band 8  PCI 330 4-port combining   4.2  ->  0.0
```

PCI 330 reads **25 resource blocks, a 4.5 MHz carrier, PHICH normal 1, and
four antenna ports** -- and the four is corroborated rather than merely
consistent: `crs_coherence` in the probe reads 0.867 and 0.918 on ports 2 and
3 from the reference phases alone, before any decoding, sharing no code with
the parity mask the count comes out of.

**The check that pins it.** `testfiles/lte_b8_pci330_4port.bin`, three
megabytes of the same recording, asserted to read cell 330, 25 blocks and four
ports. Reverting the one line fails it at 0 messages in 12 blocks while
`lte_b20_pci28.bin` stays green -- which is the whole argument for keeping it,
and the same argument that gives the GSM set three captures for three BCCs.
Three megabytes rather than two because the frame-number claim wants more than
eight consecutive steps and two gave seven.

## Next

Status stays `needs-triage`; this narrows it, it does not close it.

Shift and N_ID_2 are confounded in the evidence: `PCI mod 6 == 0` implies
`PCI mod 3 == 0`, so every shift-0 cell also has N_ID_2 0. They come apart at
**`PCI mod 6 == 3`** -- N_ID_2 0 with shift 3. Neither band has such a cell
among the four found, so that discriminator is not available at this site and
this may not be resolvable here.

Resolved by section 11. The four-port transmit-diversity path was indeed the
one that had never decoded a real signal, and it was wrong -- but so was the
two-port path, which nothing had noticed because single-port combining is
tried first and succeeds on a strong two-port cell.

What is left is not this ticket. `probe-lte-chain` gained four diagnostics on
the way here -- both cyclic prefixes, the timing sidelobe, the 40 ms
repetition, the reference coherence and the parity distance -- and none of
them is on a screen. The LTE view shows a cell and a message and nothing about
how well either was read.
## Comments

The chain gained two things it did not have and one it should not keep:
`cell.cp_score[]` / `cell.cp_measured[]` (both prefixes, always measured, one
extra secondary-sequence read at an offset already known), and
`cell.timing_shift` / `cell.timing_sidelobe`. `check-lte-dsp` asserts both
prefixes are measured on every synthetic cell and that the reported verdict
matches the better score; reverting the extra read fails those four checks,
which is how they were shown not to be vacuous.
