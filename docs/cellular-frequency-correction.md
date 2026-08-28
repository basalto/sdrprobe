# Frequency Correction Using Cellular Networks

## Purpose

An RTL-SDR crystal oscillator is not perfectly accurate. Its error shifts the
received frequency axis by a proportional number of parts per million (PPM).
A cellular base station can provide a useful reference because its downlink is
normally continuous and disciplined by a stable network clock.

This procedure estimates an RTL-SDR correction. It does not calibrate signal
power, antenna gain, or cellular timing.

## Current Application Support

`rtl_raylib` includes an interactive cellular calibration screen reached from
**Settings → Cellular calibration**. The screen displays selectors for 2G, 4G,
and 5G, but the current measurement implementation supports only:

- Technology: 2G GSM.
- Band: GSM 900 primary band.
- Downlink channels: ARFCN 1-124.
- Live RTL-SDR input at 1 MS/s or greater.

The 4G and 5G controls are placeholders for future band tables and
technology-specific reference detection. They cannot start a measurement.

The implemented GSM estimator measures the power center of a strong,
isolated carrier near the selected channel. It is not a GSM decoder and does
not prove that the observed signal is the selected BCCH. The operator, band,
and ARFCN must be identified independently.

## Channel Power Scan

FCCH refinement and a fast stable lock both require the selected ARFCN to carry
a strong BCCH. To help pick one, the calibration screen has a **Scan** button
that sweeps the whole GSM 900 downlink band and charts the received power of
every channel.

- The sweep retunes across `935.1-959.9 MHz` in steps of the usable span
  (`sample_rate − 2 × 200 kHz` edge margin, ~1.6 MHz at 2 MS/s), dwelling
  `SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS` (0.35 s settle + 0.45 s
  probe) at each step, ~10-13 s in total.
- At each step `sdr_dsp_channel_powers()` reduces the current dBFS spectrum
  into per-ARFCN power for the channels within the trusted central window of the
  tuned span, filling a `power[1..124]` array.
- Each channel in that window is probed for its **FCCH tone** (via
  `gsm_fcch_detect` at `channel + 67.708 kHz`); the **peak coherence**
  seen at the tone offset across the step's probe blocks is kept, and a channel
  is flagged as a **BCCH** when it reaches `SCAN_BCCH_MIN_CONF` (0.85 — slightly
  below the live-lock 0.9 bar, since the scan is a hint you then confirm by
  calibrating). FCCH bursts are intermittent and a strong neighbour can lower a
  block's coherence, so the peak-over-blocks is far more reliable than a
  single-block test. Power alone does not identify a BCCH — a strong traffic
  carrier can outrank it.
- The scan screen charts ARFCN (x) against power in dBFS (y) as bars. **BCCH
  channels are drawn green** with a cap marker; other channels are blue. The
  header reports the strongest BCCH and its detection confidence, hover shows an
  `ARFCN / MHz / dBFS / BCCH conf` readout, and the strongest BCCH (or, if none,
  the strongest channel) is preselected.
- **Clicking a bar** sets that ARFCN and starts calibration on it; **Rescan**
  repeats the sweep; **Back** restores the pre-scan tuning and returns to the
  calibration screen.

Calibration measurement is suspended while the scan screen is open so the
retuning sweep cannot pollute the recent-residual history. The scan requires a
live receiver and a sample rate of at least 1 MS/s.

## Using The Calibration Screen

1. Warm the RTL-SDR for at least 10 minutes.
2. Start `rtl_raylib` with a moderate manual gain and the correction currently
   believed to be correct, for example:

```sh
./rtl_raylib --frequency 1090M --sample-rate 2000000 \
  --gain 32.8 --ppm 0
```

3. Open **Settings**, then **Cellular calibration**.
4. Select **2G**. The screen reports **Band: GSM 900**.
5. Enter a known downlink ARFCN from 1 through 124. Locally useful examples
   identified during development were:

