# 06 — Diagnose the frame number: the bits are right, the number is not

Status: resolved
Blocked by: —

Ticket 05 established that the SCH decode is not ISI-limited and that the MLSE
channel estimate changes nothing. This ticket carries the diagnosis forward and
records what has been ruled out, so the next attempt does not re-tread it.

## Burst selection is NOT the fault

`best_pos` was the leading suspect: a 65.5 ms block spans ~1.4 SCH periods, so
most blocks hold two candidate bursts and the search takes the strongest
training correlation. Enumerating *every* candidate in every block (all local
correlation maxima above 56/63, de-duplicated) gives 43 bursts across 31 blocks:

- **Every one is a genuine SCH burst** — all 43 match the training sequence
  63/63.
- **Their positions form a flawless SCH cycle.** Consecutive gaps, in frames,
  across the whole capture: `10 10 10 10 11` repeating, 42 gaps, no exceptions.
  That is exactly the spacing of T3 ∈ {1,11,21,31,41} within the 51-multiframe.
- **Picking a different candidate never helps.** Allowing *any* candidate per
  block, the best single frame-number origin still fits only 3 of 31 blocks.

So the burst finder is exonerated, and the timeline it produces is trustworthy
enough to serve as ground truth.

## Ground truth: the true T3 of every burst is now known

The 11-frame gap is the 41 → 1 multiframe wrap, so the burst following it is
T3=1 and the cycle is pinned for all 43 bursts. The resulting distribution over
{1,11,21,31,41} is 9/9/9/8/8 — uniform, as it must be. **This is real-capture
ground truth and is exactly what ticket 03 needs.**

## The received bits are a valid codeword, carrying the wrong number

21 of the 43 bursts reconstruct to a burst whose 78 coded bits **exactly** equal
the encoding of some SCH message (a 78-bit exact hit from a ~780-codeword
candidate set — not chance). Against the timeline truth:

- The encoded T3 is correct on only **3 of 21**.
- The encoded T3 only ever takes the values **{1, 11, 41}** — never 21 or 31 —
  while the truth is uniform over all five.

## Ruled out

- **ISI / branch metric** (ticket 05): MLSE and the correlation metric decode
  every block identically; both recover every field on synthetic ISI+AWGN.
- **Burst selection**: above.
- **Field layout**: all 24 orderings of BSIC/T1/T2/T3' × 16 MSB/LSB-first
  combinations were scored against the timeline truth on the 21 exact bursts.
  None beats the standard reading, and the standard reading gets 3/21.
- **Codeword-complement ambiguity**: the thought was that the differential
  demod's global polarity choice might map a codeword to another valid
  codeword, which would explain "always valid, usually wrong". It cannot —
  0 of 1500 complemented SCH codewords parse as valid messages.
- **Capture integrity**: the burst's sub-frame position drifts 0.014 frames
  over 428, matching the receiver's ~33 ppm clock error. The file is
  contiguous; within-block burst positions advance by exactly one SCH period.

## What is left

The burst is located correctly, its coded bits reconstruct to an exact valid
codeword, the code and the field layout are standard — and the frame number
still contradicts the timeline. Something maps one valid codeword onto another.
The two untested suspects:

