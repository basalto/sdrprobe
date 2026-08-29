# 03 — Wire Burst Analysis into GSM View

Status: resolved
Blocked by: 02

In `src/sdrprobe.c`:
- Add a new state variable `int gsm_analysis_mode;` (0=Correlation, 1=Soft Bits, 2=Phase).
- Update `draw_gsm()`: If `app->scan_selected_arfcn > 0`, do *not* draw `sdrgui_scan_chart`. Instead, draw the toggle buttons for the analysis modes and call `sdrgui_burst_chart` using the data from `app->gsm_sch_symbols`.
- Add a "Back to Scan" button that clears `app->scan_selected_arfcn` and calls `leave_gsm()`.

## Comments
