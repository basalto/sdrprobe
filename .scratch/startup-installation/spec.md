# The session begins at a known installation

A run of this program produces numbers that only mean something against the
arrangement that produced them: a site, an antenna, a receiver, and a crystal
correction. Today three of those four are *assumed* at startup and the fourth
is restored from a file. Nothing asks, and the first thing that notices a
wrong assumption is a survey saved under the wrong site -- which is silent,
and permanent.

This is a startup overlay that asks, a calibration that runs while it asks, a
health indicator that goes on saying what the calibration found, and a log
that records both.

## What is already here, and what is genuinely new

The half of this that exists is larger than it looks, and building the missing
half without reading it is how this ends up with two of everything.

| piece | where it already is |
| --- | --- |
| site, antenna, and the lists to pick them from | `config.{c,h}`, `config_remember_site()`, `config_remember_antenna()` |
| a site/antenna form with combo menus | `view_survey.c`, fields 3 and 4 |
| the receiving setup as a model | `installation.{c,h}`, ADR-0018 and ADR-0022 |
| restore a stored correction at startup | `sdrprobe.c:2860`, `installation_ppm()` |
| scan a band, take the strongest cell, calibrate it | `run_headless()`, `sdrprobe.c:2435-2570` |
| the lock gate | `calibration_gate.h`, ADR-0004 |
| a periodic re-check and a health indicator | `update_drift_check()`, ADR-0006 |
| a full-screen overlay orthogonal to the tabs | `overlay_settings.c`, `overlay_calibration.c`, ADR-0008 |

What is new is exactly three things: **a form that runs before any tab**, **a
calibration nobody had to start**, and **a machine that sequences the two**.
Everything else is reuse -- including where the verdict goes, which is the
health indicator that has been in the header since ADR-0006 -- and where it is
not reuse the ticket says why.

## The finding that sets the shape

**Calibration owns the tuner for its whole duration.** `start_calibration()`
borrows a `receiver_lease` and retunes to the reference carrier;
`CALIBRATION_MIN_SECONDS` is 8 and the gate usually wants longer. There is no
arrangement in which calibration runs "in the background" and the rest of the
program shows what its own controls say -- every view would be drawing the
calibration carrier. So the overlay is **modal**, and that is not a UI
preference: it is the only honest presentation of a receiver that is busy.

**And the reference has to be found before it can be measured.** There is no
bare "calibrate": it wants an ARFCN, an EARFCN, or a scan to find one.

## Why GSM is tried first, and why the usual reason is not the reason

**The stated assumption -- that GSM is the more precise reference -- is not
what this repository has measured, and the conclusion is right anyway.** Both
sources pass through one gate, `calibration_is_stable()`, which will not lock
either until the standard error of the centre is under
`CALIBRATION_MAX_SEM_PPM`. The gate equalises precision by construction: a
lock is a lock at the same tolerance whichever reference produced it. And the
one on-air comparison in `CLAUDE.md` has them agreeing to about a ppm -- GSM
ARFCN 113 at -31.3, LTE EARFCN 6200 at -32.5 -- with nothing to say which of
the two is nearer the truth. Writing "GSM is more precise" into the code would
be the exact shape of fault the `check-claims` skill is about: impeccable
arithmetic under a claim nobody measured.

GSM goes first for three reasons that *are* established, and they are stronger
than the one that is not:

1. **Only an FCCH-backed calibration enables the drift re-check.**
   `update_drift_check()` returns immediately unless `cal.gsm_valid`, and the
   Apply path sets `gsm_valid` only when `track.source ==
   CALIBRATION_SOURCE_FCCH`. An LTE calibration leaves `drift_health ==
   CAL_HEALTH_UNKNOWN` and no periodic re-check at all (ADR-0006). So the
   choice of reference decides whether the receiver watches its own crystal
   for the rest of the session -- which is a capability difference, not a
   precision one.
2. **An FCCH has no modulo ambiguity.** It is an unmodulated tone by
   construction, and the estimator is a coherent tone detector. LTE's offset
   is measured from a PSS *phase*, which wraps every 15 kHz, and recovering
   the whole subcarriers needs the integer sweep `CLAUDE.md` names "the third
   and worst trap" -- an uncalibrated dongle is two subcarriers out at 800 MHz
   and the PSS still locks at 0.8 with all of it present.
3. **Finding a GSM reference is about thirteen times cheaper.** At 2 MS/s
   `scan_plan_make()` covers the 24.8 MHz downlink in **16 steps** of
   `SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS` -- **12.8 s** against
   roughly 170 s for one LTE band.

**And the scan that does it already exists and already answers this exact
question.** `scan_select_bcch()` in `scan_plan.h` returns "the loudest channel
that also carries an FCCH tone", `bcch_conf[]` is held across a step's blocks
because FCCH is intermittent, and `scan_plan.h`'s own header says the scan's
whole output is a single ARFCN for the operator to spend their time on. The
GSM half of this feature is a caller, not a scanner.

