# 05 - Folders as link boundaries

Status: resolved, 2026-09-27. ADR-0028 is the decision;
`check-layers` holds it. The layered include spelling is the one piece
deliberately left to a second commit.
Blocked by: 04 (done)

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

- [x] `git mv` only -- `git log --follow` keeps every file's history, and no
      file's content changes in the move commit beyond `#include` lines.
- [x] The per-folder include rule is gated and clean.
- [x] `make check-touched` still picks the right suites after a change in
      each folder -- try one per folder, since this is the tool most likely
      to break silently.
- [x] `make check`, `make check-pipelines`, `make screens` warm,
      `make check-web-layout` pass unchanged.
- [x] Living documentation has no stale `src/<file>` path (a grep for each
      moved file's old path finds only resolved tickets).
- [x] **An ADR**: folders are link boundaries, and what each may include.
      It extends ADR-0007 (components never see `struct app`) outward to
      runtime, models and server, and ADR-0012 (every decision reachable
      without a window) now has a link check behind it.

## Done, 2026-09-27

194 files into seven directories, `git mv` only. **ADR-0028** is the
decision and the table; what follows is what the move itself taught.

### The decisions, as taken

1. **Rewrite living material, leave resolved tickets.** 158 paths rewritten
   in `CLAUDE.md`, `AGENTS.md`, `docs/` (the ADRs included), the skills,
   source comments and the scripts. A path in a finished ticket is where the
   file *was*.
2. **Staged includes.** One `-I` per layer, so all 717 `#include` lines are
   untouched and this commit is `git mv` plus Makefile paths. The layered
   spelling is a second commit, separately verifiable -- two reviewable
   steps instead of one 1,450-line diff.
3. **Worth doing:** yes, by the operator.

### Four things the move found that nothing else had

**The layer order is not the obvious one, and it was settled by counting.**
Both candidates were run against the real include graph: `model` above
`runtime` gave three violations, beneath it gave six. Chasing the three
produced the better definition -- `survey_record` and `survey_store` are
written *by* runtime and read by a file and a script, so they are not view
models at all. Into `runtime/`, and the count went to **zero**. `model/` is
now exactly *what crosses the seam to a reader*.

**Two files were in the wrong place and the audit said so.** `view_input.h`
is raylib-free routing state -- sibling of `input_route.h` -- and was in
`gui/` only because it is named `view_*`. The survey's analysis headers
(`survey_carrier`, `survey_sweep`, `survey_suspect`, `survey_confirm`,
`survey_bands`) are pure arithmetic over readings, the same family as
`clock_chain.h`, and belonged in `core/`.

**The hand-kept header audit broke exactly as predicted.** It grepped
`$(SRC)/<file>`; paths grew a folder; it reported **104 of 104 headers
missing**. It failed loudly, which was luck -- a pattern matching
*everything* would have reported none and been believed. `check-layers`
reads the folders and needs no pattern. `CLAUDE.md`'s copy is corrected and
says what happened to it.

**Two build rules had no `-I` at all**, because everything used to sit in one
directory and a relative include found its neighbour. Both binaries stopped
compiling on the first build after the move -- caught immediately, but it is
the reason `SRC_INC` exists rather than a per-rule flag.

### Verified

- `make check`: **83 suites, 22323 checks**. Both Makefile audits clean.
- `check-layers` is gated and **mutation-tested**: an `#include "view.h"`
  added to `src/core/sdr_dsp.c` fails it, naming file, header and layer.
- **The move changed nothing observable**, measured rather than assumed: the
  binary from before and the binary after, over four headless cases. GSM, FM
  and the capture survey **byte-identical**; ADS-B differs by one second of
  wall clock with every decoded field -- position, altitude, raw hex --
  identical, which is the difference `CLAUDE.md` already records.
- `make check-touched` picks the right suites from **all seven** layers,
  verified per folder. It needed no change: it substitutes `$(SRC)` for
  `src`, and the Makefile's paths now carry the folder.
- `make screens NAMES="fm survey"` warm, both correct.
- CodeGraph reindexed.

### Left open, deliberately

`tech/` is 50 files and stays flat. The ticket said to decide on the
evidence of the move rather than before it, and the evidence is not in yet.
