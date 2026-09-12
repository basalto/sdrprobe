# 06 - One walk, two adapters: fold --calibrate onto the machine

Status: resolved, 2026-09-12

`run_headless()` already contains this walk. `sdrprobe.c:2435-2570` scans a
band, takes the strongest cell, starts the calibration, prints every residual,
applies the gate's own reasons and reports a verdict. Ticket 01 writes the
same sequence for the window.

Two copies of one walk is how `--lte-chain` and `probe-lte-chain` drifted
twice -- the repeated-message rule written out in both, and
`lte_cell_search_all` reaching a committed capture only after the live-only
path had become able to return **fewer** cells than the search it generalises.
`lte_chain_analysis.{c,h}` is what that cost, and this ticket is the same
lesson applied before the drift rather than after it.

## What to build

`run_headless()`'s `--calibrate` block becomes a **second adapter** over
`startup_session`: it drives the machine, and its printf loop formats what the
machine decided. Exactly the relation `survey_report.c` has to
`survey_session`, and `--lte-chain` has to `lte_chain_analysis`.

The verdict vocabulary is already shared by construction, because ticket 01
took it from here: `locked`, `no-cell`, `too-few-measurements`, `sem-too-wide`,
`timeout`.

**And it gains the GSM-first search on the command line** -- as a new value,
not by changing an existing one. `--calibrate 1` still means GSM at a given
ARFCN and `--calibrate 2` still means LTE; `--calibrate auto` is the machine's
own GSM-then-LTE search, which until now has been reachable only by launching
a window.

## The refusal that has to survive

The headless path takes **no lease**, deliberately, and the comment says why:
a headless calibration exits when it is done, so there is nobody to give the
receiver back to. The window borrows because a screen outlives the
measurement. The machine does not own the lease either way -- it says what
tuning it wants -- so this stays a property of the adapter, and the comment
stays.

## Acceptance criteria

- `--calibrate 1 --arfcn N`, `--calibrate 2 --earfcn N` and `--calibrate 2
  --calibrate-band B` produce **byte-identical stdout** to the current binary
  over the same input, the wall clock apart. That is the whole test of a
  refactor that is supposed to change nothing.
- `--calibrate auto` works on air and is documented.
- `make check-options` covers the new value and its rejections.
- `make check` passes.

## How to measure it, since a green suite will not

A capture cannot be calibrated, so no check here runs this path at all. Run
the old binary and the new one alternately against the same live cell -- build
the current `master` binary aside first -- and diff the `cal-measure` and
`calibrate-result` lines. The survey extraction was byte-identical on every
capture and on screen while a settle was disabled; one live line is what
caught it.


## Comments

**2026-09-12 -- this ticket's acceptance criterion contradicts ticket 01, and
the ticket is wrong rather than the code.**

It asks for **byte-identical stdout** from `--calibrate` before and after the
fold. That cannot be delivered, because ticket 01 made a deliberate decision
the other way: `startup_session`'s GSM measurement **refuses the centroid
fallback** that `update_calibration_measurement()` allows. The overlay serves
an operator watching a chart, where a reading that keeps moving is worth
having; the startup machine files a correction unattended, and a centroid
residual is a different measurement of a different thing (ADR-0004).

So folding `--calibrate` onto this machine would silently change **what it
measures** on any run where the tone drops out and the centroid takes over --
and the byte-identical criterion, written to prove the refactor changed
nothing, would be the first thing to fail, correctly.

Three ways out, and the ticket needs to choose one before anyone starts:

1. **Give the machine a mode.** `startup_session` takes whether the centroid
   may contribute, the headless path says yes, the startup form says no. One
   machine, one flag, and the flag is a real difference rather than a
   convenience -- which is the test `.scratch/lte-chain` applied to
   `lte_session` and failed.
2. **Change `--calibrate` deliberately**, dropping the centroid there too, and
   accept that the output moves. That is a PATCH under ADR-0016 only if the
   centroid-backed answer was never worth having -- which is arguable, since
   the gate already treats it as the weakest source, and would need measuring
   rather than asserting.
