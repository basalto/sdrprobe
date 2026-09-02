# 03 — A view, and a way to read it without one

Status: needs-triage
Blocked by: 02

Follow `AGENTS.md`'s "Adding a view" checklist -- `src/fm_layout.h`,
`src/view_fm.c`, `struct fm_view` in `app.h`, extend `enum decode_kind`, the
`tests/layout_test.c` include, and the `check-layout` prerequisite that is easy
to forget.

What the screen owes a reader, learned from the LTE view: the funnel. Two empty
panels look identical whether nothing is transmitting or every group is failing
its syndrome, and blocks -> groups -> identifications -> names is the line that
separates them.

Then a headless path, because a decode nobody can script is a decode no check
can reach (ADR-0012) -- `--technology fm --decode` alongside the existing ones,
one line per station identified.

Worth considering once this works: FM is the one technology here where a scan
is cheap. 100 kHz raster, 20 MHz of band, and a station is either there or not
within a few hundred milliseconds -- the whole of band II in under a minute,
against two and a half for one LTE band.
