# 17 - The two overlays, as state and named commands

Status: **done** (2026-09-27/28). Both overlays ship; every acceptance
criterion met. The premise this ticket was written to correct turned out to
be the most valuable part of it.

## What this closes

Ticket 07's last two items and ticket 14's Phase 4 remainder: Settings and
Calibration in the browser. All seven views are done (07, 2026-09-27) and the
window reads their models too (16, same day); these two are what is left.

## The premise both those tickets carried, and it is wrong

Ticket 07 says the overlays are "mostly widgets and typed input, which is the
input half of the seam and is not solved", and quotes 161 raylib input call
sites, 11 of them `GetCharPressed()`. That figure is about the seven **views**
-- panning a chart, zooming, clicking a log row, hit-testing a marker -- where
immediate mode really does entangle input with layout, because the rectangles
a click is tested against exist only while drawing.

**It does not describe these two panels.** They are a form with a commit
button:

- `handle_settings_input()` only *stages*. It writes typed characters into
  `app->set.ppm`, steps `gain_choice` and `fft_choice`, toggles `remove_dc`
  and `auto_drift`. Nothing it does touches the receiver.
- `apply_settings()` is **one function** that validates the whole staged set
  and applies it, returning 0, or -1 with a sentence in `set.error`.
- Calibration is a machine with a start, a stop and a reported verdict
  (`calibration_gate.h`, `startup_session.h`), already driven headlessly by
  `--calibrate`.

So there is nothing here to reproduce. The mutations are already named,
callable functions with one writer each.

## The decision

**State travels read-only, as every other view's does. The mutations become
named commands**, through the `viewer_command` mechanism that `tune <hz>` and
`view <name>` already use. The browser never reproduces a click.

Rejected, and why:

- *Read-only panels with no commands.* The browser can already move the
  receiver (`tune`), so this would leave it able to retune but not to correct
  the crystal that makes the tuning wrong -- an asymmetry with no argument
  behind it.
- *Window-only, close 07.* Defensible, but the stated reason (typed input is
  unsolved) does not apply here, so the refusal would have to rest on
  something else, and nothing else was offered.
- *Reproducing the interactions.* There is no interaction to reproduce.

## Where the overlays go in a browser

They are **tabs**, not overlays. The window makes them full-screen modals
because it is one window and they are orthogonal to the tab bar (ADR-0008); a
browser page has a tab bar already and a modal over it would be a second
thing to dismiss. `receiver_state.screen` reports them when they are up, so a
Viewer follows the window into Settings the same way it follows it into a
decode view -- which is ADR-0027 unchanged.

## What travels

**`settings_state`** -- the staged set and what is applied, which are
different and both wanted: a reader needs to see that they have typed 32 into
a field whose applied value is still 0.

    staged:   ppm, gain (index + formatted), fft_size, remove_dc, auto_drift
    applied:  ppm, gain, fft_size, remove_dc, auto_drift
    dirty:    whether the two differ
    error:    the panel's own line, verbatim

**`cal_state`** -- what the gate holds and what it concluded.

    source:        fcch | lte | centroid | none, by name
    observed_ppm, sem_ppm, samples, locked
    reason:        the five-word vocabulary --calibrate already prints
    applied_ppm:   what is in force now, and for which receiver and site

## Commands

One row each in `viewer_command.c`'s table, which is what a check can walk:

    set ppm <n>        stage a correction
    set fft <n>        stage a transform size
    set gain <n>       stage a gain, by index
    set dc on|off      stage DC removal
    set drift on|off   stage the auto drift re-check
    apply              apply_settings(); the result carries its error
    calibrate gsm|lte|auto
    calibrate stop

`apply` is deliberately separate from each `set`, mirroring the panel: one
click on a stepper should not restart acquisition, and `apply_settings()`
validates the set as a whole -- a rejected PPM must not lose a transform size
the reader had just chosen, which is already why that function applies the
size first.

## Acceptance criteria

