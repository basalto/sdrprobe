# 01 — Pick a band to sweep, instead of typing its edges

Status: needs-triage
Blocked by: (none)

The survey sweeps whatever `Range` and `to` say, and the default is the
tuner's whole span -- 24 to 1766 MHz, which takes minutes. Sweeping one band
already works and is the common case; what is missing is a way to *ask* for
one without knowing its edges.

Today that means typing `935.1M` and `959.9M` from memory, or from
`band_plan.c`, which the program is already carrying.

## What it should do

A selector beside the range fields, listing the allocations worth sweeping,
which fills the two fields in. The band plan is the list: it has 64 entries
with names, and `band_plan_entry_at` walks them.

Two things to decide rather than assume:

- **Not all 64 are worth offering.** Long-wave broadcast is in the table and
  this receiver cannot reach it; the tuner starts at 24 MHz. The list should
  be the allocations that overlap what the receiver can tune, which the
  program knows.
- **A band is not always one sweep.** Some allocations are a few hundred
  kilohertz and some are tens of megahertz, and the dwell that suits one
  suits the other badly. Picking a band might reasonably set the dwell too.

## Why it earns its place

The survey is the screen the program opens on and the whole-tuner sweep is the
slowest thing it does. Every session that starts with "what is on 900 MHz"
currently starts by typing two numbers -- and getting one wrong is a sweep of
the wrong band that takes just as long to find out about.

It also pairs with what Inspect became: the band plan already names what lives
where and now sends a reader to the right decoder. Letting it choose what to
sweep in the first place closes the loop.

## What must be checkable

Which allocations are offered, and what range each fills in, are decisions --
a pure function from the band plan and the tuner's limits to a list. The combo
itself is drawing.
