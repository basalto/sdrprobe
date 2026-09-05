# 02 — Rate matching, and the 24-bit parity

Status: resolved
Blocked by: 01

Between the turbo code and the air: sub-block interleaving of the three
streams, the circular buffer, and the puncturing that fits a block to the
resource elements it was given.

Different from the MIB's rate matching in a way worth stating before starting:
the MIB repeats a fixed 120 bits into a fixed 1920 and the receiver always
knows both numbers. Here the output length depends on the resource allocation
the control message carried, so the dematcher has to be told how many bits it
is unpacking rather than knowing.

The filler bits are the trap. When the transport block is shorter than the
code block size the standard prepends `<NULL>` bits which are *not*
transmitted, and a dematcher that forgets them shifts every stream.

Also here: **CRC-24A**, polynomial 0x1864CFB, which is not the MIB's 16-bit
one and not the same as CRC-24B used for code block segmentation.

## What must be checkable

- A round trip at several lengths, including one that needs filler bits.
- CRC-24A against a fixed vector, and that it is distinguishable from
  CRC-24B -- two 24-bit polynomials in the same document is how the wrong one
  gets used.

## Comments

**2026-09-05 — resolved.** `src/lte_transport.{c,h}`, checked by
`check-lte-transport`.

Both things transcribed from the standard are pinned against properties rather
than against their own use, because a wrong constant that the matcher and the
dematcher share round-trips perfectly and fails only on air -- which is how a
conjugated primary sequence and a scattered SCH field layout each stayed green
here for months.

- **The polynomials.** A CRC register shifting in a message computes
  `M(D)·D^24 mod g(D)`, so feeding it the twenty-five bits of `g(D)` itself
  must leave no remainder. That holds for the right constant and for no other,
  and it catches the two being swapped: CRC-24A does not divide CRC-24B's
  polynomial or the other way round. No external test vector was available and
  none was invented; a value this file computed and then asserted would have
  pinned the implementation rather than the standard.
- **The column permutation.** The 32 entries are the bit reversal of the
  five-bit column index, so the table is checked against that rather than
  copied into the test twice.

The rest is arithmetic over the buffer and is checked as such: at K = 40, 128,
512, 1024 and 6144, every one of the `3(K+4)` encoded bits reaches the circular
buffer exactly once and nothing else does, and the only holes are the
interleaver's padding. With fillers the holes are the padding plus `2F`, and
**stream 2 stays complete** -- nulling all three would drop parity that was
really transmitted.

Round trips run through the real turbo encoder and decoder, because a rate
matcher that is subtly wrong still produces the right *number* of bits and only
the decoder notices: whole codewords at K = 40 and 512, fillers at K = 128 with
F = 4 and F = 12, a doubly-repeated allocation at K = 256, and redundancy
version 1.

The checks found one bug, which is the reason the repetition case is in there.
When the allocation is longer than the codeword the buffer wraps, and the
repeat was indexed by how many *steps* the walk took rather than by how many
positions it *found* -- the two differ by exactly the holes, so the first
repeat read the slot it was about to write. Uninitialised memory, showing up as
two bits of a doubly-repeated block arriving once instead of twice: a 3 dB loss
on those bits, and nothing a clean unrepeated round trip would ever have seen.

Two deliberate choices worth knowing before ticket 03 uses this:

- `lte_rate_dematch()` **accumulates** into the three streams rather than
  assigning. That is where the repetition gain comes from, and assigning would
  keep only the last copy.
- It also sets the filler positions of stream 0 to a certainty
  (`LTE_RM_KNOWN_LLR`) rather than leaving them erased. The standard defines
  those bits as zero, so telling the decoder is free and starting it from the
  truth is better than making it infer them. Stream 1's fillers stay erased:
  those are parity, and parity over known bits is still whatever the encoder's
  state made it.

**Ticket 03 is not unblocked after all**, and not for a reason this ticket
could have known: SIB1 does not fit the receiver. The cell is 50 resource
blocks and 1.92 MS/s sees six, so the control message that locates SIB1 cannot
be assembled. The spec carries the measurement and 03 and 04 are wontfix.

So this module has no consumer today. The code is correct and checked and the
work was not wasted in the sense that it is wrong -- turbo coding and CRC-24
are the transport layer of every LTE shared channel, so anything that ever
reaches one needs them -- but it was built for a message that cannot be
received here, and the check that would have caught that costs one command:

```
$ ./sdrprobe --headless --lte-chain --earfcn 6200 --lte-chain-seconds 20
chain 1 mib ports 2 prb 50 ...
```

"Does it fit the receiver" is the first of the two gates the `rf-environment`
skill names, and it was skipped because the spec asserted the answer.