| ARFCN | Expected downlink |
| ---: | ---: |
| 113 | 957.6 MHz |
| 117 | 958.4 MHz |
| 120 | 959.0 MHz |

   If you do not know which channel carries a strong BCCH, press **Scan** first
   (see [Channel Power Scan](#channel-power-scan)) and click the strongest
   channel to fill in the ARFCN.

6. Press **Start**. The application stops acquisition, tunes 400 kHz below the
   expected carrier, resets the receiver buffer, and restarts acquisition. The
   waterfall zooms to the selected channel (`expected ± 250 kHz`) and labels its
   axis with ARFCN numbers.
7. Wait through the two-second settling interval and then for a stable lock.
   The lock needs a few seconds of consistent measurements for the correction's
   standard error to fall below the stability threshold; an FCCH-tone lock
   converges faster than a centroid-only lock.
8. Compare the green expected marker and amber measured marker. The top panel
   reports offset, observed PPM, the recent center/spread and its standard
   error, peak, guard-band floor, prominence, and suggested correction. Use
   **Up** and **Down** to tighten the waterfall colour range if the channel is
   not clearly visible.
9. To measure a different channel, edit the **ARFCN** field and press
   **Retune** (the **Start** button is relabelled while running); calibration
   re-tunes in place without leaving the screen.
10. **Apply PPM** becomes meaningful only after the status reports
    **Stable lock**. Applying the suggestion restarts measurement so the
    residual error can be checked.
11. Press **Back** to restore the pre-calibration center frequency. A PPM value
    applied during calibration remains active.

The waterfall history is intentionally cleared when tuning or PPM changes,
because rows captured under different frequency mappings must not share one
axis.

## Calibration Data Flow

The implemented path is:

```text
RTL-SDR callback
    → latest raw sample block
    → center unsigned I/Q bytes around 127.5
    → optional mean-I/mean-Q removal on the spectrum copy
    → 64 non-overlapping 2048-point Hann-windowed FFTs
    → average linear power, then convert to shifted dBFS spectrum
    → append one dBFS row to waterfall history
    → estimate the carrier centroid (peak, floor, prominence, fallback)
    → refine the carrier from the FCCH tone when present
    → accumulate recent PPM residuals
    → gate the correction suggestion on confidence and stability
```

The calibration consumes the same spectrum used by the normal spectrum and
waterfall views. It does not open another receiver stream or invoke
`rtl_test`; librtlsdr permits only one process to own the USB interface.

## Codebase Map

| File | Responsibility |
| --- | --- |
| `src/rtl_raylib.c` | Calibration screen, channel-scan sweep and chart, application state, input handling, receiver stop/retune/restart, FCCH-vs-centroid selection, robust stability accumulation, waterfall markers, and PPM application |
| `src/sdr_dsp.h` / `src/sdr_dsp.c` | Generic SDR primitives reused by calibration: byte→float I/Q, DC removal, signal stats, FFT/dBFS spectrum, two-stage carrier estimator, evenly-spaced channel-power reducer, and PPM correction |
| `src/gsm_dsp.h` / `src/gsm_dsp.c` | GSM 900 technology plugin: ARFCN→frequency map and the FCCH tone detector |
| `tests/sdr_dsp_test.c` / `tests/gsm_dsp_test.c` | Hardware-free checks — generic primitives, and GSM calibration (ARFCN conversion, carrier estimation, correction sign, FCCH detection/rejection) respectively |

The generic-core / per-technology-plugin split is recorded in
[docs/adr/0001-technology-plugin-dsp-architecture.md](./adr/0001-technology-plugin-dsp-architecture.md).

## DSP Public Interface

The testable seam is declared in `src/sdr_dsp.h` (generic) and `src/gsm_dsp.h` (GSM):

```c
struct sdr_channel_estimate {
    double measured_frequency_hz;
    double peak_frequency_hz;
    float peak_dbfs;
    float floor_dbfs;
    float prominence_db;
};

int gsm_downlink_hz(unsigned int arfcn,
                               uint32_t *frequency_hz);

int sdr_dsp_estimate_channel_center(
    const float *spectrum_dbfs,
    size_t bin_count,
    double lower_frequency_hz,
    double upper_frequency_hz,
    double expected_frequency_hz,
    double coarse_half_width_hz,
    double fine_half_width_hz,
    struct sdr_channel_estimate *estimate);

int sdr_dsp_corrected_ppm(int current_ppm,
                             double measured_frequency_hz,
                             double expected_frequency_hz);

struct gsm_fcch_result {
    int detected;
    double tone_frequency_hz;   /* baseband */
    float confidence;           /* coherence in [0, 1] */
    float amplitude;
};

int gsm_fcch_detect(
    const float *i_samples,
    const float *q_samples,
    size_t pair_count,
    double sample_rate,
    double target_offset_hz,
    double search_window_hz,
    struct gsm_fcch_result *result);

int sdr_dsp_channel_powers(
    const float *spectrum_dbfs,
    size_t bin_count,
    double spectrum_lower_hz,
    double spectrum_upper_hz,
    double accept_lower_hz,
    double accept_upper_hz,
    double base_hz,
    double spacing_hz,
    int arfcn_min,
    int arfcn_max,
    float *powers_dbfs);
```

Keeping these operations outside `src/rtl_raylib.c` has two consequences:

- The channel and correction math can be tested without raylib or hardware.
- GUI code consumes named measurements rather than reproducing FFT-bin or PPM
  formulas.

## GSM 900 ARFCN Conversion

`gsm_downlink_hz()` implements the GSM 900 downlink formula:

```c
if (!frequency_hz || arfcn < 1 || arfcn > 124)
    return 0;
*frequency_hz = 935000000U + arfcn * 200000U;
```

Therefore:

```text
ARFCN 113 = 935.0 MHz + 113 × 0.2 MHz = 957.6 MHz
ARFCN 117 = 935.0 MHz + 117 × 0.2 MHz = 958.4 MHz
ARFCN 120 = 935.0 MHz + 120 × 0.2 MHz = 959.0 MHz
```

This formula is deliberately limited to the primary GSM 900 range. Extended
GSM channels and overlapping ARFCN meanings require a distinct band profile
rather than weakening this validation.

## Receiver Tuning Strategy

`start_calibration()` in `src/rtl_raylib.c` validates the technology, band,
sample rate, and ARFCN. It then places the expected carrier 400 kHz above the
receiver center:

```c
app->calibration_expected_hz = expected;
app->calibration_tune_hz = expected - 400000U;
```

At 2 MS/s, the visible complex bandwidth is approximately center ±1 MHz. A
400 kHz offset keeps the complete carrier and its search/guard regions inside
the sampled band while separating it from the zero-IF center artifact. The
screen requires at least 1 MS/s so the expected carrier and surrounding
regions remain visible.

`retune_receiver()` performs the hardware transition:

```text
stop acquisition
    → set PPM correction if changed
    → set center frequency
    → reset the RTL-SDR buffer
    → verify frequency readback
    → clear stale spectrum/waterfall state
    → restart acquisition
```

The helper remembers the previous frequency and PPM. On failure it attempts
to restore both before restarting acquisition. This prevents a failed
calibration Start from silently leaving the normal views tuned elsewhere.

The small `set_frequency_correction()` wrapper first reads the current value
and skips the setter when unchanged. This matters because librtlsdr can report
an unchanged correction as a non-success result even though no hardware change
is required.

## Spectrum Feeding Calibration

`process_block()` in `src/rtl_raylib.c` converts each raw sample block once. The
raw centered I/Q remains available to magnitude and scatter views. When DC
removal is enabled, only the spectrum copy is modified:

```c
memcpy(app->spectrum_i, app->i_samples, pair_count * sizeof(float));
memcpy(app->spectrum_q, app->q_samples, pair_count * sizeof(float));
rtl_raylib_remove_dc(app->spectrum_i, app->spectrum_q, pair_count);
```

`sdr_dsp_spectrum()` then processes every complete 2048-pair window:

```text
centered I/Q
    → divide by 127.5
    → apply Hann coefficients
    → forward complex FFT
    → divide by Hann coefficient sum
    → calculate linear bin power
    → average power across windows
    → 10·log10(power)
    → FFT-shifted dBFS array
```

The normal spectrum and waterfall views and the calibration estimator all
consume `spectrum_average`, the per-block linear-power average converted to
dBFS. Calibration deliberately uses the raw per-block spectrum, not peak hold
and not a smoothed copy: the per-block estimates are treated as independent
samples so their scatter can be reduced statistically (see
[Temporal Confidence And Stability](#temporal-confidence-and-stability)) rather
than by a filter that would correlate successive measurements.

An earlier revision fed the estimator an exponential moving average of the
spectrum. It was removed: on a modulated GSM channel the per-block center hops
between spectral features, which spectral smoothing cannot fix, and the
correlation it introduced would have made the standard-error stability gate
report false confidence.


## FFT Bin Frequency Mapping

The shifted spectrum spans `sample_rate` Hz. A bin's frequency is:

```c
bin_width = (upper_frequency_hz - lower_frequency_hz) / bin_count;
frequency = lower_frequency_hz + bin_width * bin_index;
```

The divisor is `bin_count`, not `bin_count - 1`: an N-point complex FFT has N
bins spaced by `sample_rate / N`; its final bin is one bin below the upper
Nyquist endpoint.

With 2 MS/s and a 2048-point FFT:

```text
bin width = 2,000,000 / 2048 = 976.5625 Hz
```

This bin spacing is also the approximate lower bound on one-frame visual
frequency resolution. The centroid can interpolate between bins, but its true
accuracy still depends on signal shape, interference, oscillator stability,
and SNR.

## Two-Stage Carrier Estimate

`sdr_dsp_estimate_channel_center()` uses two stages.

### 1. Coarse acquisition

The application requests a ±100 kHz search around the expected GSM carrier:

```c
sdr_dsp_estimate_channel_center(
    spectrum_average,       /* raw per-block dBFS spectrum */
    SDR_DSP_FFT_SIZE,
    lower_hz,
    upper_hz,
    expected_hz,
    100000.0,  /* coarse half-width */
    50000.0,   /* fine half-width */
    calibration_workspace,
    &estimate);
```

The estimator finds the strongest dBFS bin in that coarse region. It rejects
a peak within one fine-window width of the search boundary because a clipped
fine window would bias the centroid.

### 2. Guard-band floor

The local floor comes from bins between one and two coarse half-widths from
the expected carrier. The estimator sorts those bins and uses their 20th
percentile:

```c
qsort(workspace, floor_count, sizeof(*workspace), compare_float);
floor = nearest_rank(workspace, floor_count, 0.20);
```

A percentile is more robust than the minimum: a single spectral notch cannot
make ordinary noise appear to be a strong carrier. If guard bins are not
available, the estimator falls back to the coarse search region.

The candidate must have at least 8 dB prominence:

```c
if (peak - floor < 8.0f)
    return 0;
```

### 3. Fine centroid

The fine estimate uses bins within ±50 kHz of the acquired peak. dBFS is
converted back to linear power and the guard-floor power is subtracted:

```c
weight = pow(10.0, bin_dbfs / 10.0) - floor_power;
weighted_frequency += frequency * weight;
total_weight += weight;

measured_frequency = weighted_frequency / total_weight;
```

Subtracting floor power prevents uniform background noise from pulling the
centroid toward the middle of the window. The window stays centered on the
detected peak rather than on the expected carrier: centering it on the expected
carrier instead would clip an offset channel asymmetrically and bias the
measured offset toward zero, which would corrupt the very correction being
measured. The result structure carries both the strongest-bin frequency and the
fine measured center, plus the peak, floor, and prominence used by the UI.

The centroid still wanders block to block on a modulated channel; that scatter
is handled statistically by the stability gate below, and is bypassed entirely
when the FCCH tone is available (see
[FCCH Tone Refinement](#fcch-tone-refinement)).

## Temporal Confidence And Stability

One accepted spectrum is not enough to change receiver calibration. The
application applies the following gates in `update_calibration_measurement()`:

```c
#define CALIBRATION_SETTLE_SECONDS 2.0
#define CALIBRATION_MIN_SECONDS 8.0
#define CALIBRATION_RECENT 64
#define CALIBRATION_MAX_SEM_PPM 1.0
```

### The measurement loop

Every spectrum block, while calibration is running, does exactly one of three
things:

1. **Settling.** For the first `CALIBRATION_SETTLE_SECONDS` (2.0 s) after any
   Start, Retune, or Apply, the block is discarded and the screen reports
   "Settling receiver...". This lets the tuner and AGC stabilize after the
   frequency jump before any sample counts.
2. **Carrier missing.** If `sdr_dsp_estimate_channel_center()` finds no
   isolated carrier at least 8 dB above the guard-band floor, the application
   **resets everything**: measurement count, the recent ring buffer, center,
   spread, and stable lock. A dropout discards all accumulated confidence rather
   than reusing stale samples.
3. **Carrier found.** The block contributes one measurement.

### What each accepted measurement records

Each accepted block computes an instantaneous residual and folds it into a
rolling 64-value history (`CALIBRATION_RECENT`):

```text
observed_ppm = (measured_frequency - expected_frequency)
               / expected_frequency × 1,000,000
```

From the buffered residuals the application derives a **robust** center and
spread (`robust_center_spread()`), resistant to a peak that momentarily hops to
an adjacent spectral feature:

- `recent_center` — the **median** residual. This is the value turned into the
  suggested correction.
- `recent_spread` — a normal-consistent scale estimate, `1.4826 × MAD` (median
  absolute deviation).
- `recent_sem` — the **standard error of the center**, `recent_spread /
  sqrt(count)`. This is the quantity displayed as *correction uncertainty* and
  the one the lock gate tests.

### The Stable lock gate

**Stable lock** requires all of these conditions simultaneously:

| Gate | Constant | Meaning |
| --- | --- | --- |
| `elapsed ≥ 8.0 s` | `CALIBRATION_MIN_SECONDS` | Minimum dwell since the last retune |
| `measurements ≥ 32` | — | Enough total accepted samples |
| `recent_count ≥ 32` | — | Ring buffer sufficiently filled |
| `recent_sem ≤ 1.0 PPM` | `CALIBRATION_MAX_SEM_PPM` | The correction is well determined |
| `prominence_db ≥ 8.0` | — | Carrier well above the noise floor |

Only when all five hold does the status flip from **Acquiring** to **Stable
lock**, and only then is **Apply PPM** meant to be trusted.

### Why the gate is on the standard error, not the raw spread

The quantity actually applied is the median residual, and how well *that* is
known is its standard error, `spread / sqrt(count)` — not the raw per-block
spread. On a modulated GSM channel the individual per-block estimates scatter
widely (6-9 PPM is normal) because the power center hops between spectral
features, yet the median of 32-64 such samples is determined to about
`spread / sqrt(count)` ≈ 1 PPM. Gating on the raw spread would demand that every
65 ms block be individually precise, which a modulated channel cannot deliver,
so the lock would never settle even though the correction is already good.

Gating on the standard error instead certifies *how well the correction is
known*. The median and MAD keep a hopping or bimodal peak from biasing or
inflating the estimate. This does assume the per-block samples are independent,
which is why no smoothing filter is applied to the spectrum feeding the
estimator.

When the FCCH tone is available the per-block residuals are already precise and
consistent, so the standard error falls below the threshold within a few
seconds.

### After Apply PPM

Applying resets the measurement counters and the settle timer and re-tunes with
the new correction. The application then measures the *residual*, which should
show a center near 0 PPM and must re-satisfy the same standard-error gate before
it is considered locked again.

These thresholds control repeatability, not truth. A stable receiver spur can
also be stable. The expected ARFCN must still be independently verified.

## FCCH Tone Refinement

When the selected channel is a BCCH carrier, its FCCH (Frequency Correction
Channel) burst is an all-zeros GMSK sequence, i.e. a **pure tone** at exactly
`+1625/24 kHz = +67 708.33 Hz` above the carrier center. A pure tone can be
located far more precisely than a modulated channel's power centroid and does
not wander, so it is used to refine the per-block carrier estimate.

`gsm_fcch_detect()` (in the DSP seam) slides a ~400 µs window across the
raw I/Q block, accumulates the lag-1 autocorrelation `Σ z[n]·conj(z[n−1])`, and
takes:

- the **angle** of that sum as an amplitude-weighted tone-frequency estimate;
- its magnitude divided by `Σ |z[n]||z[n−1]|` as a **coherence** in `[0, 1]`
  (near 1 for a pure tone, near 0 for modulation or noise).

It keeps the highest-coherence window whose frequency lands within the search
window of the expected FCCH offset, and reports a detection when the coherence
exceeds `GSM_FCCH_MIN_COHERENCE` (0.9).

`update_calibration_measurement()` runs the detector each block after the
centroid estimator (which still supplies the peak, floor, and prominence
metrics). With the receiver tuned 400 kHz below the carrier, the tone sits near
`+467.708 kHz` baseband; on a confident detection the carrier is recovered as:

```text
carrier_RF = applied_frequency + tone_frequency - 67708.33
```

### Homogeneous residual buffer

FCCH and centroid residuals differ by many PPM (the tone is unbiased; the
centroid can sit tens of kHz off), so the recent-residual buffer must never mix
them — mixing makes the median flip between two clusters and the lock jump. The
measurement tracks a **source** (`centroid` or `FCCH`) with this policy:

- FCCH detection runs **independently of the centroid**, so a momentary dip in
  centroid prominence never wipes an FCCH accumulation.
- When a tone is detected, the source switches to `FCCH` (resetting the buffer
  on entry so it stays homogeneous) and the FCCH-derived residual is recorded.
- FCCH bursts are intermittent (~5 per 235 ms), so an occasional block has no
  burst. While locked to the tone, such a block is **skipped** — the lock and
  the buffer are held, not polluted with a wandering centroid sample.
- Only after `CALIBRATION_FCCH_MISS_LIMIT` (12) consecutive burst-free blocks
  does the source revert to `centroid`, resetting the buffer again.

The 8 dB prominence requirement gates **centroid mode only**; a detected FCCH
tone is its own quality proof, so its lock is not blocked by a fluctuating
prominence metric.

The status line reports the active source and diagnostics that reveal whether a
real BCCH was selected: the standard error, the raw residual spread, the running
count of FCCH **hits** and consecutive **misses**, and the last tone
**confidence** (the detector coherence). A genuine BCCH shows a high confidence
(~0.95+), a small spread, and steadily climbing hits; a strong non-BCCH channel
shows marginal confidence and a large spread and will not hold an FCCH lock.
Note that the channel scan ranks channels by **power**, which is not the same as
being a BCCH — use these diagnostics to confirm the selection.

The implementation and remaining follow-ups (a waterfall tone marker and a UI
toggle) are described in
[fcch-tone-detection-plan.md](./fcch-tone-detection-plan.md).


## PPM Sign And Suggested Correction

The displayed residual is:

```text
residual_ppm =
    (measured_frequency - expected_frequency)
    / expected_frequency
    × 1,000,000
```

The correction update uses the opposite sign:

```c
new_ppm = current_ppm - round(residual_ppm);
```

This is implemented by `sdr_dsp_corrected_ppm()`. For example, if the
current correction is `+5 PPM` and the reference still measures `+20 PPM`
high, the next candidate is approximately `-15 PPM`.

The application clamps suggestions to the same `-1000..1000` range accepted
by the CLI and Settings. **Apply PPM** is enabled only after stable lock. After
application, acquisition and stability accumulation restart; the user should
wait for a second stable result and confirm that residual offset approaches
zero.

## Waterfall Markers And Results

`draw_calibration()` reuses the normal retained dBFS waterfall. It overlays:

- A green vertical line at the standardized expected downlink frequency.
- An amber vertical line at the current fine measured center.
- Configuration: expected frequency, tuned center, and current PPM.
- Measurement: measured frequency, signed offset, instantaneous observed PPM,
  the recent center, robust spread, and standard error.
- Confidence: peak dBFS, guard-floor dBFS, prominence, sample count, lock
  state, active source (FCCH tone or centroid), and suggested correction.

### Zoomed channel axis

In calibration mode the waterfall is zoomed to `expected ± 250 kHz`
(`CALIBRATION_VIEW_HALF_WIDTH_HZ`) rather than the full sampled span, clamped to
the sampled band. Only a sub-rectangle of the full-span waterfall texture is
drawn across the plot. This makes the target channel and the expected/measured
markers legible; at the full 2 MHz span a sub-10 kHz offset is smaller than one
pixel. The horizontal axis is labelled with GSM 900 ARFCN channel numbers, and
the expected/measured markers are rendered into the same zoomed span:

```text
x = plot_left
    + (frequency - display_lower)
      / (display_upper - display_lower)
      × plot_width
```

### Colour scale control

The waterfall colour range defaults to the full `SDR_DSP_DBFS_FLOOR` to
`SPECTRUM_TOP_DBFS` span, which crushes a 15 dB cellular signal into a small
part of the ramp. **Up** and **Down** raise and lower the lower colour bound in
the calibration screen exactly as in the standalone waterfall view
(`adjust_waterfall_scale()`), so the operator can tighten the range onto the
channel and make individual carriers visible. Number keys type the ARFCN, so
they do not conflict with this control.

The screen is a mode of the existing raylib window, not a second native
window. This keeps every raylib resource and draw call on the main thread.

## Tests

The hardware-free DSP checks (`tests/sdr_dsp_test.c` + `tests/gsm_dsp_test.c`) verify the DSP contract:

- ARFCN 113 → 957.6 MHz.
- ARFCN 117 → 958.4 MHz.
- ARFCN 120 → 959.0 MHz.
- ARFCN 125 is rejected.
- A synthetic offset carrier is estimated near its known center.
- Peak, guard floor, and prominence are reported.
- A flat spectrum is rejected.
- Positive and negative residuals produce corrections with the proper sign.
- A synthetic FCCH tone in noise is detected and its frequency recovered within
  tolerance; noise-only and out-of-window tones are rejected.
- A synthetic single-channel spectrum reduces to the correct per-ARFCN power,
  neighbouring channels stay at the noise level, and the accept window limits
  which channels are filled.

Run:

```sh
make check-raylib-dsp
```

The deterministic checks validate formulas and rejection behavior. They do
not validate that a real observed carrier is GSM, nor do they replace the
post-application residual measurement.

## Implementation Limitations

- The carrier estimate is either a power centroid or an FCCH-tone frequency; it
  does not demodulate or decode GSM SCH, BSIC, or BCCH data. FCCH detection
  recognizes the tone's shape, not its content.
- Adjacent-channel energy, multipath, asymmetric filtering, overload, and
  receiver spurs can bias the centroid; the FCCH-tone refinement is used when
  available because it avoids that bias.
- The correction belongs to one receiver and changes with crystal
  temperature.
- LTE and 5G require their own band/channel conversion and reference-feature
  estimators; a wide channel's visual center is insufficient.
- A known RF reference remains more authoritative than sample-count PPM from
  `rtl_test -p`.

## Required Local Information

Identify all of the following before calculating a correction:

- Mobile network operator.
- Radio technology: GSM, LTE, or 5G NR.
- Band number, such as GSM 900, LTE Band 3, or NR n78.
- Downlink channel number:
  - GSM: ARFCN.
  - LTE: EARFCN.
  - 5G NR: NR-ARFCN.
- Exact standardized downlink frequency for that channel.
- Confirmation that the observed transmission is a base-station downlink,
  not a handset uplink.

Possible information sources include a phone's engineering mode, modem
diagnostics, CellMapper, national spectrum assignments, or an appropriate
cellular scanner. Verify channel-to-frequency conversion against the relevant
3GPP band definition; do not infer an exact reference solely from a spectrum
peak.

## Choosing a Reference

Prefer, in order:

1. A continuous GSM BCCH carrier with a known ARFCN.
2. A known LTE downlink whose synchronization/reference position can be
   identified reliably.
3. A known 5G NR downlink whose synchronization signal block or channel center
   is known.

GSM BCCH is usually easiest because it is narrow, continuous, and referenced
to the base station clock. LTE and 5G channels are wide; their strongest peak,
occupied-band edge, or visual midpoint is not necessarily an accurate
frequency reference.

Do not use:

- The RTL-SDR center-frequency DC spike.
- An unidentified receiver spur.
- A handset uplink.
- A transient burst.
- An unknown narrow line.
- ADS-B transmissions.

## Measurement Procedure

1. Warm the receiver for at least 10 minutes. Crystal error changes with
   temperature.
2. Use a moderate manual gain. Avoid clipping and front-end overload.
3. Tune so the reference is 200-500 kHz away from the display center. This
   prevents confusion with the receiver's DC spike.
4. Disable DC removal only if it interferes with inspecting the chosen
   reference. It normally affects the tuning center, not an off-center
   carrier.
5. Allow the display and oscillator to settle.
6. Measure the reference frequency with the spectrum cursor. Average several
   readings rather than using one frame.
7. Record:
   - `known_hz`: standardized frequency derived from the channel and band.
   - `measured_hz`: frequency shown by the visualizer.
8. Calculate the observed error:

```text
observed_ppm = (measured_hz - known_hz) / known_hz * 1,000,000
```

Example:

```text
known_hz    = 950,000,000
measured_hz = 950,019,000

observed_ppm = 19,000 / 950,000,000 * 1,000,000
             = +20 PPM
```

9. Apply the candidate correction and repeat the measurement.
10. Verify the sign experimentally. If the reference moves farther from its
    known frequency, reverse the correction sign.
11. Keep the value that minimizes the residual frequency error.

## Correction Accuracy

One PPM corresponds to a frequency error proportional to RF frequency:

| Frequency | Error per PPM |
| ---: | ---: |
| 100 MHz | 100 Hz |
| 900 MHz | 900 Hz |
| 1090 MHz | 1.09 kHz |
| 1800 MHz | 1.8 kHz |

For example, a `+20 PPM` oscillator error corresponds to about `+21.8 kHz` at
1090 MHz.

## GSM

Use the BCCH ARFCN and the correct regional GSM band. An ARFCN is meaningful
only together with its band because channel-number ranges can overlap between
bands and extensions.

Confirm that:

- The channel is a downlink channel.
- The ARFCN-to-frequency formula matches the deployed GSM band.
- The measured carrier is the identified BCCH rather than an adjacent channel
  or receiver image.

GSM channel spacing is 200 kHz. A wrong ARFCN or band can therefore produce a
large, apparently plausible calibration error.

## LTE

Use the EARFCN together with the LTE band. The standardized downlink frequency
is derived from the band's frequency offset and EARFCN offset.

An LTE signal occupies many FFT bins. Do not use the visually strongest
subcarrier as the channel center. Prefer a decoder or scanner that identifies
the EARFCN and synchronization/reference structure. Verify the resulting
frequency against the 3GPP band table.

## 5G NR

Use the NR-ARFCN, band, and applicable frequency raster. NR-ARFCN conversion is
piecewise and depends on the standardized global raster. The synchronization
signal block can also be offset from other channel reference points.

Do not estimate correction from a wide NR channel's visual center unless the
network's channel and synchronization configuration are known. A decoder that
reports NR-ARFCN and SSB information is preferable.

## Sample-Clock Measurement Is Different

`rtl_test -p` estimates sample-clock error by comparing received sample count
with host elapsed time. The tuner frequency synthesizer and ADC often derive
from the same crystal, so that estimate is a useful starting point, but it is
not guaranteed to equal RF frequency error exactly.

Use `rtl_test -p` as a candidate value, then verify it against a known RF
reference as described above.

## Recording Results

Record calibration context with the result:

```text
Receiver serial:
Tuner:
Warm-up time:
Reference technology/band/channel:
Known frequency:
Measured frequency before correction:
Applied correction:
Measured residual error:
Receiver temperature or ambient conditions:
Date:
```

PPM is device-specific and can change with temperature. Do not assume the same
value applies to another RTL-SDR board, even if both use the same tuner.
