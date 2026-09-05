# 06 — The view

Status: needs-triage
Blocked by: 04

A decode view, arranged like the others: the constellation, the burst timing,
the funnel from bursts to blocks to messages, and the identity when there is
one.

`view_gsm.c` is the model and `adsb_layout.h` the pattern for the geometry --
all of it in the layout header, not some of it, since a header holding half a
screen puts a green tick over the half it does not model. `--view tetra` and
`--analysis` open it from the command line, because every screen has to be
reachable that way (ADR-0012).

## What must be checkable

- `check-layout` over a `tetra_layout.h` that carries every rectangle.
- A screenshot looked at by a person before it is called done, and at its
  neighbours, since `check-layout` compares rectangles and cannot see two
  panels drawing into the same one.
