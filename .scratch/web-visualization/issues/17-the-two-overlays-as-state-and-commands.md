# 17 - The two overlays, as state and named commands

Status: ready-for-agent

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
