# 16 - The window reads the model too

Status: **done** (2026-09-27) -- all four views converted.

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

**2026-09-27 — SRD done.** `7fd0308`. The four acceptance criteria all met;
`make screens NAMES="srd"` draws the same screen.

It found four real drifts rather than the hypothetical one the ticket was
written about, which is worth knowing before the other three:

- **The browser had already grown a different sentence** for a log row.
  `draw_log()` chose one of seven and `srd.js` composed its own out of the
  bit count and the violations. Not a risk — a fact, at this commit.
- **The 2-FSK prefix test was spelled out a second time in the markers**,
  four lines below a comment recording that this had already happened once
  and been fixed. And spelled *wrongly*: two bytes where
  `srd_device_type_of()` checks three, and no test of the frame kind.
- **A ternary chain's last branch was doing duty as its fallback**, so a
  frame of no kind was labelled `GENERIC`.
- **Two causes of an empty table**, which the window distinguished and the
  browser did not.

Left in the drawing deliberately: the analysis charts' envelope and chip
arrays and the y-range computed over them. Moving that decision means moving
512 floats into the model, and the browser has no analysis mode to show them
in. If TETRA or LTE turns out to have the same shape, that is a ticket of
its own about whether a view model carries bulk arrays.

The check's link line grew: `srd_frame.c`, `srd_dsp.c`, `sdr_dsp.c` and
`signal_probe.c`, which is what `check-srd-frame` already links. Still no
raylib and no librtlsdr, which is the constraint that matters — but a model
check that pulls in the whole technology module to ask one pure question is
worth noticing if it happens again.


**2026-09-27 — TETRA done.** `967978a`. The model was already right and the
window was not reading it, which cost a diagnosis the program has been able
to make since ticket 07 and had never shown: at a sample rate the channel
filter cannot decimate, the window read "no network identity yet (lock
0.00)" -- a statement about the air -- where the true statement is about the
receiver. The *browser* had been drawing it since ticket 07.

Also: "LA unread" is not "LA 0". The window asked `broadcast_total > 0` in
two places and worded it two ways; the **waterfall marker did not ask at
all** and printed a location area of zero over the carrier.

One thing worth carrying: the first draft of the wrong-rate header carried
its reason in parentheses and `sdrgui_text_fit()` cut it to "(the channel
filter ne..." at 640 px. **A clipped explanation is a dangling fragment and
worse than none.** The claim fits at every width; the why lives in the
browser, which wraps, and in the headless report, which does not truncate.

**2026-09-27 — ADS-B done.** `68de997`. Three decisions moved --
`funnel_warn` (frames arriving and none decoding, which the browser drew in
one colour), `enum adsb_readiness`, and which kind of empty an empty log is.

**And one deliberately did not, which is the finding of this ticket.**
Moving the row loop to the model was the obvious next line and is wrong:
`ADSB_VIEW_MODEL_LOG` is 48 because that is what the wire can afford every
block (ADR-0027) and the window keeps 256, so reading the rows out of the
model would have cut the screen's scrollback to a fifth with nothing
failing. **A view model built to a wire's budget does not bound the
window.** Nothing in that loop decides anything, so it is drawing and it
stays. The markers are the same case.

**2026-09-27 — LTE done.** `6799552`. Four things, the first of which is the
ticket's own argument made for it: the header printed the *picker's* band
under a caption promising the band the **tuning** is in, a fix that lived in
one `snprintf` where a browser could not see it and which this view already
carried a comment about. Also `funnel_warn`, the scan's cost sentence, and
`lte_findings_from()` being called twice -- once by the builder and once by
the drawing, each with its own idea of the carrier frequency.

## What the four came to

Eleven drifts across four views, and **not one of them was hypothetical**.
The ticket was written expecting to close a gap that might open; every view
had already opened it.

The shapes, in case a ninth view is ever added:

1. **A sentence chosen from several** — every view had one, and in three of
   four the browser had grown a *different* sentence for the same state.
2. **A byte offset or a protocol constant written into a drawing** — SRD,
   twice, one of them four lines below a comment recording that it had
   already happened once.
3. **A ternary chain whose last branch was doing duty as its fallback** —
   SRD's marker labelled a frame of no kind `GENERIC`.
4. **A field the model carried and the window ignored** — TETRA's
   `rate_supported`, a diagnosis the browser drew and the window did not.
5. **A verdict drawn as a colour** — ADS-B's and LTE's funnels, invisible to
   anything that does not read pixels.
6. **Two facts under one caption** — LTE's band.

And one anti-pattern: **do not move data just because it is next to a
decision.** ADS-B's 256-row log would have become 48.

## What is left, in all four

The analysis modes' bulk arrays and the captions chosen over them: SRD's
envelope and chips, TETRA's phase steps and 255-symbol repeat profile,
ADS-B's frame trace, LTE's four charts. Each is a decision over a few
hundred floats the model does not carry, and no browser view has an analysis
mode to show them in. Moving them is a question about whether a view model
carries bulk arrays at all, which is its own ticket and not obviously worth
opening: ADR-0027 budgets the wire, and these would not fit it.
