# 01 - Give the two-cell positive a margin, and pin the edge separately

Status: resolved, 2026-09-09 -- and the ticket's own plan was not what fixed
it. The margin is in the *population*, not in the level, because the level
turned out not to be the variable.

## What the harness found first, which is what this ticket demanded

> A harness for this was attempted and did not reproduce the test's
> conditions: `build_two_cell_carrier()` compiled into a standalone sweep
> returned zero cells at every level. Something in the suite's own setup is
> load-bearing and was not carried over. **Find that before trusting any
> sweep.**

It is **`twiddles_init()`**. The fixture's inverse transform reads a twiddle
table that the suite's `main` fills; without it the table is zeros, the
transform returns zeros, and the buffer is noise -- which reads as zero cells
at every level, exactly as reported. `make probe-two-cell MODE_TWO_CELL=--diagnose`
prints it:

```
diagnose      everything set up            -> 2 cells
diagnose      without twiddles_init()      -> 0 cells
diagnose      with full_scale 0            -> 2 cells
```

The sweep is therefore built **from the check's own translation unit**
(`tests/lte_dsp_test.c` with `-DLTE_TWO_CELL_SWEEP`, including
`tests/two_cell_sweep.inc`), with `lte_dsp.c` compiled in rather than linked
so it can reach the per-root scores. A copy of a fixture is a second fixture;
this cannot drift from the one the suite runs.

Its gate is worth keeping in mind if it is ever edited. The first version
refused to sweep unless it reproduced the suite's own two-cell count at 0.85 --
which is the answer under investigation, so under clang it refused to run the
sweep that would have explained the refusal. A gate before a measurement has
to hold to something *not* in dispute: that the fixture is alive.

## The cause, which was not the one on the tin

`spec.md` blamed float codegen at an algorithmic edge. It was neither.

`fill_other_traffic()` drew both bits of every QPSK symbol as two `rng_next()`
calls in one argument list. Evaluation order there is **unspecified**, gcc and
clang choose opposite ways, and the two compilers therefore built **different
carriers**. Stage hashes of the fixture:

| stage | gcc | clang |
| --- | --- | --- |
| twiddles | fc3676d5.. | fc3676d5.. |
| pss / sss / crs sequences | agree | agree |
| broadcast bits | b2d65afc.. | b2d65afc.. |
| **after fill_other_traffic** | ba57e0fa.. | 126133f9.. |
| the two-cell fixture | 899a2285.. | 7a77c73e.. |

`make probe-two-cell MODE_TWO_CELL=--fixture` prints that table. The bits are
drawn in separate statements now and both compilers build `7a77c73e...`.

## Why the ticket's plan could not have worked

With the draw order fixed, the old fixture separates two cells under
**neither** compiler, at any level. The sweep says why -- the spread is across
the *traffic*, not across the level:

| level | dB | 0 cells | 1 cell | 2 cells | pair right when 2 |
| --- | --- | --- | --- | --- | --- |
| 1.00 | 0.00 | 2 | 18 | 19 | 19 / 19 |
| 0.92 | -0.72 | 1 | 18 | 20 | 20 / 20 |
| 0.85 | -1.41 | 2 | 23 | 15 | 15 / 15 |
| 0.45 | -6.94 | 0 | 39 | 1 | -- |

Forty draws each. "Choose a level with margin over the edge" has no answer:
19 of 40 at equal power is the ceiling, and the old fixture's 0.85 was one
draw of it. The measured distance from the edge this ticket asked to record
is therefore not a decibel figure -- it is **9 of 16 against a floor of 4**.

## What the checks say now

`test_two_cells_on_one_carrier` builds 16 carriers with the same identities
and different traffic, and asserts:

- two cells separated on at least **4** of 16 (measured 9, so more than half
  of what works can be lost before it fails);
- **every** pair reported is 190 and 402 -- absolute, not statistical, and
  54 of 54 across the whole sweep. A search that guessed would fail this long
  before it failed the rate;
- never a third identity;
- the single-cell path returns exactly one cell on all 16, always one of the
  two really there.

`test_two_cells_needs_similar_levels` is the limit, over the same population:
at -6.9 dB at most 3 of 16 separate (measured 1 -- so "not recovered" is a
rate, not an absolute, which is the honest way to state it), a cell is found
on every carrier, and the one reported is the stronger.

Cost: `check-lte-dsp` goes from 1.3 s to 3.5 s, 585 checks to 590.

## What this leaves open

Two things, and neither is this ticket:

- **The rate itself.** Nine of sixteen is what the multi-cell path manages
  when both cells are fully loaded and mutually interfering; the binding
  constraint is the antenna-port coherence gate at `LTE_PORT_COHERENCE_PRESENT`
  (0.55), which both cells' references sit either side of. `probe-two-cell`
  is the number to move. Ticket 02.
- **Zero cells on a carrier that has two**, which happens on 2 of 40 at equal
  power: both cells are found by the primary sequence, both yield the right
  identity, and both are then rejected by that same gate. That is a worse
  answer than reporting one, and it is in ticket 02 as well.
