# 04 - A server built without the window, and a real per-block check

Status: needs-triage
Blocked by: 02, 03

## Why

Tickets 01-03 make the boundary *true*. This one makes it *held*: a build
that fails the moment a server-side file reaches for a view, an overlay,
raygui or raylib, and a check that drives the real per-block step instead of
nineteen stubs.

Without it the boundary decays the way this repository's other hand-kept
lists have: `check-signal-probe` existed, passed and was never run by the
gate; `link_health` named three streams of nine for weeks; the subscribe
parser was two names short. A boundary nothing enforces is a comment.

## What to do

1. **A link target that proves the server stands alone.** Core, tech,
   runtime, model and server objects, linked with `-lm -pthread` and the
   receiver backend -- no `view_*`, `overlay_*`, `sdrgui*`, `raygui_impl`,
   no `pkg-config --libs raylib`. It goes in `CHECK_UNITS`: if it stops
   linking, the gate fails, and says which symbol pulled the window in.
2. **`check-frame-advance` runs real callees for at least one technology.**
   Its 19 stubs are the list of what ticket 02 moved. Replace FM's with the
   real `fm_runtime` over synthetic blocks, and assert what the per-block
   step *did* -- the pilot locked, the funnel advanced -- rather than that a
   stub was called.
3. **An include audit**, one line like `MISSING:` and `NOT GATED`: no file
   outside the GUI set includes `<raylib.h>`, `"raygui.h"` or `sdrgui*.h`.
   Gated, so a stray include fails `make check` rather than a code review.

## The decision to make -- ask, do not pick

**A link check, or a second binary?**

- **(A) A link target only** (recommended). The boundary is held; one
  `sdrprobe` binary still ships, and `server` inside it links raylib as now.
  Nothing a user runs changes.
- **(B) A shipped `sdrprobe-server` binary** without raylib, for a headless
  box with no graphics stack installed. Real value on a Raspberry Pi beside
  an antenna -- and a packaging and install decision (two binaries, two sets
  of command words), which belongs to the operator rather than a refactor.

(A) is the ticket; (B) is a follow-up if it is wanted.

## Acceptance criteria

- [ ] A server link target in `CHECK_UNITS` that fails when any server-side
      object needs a GUI symbol, naming the symbol.
- [ ] `check-frame-advance` exercises at least FM's real runtime and asserts
      on its effects, not on stub call counts.
- [ ] The include audit is gated and clean, and fails when a
      `#include <raylib.h>` is added to a runtime file (mutation-tested).
- [ ] `make check` and `make check-pipelines` pass.

## Take into account

- **The receiver backend is not the window.** `librtlsdr` is a legitimate
  server dependency; only raylib and raygui are excluded. `HAVE_UHD` stays
  as it is.
- **A link check reports the first undefined symbol, not all of them.** Make
  its failure message say "the server pulled in the window through X" so a
  reader knows what boundary broke rather than reading linker output.
