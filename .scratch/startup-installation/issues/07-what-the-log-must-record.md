# 07 - What the log must record, and the audit that found it did not

Status: resolved, 2026-09-12

The installation parameters and the calibration must be written to the log.
Auditing what already is, the answer is: **almost none of it**, and the gaps
are not where you would guess.

## The audit

`debug_log_write()` has **twenty-one call sites in two files**, `sdrprobe.c`
and `view_fm.c`. Every other file in `src/` writes nothing, ever.

What is recorded today:

| keyword | written by |
| --- | --- |
| `open`, `close` | `main`, `run_gui` |
| `tune` | both retune functions |
| `lease` | the three out-of-order refusals |
| `key`, `click`, `screen` | the frame loop |
| `fm-audio`, `fm-scan` | `view_fm.c`, in detail |

`fm-scan` is the model to copy -- begin with the step count, each carrier
found with its level, each station named with how long it took, and a summary.
It is the only subsystem in the program with a usable trace.

### Gap 1 -- the run does not say what installation it belongs to

The `open` line is `sdrprobe, <file-or-receiver>, <rate>, <frequency>, <ppm>`.
It does not carry the **site**, the **antenna**, the **receiver identity** or
the **gain** -- which is to say a log cannot answer the one question every
measurement in this program is keyed by (ADR-0018, ADR-0022). Nor does it
carry the **version**, so a log read next month cannot say which build wrote
it.

It also cannot: `installation_load()` runs *after* the log is opened, because
the receiver's serial is not known until the device is open. So this is a
second line, not a longer first one.

### Gap 2 -- calibration writes nothing at all

`overlay_calibration.c` has no `debug_log_write()` in it. Not the start, not a
residual, not the gate opening, not Apply, not the suggested ppm, not the
drift re-check's verdict, not the health transition. A calibration performed
in the window leaves **no record anywhere** except the final integer in the
config file -- no sequence, no reference, no scatter.

That is the sharpest gap, because the headless path prints all of it: every
`cal-measure` line exists precisely because "the verdict is one bit and the
sequence is what shows whether the scatter is the estimator or the crystal."
The window discards exactly what the command line was careful to keep.

### Gap 3 -- a tune is logged, its outcome is not

`retune_receiver()` logs before the attempt, and the comment says why: "a
retune that fails is exactly the one worth having a record of." But the
result is returned and never written, so the log shows a request and a reader
cannot tell a tune that took from one that was refused and rolled back -- nor
read `app->receiver_error`, which by then has the reason in it. The comment
promises something the code does not do.

### Gap 4 -- two of three band scans are silent

`fm-scan` is logged thoroughly. The **GSM band scan** (`overlay_scan.c`) and
the **LTE band scan** (`view_lte.c`, `lte_scan`) write nothing. Both are minutes
long, both end in a single number the operator then spends their time on, and
both are about to become the startup path's reference search.

### Gap 5 -- the survey's decisions are silent

`view_survey.c` and `survey_session.c` write nothing. This is the *smallest*
gap and should be scoped accordingly: a survey already records itself durably,
in `surveys/*.json` and the site history, which is more than a log line would
give. What is missing is only the machine's own transitions -- the settle, the
retune refusals, the confirmation pass -- which is what a live sweep needed
when the extraction broke.

### Gap 6 -- the config is written without a line saying so

`config_save()` and `installation_commit()` are reached from four files. A run
that rewrites the operator's stored calibration says so on **stderr**, which a
windowed run does not capture. Six `Acquisition failed` sites are stderr-only
too.

## What to build

**A second `open` line, after `installation_load()`:**

```
installation receiver <id|none> site "<site>" antenna "<antenna>" ppm <n> source <restored|flag|none> version <x.y.z> gain <formatted>
```

`device_gain_format()` already spells a gain for a panel, and it is the right
speller here: an AD9361's `index 40` must not be logged as a decibel it is not.

**Calibration, in the `fm-scan` idiom** -- one keyword, `cal`:

- `cal begin` with technology, channel, expected Hz and applied ppm -- the
  same four fields the headless `calibrate` line prints;
