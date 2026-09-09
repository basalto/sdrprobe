# 08 - The GSM band scan's state into `struct band_scan`

Status: ready-for-agent
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