3. **Leave the two as they are** and accept the duplication this ticket was
   written to prevent. The honest cost: `start_calibration()` and
   `startup_session_begin()` both know how to point a receiver at a reference,
   and a change to one will not reach the other.

Option 1 is the one to take, but it is a design decision rather than a
mechanical fold, so it is not started. What this ticket must **not** do is
adopt its own acceptance criterion by quietly re-adding the centroid to the
startup path: that would make the two agree by undoing the decision that made
them differ, with a green diff to show for it.

The two other halves of the ticket stand and are unaffected: `--calibrate auto`
(the GSM-then-LTE search from the command line) and the no-lease comment.


## What was built

Option 1 from the comment above, and it turned out to uncover a shipped bug.

- `struct startup_session` gains **`allow_centroid`**, a real difference
  between two callers rather than a convenience: the startup form files a
  correction unattended and refuses a centroid residual (ADR-0004), while
  `--calibrate` serves an operator reading every residual and allows it.
  `startup_block` gains a `scratch` workspace for the centroid estimator, the
  same arrangement `survey_block` uses -- the machine allocates nothing.
- **`startup_session_measure_gsm()` / `_measure_lte()`**, for a channel the
  caller named, with no search -- which is what `--calibrate gsm --arfcn N`
  does -- plus `startup_session_set_budget()`.
- **`--calibrate auto`**, the machine's own GSM-then-LTE search from the
  command line. A new value, not a change to what `gsm` and `lte` mean.
- `run_headless()`'s block now drives the machine and formats what it decided.
  The `--calibrate-band` LTE scan is unchanged and still the decode view's own.

## What the measurement found, which a green suite did not

Old binary and new, alternated three times each against the same live cell
(ARFCN 113, 25 s budget):

| | centre_ppm | measurements | suggested |
| --- | --- | --- | --- |
| **before** | -3.29, **-48.59**, **-44.42** | 876, 887, 868 | 35, **81**, **76** |
| **after** | -2.85, -2.13, -3.74 | 123, 123, 123 | 35, 34, 36 |

The old headless path called `update_calibration_measurement()` **on every
loop iteration** rather than once per block, so each block's residual was
recorded five to twenty times running -- the raw output shows `-50.76` five
times, then `-49.58` seven times, and one value twenty times.

That is not an inflated counter. The residual ring holds 64 values and the
gate reads a **median and a MAD** over it, which exist precisely to resist a
peak that hops for a few blocks -- and duplicates defeat exactly that:
sixty-four slots filled by six distinct blocks make the median the median of
six. Two of three runs locked on a run of outliers and suggested a correction
**45 ppm wrong**, with `locked` printed beside it.

**The window was never affected**: `run_gui` calls the same function from
inside `if (spectrum_updated)`. So this was precisely the drift between two
copies of one walk that this ticket was written to prevent, and it had already
happened. `test_a_block_is_measured_once` pins it now.

## And a second gate the measurement forced

`--calibrate auto` chose **ARFCN 63** over 113 -- it is louder, and
`scan_select_bcch()` takes the loudest channel carrying a tone. It suggested
+57 where 113 suggests +35.

The first hypothesis was a false FCCH -- a coherent line that is not a
broadcast carrier's tone -- since `GSM_FCCH_SEARCH_HALF_HZ` is 50 kHz and the
detector reports any line inside that. (It was first written up as a
*refarmed* carrier, which was wrong twice over: there is no GSM refarming
here, and the hypothesis was refuted anyway.) So the machine gained a **`STARTUP_VERIFY_GSM` phase**: before
measuring, the chosen channel has to produce a parity-valid synchronisation
burst, which a carrier that is no longer GSM cannot do however coherent its
spurs. A rejected candidate is dropped and the next best takes its place.

**It confirmed ARFCN 63 rather than rejecting it** -- BSIC 42 -- so the
hypothesis was wrong and the disagreement is something else. That is
`08-two-cells-one-crystal.md`, where the 2x2 at two applied corrections shows
it is a constant 13 kHz bias rather than a crystal disagreement. The
verification stays regardless: it is the difference between "a tone" and "a
base station", it is cheap, and a null from a gate that can fire is worth
having.
