# 03 — A view, and a way to read it without one

Status: resolved
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

## Comments

**2026-09-03** — Done. `src/fm_layout.h`, `src/view_fm.c`, `struct fm_view` in
`app.h`, `DECODE_FM` at the head of `enum decode_kind`, the layout check and
its Makefile prerequisite, and `--view fm` / `--technology fm --decode`.

FM sits at position 1 in the Decode tab at the user's request, with ADS-B, GSM
and LTE shifted right. The enum order is what the header draws from, so
`DECODE_FM` leads the enum too.

**The one thing that was genuinely hard**, and it is worth writing down because
nothing else in this program has the shape: FM is the only decode view here
that cannot work a block at a time. The pilot needs a quarter second before its
lock means anything, and the symbol grid and the bitstream run straight through
a block boundary.

The first attempt kept a window of *soft bits*, decoding each block's baseband
on its own and concatenating. It looked completely reasonable and produced
zero groups from 2020 bits where the same capture decoded offline gives
eighteen. Soft bits are only meaningful relative to a symbol grid and a
subcarrier axis, both of which are worked out over whatever span they are
worked out over -- and the axis carries a 180 degree ambiguity that the
differential decode absorbs happily inside one run and not at all across a
join. The window has to hold *baseband*, with one timing search and one axis
over the whole of it. It does, and the view now produces bit-for-bit what the
offline decode of the same span gives, which is why a script and the screen
cannot disagree.

**The funnel earned itself immediately.** Blocks 0 / groups 0 with the pilot
locked is exactly what the broken version showed, and it says "sync is not
holding" rather than "nothing is transmitting".

**A screenshot found what check-layout could not.** The waterfall draws its
footer 36 px below its plot where `sdrgui_chart_area` reserves 8, so it lands
outside the rectangle it was handed -- which every other view got away with
because it had empty space underneath, and this one did not. It read as a
stray "dB" on top of a panel. Fixed in the component rather than by leaving
room in the caller, which is what CLAUDE.md requires, and all five waterfall
screens were re-rendered and looked at.

**Not done, deliberately:** the band scan the ticket floats at the end. FM is
indeed the cheap one -- 100 kHz raster, a station present or not in a few
hundred milliseconds -- but it is a feature rather than the wiring this ticket
is about, and it wants its own ticket.
