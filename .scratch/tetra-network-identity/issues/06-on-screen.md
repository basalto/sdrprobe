# 06 — The view

Status: resolved
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

## Comments

**2026-09-06 — resolved. `src/view_tetra.c`, `src/tetra_layout.h`, pinned by
`check-layout` and rendered by `make screens NAMES="tetra tetra-charts"`.**

Two arrangements, as every decode view here has. The log says what the network
said and when; the analysis says how it was read -- the phase steps the dibits
came off, and how much of each 255-symbol slot repeats. `--view tetra` and
`--analysis` open either from the command line (ADR-0012), and key 5 reaches it
from the Decode tab.

The header carries the identity and, under it, the funnel -- bursts found,
blocks whose parity checked, blocks that failed, broadcast blocks -- because
that is the diagnosis when nothing decodes. Bursts but no parity is a coding
fault; no bursts at all is tuning or band.

### Three things the screenshot caught that arithmetic did not

The ticket said `check-layout` compares rectangles and cannot see the rest, and
all three of these were invisible to it.

- **The view opened on the Survey tab.** `START_VIEW_TETRA` set the decoder and
  never called `set_tab`, so `--view tetra` drew the survey. Every layout check
  passed.
- **The burst-profile chart was empty**, reading "no burst grid found" on a
  signal whose grid is unmistakable. `tetra_burst_find` wants four periods at
  the longest lag asked for, and it was asked to 400 -- 1600 symbols against a
  block's 1180, so it declined every time. Asked to 280 it finds 255 and
  reports 181 fixed positions, which is exactly what the offline measurement
  gave.
- **The lower row was too tall.** Split the ADS-B way, two panels holding six
  short fields and one row sat over half the screen. The charts take 0.58 here
  and the reason is in the header: the ADS-B log fills up and this one does
  not, because a row is one *identity* and the identity changes when the cell
  does.

### And one the screenshot did not catch

`tetra_layout.h` declared a `record_button` copied from `adsb_layout.h` that
the view never drew. A check would have pinned its position happily for ever.
The header now says that a rectangle for a control nothing draws is the same
fault as a header modelling half a screen, wearing the opposite coat.