- [ ] Both panels' data comes from a view model checkable with `-lm` alone.
- [ ] The **window** reads the same models (ticket 16's rule).
- [ ] Every command is a row in the table, not a branch.
- [ ] A command that fails reports the same sentence the panel shows.
- [ ] `receiver_state.screen` names both, and `viewForState()` follows.
- [ ] `make screens` unchanged; `check-web-layout` covers both tabs.
- [ ] Both audits in `CLAUDE.md`, and `VIEWER_STREAM_*` paced in
      `viewer_stream_pacing()` -- ticket 12's enumeration fails otherwise.

## Not in scope

- The **startup form**. It is a third overlay and a different thing: it runs
  before a session exists, on a receiver nobody has identified yet, and
  ADR-0024 makes it opt-in. A browser attached to a running server has
  already missed it.
- The help overlay and the band-scan overlay.
- Reproducing chart interactions in the views (07's real unsolved half).

## Comments


**2026-09-28 -- done, in two commits.** `37855b6` (Settings), `d0bbb6b`
(Calibration).

### The premise, which is why this ticket existed

Tickets 07 and 14 both said the overlays were blocked on unsolved typed
input, quoting 161 raylib input call sites. **That figure is about the seven
views.** These two panels are a form with a commit button:
`handle_settings_input()` only stages, `apply_settings()` validates and
applies the whole set at once, and calibration is a machine with a start, a
stop and a verdict. There was nothing to reproduce, and the work took two
commits rather than the ticket nobody wanted to open.

The lesson generalises past this ticket: **a blocker quoted from another
ticket's measurement is a number about something else until somebody checks
which thing it measured.**

### What each overlay turned up

**Settings.** The staged set and the applied one are different facts and
both travel -- the window shows the difference by having a text field in
front of the reader, and a second reader with no field of its own would be
told one number and have no way to know which. A field that does not parse
counts as **dirty**, because "3-" is a state a reader can be in and a panel
that looked settled over it would be lying.

**Calibration.** Three decisions, all of them previously unreachable:

- The **source** was spelled in two ternary chains -- `headless_run.c` and
  the overlay's debug-log line -- and shown on screen nowhere, although
  ADR-0004 makes it the difference between a correction worth trusting and
  one belonging to neither reference.
- **Which clause of the gate is unsatisfied** was named nowhere at all.
  `calibration_is_stable()` returns one bit, so "it needs four more seconds"
  and "the scatter is too wide to ever settle" looked identical. The window
  now says which.
- **Two references are not one reference twice.** `have_both` is its own
  field because reporting a gap of `0 - 32` as a disagreement would be the
  most misleading number on the screen.

And a latent division by `expected_hz` with no guard, reachable only through
an invariant that line was not the right place to rely on.

### The decision the ticket asked for, made

**`calibrate` starts and stops; it does not apply.** A calibration writes a
standing fact about this receiver at this site (ADR-0018, ADR-0022), so
applying it is `set ppm` then `apply` -- one more deliberate act. The
browser's button stages the suggestion and says where to commit it.

### Two checks corrected assumptions during the work

- `check-viewer-session` failed until the on-time stream count went 2 -> 4.
  That is exactly the enumeration ticket 12 built, working: a panel that
  changes when somebody *types* must not wait for a sample block.
- `check-web-layout` failed on "has a canvas" and "its biggest chart
  dominates". **Not every view shows a measurement**, and asserting they all
  do has now been wrong twice -- ADS-B's table, and a form. A form is held
  to the scroll assertions instead, which is what "all its controls are
  reachable" means for one.

### Numbers

`check-settings-view-model` 24, `check-calibration-view-model` 31, both
`-lm` alone. `check-viewer-command` 53 -> 101. `check-viewer-link` 408 ->
449. `check-web-layout` 107 -> 113, nine tabs. Gate **90 suites, 23022
checks**.

### What is still window-only, and named rather than implied

The **startup form**, deliberately: it runs before a session exists, on a
receiver nobody has identified yet, and a browser attached to a running
server has already missed it (ADR-0024). The help and band-scan overlays.
And the seven views' chart interactions -- panning, zooming, hit-testing a
marker -- which is ticket 07's real unsolved half and the thing those 161
call sites actually describe.