**The risk is availability, and it is why LTE is the fallback rather than the
alternative.** Which GSM cells are audible is a fact about *where the receiver
is*: two of the three committed GSM captures are of cells that cannot be heard
from the desk this is worked on, because they were recorded somewhere else.
(This spec first said the band was being refarmed. It is not -- there is no
GSM refarming here -- and that claim was inferred from two captures going
quiet after the receiver moved, which is exactly the confusion ADR-0022 exists
to prevent.) A site with no BCCH within reach is a real case, not a defensive
one, and there `scan_select_bcch()` returns 0 and the LTE band scan takes
over.

## What that does to the cost

The three-minute cold launch this spec was originally written around is the
**fallback** path, not the normal one. Where GSM 900 is on air the sequence is
12.8 s of scanning plus the gate's own settling -- `CALIBRATION_MIN_SECONDS`
is 8 and a clean FCCH reaches 32 residuals quickly -- so a cold launch is
**well under a minute**. Only a site with no reachable BCCH pays for the LTE
band scan.

That cost is still accepted deliberately and remains the largest risk here.
Three things keep it from being a trap: **Skip** abandons the measurement and
releases the form at any moment, a scripted run never sees the overlay at all,
and both the GSM outcome and the LTE band are remembered per site.

## The trap this spec exists to avoid

The sequence -- scan, choose, measure, gate, apply, file, notify -- is a state
machine, and the obvious place to write it is inside the overlay that draws
it. That is the mistake `.scratch/survey-to-decoder` cost a week on and
`CLAUDE.md` states as a rule: **a function that draws or reads input may not
also decide.** `survey_session.{c,h}` is the shape to copy, including its two
refusals -- it does not touch the receiver, it says where it wants the tuning
and the adapter obeys; and it does not read or write files.

It is worth re-reading what that extraction *broke*, because this machine has
the same two hazards. The settle timed from the request rather than from the
tuning reported `settling 0` on a live sweep with every check green, and a
step that returned early on "no block" lost a block a step. **No capture
retunes**, so no check in this repository can exercise either. The acceptance
criteria below therefore require a live run against the current binary, and
say which line to read.

The second trap is quieter. `run_headless()` already contains this walk, once,
with printfs through it. A second copy in the window is how `--lte-chain` and
`probe-lte-chain` drifted twice. Ticket 06 folds the headless path onto the
same machine, and it is not optional.

## Decisions

- **Modal until the calibration finishes**, or until Skip. Continue is
  disabled while a measurement runs. Anything else shows an operator a live
  view of a frequency they did not choose.
- **GSM first, LTE only when GSM has nothing to offer.** The startup scan runs
  `scan_plan.h`'s GSM 900 walk and takes `scan_select_bcch()`. A non-zero
  ARFCN calibrates against its FCCH and the LTE scan never runs. Zero -- no
  channel reached `SCAN_BCCH_MIN_CONF` anywhere in the band -- falls through
  to the LTE band scan. **The fall-through condition is "no BCCH found", not
  "no GSM power found"**: choosing by power would hand the calibration a
  channel with no tone in it and then spend the whole budget failing to lock
  on it.
  `scan_select_strongest()` is right there beside `scan_select_bcch()` and is
  the wrong one to call here.
- **A scan every startup**, with the LTE band a field on the form for the
  fallback. Defaulted from a new `startup_band <site> <band>` config key, else
  the lowest band `view_lte_bands()` says this tuner can reach. No reachable
  band, and no GSM BCCH, means calibration is unavailable and says so.
- **The GSM scan needs at least 1 MS/s and the LTE one takes 1.92.**
  `start_calibration()` already refuses GSM under 1 MS/s and
  `start_lte_calibration()` already retunes to `LTE_SAMPLE_RATE_HZ` and gives
  the rate back on close (ADR-0014). The machine asks for a rate the same way
  it asks for a tuning -- it says what it wants, the adapter obeys -- so
  neither rate is written into the overlay.
- **The crystal is measured without a site; the result is filed with one.** A
  ppm does not depend on where it was measured, only on which crystal -- so
  the scan may start immediately while the operator is still typing. Filing is
  keyed by receiver **and** site (ADR-0018), so it happens on Continue,
  against the committed site, and never before.
- **`--ppm` suppresses the whole thing.** The operator has stated the
  correction for this run; measuring one and offering to overwrite it is the
  `--ppm` / `--claim-calibration` confusion that
  `.scratch/device-model/issues/12-*` was written about.
- **Saying what the form would ask is how it is bypassed.** The overlay exists
  to find out the installation; a run that already states it has nothing to be
  asked. `--site` given on the command line skips the form, and so does
  `SDRPROBE_SITE` in the environment -- which is the form a launcher, a
  systemd unit or a field script can actually use, where editing an `ExecStart`
  line is not always available. `SDRPROBE_ANTENNA`, `SDRPROBE_RECEIVER_LABEL`
  and `SDRPROBE_NO_STARTUP` complete the set. **A flag beats an environment
  variable beats the config file**, one rule, and a value arriving by any of
  the three routes is remembered the same way -- a site named once should be
  offerable next time however it was named, and a second rule here is how one
  place becomes two.
