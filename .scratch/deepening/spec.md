# Deepening: shallow modules the architecture review found

An architecture review on 2026-09-07 (`/tmp/architecture-review-20260907-sdrprobe.html`,
regenerable from this spec) walked `src/` for modules whose interface is
nearly as wide as their implementation, and for decisions that sit in more
than one place. Six candidates, ranked. Each is a ticket in `issues/`.

Vocabulary: a **module** has an interface and an implementation; it is
**deep** when a lot of behaviour sits behind a small interface and **shallow**
when a caller must know nearly as much as the implementation does. A **seam**
is where an interface lives; an **adapter** satisfies it. **Locality** is what
maintainers gain (bugs concentrate in one place); **leverage** is what callers
gain (one implementation, N call sites). The deletion test: delete the module
-- if complexity vanishes it was a pass-through; if it reappears across the
callers it was earning its keep.

## What was measured

| finding | evidence |
| --- | --- |
| Nine private `return_frequency` / `return_sample_rate` fields | `app.h:134,322,348,356,568-569,618,822,864,1000` |
| Nine restore sites, each its own `retune_receiver(...)` | `view_gsm.c:645`, `view_lte.c:129`, `view_fm.c:1177`, `view_survey.c:439,895,1340,1677`, `overlay_calibration.c:458,500-506`, `overlay_scan.c:180` |
| TETRA decode orchestration written twice | `view_tetra.c:85-150` and `sdrprobe.c:1849-1890` |
| LTE "second MIB that agrees" written twice | `view_lte.c:558-565` and `run_headless` around `sdrprobe.c:2251` |
| `run_headless` | 760 lines, five branches (`sdrprobe.c:1995-2755`) |
| Only GSM shares per-block orchestration between window and headless | `update_gsm_sch` called from `sdrprobe.c:1557` and `:1962` |
| Config written from four call sites | `view_survey.c:691-693`, `overlay_calibration.c:673-675`, `overlay_settings.c:89`, `sdrprobe.c:2793-2812` |
| History loaded from disk five times in the survey view | `view_survey.c:257,259,577,613` + `survey_history_refresh` |
| Inline `y +=` still in views | `view_survey.c` 18, `view_fm.c` 7 (see `.scratch/panel-rows/`) |
| `struct fm_pilot` exposes the loop's internals | `fm_dsp.h:84-110`, ~20 fields incl. gains and smoother coefficients |
| Two pilot loops per FM station | `struct fm_rds_front` and `struct fm_audio` each own one |
| Stragglers in `struct app` | `gsm_analysis_mode` loose while `fm/lte/tetra/adsb.analysis_mode` are nested; `cal_return_sample_rate` outside `struct calibration` |

## Order

1. **Receiver lease** (`01`) -- smallest interface, nine callers, a bug class
   no check reaches. Tickets 02 and 04 borrow the receiver through it.
   **Done, 2026-09-07**: `src/receiver_lease.h` and `check-receiver-lease`,
   63 checks where there were none. All nine return fields deleted; the five
   helpers are in `view.h`. It corrected one place where the receiver was left
   on a frequency nobody chose, and it made abandoning an inner owner
   (`leave_gsm` and the calibration reset, both walking away from a band scan)
   an obligation that has to be written down rather than a habit that happened
   to work.
2. **Decode sessions** (`02`) -- the largest duplication; makes ADR-0012's
   layer 1 reachable for decode orchestration.
3. **Installation** (`03`) -- where ADR-0018 and ADR-0022 have to land anyway.
4. **Survey machine** (`04`) -- the Probe-side twin of 02.
5. **FM receiver interface** (`05`) -- speculative; analysis mode wants
   internals on screen and they must become readouts, not vanish.
6. **`struct app` carve-out** (`06`) -- not a project; the measure of whether
   01-04 worked.

## What this is not

- Not a change to any DSP answer. Every capture in `testfiles/` must decode
  identically before and after each ticket; `check-pipelines` is the gate.
- Not a uniform interface across technologies. ADR-0023 says modules share
  dependency and testability boundaries, not a function table; a session per
  technology may have its own shape.
- Not a runtime plugin system. Everything stays statically linked.

## ADRs in play

- ADR-0002 (single slot) and ADR-0014 (LTE rate) constrain 01: the lease is
  what restarts the worker on a rate change, and there is one such adapter.
- ADR-0007 (components never see `struct app`) is unaffected; the sessions
  sit between DSP and views, not inside `sdrgui_`.
- ADR-0012 is the reason for 02 and 04: orchestration that only
  `check-pipelines` can reach is layer 2 where it should be layer 1.
- ADR-0018 / ADR-0022 are the load 03 has to carry.
- ADR-0023 shapes 02 and 05.
