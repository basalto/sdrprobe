# 07 — Which control a key press reaches

Status: resolved
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

## Triage

Worth doing, and cheaper than the ticket feared. The chain reads raylib at each
step, but the *precedence* is a function of six flags -- which overlay is open,
which tab, whether a field has focus -- and only the "which key" part needs
raylib. Splitting those two apart is a restructuring of one function, not of
the loop.

## Answer

Done in `src/input_route.h`, checked by `tests/input_route_test.c`
(`make check-input`, 29 checks). It holds `struct input_state`,
`input_route()` returning one of six targets, and the predicates
`input_takes_typing`, `input_shortcuts_live`, `input_help_opens` and
`input_view_keys_live`. `enum active_tab` and `TAB_COUNT` moved here with it.
`run_gui()` snapshots the flags once a frame and switches on the target.

It found the inconsistency the ticket suspected. Quit-on-`q` excluded the
settings panel -- "losing half-entered settings to a stray letter is worse than
having to leave the panel first", says the comment -- but not the survey's
range and dwell fields, which take typed input in exactly the same way while
the rest of the view stays live. Typing a stray `q` while entering a frequency
quit the program. `h`, `s` and `c` had the same hole: each opened an overlay
from under a field that was being typed into. All four now go through
`input_shortcuts_live()`. A *click* on a button still works while typing, which
is right -- a click is unambiguous; only the letter standing for it is
suppressed.

The check that will keep it honest is the sweep: all 64 flag combinations,
each required to route to a target consistent with its own flags, and the view
keys required to be dead wherever anything is over the view. Swapping help and
settings in the chain fails it on 16 combinations; dropping the text-focus
clause fails three checks by name.
