# 03 - The data contracts stop taking `struct app`

Status: **resolved, 2026-09-28**. All four items were done on 2026-09-27 and
the property has held through six more view models since, which is the
stronger claim: it was not a one-off clean-up but a rule new work follows.

Re-measured on closing -- **eleven** view models, not the five this ticket
was written about:

    adsb  calibration  fm  gsm  lte  receiver
    scope  settings  srd  survey  tetra

- **No builder takes `struct app`.** `grep -rn "_view_model_build(const
  struct app" src/` returns nothing.
- **All eleven checks link `-lm` alone.** The ticket asked for five to be
  genuinely raylib-free; every one of them is, including the six added
  afterwards (`web-visualization/07`, `/17`).
- **No enum crosses the wire as an integer.** `web-visualization/15` is the
  reason -- the survey's marks drew swapped for months -- and every enum
  added since travels by name with a `*_name()` beside it:
  `gsm_sch_reading`, `srd_frame_kind`, `srd_modulation`, `srd_readiness`,
  `adsb_readiness`, `calibration_source_name()`, `calibration_health_name()`.
- **The shell's state is a type of its own**, and it grew correctly rather
  than being worked around: `struct scope_view_model_in` took two more
  fields for the overlays (`settings_open`, `calibration_open`) when an
  overlay became a screen.
Blocked by: 01 (done)

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

- [x] No `*_view_model_build()` takes `struct app`; `grep 'struct app'
      src/*_view_model.h` finds only the forward declaration, or nothing.
- [x] `check-fm-view-model`, `check-scope-view-model` and
      `check-survey-view-model` build their inputs without a `struct app` at
      all -- no 9 MB static, no `zero_app()`.
- [x] `viewer_link.c` includes no `sdrgui*.h`, and `check-viewer-link` builds
      with `-lm` and no raylib cflags.
- [x] No enum travels as an integer; `web/` indexes no table by an ordinal.
- [x] The survey's four marks are pinned by name in a check, including the
      receiver-like/empty pair.
- [x] `make check`, `make check-pipelines`, warm `make screens NAMES="scope
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


## Item 2 done, 2026-09-27 -- the mark leaves the components header

`src/survey_mark.h`: `enum survey_peak_mark`, the four suspicion flags, the
precedence that turns a flag word into one of the four (`survey_mark_of()`)
and its name (`survey_mark_name()`). Out of `sdrgui.h`, which is the
components header ADR-0007 says takes plain data and decides nothing -- and
this is a decision, the precedence in which "a closer look found nothing"
beats "on the receiver's own comb", because as `CLAUDE.md` puts it, "there
is nothing here" is what a reader acts on.

`struct survey_candidate_view` carries `mark` now, decided by
`survey_view_model_build()`. Both readers take it: the chart draws the mark
it is handed, and `survey_state` sends its name. That is what
`web-visualization/15` was about -- two readers deriving a mark from one flag
word disagreed for months.

**`viewer_link.c` includes no GUI header any more, and `check-viewer-link`
builds with no raylib**, proven by building the whole suite with a `#error`
raylib.h ahead of the real one rather than by dropping the flag and seeing
it pass.

One check had to change rather than the code: the suite's fixture set
`candidates[0].flags` by hand, and the publisher now reads `mark`. It derives
the mark through `survey_mark_of()` instead of naming one, so the fixture
still exercises the precedence rather than asserting around it.


## Item 3 done, 2026-09-27 -- no enum crosses the wire as an integer

Four were still travelling as ordinals; all four travel as names now.

- **`seen`** -> `site_seen_name()`, which already existed for the window's
  own candidate list. It moved from `site_history.c` into the header as a
  `static inline`, so a reader that only needs to *spell* a verdict does not
  link the whole history -- the same reason `survey_shape_name()` and
  `survey_mark_name()` are inline. (I first wrote a second copy of it in the
  header without noticing the original; the compiler caught the
  redefinition.)
- **`reading_tone`** -> `fm_reading_tone_name()`.
- **`tab` and `decode`** -> a single **`screen`** name. This is the one that
  was more than a rename: the browser matched `state.tab === v.tab &&
  state.decode === v.decode`, so every view declared `tab: 2, decode: 0` --
  two of this program's enums re-declared as numbers in a second language,
  which is exactly the shape that drew the survey's marks swapped for
  months. The vocabulary already existed and is the same one `view <name>`
  uses: "survey", "scope", or the technology on the Decode tab. It matches
  each browser view's own `id`, so `viewForState()` is now a lookup by name
  and the three views lost their `tab`/`decode` literals entirely.

