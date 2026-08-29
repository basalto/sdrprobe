# 01 — Expand Plugin API for Visualization Data

Status: resolved
Blocked by: (none)

Expand `struct gsm_sch_symbols` in `src/gsm_dsp.h` to carry the new visualization data. We are passing this struct anyway; adding ~300 floats is trivial and avoids recalculation.
- Add `float corr[GSM_SCH_BURST_BITS]` (we only need the correlation scores *around* the found peak to show the landscape).
- Add `float soft_mag[GSM_SCH_BURST_BITS]`
- Add `float phase[GSM_SCH_BURST_BITS]`

Update `gsm_sch_decode()` in `src/gsm_dsp.c` to populate these arrays when `symbols != NULL` and a valid burst is found.

## Comments
