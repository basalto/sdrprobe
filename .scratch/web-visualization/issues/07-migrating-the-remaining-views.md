# 07 - Migrating the remaining views

Status: needs-triage

## Goal

Repeat ticket 03 for the views that are not the Scope, one at a time, until
the view model is the seam everywhere rather than on one screen.

This is a tracking ticket, not a unit of work. Each view is its own change,
its own screenshot comparison and its own commit. **Any of them may be
declined**: a view nobody wants in a browser is a view that keeps reading
`struct app`, and that is a legitimate resting place rather than a debt.

## Order, and why

The cheap and useful ones first: **survey**, because a sweep is the thing most
worth watching from elsewhere and its record is already a plain struct
(`src/survey_record.c`, built for exactly this reason); then **FM**, which is
small; then the decode views -- GSM, ADS-B, TETRA, LTE, SRD -- whose sessions
already produce plain results. The overlays last: settings and calibration are
mostly widgets and typed input, which is the input half of the seam and is not
solved.

## What this is not

Not a rewrite of the drawing. The views bypass `sdrgui.h`'s components **174
times** with direct raylib calls, 104 of them bare `DrawText`. Moving those
into components is a separate and optional cleanup; this ticket only moves the
*data* behind a view model.

## The input half is unsolved and is not this ticket

161 raylib input call sites in the views and overlays -- 73 `IsKeyPressed`, 33
`GetMousePosition`, 24 `IsMouseButtonPressed`, 14 `CheckCollisionPointRec`, 11
`GetCharPressed`. Immediate mode entangles input with layout by construction:
hit-testing runs against rectangles that exist only during drawing. A Viewer
sending commands does not need this solved, because a command is not a click.
A browser reproducing the window's *interactions* would need it, and that is a
different ticket nobody has written.

## Acceptance criteria

Per view, not for the ticket as a whole:

- [ ] The view's data comes from a view model checkable with `-lm` alone.
- [ ] `make screens NAMES="<view>"` is unchanged against the previous commit.
- [ ] `make check` and `tests/pipelines.sh` unchanged.
- [ ] The view model carries no raylib type.

## Not in scope

- Retiring the raylib window. ADR-0027 records that it remains the primary
  presentation; if that changes, the ADR is amended first and the cost is
  named -- ADR-0012's windowless checks, the `*_layout.h` headers,
  `check-layout`, `panel_rows.h` and `make screens` are all raylib-shaped and
  have no web equivalent.
