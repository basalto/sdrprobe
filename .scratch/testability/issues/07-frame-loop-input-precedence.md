# 07 — Which control a key press reaches

Status: needs-triage
Blocked by: (none)

`run_gui` in `src/sdrprobe.c` resolves input through a fixed if/else chain:
settings overlay, then calibration overlay, then tab switch, then the overlay
buttons, then the per-tab handler. The order is a decision — it is what stops a
key typed into a text field from also zooming the chart behind it — and the
survey view's text fields made it load-bearing. `survey_editing()` exists
because of exactly this.

Untested and easy to break by adding a branch in the wrong place: a digit typed
into a dwell field must not reach the zoom keys; `h` must open help from every
view but not while editing; `q` must quit from everywhere except a text field.

Needs triage because the shape is not obvious. A `input_route(state) -> target`
enum over a plain snapshot (which overlay is open, which tab, whether a field
has focus) would be checkable, but the chain currently reads raylib directly at
each step, so this is a real restructuring rather than an extraction. It may be
worth less than tickets 01-06; decide before starting.

## Comments
