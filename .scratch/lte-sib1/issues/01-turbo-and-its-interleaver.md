# 01 — The turbo code, and the interleaver that defines it

Status: resolved
Blocked by: (none)

`src/lte_turbo.{c,h}`: the rate-1/3 parallel concatenated convolutional code
of 36.212 section 5.1.3.2, its QPP interleaver, and a soft-in decoder.

Self-contained and needed by everything downstream, so it goes first. Nothing
in it touches a receiver, a window or a sample -- it takes soft bits and
returns a block, which is what lets the whole thing be checked against fixed
vectors (ADR-0012).

## Why the interleaver is the risky half

The code itself is two identical 8-state recursive encoders, which is
ordinary. What defines the code is the **quadratic permutation polynomial**
between them: `pi(i) = (f1*i + f2*i*i) mod K`, with f1 and f2 tabulated per
block size -- 188 sizes from 40 to 6144.

That table is the failure mode. A single wrong pair produces an interleaver
that still *looks* like one, still round-trips through an encoder that shares
the same wrong table, and decodes nothing off the air. It is exactly the shape
of the conjugated LTE primary sequence and the scattered GSM SCH layout, both
of which were green throughout (`dsp-validation`).

**So the table gets an independent check rather than a round trip.** A QPP is
a permutation if and only if f1 is coprime with K and every prime factor of K
divides f2. That is a property of the numbers alone, provable without an
encoder, and a mistyped pair fails it with high probability. Assert it for all
188 sizes, and assert the map is a bijection by construction -- every output
hit exactly once.

## What must be checkable

- **The interleaver is a permutation, for every K in the table**, by the
  coprimality property and by counting hits.
- **The block sizes themselves.** They are not arbitrary: 40 to 512 in steps
  of 8, 528 to 1024 in 16, 1056 to 2048 in 32, 2112 to 6144 in 64. That is 188
  and the check should say so rather than trusting the table's length.
- **The encoder's trellis terminates.** LTE turbo does not tail-bite; it
  spends 12 tail bits putting both encoders back to zero, and a decoder that
  assumed otherwise would lose the last few bits of every block.
- **The decoder recovers a block through noise**, and gets better with
  iterations rather than merely different -- a decoder whose error count does
  not fall between iteration one and iteration eight is not iterating.

## Not in this ticket

The rate matching that feeds it, the CRC-24A that checks it, and everything
above the physical layer. Those are 02 and later.

## Comments

Resolved. `src/lte_turbo.{c,h}` and 219 checks.

**The table is right, and it is right for a reason that does not involve our
encoder.** All 188 rows satisfy the permutation property -- f1 coprime with K,
every prime factor of K dividing f2 -- and every interleaver is a bijection in
fact as well as in principle: no two bits to one place, no place left empty,
nothing pointing outside its block, at all 188 sizes. That is the check the
ticket was written for, and it passed first time.

The decoder is max-log-MAP, and it behaves like a turbo decoder rather than
like something that merely passes a clean round trip. Measured on K = 512:

| Es/N0 | after 1 iteration | after 8 |
| --- | --- | --- |
| -1.1 dB | 0 | 0 |
| **-3.0 dB** | **19** | **0** |
| -4.6 dB | 88 | 122 |

The middle row is the one worth having: the iteration is doing the work.
The last row is the cliff, where iterating makes it worse -- which is what a
turbo decoder does below its threshold, and is itself a sign the feedback is
real. Rate 1/3 at -3.0 dB Es is 1.8 dB of Eb/N0, which is where max-log-MAP on
a block this size belongs.

## Three of my own claims were wrong before any code was

Worth recording, because they are the `check-claims` shapes exactly.

- **The block sizes.** I generated the expected sequence with the step chosen
  by `expected <= 512`, which makes 512 be followed by 520. The step changes
  *at* the boundary: 512 is followed by 528. 128 of 188 sizes "failed" against
  my arithmetic, not the standard's.
- **The prime factors.** I divided out one prime and then treated the whole
  remaining cofactor as prime, so K = 120 was reported as failing on 40 --
  which is neither prime nor a factor anybody claimed. 1289 false failures.
- **The operating point.** The first version of the iteration check used three
  times the noise it should have. One iteration and eight both left about a
  third of the block wrong, and the check said the decoder was broken when it
  was past the code's cliff, where no decoder converges. The point had to be
  *found* by sweeping, not guessed -- and the sweep is now a second check
  rather than a thing I did once.
