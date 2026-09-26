# 05 - Folders as link boundaries

Status: needs-triage -- **optional**; decide after 04
Blocked by: 04

## Why last, and why optional

Tickets 01-04 deliver all of the decoupling -- a raylib-free `app.h`, the
runtime out of the drawing files, contracts that take plain state, and a
gated link check -- without moving a single file. This ticket draws the
boundaries those tickets made true, so a reader sees them in the tree
instead of learning them from an audit.

Done first it would have relocated the entanglement into subdirectories and
fixed nothing. Done last it is mechanical, and its whole cost is churn.

## The layout

```
src/core/      sdr_dsp, signal_*, device_*, capture_*              -lm
src/tech/      gsm_*, lte_*, adsb_*, tetra_*, fm_*, rds, srd_*      -lm, core
src/runtime/   acquisition, receiver_*, frame_advance, sessions,
               options, config, <tech>_runtime.c                    -pthread; no raylib
src/model/     *_view_model, survey_record                          -lm
src/server/    websocket, viewer_*                                  no raylib
src/gui/       sdrgui*, view_*, overlay_*, *_layout.h, raygui_impl  raylib
src/app/       sdrprobe.c
```

Each folder may include only what is beneath it in that list, and ticket
04's include audit becomes a per-folder rule rather than a GUI list kept by
hand. `tech/` may be split per technology (`tech/gsm/`, ...) if the flat
`tech/` is still long -- decide on the evidence of the move, not before.

## What it costs, measured

Full `src/<file>` paths that a move would stale, counted 2026-09-26:

| where | references |
|---|---|
| `CLAUDE.md` and `AGENTS.md` | 74 |
| `docs/`, including the ADRs | 59 |
| comments in `src/`, `tests/`, `scripts/`, `web/` | 26 |
| `.scratch/`, open and resolved tickets | 210 |

Bare file names -- `view_fm.c`, `sdr_dsp.c` -- survive a move and are most of
the prose; only full paths break. Beyond prose: nearly every Makefile rule
names `$(SRC)/<file>`, and `scripts/check_touched.py` maps `$(SRC)` to one
directory, so `make check-touched` would silently stop picking suites unless
it learns the folders too. The CodeGraph index needs a rebuild.

## Decisions to make -- ask, do not pick

1. **Rewrite the history, or not.** Living material (`CLAUDE.md`,
   `AGENTS.md`, `docs/`, skills, source comments -- about 160 references)
   must be rewritten, or it lies. Resolved tickets describe the past: a path
   in one is where the file *was*. Recommended: rewrite living material,
   leave resolved tickets alone, and say so once in each affected ticket
   family rather than in every file.
2. **How includes spell a path.** `-I` per folder keeps every `#include
   "sdr_dsp.h"` unchanged and the diff small; `#include "core/sdr_dsp.h"`
   against one `-Isrc` makes the layer visible at every include site and
   lets the audit read it off the line. Recommended: the second -- the point
   of the move is that a boundary can be *seen*.
3. **Whether this is worth doing at all**, with 01-04 already delivering the
   decoupling. It is the one ticket here whose benefit is navigation rather
   than a property a check can hold.

## Acceptance criteria

- [ ] `git mv` only -- `git log --follow` keeps every file's history, and no
      file's content changes in the move commit beyond `#include` lines.
- [ ] The per-folder include rule is gated and clean.
- [ ] `make check-touched` still picks the right suites after a change in
      each folder -- try one per folder, since this is the tool most likely
      to break silently.
- [ ] `make check`, `make check-pipelines`, `make screens` warm,
      `make check-web-layout` pass unchanged.
- [ ] Living documentation has no stale `src/<file>` path (a grep for each
      moved file's old path finds only resolved tickets).
- [ ] **An ADR**: folders are link boundaries, and what each may include.
      It extends ADR-0007 (components never see `struct app`) outward to
      runtime, models and server, and ADR-0012 (every decision reachable
      without a window) now has a link check behind it.
