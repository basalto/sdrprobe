# 01 - Give the two-cell positive a margin, and pin the edge separately

Status: needs-triage

## What to measure first

Where the edge actually is, under both compilers, as a function of the second
cell's level -- the sweep the fixture comment describes having done once. Then
choose the positive fixture with margin over the worse of the two, and keep a
separate check that pins the edge itself so the limit is still recorded.

A harness for this was attempted and did not reproduce the test's conditions:
`build_two_cell_carrier()` compiled into a standalone sweep returned zero
cells at every level from -4.4 dB to 0, where the suite finds two at -1.4.
Something in the suite's own setup is load-bearing and was not carried over.
**Find that before trusting any sweep**, because a sweep that finds nothing
everywhere will happily suggest any fixture level at all.

## Why not simply raise the level

Because the number is a measurement and the comment says what it measures:
"the real pair differ by 1.7 dB". A fixture at 0 dB would pass everywhere and
would stop describing the case that matters. The answer is two checks -- one
comfortably inside the working region for the capability, one at the edge for
the limit -- not one check moved.

## What must be checkable

The same thing this is about: that the capability check has margin. Whatever
level is chosen, the ticket should record the measured distance from the edge,
so the next person can see it was chosen rather than found by trying.
