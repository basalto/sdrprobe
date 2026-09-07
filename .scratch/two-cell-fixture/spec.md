# One check passes on gcc and fails on clang

`check-lte-dsp`'s "two cells on one carrier: how many are found" expects 2,
gets 2 under gcc at -O2 and -O3, and gets **1** under clang 22 at -O3 --
with `-ffp-contract=off` as well, so it is not FMA contraction alone.

It is not undefined behaviour. The same suite under gcc with
`-fsanitize=undefined,address` runs 588 checks clean, no runtime errors and no
memory errors.

## Why it is fragile by construction

The fixture's own comment says so:

> it is only 1.4 dB down, because that is where this works: a sweep from
> -16.5 dB to -1.4 dB finds the second cell at -1.4 and nowhere below it

So the positive claim -- that two cells on one carrier are separable -- is
made at the exact edge of what the algorithm can do, and a correlation moved
by a hair of float codegen crosses it. `test_two_cells_needs_similar_levels`
pins the *negative* at 0.45 amplitude and nothing pins a comfortable positive.

**That is the wrong place for a capability check to sit.** A check at the edge
flakes across compilers, optimisation levels and CPUs, and when it flakes it
says nothing about the capability -- only about the arithmetic of the day. The
edge is worth recording; it is not worth being the only evidence.

## What this is not

Not a reason to loosen the check or to raise the fixture until it passes. That
is tuning a constant until it agrees, which this repository has refused twice
in one session -- once for `SIGNAL_CARRIER_PRESENT_DB` when a single frequency
slipped past it.

Not a reason to prefer one compiler. gcc is the default for measured reasons
(`Makefile`), and clang passing 41 of 42 suites is a useful second opinion
rather than a vote.
