# 02 — Rate matching, and the 24-bit parity

Status: needs-triage
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
