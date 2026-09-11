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
   **Done, 2026-09-09**: `src/survey_session.{c,h}` and
   `check-survey-session`, 158 checks where the sweep, the confirmation pass,
   the watch and the measurement had none. The session says where it wants the
   tuning and never touches the receiver; it holds a site history and never
   reads a file. It found four faults, three of them differences between the
   window's copy and the headless one that no check could see: the headless
   sweep folded stale blocks, the watch reported its first sweep's carrier
   count as zero, the two `# confirm` headers disagreed with their own rows,
   and the window handed a *rebuilt* spectrum where the headless path handed a
   peak hold. Two the extraction itself introduced and measurement caught: the
   settle timed from the retune *request* rather than from the tuning, and a
   step that could only end when a block arrived. `update_survey()` is now
   called every frame, because a look counts blocks and a step counts time.
   Three more the extraction introduced and review caught, all the same wrong
   reset: a no-receiver select that cleared the sweep, a stop that left a
   confirmation pass running in front of a view that waits on one, and a Reset
   zoom that restored the kept sweep and then emptied it. That last one took
   the narrowing snapshot into the session as well
   (`survey_session_keep`/`_restore`), which `.scratch/testability/` ticket 01
   had named as an operator-reported fault with no check -- and the restore
   turned out never to have put the sweep's **plan** back, so a wide sweep's
   peaks were being read with a narrow sweep's bin width. Both capture surveys
   and the survey screen are byte-identical.
5. **FM receiver interface** (`05`) -- speculative; analysis mode wants
   internals on screen and they must become readouts, not vanish.
6. **`struct app` carve-out** (`06`) -- not a project; the measure of whether
   01-04 worked. **Closed 2026-09-11 at 37 fields**, down from 85: twenty
   containers, eight handoffs, ticket 09's three parked for the second
   receiver, and six that `sdrprobe.c` alone reads and that are its own
   process lifecycle. The two largest reductions came from `11` and `10`,
   neither of which was opened to shrink this record -- they asked what owns a
   field rather than where it should live. The earlier measurement, for the
   record: **2026-09-09**: `app.h` is 898 lines against the
   1019 the ticket cites, and `struct app` holds **85 fields, 30 of which are
   handoffs or per-view containers**. So 01-04 worked, and the ticket cannot
   close: its premise -- "each straggler belongs to one of 01-04" -- is wrong,
   because the 55 survivors cluster into calibration (16 fields, five with a
   single reader, beside a `struct calibration` that already exists), the GSM
   band scan (8, same shape), the receiver's applied state (7 fields across
   nineteen files, three of them read more widely than the sample buffers, a
   module that never had a name) and the
   spectrum display (16, and almost all of it is `sdrprobe.c` computing for
   `view_scope.c`, which is what a handoff looks like). Those become tickets
   07, 08 and 09; the spectrum cluster is left alone. The one direct fix was
   `gsm_analysis_mode`, the last analysis mode not nested in its own view.

7. **Calibration state into `struct calibration`** (`07`) -- sixteen fields
   beside a struct that already holds one of them, five with a single reader.
   Opened by 06's audit, not by the original review.
   **Done, 2026-09-09**: `struct app` is **70 fields, down from 85**, and the
   boundary had been running through the middle of coherent pairs --
   `drift_health_prev` inside while `drift_health` was outside,
   `gsm_cal_expected_hz` inside while `gsm_cal_valid` was outside. The
   ticket's hypothesis was **false for one field, and the reason was a
   fault**: `retune_receiver()` -- which every screen in the program uses --
   wrote all five of its failure messages into `calibration_status`, prefixed
   "Calibration", so a survey step that would not tune reported its reason on
   the calibration overlay's status line, mislabelled, on a screen nobody was
   looking at. `view_lte.c` already read that buffer by hand to find out why
   1.92 MS/s had been refused, which is the tell. It is `app->receiver_error`
   now, a real handoff, and the two calibration paths that had been depending
   on the shared buffer -- returning -1 with no status and letting the
   headless report print whatever the retune had left there -- quote it
   deliberately.