`check-scope-view-model` walks **every** screen -- all six decode kinds plus
the two tabs -- rather than sampling, because a decode kind added to the enum
without a name here falls through to "decode" and nothing else would say so.
It also pins that the survey's name does not change with whatever decode kind
was last remembered, and that an out-of-range kind is named rather than
indexed past the table.

Verified on the wire (`screen=fm`), in a real browser (`check-web-layout`,
25 checks), and with the headless survey byte-identical.


## Item 1 done, 2026-09-27 -- no builder takes `struct app`

- **FM** takes `const struct fm_view *`. It read exactly one member.
- **Survey** takes `const struct survey_session *` and the
  `struct survey_record_tuning` that `survey_tuning_from()` already gathers
  -- which is what this ticket said to reuse rather than invent.
- **Scope** takes a `struct scope_view_model_input`: frame, scope view,
  applied tuning, device profile, tab, decode. A struct rather than six
  parameters, on the precedent `signal_frame_input` already sets here.

**`survey_reading_clock()` and `survey_tuning_from()` moved out of
`survey_view_model.c` into `survey_runtime.c`.** They are the adapter between
the application and the contract -- they read `struct app` -- so the model
keeping them was what kept the model depending on the application. That was
the last thing holding raylib into that suite.

**`frame_advance.c` includes `runtime.h` instead of `view.h`**, which was the
last raylib path into the per-block step. `process_block()`'s declaration
moved with it.

### The five checks ticket 01 predicted are all raylib-free now

`check-fm-view-model`, `check-scope-view-model`, `check-survey-view-model`,
`check-frame-advance` and `check-viewer-link`. Each verified by building the
whole suite with a `#error` raylib.h ahead of the real one -- never by
dropping the flag and watching it pass, since the system header sits in
`/usr/include`.

The three model suites also build their inputs directly: no `struct app`, no
`zero_app()`, no nine-megabyte static to set four fields. The three that
still take raylib cflags are `check-layout`, `check-geometry` and
`check-row-list`, which test GUI geometry and use `Rectangle` on purpose.

Verified: `make check` green, `check-web-layout` 25 checks, the headless
survey and the FM decode byte-identical, a live `server` still sending
2048-bin spectra.

## Item 4 done, 2026-09-27 -- the shell's state is a type of its own

`src/receiver_view_model.{c,h}`: `struct receiver_view_model` is the screen's
name, the applied tuning, rate and ppm, ADR-0027's tuning generation and the
container's full scale. `receiver_view_model_build()` takes a
`struct receiver_applied *` and a `struct device_profile *` and nothing else,
so `check-receiver-view-model` sets two plain structs where the Scope's suite
still needs a whole sample block -- 20 checks, `-lm` alone, milliseconds.

`viewer_link_publish_receiver_state()` takes the receiver model now, which is
the point of the split: `receiver_state` is what *every* browser view reads,
and it was being built out of a struct named for one of them. The Scope's
model **embeds** one rather than restating it -- `svm->receiver.center_hz` is
what the frequency axis is drawn against and `svm->receiver.tuning_generation`
is what stamps every binary message -- because the charts genuinely need those
numbers and two copies that agree today is the shape this repository keeps
paying for.

**`tab` and `decode` are deleted rather than moved.** The grep that decided it
found zero readers of either, on both sides of the wire: item 3 replaced them
with `screen` on the wire, `viewForState()` matches on the name, and nothing
in `src/` had read them since. A field moved is a field still to be read; a
field with no readers is a deletion, and the ticket asked for a move because
it was written before item 3 landed.

Two things came out of it that the ticket did not ask for:

- **`scope_screen_name()` is `receiver_screen_name()`** and lives with the
  model whose field it fills. It was in `scope_view_model.c` for the same
  reason the fields were -- that was the only model there was.
- **The name table is coupled to its enum at compile time.** It is indexed by
  `enum decode_kind` and was six strings with nothing tying them to it, so a
  seventh technology would have been named `"decode"` by a silent fall-through
  in the one function whose output a browser matches on. A
  `typedef char ...[(DECODE_SRD + 1 == DECODE_SCREEN_COUNT) ? 1 : -1]` makes
  that a build error -- the same device `sdrprobe.c` already uses for
  `input_route.h`'s mirrored value. The suite's exhaustive walk stays: the two
  failures are different, a kind nobody named against one named wrongly.

Verified: `make check` green, **80 suites and 22016 checks** (79 before);
`make check-web-layout` 25 checks in a real browser; both Makefile audits
clean. The wire is unchanged -- `receiver_state` carries the same seven
fields, built from a different struct -- which is why the browser needed no
edit at all.
