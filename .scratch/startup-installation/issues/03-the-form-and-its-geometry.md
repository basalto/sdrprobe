# 03 - The startup form: geometry, drawing, routing

Status: resolved, 2026-09-12

The overlay. Ticket 01 decides, this one draws and turns clicks into intents.

## What to build

**`src/startup_layout.h`** -- every rectangle, and **all of them, not some of
them**: a header modelling half a screen puts a green tick over the half it
does not model, which is worse than having none. `panel_rows.h` owns the row
positions and the label/value columns; pass this panel's own row step rather
than borrowing another view's, and let the capacity refuse a row that would
draw past the bottom edge.

The form, top to bottom:

| field | widget | source of its default |
| --- | --- | --- |
| Site | text + combo | `config.sites[]`, `config_remember_site()` |
| Antenna | text + combo | `config.antennas[]`, default `telescopic` |
| Receiver label | text | `--receiver-label`, or empty |
| Gain | stepper | `device_gain_option_*()`, formatted by `device_gain_format()` |
| Start frequency / band | text or picker | `--frequency`, or what the receiver has |
| LTE band (fallback only) | picker | `view_lte_bands()`, `startup_band` for this site |

Then the **calibration section**, which is the primary surface for the result:
the phase in words, the scan's progress, each residual as it arrives, and the
verdict with its reason. The overlay is modal, so there is a whole screen to
report into and a popup over it would say what the screen already says.

Then **Continue**, disabled while a measurement runs (ticket 01 decides that,
`startup_may_commit()`), and **Skip calibration** beside it.

**`src/overlay_startup.c`** -- draws it, reads input, and decides nothing. If
it computes a threshold, chooses a range, maps a pointer to an index or
advances the machine, that part comes out into `startup_session` with a name.

**Routing.** `input_route()` gains `INPUT_TARGET_STARTUP`, between Help and
Settings: Help stays outermost because it can be raised over anything, and the
form outranks Settings because Settings is reached through it. `open` nests
inside the startup state like `cal.open` and `help.open` do -- do not add a
flag to `struct app`.

`--view startup` opens it (a new `START_VIEW_STARTUP`), so it can be
screenshotted. `START_VIEW_CALIBRATION` exists for precisely this reason and
its comment says what shipped without it.

**Receiver label, and why it is on this form.** A receiver with no identity
cannot have a calibration profile at all (ADR-0018), and many of these dongles
ship with no serial or a shared one. When `installation_identified()` is
false, the form says so on the label field rather than silently measuring a
correction it will not be able to file.

## Acceptance criteria

- `make check-layout` covers every rectangle in `startup_layout.h`, and
  `check_panel_rows()` walks its panels. Adding three to a capacity must fail it.
- `make check-input` covers the new target and the precedence either side of
  it: Help over the form, the form over Settings, and the form over the tabs.
- `check-debug-log` pins the new target name against `input_route.h`'s enum.
- `make screens NAMES="startup"` renders it, and somebody looks at it -- at
  1100x720 and at 640x400. A change that draws is not finished until somebody
  has looked at it.
- `startup_layout.h` is in `APP_HDR`, and `check-startup-layout`, if it gets
  its own rule, is in `CHECK_UNITS`.

## Not in scope

- Deciding anything. If this file grows a threshold, it is in the wrong file.


## What was built

`src/startup_layout.h` and `src/overlay_startup.c`; `INPUT_TARGET_STARTUP` and
`startup_open` in `input_route.h`, named in `debug_log.c`; `START_VIEW_STARTUP`
and `--view startup`.

**`check-input`'s `state_of()` did not initialise the field it was given**, and
adding one to `struct input_state` left it reading stack garbage -- in a sweep
that reads every field of every combination, which makes the exhaustive test
nondeterministic while still passing most runs. It is `memset` first now, and
the sweep runs 192 combinations rather than 64.

**The first screenshot showed the report panel swallowing the form**: taking
"whatever is left" gave it two thirds of the screen to draw one line into. The
panel is sized to its content and centred now. And `check-layout` then failed
at 640x400, where the comfortable metrics need 502 pixels of a 320-pixel
panel -- there are two sets now, and the check found it rather than an
operator.
