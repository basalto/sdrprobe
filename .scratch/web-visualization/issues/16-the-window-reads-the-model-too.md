# 16 - The window reads the model too

Status: ready-for-agent

## The residue ticket 07 left

Seven browser views are done, and each one came with a view model in
`src/model/` and a builder in `src/runtime/`, checkable with `-lm` alone.
Four of the seven window views **still read `struct app` directly** rather
than the model built beside them:

| view | reads its model? |
|---|---|
| `view_scope.c` | yes |
| `view_survey.c` | yes |
| `view_fm.c` | yes |
| `view_gsm.c` | yes |
| `view_adsb.c` | **no** |
| `view_tetra.c` | **no** |
| `view_srd.c` | **no** |
| `view_lte.c` | **no** |

## Why that is a debt and not a style preference

The `web-view` skill states the rule and the reason in one line: *"The
window's own drawing then reads the model too, so there is one decision
rather than two that agree today."*

For those four views there are two implementations of the same decisions.
They agree at this commit because the builder was written by reading the
drawing. Nothing holds them together afterwards:

- **The check only reaches one of them.** `check-srd-view-model` asserts
  what the *model* decides. If somebody changes the window's wording, its
  threshold or its ordering, every suite stays green and the browser keeps
  saying the old thing — which is exactly the shape of
  `web-visualization/15`, where the browser drew the survey's marks swapped
  for months under a green gate.
- **It is the half a check cannot see.** ADR-0012 exempts drawing and not
  deciding. A ternary chain inside a `DrawText` call is a decision in the
  exempt half.
- **Three of the four already produced an instance.** `view_srd.c` spelled
  `enum srd_frame_kind` and `enum srd_modulation` as ternary chains, so the
  names existed twice until ticket 07's SRD commit gave them
  `srd_frame_kind_name()` and `srd_modulation_name()`. `update_tetra()`
  computed `rate_unsupported` and **discarded it**, so the headless path
  could print it and the window could not. `view_lte.c` passes
  `lte_phich_resource_name()`'s result straight to `%s`, where the model
  guards the NULL the field cannot encode.

## Goal

Each of the four views builds its model once per frame and draws from it,
the way `view_gsm.c` does. Where a decision is still in the drawing, it
moves into the model and the model's check grows an assertion for it.

## Not the goal

**Not a rewrite of the drawing.** Colours, fonts, rectangles and the 174
direct raylib calls stay exactly where they are. This moves *what is
decided*, not *what is drawn*. A view that ends up building the model and
reading three fields out of it has done the whole job if three fields is
what it decided.

**Not a new model per view.** All four models exist. If a view needs a field
the model does not carry, that field is added to the model — which is the
point, because a field only the window has is a field the browser cannot
show.

## Order

Smallest first, so the pattern is settled before it costs anything:

1. **SRD** — one header line, two counters, a frame log.
2. **TETRA** — identity, funnel, identity log.
3. **ADS-B** — the message log and the funnel.
4. **LTE** — three panels, the statistics table and the findings.

## Acceptance criteria, per view

- [ ] The view calls `<tech>_view_model_build()` once per frame and draws
      from the result.
- [ ] No decision remains in the drawing: no threshold, no choice between
      sentences, no ordering, no derived number. A colour chosen for a
      verdict the model handed over is drawing and stays.
- [ ] Anything that moved is asserted in that model's own check.
- [ ] `make screens NAMES="<view>"` is unchanged against the previous
      commit, or the difference is named and intended.
- [ ] `make check` green, and `check-web-layout` unchanged.

## How to tell a decision from a drawing

The test that worked in ticket 07: **would a second implementation get this
right?** If a browser written from the screenshot would word it differently,
order it differently or pick a different threshold, it is a decision.

A sentence chosen from several, a unit conversion, a count, a comparison
against a constant, a clamp, a sort, a "which of these is the primary" —
decisions. A colour, a font size, a rectangle, a column width, whether a
label reads `MHz` or `MHz:` — drawing.

## Comments