1. **The coded-bit reconstruction.** Data1 is rebuilt backwards from
   `sch_training[0]` and data2 forwards from `sch_training[63]`
   (`gsm_sch_decode`, and the same shape in the trellis's position map). An
   index or anchor slip there would permute the message while leaving it a
   valid codeword, which is precisely the observed signature.
2. ~~**`gsm_sch_encode` / `sch_parity` vs the network.**~~ **Ruled out — see
   below.**

## The coding chain is standard (suspect 2 ruled out)

Checked against the 3GPP definitions with independent from-scratch
implementations, over 20 000 random information words:

- **Parity.** `sch_parity`'s LFSR was compared with literal GF(2) long division
  by g(D) = D^10 + D^8 + D^6 + D^5 + D^4 + D^2 + 1 (TS 45.003 §4.7.1):
  **0 mismatches**. The defining property also holds — the 35-bit sent word
  leaves a remainder of all ones on every trial, which is what GSM's inverted
  SCH parity requires.
- **Convolutional code.** `sch_conv_encode` vs the literal generators
  c(2k) = u(k)+u(k−3)+u(k−4), c(2k+1) = u(k)+u(k−1)+u(k−3)+u(k−4)
  (TS 45.003 §4.7.2): **0 mismatches**.
- **Viterbi.** encode → `sch_viterbi` round trip: **0 mismatches**.
- **Training sequence.** `sch_training` is byte-identical to the SCH extended
  training sequence published in TS 45.002 table 5.2.5:
  `1011100101100010000001000000111100101101010001010111011000011011`.

So the encoder, parity, Viterbi and training sequence are correct. This is now
proven rather than assumed, and the round-trip self-check is no longer the only
thing standing behind them.

## A minimal reproducer that assumes nothing

The sharpest form of the bug needs no timeline, no ground truth, and no
external reference — only that two bursts in one block are a known number of
frames apart. Two independent blocks show it identically:

```
 blk    af  gap        FN    T1   T2   T3
  20   285    0   2507864  1891    8   41
  20   295   10   2507630  1891    8   11     dFN=-234, dT2=+0  (must be +10)
  22   316    0   2506640  1890    6   41
  22   326   10   2506406  1890    6   11     dFN=-234, dT2=+0  (must be +10)
  25   356    0   2506447  1890   21    1
  25   367   11   2506436  1890   10   41     dFN=-11,  dT2=-11 (must be +11)
```

Both bursts of each pair reconstruct to an **exact** valid codeword. Two SCH
bursts 10 frames apart must have T2 differing by 10, because T2 = FN mod 26.
They come out equal, and dFN is the same −234 in both blocks — deterministic
and reproducible, not noise.

Note what this rules in: the two bursts of a pair share a block, a carrier
estimate, a timing search and a channel estimate, so no per-block artefact can
explain the difference. Whatever is wrong is inside the per-burst path.

Start from this reproducer — it is far cheaper to iterate on than the full
capture sweep.

## Harness

The investigation harnesses (candidate enumeration with timeline-derived true
T3, per-burst codeword fitting, layout search, complement test) are throwaway
scratch programs, not committed. Worth promoting the candidate enumerator to
`scripts/` with a Makefile target if this ticket is picked up — it is what
produces the ground truth above.


## Working the pair: the anomaly is in data1, and it is not noise

Driving the block-20 / block-22 pairs directly narrows this a long way.

**The two decoded words of a 10-frame pair differ only in T3'.** The XOR of the
two 25-bit information words is byte-identical in both blocks:

```
0000000000000000000000101      (bits 22 and 24 -- the T3' field)
 BBBBBB TTTTTTTTTTT 22222 333
```

BSIC, T1 and *all five T2 bits* come out identical. Since u[0..21] agree, the
entire first data field agrees — confirmed directly:

```
            data1 (39 bits)   data2 (39 bits)   raw magnitudes
blk 20      39/39 identical   20/39 identical   9.3% mean diff, 27% max
blk 22      39/39 identical   20/39 identical   7.3% mean diff, 24% max
blk 25*     differs           differs           (* 11-frame gap)
```

Two things follow, and they point in opposite directions from where we were
looking:

1. **The capture is not repeating.** Raw sample magnitudes differ by ~9% between
   the paired bursts, so these are two genuinely different transmissions, not
   the same burst seen twice.
2. **data1 cannot legitimately be identical.** data1 carries e(0..38), i.e.
   u(0..19) = BSIC + T1 + the top three bits of T2. Over 10 frames T2 advances
   by 10, so floor(T2/4) must change — it cannot be invariant under +10. Yet
   data1 is bit-identical, in both pairs. Over the 11-frame gap it does differ.

And data1's behaviour across all 43 bursts looks *correct*: 26 distinct
patterns, sharing a constant 30-bit prefix (exactly the span fed by the
constant BSIC and T1 high bits) and varying only in the last 9 (fed by T1's low
bits and T2's top bits). So the data1 reconstruction is not obviously broken —
which makes the identical-at-10-frames result harder to dismiss.

Also ruled out along the way:

- **Index misalignment in data2.** Sliding the received differential against the
  predicted true differential over offsets −4..+4: training peaks sharply at
  offset 0 (63/63) and data1 at offset 0 (34/38), but data2 is flat and
  near-random at *every* offset (15–26 of 38). data2 is not simply shifted.
- **A fixed additive error pattern.** If the demodulator XORed in a constant
  pattern that is itself a codeword, every decode would land on a valid but
  wrong message. The pair test kills this without needing FN0: the observed
  pair XOR matches no XOR of two true words 10 frames apart (0 candidates for
  both 10-frame pairs).
- **Errors concentrated at weak symbols.** Under the "B is correct" hypothesis
  A's three data1 divergences sit at soft magnitudes 0.38/0.51/0.79 of the mean
  (plausible noise), but its data2 divergences sit mostly at 1.2-1.7x the mean —
  strong symbols decoding wrongly, which noise does not explain.

### Where to go next

The evidence now says the transmitted SCH content does not advance between two
bursts 10 frames apart, while the receiver front end is demonstrably healthy.
Before any further decoder work, establish what this capture actually contains:

**Confirmed by the author: it is a live recording, made with the Record
button.** So the replay/loop explanation is out and the anomaly is real.

Two further results narrow it:

- **The repetition is in the modulated bits, not in the recorded bytes.**
  Correlating the raw I/Q against itself at the 10-frame lag gives a noisy
  0.1-0.85 across the burst (carrier drift over 46 ms destroys raw complex
  correlation), so the capture does not literally repeat samples. But the
  *differential* symbols, which cancel carrier phase, match at **r = 0.975**
  with 39/39 identical signs over data1, against 23/39 over data2. The two
  bursts genuinely carry the same data1 modulating bits.
- **The capture itself is contiguous.** All 42 SCH gaps are exactly 10 or 11
  frames in the regular multiframe pattern; a dropped block would leave a ~24
  frame gap and there is none. (The Record path *can* drop blocks though — see
  `.scratch/capture-integrity/issues/01-record-drops-blocks.md`. It did not
  here, but it makes any future capture's integrity a coin flip, so fix that
  before recording a second fixture.)

So: two SCH bursts, one contiguous block, 10 frames apart, carrying identical
data1 bits — which cannot happen, because data1 carries T2's top three bits and
floor(T2/4) cannot survive a +10 change.

Next step: fix the Record path, take a second capture, and see whether the
identical-data1 signature reproduces. If it does, it is in the receive or
demodulation path and the pair reproducer above localises it to 39 symbols. If
it does not, something specific to this fixture is at fault after all.


## Second capture, and a correction

Took an independent capture with the dongle: ARFCN 73 (949.6 MHz, tuned 400 kHz
low), 2 s at 2 MS/s, max gain, via `rtl_sdr` rather than the app's Record
button — deliberately, so sdrprobe's own write path is not a variable. Saved as
`captures/gsm_arfcn73_bsic49_20260830.bin` (gitignored). Different cell:
**BSIC 49**, where the old fixture is BSIC 45.

Results on the new capture:

- **BSIC decodes reliably**: 10/10 blocks agree on BSIC 49.
- **T1 is consistent: 1616..1617.** Two adjacent values over a 2 s capture is
  exactly right, since T1 advances once per 1326 frames (~6.1 s). The old
  fixture gives 1634..1891 on the same code, so the old fixture is the worse
  signal, not the better one.
- **The frame number is still wrong.** Decoded FNs span 2142817..2145418 =
  2601 frames ~ 12 s across a 2 s capture, so T2/T3 remain unreliable.

### Correction: the "identical data1" signature was over-claimed

The previous section reported that two bursts 10 frames apart have
bit-identical data1, "reproducible" across blocks 20 and 22, and called it
impossible. **That was wrong on two counts.**

First, the baseline was never computed. data1 carries u(0..19) = BSIC + T1 +
T2's top three bits. BSIC and T1 do not change over 10 frames, so roughly the
first 30 of the 39 coded bits are *supposed* to be identical. The expected
figure is ~30/39, not something low. 39/39 only means the ~9 varying bits also
coincided.

Second, only the two pairs that hit 39/39 were examined. Across all twelve
same-block pairs in the old fixture the figure is:

```
5, 23, 35, 31, 28, 5, 21, 39, 39, 35, 38, 22   (of 39)
```

and in the new capture, six pairs:

```
26, 18, 7, 38, 31, 35   (of 39)
```

A broad noisy distribution in both, with 2 of 12 landing on 39 in one and 0 of
6 in the other. That is a tail, not a signature. The conclusion drawn from it —
that something makes data1 fail to change when it must — is not supported, and
neither is the "minimal reproducer" framing of the block-20/22 pair in the
section above. Treat both as withdrawn.

### Where this actually leaves the problem

The solid findings are unchanged: burst location is exact, the coding chain is
standard, the MLSE metric changes nothing, and BSIC decodes reliably on two
independent captures. What is now clearer is the shape of the residual failure:
the fields that survive are the ones that are constant or near-constant across
bursts (BSIC always; T1 on the better capture), and the fields that fail are
the ones that change every burst (T2, T3). That is the ordinary signature of a
**high residual bit-error rate in the data fields**, not of a structural bug —
constant fields are effectively protected by repetition across bursts, and the
tracker's T1 vote exploits exactly that.

If that reading is right, the next useful measurement is a raw bit-error rate
on the data fields against a trusted reference, not another structural hunt.
The honest position is that no structural explanation has survived contact with
evidence, and the remaining candidate is simply that these captures are not
clean enough for single-burst frame-number decoding — which is what the
multi-burst tracker was added to paper over in the first place.


## Raw bit error rate on the data fields

Measured on the **differential decisions**, not the reconstructed channel bits,
so differential error propagation cannot inflate the count. Three references
with different bias:

- *training* — exact truth, but the burst position is chosen to maximise this
  match, so it is biased low.
- *tails* — burst positions 1, 2, 146, 147 are tail-to-tail transitions, known
  to be 0, and never looked at by the position search. **Unbiased**, 4 bits per
  burst.
- *data1 / data2* — versus the nearest valid codeword (re-encoding whatever the
  decoder settled on). A **lower bound**: the Viterbi picks the nearest
  codeword, so the true distance can only be larger.

```
testfiles/gsm_arfcn_69.bin   (31 blocks, 31 bursts, 25 parity-valid)
  training      0/1953  =  0.00%
  tails         0/124   =  0.00%     <- unbiased
  data1         1/950   =  0.11%
  data2         2/950   =  0.21%

captures/gsm_arfcn73_bsic49_20260830.bin   (30 blocks, 16 bursts, 10 valid)
  training     12/1008  =  1.19%
  tails         1/64    =  1.56%     <- unbiased
  data1        24/380   =  6.32%
  data2        57/380   = 15.00%
```

### This refutes the high-BER hypothesis for the good capture

The previous section guessed that the frame number fails because the data
fields carry a high residual error rate. On `gsm_arfcn_69.bin` they do not:
**zero** unbiased tail errors in 124 bits, and 3 errors in 1900 data bits
against the decoder's own codeword. The demodulation of that capture is
essentially perfect.

So the decoded messages are, to ~0.1%, exactly what those bursts carry. The
bursts really do encode the frame numbers we read out of them — and those
numbers do not follow a burst timeline that is itself exact. Noise is not the
explanation. That guess is withdrawn like the one before it.

Caveat worth stating: the data-field figures cover only the blocks that
produced a parity-valid decode (25 of 31, and 10 of 30), so they carry
survivorship bias — bursts too corrupt to decode contribute nothing.

### The new capture is a different, simpler story

ARFCN 73 is noise-limited: 1.2-1.6% raw BER, only 16 bursts located in 30
blocks, 10 decodes. Its data2 lower bound of 15% against a ~1.5% unbiased
channel BER mostly reflects the Viterbi converging on wrong codewords, which is
expected at that SNR. Nothing structural can be concluded from it; it is simply
a weaker signal. Use `gsm_arfcn_69.bin` for any further work.

### What is left, stated narrowly

Every mechanism proposed so far has been measured and eliminated: ISI, burst
selection, the coding chain, the field layout, codeword complement, a fixed
additive error, capture splicing, and now noise. On the good capture the chain
from antenna to 25 information bits is demonstrably clean.

The one assumption never tested is that **all the detected bursts belong to a
single, continuously-numbered frame counter**. Everything in this ticket
measures consistency *against* that assumption; nothing has verified it. The
cheap next test is to ask whether the decoded frame numbers, while fitting no
single timeline, split into two or more internally consistent sub-sequences —
which is what co-channel cells sharing a BSIC, a repeater, or a DAS would look
like. If they do not split cleanly either, the assumption survives and the
contradiction is genuinely unexplained.


## Found: the frame-number bits are not where `sch_parse` reads them

The sub-sequence test came back negative — 18 distinct `FN - af` offsets across
21 bursts, largest group 2, only 3 of 210 pairs sharing a timeline. Not two
interleaved sources. But decomposing *which field* fails pointed somewhere
better.

Pairwise, anchor-free, on the 21 exact-codeword bursts:

```
T1   pairs differing by 0 or 1        210/210   (weak: constant top bits satisfy it)
T2   (T2_i-T2_j) == (af_i-af_j) mod 26  12/210   chance is ~8/210
T3   matching the timeline-derived T3    3/21    chance is ~4/21
```

So BSIC and T1's high bits are fine and everything that *varies* is at chance.
Only 8 of the 25 information bits vary at all across the capture:
`{16,17,18,19,20,21,22,24}` — note d[23] never changes, which is why decoded T3
never took the values 21 or 31.

Searching those 8 positions for an assignment reproducing the timeline-derived
T3' gives **exactly one**, and it survives a holdout split (fitted on 10
bursts, verified on the other 11):

```
T3'  (MSB, mid, LSB) = d[17], d[16], d[24]
T2   (MSB .. LSB)    = d[22], d[21], d[20], d[19], d[18]
```

T2 falls out of the five bits left over, at 210/210 pairs against ~8/210 for
chance. The truth assignment is pinned too: of the five possible rotations of
the T3 cycle, only the correct one admits any perfect fit (1, 0, 0, 0, 0).

End to end, reading T2 and T3' from those positions and checking that FN
advances exactly with the burst timeline:

```
                                   current layout   empirical layout
old fixture (ARFCN 69, 25 decodes)      3/300           300/300
new capture (ARFCN 73, 10 decodes)      1/45             21/45
```

The new capture is an independent holdout — different cell, different day, and
never used to derive the mapping. It is noise-limited, so 21/45 rather than
45/45 is expected; it is still 21x the current layout and far above the ~0.03/45
that chance would give.

### Do NOT just patch these positions into `sch_parse`

The mapping is not a field reordering — d[16] and d[17] sit inside what the
code calls T1, and d[24] is separated from them. Nothing in 3GPP describes a
layout like that, and the coding chain was already verified standard, so the
likely cause is upstream: **the 25 information bits are reaching `sch_parse` in
a permuted order**, from the coded-bit-to-burst position map, the trellis's
position map, or the traceback. The empirical mapping is the fingerprint of
that permutation, not a layout to hardcode.

This also means **BSIC has never been independently verified**. It is constant
across a capture, so a permutation would yield a constant but wrong value. The
45 and 49 reported here may not be the real BSICs.

### Next

Find the permutation, do not paper over it. The test is cheap now: apply a
candidate transformation to the received coded bits (swap the data1/data2
halves, reverse one or both, reverse `u[]` after traceback, shift the position
map by one) and check whether the standard field positions then reproduce the
timeline. The right transformation should take the 300/300 result above and
leave the standard layout reading correctly, with BSIC as a by-product.


## Permutation hunt: it is not in the bit handling

Tested nine transformations of the received 78 coded bits — swap the data1 and
data2 halves, reverse all, reverse either or both halves, swap the two bits
within each coded pair, and combinations — by Viterbi-decoding each and
checking the 10-bit parity:

```
transformation          parity ok   timeline pairs
identity                   23/43        4/253
swap halves                 0/43            -
reverse all                 0/43            -
reverse each half           0/43            -
reverse data1 only          0/43            -
reverse data2 only          0/43            -
swap within pairs           0/43            -
reverse all + pairs         0/43            -
swap halves + rev each      0/43            -
```

**Only the identity yields valid codewords.** Any rearrangement destroys the
parity, which is what the code's own structure guarantees. So the coded bits
are read from the burst correctly, the Viterbi recovers the 39 uncoded bits as
transmitted, and the parity — computed over the information bits in exactly the
order `sch_parse` uses — checks out.

That closes the question the previous section left open: **there is no upstream
permutation.** The information bits arrive in the transmitted order. The fault
is entirely in how `sch_parse` slices them into fields.

### What the mapping looks like

Written as "field bit k lives at information-word position p":

```
T2  bit0->d[18]  bit1->d[19]  bit2->d[20]  bit3->d[21]  bit4->d[22]
T3' bit1->d[16]  bit2->d[17]
      => seven of the eight varying bits form one contiguous LSB-first run,
         d[16..22], with T3' sitting immediately below T2
T3' bit0->d[24]  <- the single bit that breaks the pattern
```

A fully contiguous LSB-first field would put T3' bit 0 at d[15], but d[15] is
constant across the capture and T3' bit 0 demonstrably varies, so it cannot
live there. The displaced bit is real, not a fitting artifact.

`sch_parse` currently reads both fields **MSB-first** from d[17..21] and
d[22..24]. The measured layout is LSB-first over a run starting two positions
earlier, plus that one displaced bit.

### Recommendation

The evidence that `sch_parse` is wrong is strong: 300/300 versus 3/300 on the
fixture and 21/45 versus 1/45 on an independent capture. The evidence for
*this exact mapping* being the general layout is good but not conclusive — two
captures, one of them noisy, and the displaced d[24] does not look like
anything a specification would write.

So: **verify the bit numbering against 3GPP TS 44.018 §10.5.2.1 before changing
`sch_parse`.** The spec text was not available here and reconstructing a layout
from two captures is exactly the kind of inference that has already been wrong
twice in this ticket. The timeline test built here is the acceptance criterion
for whatever layout the spec gives: it should reproduce 300/300 on
`gsm_arfcn_69.bin`.

One consequence to check at the same time: BSIC comes from d[0..5], which is
constant within a capture, so its correctness has never been tested. If the
field boundaries move, BSIC moves with them, and the reported 45 and 49 may
both be wrong.


## Resolved: the spec layout, looked up and applied

The empirically-derived mapping is exactly what 3GPP TS 44.018 10.5.2.1
specifies. Two independent reference implementations agree with it and with
each other, bit for bit — gr-gsm `lib/decoding/sch.c` and E3V3A/gsm-parser
`sch.c`:

```c
ncc = (d[7]<<2)|(d[6]<<1)|d[5];
bcc = (d[4]<<2)|(d[3]<<1)|d[2];
t1  = (d[1]<<10)|(d[0]<<9)|(d[15]<<8)|(d[14]<<7)|(d[13]<<6)|(d[12]<<5)|
      (d[11]<<4)|(d[10]<<3)|(d[9]<<2)|(d[8]<<1)|d[23];
t2  = (d[22]<<4)|(d[21]<<3)|(d[20]<<2)|(d[19]<<1)|d[18];
t3p = (d[17]<<2)|(d[16]<<1)|d[24];
```

The displaced d[24] that looked wrong for a specification is genuinely in the
specification. BSIC is scattered too, so the caution about it was right: the
45 and 49 reported earlier in this ticket were both artifacts.

`sch_parse` read all four fields as contiguous MSB-first runs. Fixed, with the
positions in one shared table that `gsm_sch_pack_info()` also uses, so an
encoder can no longer quietly agree with a wrong decoder.

Results on the two captures:

```
                              before                    after
gsm_arfcn_69.bin    25/31 decoded, BSIC 45,    31/31 decoded, BSIC 59,
                    T1 1634..1891,             T1 793..794, frame numbers
                    frames span ~12 s          advance by exactly the SCH gaps

gsm_arfcn73_...bin  10/30 decoded, BSIC 49,    15/30 decoded, BSIC 56,
                    T1 1616..1617              T1 1576 throughout, monotonic
```

### Why it hid for so long

`tests/gsm_dsp_test.c` packed its synthetic burst with `sch_pack_info()`, the
exact inverse of the wrong `sch_parse()`. The round trip passed, the parity
passed (parity is computed over the bits in order, which was never wrong), and
BSIC looked stable because it is constant within a capture. Every check the
repo had was satisfiable by a self-consistent wrong layout.

The new real-capture assertions are not: the decoded frame numbers must
increase, must span no more than the ~433 frames the capture covers, and T1
must not vary by more than 1. Verified by rebuilding the test against the old
layout — all four assertions fire.
