# GSM Burst Analysis Visualizations

Status: plan (not yet implemented)
Scope: `src/gsm_dsp.h`, `src/gsm_dsp.c`, `src/sdrgui.h`, `src/sdrgui.c`, `src/sdrprobe.c`.
Related: tracker `.scratch/gsm-burst-analysis/`

---

## 1. Goal

The GSM SCH decoder performs complex DSP (sub-phase timing correlation, differential phase extraction, soft-decision Viterbi) that is entirely invisible to the user. When a burst fails to decode, or the SNR is marginal, the user currently only sees "waiting for synchronisation burst" or a messy constellation. 

We want to add detailed, swappable time-series charts that visualize the intermediate stages of the GSM decode chain, providing an "x-ray" into the signal quality and the decoder's decision-making process.

## 2. Layout & UX Design

The GSM Decode tab currently splits the screen:
- Top: ARFCN Waterfall
- Bottom Left: Channel Power Scan Chart
- Bottom Right: Decode Constellation

**The Change:** 
When the user clicks a channel to inspect it, the **Channel Power Scan Chart** (bottom left) will be replaced by a **Burst Analysis Chart**. 
- A "Back to Scan" button will appear to drop the selection and return to the band survey.
- Small toggle buttons above the Burst Analysis Chart will swap between the three available visualization modes (Correlation, Soft Bits, Phase Trajectory).

This layout choice ensures we don't squash the UI vertically, while pairing the time-history of the waterfall with the instant-x-ray of the burst.

## 3. The Three Visualizations

### 3.1 Timing Correlation Landscape
**What it is:** A plot of the differential training-sequence match score across the sliding window of the sample block.
**What it shows:** How the decoder finds the start of the burst. A clean signal will show noise, then a sharp, prominent spike exactly at the burst start position, followed by noise. Fading or multipath will show multiple peaks or a smeared spike.
**Data needed:** An array of correlation scores (floats `[0.0, 1.0]`) covering the search window.

### 3.2 Soft Symbol Magnitudes
**What it is:** A bar chart of the 148 soft-decision magnitudes (`|Im|`) across the burst, aligned with the burst's bit positions.
**What it shows:** The "shape" of the burst's energy and confidence. The 64 training symbols in the middle should be strong; the data fields on either side might fade. A dip in magnitude explains why the Viterbi trellis might have struggled to reconstruct the frame number.
**Data needed:** An array of 148 floats.

### 3.3 Differential Phase Trajectory
**What it is:** A line chart of the unwrapped differential phase across the 148 symbols of the burst.
**What it shows:** The literal ±90° GMSK phase steps. The user can visually see the phase walking up and down. ISI (Inter-Symbol Interference) will manifest as the phase steps not quite reaching the expected ±90° rails, illustrating exactly what the Viterbi equalizer is trying to correct.
**Data needed:** An array of 148 floats (phase in radians or degrees).

## 4. Implementation Plan

### Phase 1: Expand the Plugin API
Expand `struct gsm_sch_symbols` in `src/gsm_dsp.h` to carry the new visualization data. We are passing this struct anyway; adding ~300 floats is trivial and avoids recalculation.
- Add `float corr[GSM_SCH_BURST_BITS]` (we only need the correlation scores *around* the found peak to show the landscape).
- Add `float soft_mag[GSM_SCH_BURST_BITS]`
- Add `float phase[GSM_SCH_BURST_BITS]`

Update `gsm_sch_decode()` in `src/gsm_dsp.c` to populate these arrays when `symbols != NULL` and a valid burst is found.

### Phase 2: Create the GUI Component
Create a new reusable `sdrgui_burst_chart` component in `src/sdrgui.c`. It should be capable of drawing both a line chart (for Phase/Correlation) and a bar chart (for Soft Magnitudes), with zero-lines and appropriate axis scaling.

### Phase 3: Wire into the Application
In `src/sdrprobe.c`:
- Add a new state variable `int gsm_analysis_mode;` (0=Correlation, 1=Soft Bits, 2=Phase).
- Update `draw_gsm()`: If `app->scan_selected_arfcn > 0`, do *not* draw `sdrgui_scan_chart`. Instead, draw the toggle buttons for the analysis modes and call `sdrgui_burst_chart` using the data from `app->gsm_sch_symbols`.
- Add a "Back to Scan" button that clears `app->scan_selected_arfcn` and calls `leave_gsm()`.