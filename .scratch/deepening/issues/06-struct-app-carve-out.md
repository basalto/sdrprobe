# 06 - Finish moving view state out of `struct app`

Status: needs-info
Blocked by: 01, 02, 03, 04

`app.h` is 1 019 lines. Its own header comment says the carve-out into
`struct scope_view`, `struct gsm_view` and the rest is "an organisation of the
same coupling, not a set of modules": every view still reads the whole record.
The stragglers show where it stalled:

- `gsm_analysis_mode` lives at the top of `struct app` while
  `fm.analysis_mode`, `lte.analysis_mode`, `tetra.analysis_mode` and
  `adsb.analysis_mode` are nested;
- `cal_return_sample_rate` (`app.h:1000`) sits outside `struct calibration`;
- nine `return_*` fields (ticket 01);
- `struct options` has 55 fields, applied in `main` and again in `run_gui`.

## Why this is not a ticket of its own

Nothing here is worth doing directly. Each straggler belongs to one of 01-04:
the `return_*` fields to the lease, `gsm_analysis_mode` and the per-view
decode state to the sessions, `config`-adjacent fields to the installation.
When those land, what is left in `struct app` should be the handoffs between
screens -- tab, decode kind, the sample buffers, the current block -- and the
deletion test should pass for every remaining field: delete it and something
in two files breaks.

## What to do with it

Use it as the measure. After each of 01-04, count `struct app`'s fields and
list which ones two or more files still read. When the list is only handoffs,
close this ticket with the number in `spec.md`; if a field survives that only
one file reads, that is the next straggler and it goes back to whichever
ticket owns it.

## Not in scope

Making views into modules in the full sense (state with code, no `app.h`
include) the way `acquisition.c` is. That is a further step and should get its
own spec if the number above says it is close.