8. **Band scan state into `struct band_scan`** (`08`) -- the same edit,
   smaller: eight fields beside a struct with one reader.
   **Done, 2026-09-09**: `struct app` is **62 fields, down from 70**, and 07's
   question -- *is this field doing the job its name claims* -- corrected two
   of the eight. `scan_selected_arfcn` is **not the scan's**: it is the GSM
   view's inspected channel, set by `gsm_tune_selected()` and by `--arfcn`
   with no scan involved, read by a recording's sidecar, and fourteen of its
   twenty uses were already in `view_gsm.c` -- so it is `gsm.selected_arfcn`,
   beside `gsm.selected_hz`, which is the same fact in hertz written on the
   next line every time. And `scan_step_count` was a copy of
   `bandscan.plan.step_count` set once and never changed, so it is **gone**
   rather than moved. `bandscan.open` follows `cal.open` and `help.open`;
   `settings_open` is the last overlay flag still loose. A live scan still
   autoselects and decodes -- it landed on ARFCN 113 and read BSIC 38, MNC 06,
   cell 16134, which is exactly what `gsm_arfcn_113.bin` reads. A follow-up
   the same day took `settings_open` and `settings_error` into
   `struct settings_panel`, leaving **60 fields** and all four overlay flags
   nested -- and found the same fault a third time: the acquisition lifecycle
   was writing its failures into `settings_error`, which left 07's own
   `receiver_error` **stale** on the path it was created for, since
   `retune_receiver()` stops and restarts acquisition.
9. **Name the receiver's applied state** (`09`) -- seven fields, nineteen
   files, and no home. `needs-triage` and probably wants the second
   receiver first: a "what is applied" struct designed against one device is
   the mistake `.scratch/device-model/` exists to avoid.

## Second review, 2026-09-10

The first review's work is now on `master`, so this review applied the deletion
test to what was built rather than repeating the old candidates. Receiver
lease, the five technology sessions, installation, survey session, device
profile and device backend are earning their interfaces and should stay.

Three new candidates remain:

10. **Receiver runtime** -- stop/start, retune rollback, lease ordering,
   applied state, spectrum invalidation and receiver errors still occupy one
   application-root path in `sdrprobe.c`. Strong, but `needs-info` until the
   UHD adapter in `.scratch/device-model/issues/07-a-second-backend.md` gives
   the interface its second receiver. This absorbs the question in 09 rather
   than adding another applied-state container beside it.
11. **Acquired signal frame** -- `process_block()` composes checked DSP
   primitives into more than twenty loose fields, but that composition has no
   direct check. Strong and `ready-for-agent`.
12. **Survey record** -- `survey_store` includes `app.h` and combines record
   meaning with JSON and filesystem policy. Form one plain record for the
   window, headless text and JSON adapters. Worth exploring and
   `ready-for-agent`.

The report is `/tmp/architecture-review-20260910-095531.html`.

## Not from either review

13. **A legacy baseline nobody can claim** -- ADR-0022 promises an unassigned
   legacy site history that the operator may assign to a receiving setup, and
   neither half is implemented: nothing assigns one, and nothing opens one.
   Opened 2026-09-10 from a dead-code audit rather than from a review, and
   numbered 13 because the second review had already claimed 10-12.
   `needs-triage`, and the honest close may be to amend the ADR.

## Third review, 2026-09-11

Tickets 11 and 12 pass the deletion test after implementation. Ticket 10 now
owns and checks the receiver transition and rollback, while applied-state and
lease ownership deliberately wait for observed UHD semantics. The review
report is `/tmp/architecture-review-20260911-123343.html`.

One new strong candidate survived the code check:

14. **One LTE chain analysis, live or captured** -- the live `--lte-chain`
   path and `probe-lte-chain` both walk cell search, broadcast hypotheses and
   run-level evidence, but do so in separate implementations. Share the
   public per-block analysis and accumulated result while keeping root scores,
   timing nudges, CRC distance and repetition controls in the white-box
   capture probe. Strong and `ready-for-agent`.

The wider headless sample driver remains worth exploring but is not ticketed
here: its loop variations may make an interface as wide as its implementation.
Magnitude binning, a uniform session dispatcher, overlay management and a
shared analysis-mode module were rejected as false depth or conflicts with
ADR-0023.

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
