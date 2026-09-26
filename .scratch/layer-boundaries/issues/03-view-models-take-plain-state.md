# 03 - The data contracts stop taking `struct app`

Status: needs-triage
Blocked by: 01

## The problem

The view models are this program's data contracts -- the one object the
window and the browser both read -- and every one of them is built from the
god-struct:

```c
void fm_view_model_build(const struct app *app, struct fm_view_model *out);
void survey_view_model_build(const struct app *app, struct survey_view_model *out);
void scope_view_model_build(const struct app *app, struct scope_view_model *out);
```

What each actually reads is narrow:

- **FM**: `app->fm` -- the session, the audio state, the multiplex spectrum.
- **Survey**: `app->survey.session`, plus the four tuning facts
  `survey_tuning_from()` already gathers (`applied`, `device`, `remove_dc`,
  `installation`).
- **Scope**: `app->frame`, `app->applied`, `app->device`, `app->sv` -- and
  `app->tab` and `app->decode`, which are not the Scope's at all (below).

Taking `struct app` means a contract depends on everything `struct app`
depends on, and a check of it has to build one -- `CLAUDE.md` records a
suite that allocated a whole `struct app` to write one file, which is how
`survey_record` came to exist.

## What to do

1. **Each builder takes the state it reads**, never `struct app`. The
   narrowest owning structs, not a parameter per field: an FM model from the
   FM view's session-side state, a survey model from `struct survey_session`
   and `struct survey_record_tuning`, a Scope model from the frame, the
   applied tuning, the device profile and the Scope's own data. Callers
   become the adapters: `viewer_session.c` and the views pass their pieces
   in.
2. **Move `sdrgui_survey_peak_mark()` into the model layer.** It is the
   precedence that turns a candidate's flag word into one of four marks -- a
   decision, shared by the window's chart and the wire, living in the
   component header ADR-0007 says decides nothing. `viewer_link.c` includes
   `sdrgui.h`, and so raylib, only to call it. The candidate view should
   carry its mark already decided, the way `fm_view_model` carries
   `reading_tone`; `sdrgui` then takes the mark as plain data. That is what
   lets `check-viewer-link` drop raylib's cflags.
3. **Enums cross the wire by name, never as ordinals.** This is the rule
   `web-visualization/15` shows the cost of: the browser re-declared
   `enum sdrgui_peak_mark` in the wrong order and drew receiver-like and
   empty candidates swapped, green throughout. `shape` already travels by
   name (`survey_shape_name()`); `mark`, `seen`, `reading_tone`, `tab` and
   `decode` do not. Each gains a `*_name()` beside its enum, in the model
   layer, and a check pins every value's name.
4. **Take the shell's state out of the Scope's contract.** `receiver_state`
   -- tab, decode, tuning, rate, ppm, generation -- is built from
   `scope_view_model` because that was the only model there was. It is the
   shell's, not the Scope's, and every view reads it. A small
   `receiver_view_model` of its own, and the Scope model loses `tab` and
   `decode`.

## Acceptance criteria

- [ ] No `*_view_model_build()` takes `struct app`; `grep 'struct app'
      src/*_view_model.h` finds only the forward declaration, or nothing.
- [ ] `check-fm-view-model`, `check-scope-view-model` and
      `check-survey-view-model` build their inputs without a `struct app` at
      all -- no 9 MB static, no `zero_app()`.
- [ ] `viewer_link.c` includes no `sdrgui*.h`, and `check-viewer-link` builds
      with `-lm` and no raylib cflags.
- [ ] No enum travels as an integer; `web/` indexes no table by an ordinal.
- [ ] The survey's four marks are pinned by name in a check, including the
      receiver-like/empty pair.
- [ ] `make check`, `make check-pipelines`, warm `make screens NAMES="scope
      survey fm"`, `make check-web-layout`, a live `server` run.

## Take into account

- **`web-visualization/15` should not wait for this.** It is a display bug a
  reader acts on wrongly today, and its fix -- the mark sent by name -- is a
  subset of item 3. Land it first; this ticket then generalises it.
- **The five decode views still to come** (`web-visualization/07`) should be
  written in this shape from the start. Converting FM's model is one commit;
  converting six is six.
- **`survey_tuning_from()`** already exists and is exactly the "narrow
  input" pattern for the survey. Reuse it rather than inventing a second.
