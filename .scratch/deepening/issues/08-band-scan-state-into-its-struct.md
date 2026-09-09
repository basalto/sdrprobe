# 08 - The GSM band scan's state into `struct band_scan`

Status: resolved, 2026-09-09
Opened by the audit in `06-struct-app-carve-out.md`, 2026-09-09.

The same fault as ticket 07 and smaller. `struct band_scan bandscan` has
**one** reader outside `app.h` -- `overlay_scan.c` -- while eight scan fields
sit beside it in `struct app`:

| field | readers |
| --- | --- |
| `scan_open` | 5 |
| `scan_selected_arfcn` | 4 |
| `scan_power`, `scan_running` | 3 each |
| `scan_step`, `scan_step_count`, `scan_bcch_conf` | 2 each |
| `gsm_autoselect_pending` | 2 |

`scan_power[125]` and `scan_bcch_conf[125]` are the sweep's measurements, so
this is the same argument the survey's snapshot settled in ticket 04:
measurements are not window state.

## Local hypothesis

All eight move into `struct band_scan` unchanged. It is false if the frame
loop or a decode view needs one of them while the overlay is closed --
`scan_selected_arfcn` and `gsm_autoselect_pending` are the candidates, since
the GSM view acts on both after the scan has gone.

## What to check afterwards

`check-scan` covers the coverage arithmetic and which channel the scan hands
back; `check-layout` covers the overlay. As in 07, a field that cannot move
without a new check is a finding.

## Worth doing after 07, not before

They are the same edit twice and 07 is the larger one, so whatever 07 learns
about writers-while-closed applies here for free.

**What 07 learned, 2026-09-09.** Two things carry over:

- **`scan_open` should move**, and 07 settled the question the ticket was
  unsure about. `help.open` is the precedent -- the precedence chain reads it
  through `input_state_now()` and does not care where it lives -- so
  `calibration_open` became `cal.open`, and `scan_open` and `settings_open`
  are the only two overlay flags still loose.
- **Look for a field doing two jobs before moving it.** 07's hypothesis was
  false for exactly one field, and not because of where it lived:
  `calibration_status` was also the receiver's error line, written by
  `retune_receiver()` from every screen in the program. `start_scan()` was one
  of those writers and now writes `app->receiver_error` instead, so this
  ticket inherits that already fixed -- but the same question is worth asking
  of `scan_selected_arfcn` (five readers, and the GSM view acts on it after
  the overlay has gone) and `gsm_autoselect_pending`.


## What was done

Seven fields into `struct band_scan`, one **deleted**, and one moved somewhere
else entirely. `struct app` is **62 fields, down from 70**.

| was | is |
| --- | --- |
| `scan_open` | `bandscan.open` |
| `scan_running` | `bandscan.running` |
| `scan_step` | `bandscan.step` |
| `scan_power` | `bandscan.power` |
| `scan_bcch_conf` | `bandscan.bcch_conf` |
| `gsm_autoselect_pending` | `bandscan.autoselect` |
| `scan_step_count` | **gone** |
| `scan_selected_arfcn` | `gsm.selected_arfcn` |

### The two findings, and 07's question is what produced both

07's lesson was *look for a field doing two jobs before moving it*, and this
ticket's own field table had two entries wrong.

**`scan_selected_arfcn` is not the scan's.** It is the GSM view's inspected
channel, and the name was the only thing connecting it to a scan:

- `gsm_tune_selected()` sets it when the operator picks a channel. No scan.
- `--arfcn` sets it at startup (`sdrprobe.c`), with its own comment saying
  "Both the GSM view and a recording's sidecar read this". No scan.
- `start_record()` reads it to say which channel a capture is of.
- The scan writes it only when a finished scan chooses a channel, through
  `gsm_tune_selected()` like everything else.
- **Fourteen of its twenty uses were already in `view_gsm.c`.**

Moving it into `struct band_scan` would have put it in the wrong struct with a
tidier name. It is `gsm.selected_arfcn`, beside `gsm.selected_hz` -- which is
the same fact in hertz, written on the next line every single time either is
written.

**`scan_step_count` was a copy of a field one dereference away.**
`start_scan()` set it from `app->bandscan.plan.step_count` and nothing ever
changed it; its three readers now read the plan. That is what this ticket's
parent means by "if a field survives that only one file reads, that is the
next straggler" -- except this one did not need moving, it needed removing.

### Two smaller things

`bandscan.open` and `bandscan.running` got a comment, because they are two
things and read like one: the GSM view runs the same scan **inline** with the
overlay closed, so `running` without `open` is the GSM view's own scan and
`open` without `running` is the overlay sitting on a finished chart.
`start_scan()` sets both and the GSM view clears `open` immediately after.

The arrays were declared `[125]`. They are indexed by ARFCN with index 0
unused, so they are `[SCAN_ARFCN_LAST + 1]` now -- scan_plan.h already owns
that range and states the convention. The two loops that walked `arfcn < 125`
walk `arfcn <= SCAN_ARFCN_LAST`.

`bandscan.open` follows `cal.open` and `help.open`. **`settings_open` is the
last overlay flag still loose in `struct app`.**

## Measured

- `make check` green, 17781 checks in 55 suites. `check-scan` unchanged.
- The GSM view from `gsm_arfcn_69.bin` with `--arfcn 69` is
  **byte-identical**, which exercises `--arfcn` writing the field,
  `enter_gsm()` reading it, and the inline scan panel drawing.
- A live `--view gsm` runs the whole chain -- `enter_gsm()` starts a scan
  because no channel is selected, `bandscan.autoselect` makes it pick the
  best BCCH, `gsm_tune_selected()` retunes and `receiver_commit()` keeps the
  channel. Before the move it chose ARFCN 17 and decoded BSIC 10, MCC 268
  MNC 01, LAC 350, cell 40471; after, it chose **ARFCN 113** and decoded
  **BSIC 38 (NCC 4 / BCC 6), MCC 268 MNC 06, LAC 8420, cell 16134**. A
  different channel because a different one was strongest -- the air moved
  between the runs, and the chart shows the tallest bar moving with it.

  **The second answer is corroboration rather than a coincidence**: BSIC 38
  (NCC 4 / BCC 6), MNC 06 and cell identity 16134 are exactly what
  `testfiles/gsm_arfcn_113.bin` reads and what `check-pipelines` pins. The
  autoselect landed on the live cell that capture was taken from, and read
  the same identity out of it.

## Not in scope, and left as findings

- **The scan overlay is not reachable from the command line.** `--view` takes
  thirteen names and none of them opens it, so the only screenshot of it is
  from a live receiver and a click. CLAUDE.md's rule -- "every screen has to
  be reachable from the command line for the same reason every decision does"
  -- says that is a gap; the inline panel in the GSM view is reachable and was
  used here instead.
- `settings_open` into `struct settings_panel`, which is the last one.
