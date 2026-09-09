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
