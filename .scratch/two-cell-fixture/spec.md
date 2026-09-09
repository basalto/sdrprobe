# One check passed on gcc and failed on clang

`check-lte-dsp`'s "two cells on one carrier: how many are found" expected 2,
got 2 under gcc at -O2 and -O3, and got **1** under clang 22 at -O3 -- with
`-ffp-contract=off` as well, so it was not FMA contraction alone.

It was not undefined behaviour. The same suite under gcc with
`-fsanitize=undefined,address` ran clean, no runtime errors and no memory
errors.

## Resolved 2026-09-09, and the diagnosis above was wrong

Both halves of this page's reasoning were wrong, and the second is the more
useful mistake.

**The cause is not float codegen.** `fill_other_traffic()` drew the two bits
of every QPSK symbol as two `rng_next()` calls inside one argument list:

```c
qpsk((int)(rng_next() & 1u), (int)(rng_next() & 1u), &re, &im);
```

Argument evaluation order is **unspecified** -- not undefined, which is
exactly why a sanitiser has nothing to say about it -- and gcc and clang
choose opposite ways. Every symbol of the interfering traffic came out with
its two bits swapped between the two compilers, so **the two compilers built
different carriers**, and a different interference pattern is a different
answer to how many cells are separable. Hashing the fixture stage by stage is
what found it: the twiddles, the broadcast bits and all three transcribed
sequences agreed, and the resource grid did not.

The tell that "a hair of float codegen" was never the explanation was there in
the numbers and was not read: the primary-sequence correlations differed by
0.06 between the compilers, and antenna-port coherence by 0.3. Rounding does
not move a correlation by six percent.

**And the check was not merely sitting at an edge.** With the draw order made
explicit, both compilers build the same carrier -- and the old fixture then
separates two cells on *neither*. The level was never the variable this page
assumed it was. Over forty draws of the traffic, two cells are separated on 19
of them at equal power, 20 at -0.7 dB and 15 at -1.4 dB. There is no level
with margin, because the spread is not across level. The old check was pinning
one draw of something close to a coin flip, and the coin came up heads under
gcc.

## What replaced it

The fixture is a population: `TWO_CELL_SEEDS` (16) carriers with the same two
identities and different traffic. What is asserted is the rate, with a floor
of 4 against a measured 9, and -- the half worth having -- that a report of
two cells is **never** a report of the wrong two, 54 times out of 54 across
every level measured. `make probe-two-cell` is the harness.

`make CC=clang check` now passes **all 55 suites**. The "41 of 42" this
repository has carried was this one check and nothing else.