- `cal measure` per residual, with the same names the headless `cal-measure`
  line uses: `observed_ppm`, `centre_ppm`, `sem_ppm`, `spread_ppm`, `source`,
  `quality`. **The same names, because two vocabularies for one measurement
  is how a grep stops working across the two paths.** A rate limit is
  acceptable if a long lock floods the file; say what it is in the line.
- `cal result` with `locked`, `measurements`, `suggested_ppm` and `reason`,
  from the same five-word vocabulary;
- `cal apply` when a correction is applied and filed, with what it replaced;
- `cal drift` for each re-check's verdict and health transition.

**Tune outcomes:** the existing line stays where it is, and a second is
written after with the result and, on failure, `app->receiver_error`. The
"logged before the attempt" comment becomes true rather than aspirational.

**`gsm-scan` and `lte-scan`**, shaped on `fm-scan`: begin with the step count,
the choice with its confidence or correlation, the finish with the count
found. `scan_select_bcch()` returning 0 is a result and must be logged as one
-- it is the condition that sends the startup path to LTE, and a log that
cannot show it cannot explain why a session took the slow branch.

**A `config` line** whenever `config_save()` or `installation_commit()`
writes, saying which keys changed. Overwriting a measured correction is the
single most destructive thing this program does to its own state
(`.scratch/device-model/issues/12-*`), and it currently happens with a line on
a stream nobody is reading.

## Decisions

- **One keyword per subsystem, keyword first, integer Hz** -- the survey
  record's shape, which `debug_log.h` already says it follows.
- **Still off by default and still free when off.** `debug_log_active()`
  guards the expensive ones, as the frame loop already does at
  `sdrprobe.c:1131`. This ticket must not make an uninstrumented run slower.
- **The log is not a second report.** `--calibrate`'s stdout stays exactly as
  it is; ticket 06 makes both formatters read one machine, and the log is a
  third adapter over the same decisions, not a fourth vocabulary.
- **stderr stays.** The messages there are for the operator at a terminal;
  adding a log line does not mean removing a print.

## Acceptance criteria

- A windowed calibration with `--debug-log` produces a trace from which the
  verdict, its reference and its residual sequence can be read without the
  window.
- The two-line audit still prints nothing; `debug_log.h` stays in `APP_HDR`.
- `check-debug-log` gains cases for the new keywords, and keeps pinning the
  key and target names against `input_route.h`'s enum -- a log that mislabels
  what it saw turns an unanswered question into a wrong answer.
- A run without `--debug-log` shows no measurable slowdown in `make bench-dsp`.

## Not in scope

- Logging decoded messages. The decode views' output is on screen and the
  headless paths print it; a log of every ADS-B frame is the frame-rate trace
  `debug_log.h` says this must never become.
- A default-on log. Off by default is the existing contract and nothing here
  argues with it.


## What was built

- **`installation`**, a second line after `installation_load()` -- receiver,
  site, antenna, gain through `device_gain_format()`. It has to be second: the
  serial is not known until the device is open, which is after the log is.
- **`open`** carries the version.
- **`cal`** -- `begin`, `measure` (one per residual, in `--calibrate`'s own
  field names), `apply` with what it replaced, `drift` with the verdict and
  the measurement count that tells a real "no drift" from a check that never
  measured anything.
- **`startup`** -- begin, scan done, one line per residual, and the result.
- **`tune`** now logs its **outcome**, quoting `receiver_error` on a refusal.
  The comment promising "a retune that fails is exactly the one worth having a
  record of" is true now rather than aspirational.
- **`gsm-scan`** and **`lte-scan`**, shaped on `fm-scan`. The GSM one logs
  `chose` and `bcch` separately, because they differ whenever `scan_choose()`
  falls back to the loudest channel and a reader who cannot tell those apart
  is looking at a chart that can never say anything.
- **`config`**, in `config_save()` -- the one funnel every write goes through,
  `installation_commit()` included.

`check-config` and `check-installation` now link `debug_log.c`; both pass.

Gap 5 (the survey's own transitions) is deliberately not done: a survey
already records itself durably in `surveys/*.json` and the site history, which
is more than a log line gives.