- **The environment is read through a lookup, not through `getenv`.**
  `parse_options()` is pure and `check-options` reaches every flag because of
  it. `options_apply_environment(opts, lookup)` takes the reader as an
  argument, so a check hands it a table and `main` hands it `getenv`. Calling
  `getenv` inside the parser would put the one thing a check cannot control in
  the middle of the one thing every check controls.
- **A scripted run never sees it.** `--headless`, `--file`, `--duration`,
  `--view` and `--ppm` each skip it, plus `--no-startup` and a
  `startup_prompt 0` config key. `check-pipelines`, every screenshot recipe
  and every `--duration` check keep working untouched, and that is a
  requirement rather than a happy accident.
- **`--view startup` opens it anyway**, for the same reason
  `START_VIEW_CALIBRATION` exists: a screen unreachable from the command line
  is a screen that ships with three overlapping panels because nobody saw it.
- **Changing gain restarts the calibration.** A band scan's cell list is a
  function of the gain it ran at, so a cell found at one gain and measured at
  another is two measurements wearing one label. The form says so when it
  restarts.
- **The overlay's own calibration section reports while it runs; the health
  indicator reports afterwards.** While the form is up there is a whole screen
  to report into, so the scan's progress, each residual and the verdict are
  drawn in place. After Continue the verdict is a **standing fact about the
  receiver, not an event** -- ten seconds later the crystal is still corrected,
  still by that amount, still against that reference -- so it belongs in the
  header's two calibration dots, which have carried exactly that since
  ADR-0006. **Nothing in this program dismisses itself on a timer**, and a
  surface that erased itself could not answer "what am I corrected by?" at any
  later moment.
- **And all of it is written to the log**, which currently records almost none
  of it: `overlay_calibration.c` contains no `debug_log_write()` at all, and
  the `open` line names neither the site, the antenna, the receiver nor the
  version. Ticket 07 is the audit and the repair.
- **Five overlays, not a fifth ad-hoc flag.** ADR-0008 says extend the enums.
  `input_route()` gains one target, between Help and Settings: Help stays
  outermost because it can be raised over anything, and the startup form
  outranks Settings because Settings is reached *through* it.

## What this does not do

- It does not make calibration non-blocking. That would need the tuner shared,
  which it is not.
- It does not change `--calibrate 1`, which still wants an ARFCN. The
  GSM-then-LTE search is the *startup* machine's, and ticket 06 is what offers
  it to the command line -- as a new value, not by changing what the existing
  ones mean.
- It does not teach the GSM scan anything new. `scan_select_bcch()` and
  `SCAN_BCCH_MIN_CONF` are used exactly as the GSM view uses them, and a
  startup path that quietly relaxed the confidence threshold would be a second
  scanner wearing the first one's name.
- It does not add a timestamp to a calibration profile. "Re-measure when
  stale" was considered and rejected in favour of "every startup", which needs
  no format change.
- It does not put gain into any measurement key. A presence claim still
  survives an ordinary gain adjustment (ADR-0022).

## Tickets

| # | what |
| --- | --- |
| 01 | `startup_session.{c,h}` -- the machine, and a check that reaches every transition |
| 02 | The verdict in the health indicator: hover detail, and the banner position the layout should own |
| 03 | `startup_layout.h` and `overlay_startup.c` -- the form, its geometry and its routing |
| 04 | Wiring: when it runs, what it commits, and the cases that have no calibration |
| 04b | The bypass: `--no-startup`, the `SDRPROBE_*` environment, and one precedence rule |
| 05 | ADR-0024, the documentation, and the version |
| 06 | Fold `run_headless()`'s `--calibrate` onto the machine, so the two cannot drift |
| 07 | What the log must record -- six gaps, of which calibration writing nothing is the sharpest |

## Open questions

- **Does the GSM scan need its own settle here?** `scan_plan.h` has
  `SCAN_STEP_SETTLE_SECONDS` at 0.35, measured for the GSM view's own walk.
  Whether that is enough when the step before it was at a different sample
  rate -- which the LTE fall-through introduces -- is not established, and the
  survey's settle bug says what happens when it is not. Measure before
  trusting it.
- **What a second receiver does to the reference search.** An E4000 has a
  reach hole between about 1107 and 1246 MHz and an FC2580 covers 146-308 and
  438-924; GSM 900 is inside all three tuners' reach, but the LTE fall-back
  band list already comes from the profile and the GSM one does not ask at
  all. `device_tuner_reach()` should gate the GSM scan too, and nothing
  currently does.
- **Whether a hover is discoverable enough.** The verdict detail lives behind
  the pointer, and an operator who does not know to hover sees a green dot and
  a number nowhere. The alternative is a permanent header line, which is
  clutter on every screen for the rest of the session. Neither has been
  measured against an operator who is not the author.
