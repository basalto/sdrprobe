# Implementation: `rtl_raylib` Signal Visualizer

## Status

This document is the implementation and verification contract for the
`rtl_raylib` executable, now the repository's sole application. (The earlier
`rtl_init` one-shot probe and `rtl_tui` terminal visualizer, referenced
historically below, have since been removed.)

## Purpose

`rtl_raylib` is a live and capture-backed visualizer for raw RTL-SDR sample
streams. It provides four views:

1. **Magnitude view**: peak magnitude over time.
2. **Spectrum view**: averaged complex-signal power with peak hold.
3. **I/Q scatter view**: a fading distribution of centered I/Q pairs.
4. **Waterfall view**: frequency power over time, newest spectrum first.

The probe acquires and visualizes signal activity. It does not demodulate or
decode transmitted messages.

## Scope

### In scope

- Live acquisition from the first supported RTL-SDR receiver.
- Hardware-free playback from raw unsigned 8-bit interleaved I/Q captures.
- Configurable center frequency, sample rate, and live receiver gain.
- A resizable raylib window with diagnostic status and labeled plot axes.
- Deterministic DSP checks that do not require raylib, a window, or hardware.

### Out of scope

- Mode S or ADS-B demodulation and decoding.
- Calibrated RF power such as dBm.
- Device selection, recording, and interactive retuning.
- FFTW or another external DSP dependency.

## Command Line

```text
rtl_raylib [--frequency Hz|K|M|G] [--sample-rate samples_per_second]
             [--gain max|auto|dB] [--ppm signed_integer]
             [--file capture.bin]
```

Defaults:

| Option | Default | Meaning |
| --- | ---: | --- |
| `--frequency` | `1090000000` | Center frequency in Hz |
| `--sample-rate` | `2000000` | I/Q pairs per second |
| `--gain` | `max` | Maximum supported manual gain |
| `--ppm` | `0` | Signed tuner frequency correction |

Frequency accepts plain integer Hz or a decimal value with a case-insensitive
SI suffix: `K` = 1,000, `M` = 1,000,000, and `G` = 1,000,000,000. For example,
`1090000000`, `1090M`, and `1.09G` select the same center frequency. The
Settings field accepts the same forms.

Only `--file` selects capture playback; positional paths are invalid. Reject
duplicate options, unknown options, invalid numbers, and conflicting source
arguments with a usage message and nonzero exit status. Frequency and sample
rate must parse as positive integers representable by `uint32_t`; this also
prevents zero-rate capture pacing.

In receiver mode, all three settings configure hardware. A numeric gain is in
dB and must exactly match a gain reported by the receiver (librtlsdr reports
tenths of a dB); otherwise print the supported values and fail. `auto` selects
automatic gain and `max` selects the greatest supported manual gain.

In file mode, frequency and sample rate describe the raw capture. Frequency
defines the spectrum axis and sample rate defines both that axis and playback
pacing. `--gain` is invalid because gain cannot change recorded samples.

## Files

### New files

- `src/rtl_raylib.c`: CLI, signal sources, latest-block handoff, raylib lifecycle,
  view state, drawing, and orderly shutdown.
- `src/sdr_dsp.h` / `src/sdr_dsp.c`: generic SDR DSP core — raw I/Q conversion, magnitude
  reduction, Hann windowing, radix-2 complex FFT, and spectrum calculation.
- `src/gsm_dsp.h` / `src/gsm_dsp.c`: GSM technology plugin (ARFCN map, FCCH tone
  detector) layered on the generic core; see
  `docs/adr/0001-technology-plugin-dsp-architecture.md`.
- `tests/sdr_dsp_test.c` / `tests/gsm_dsp_test.c`: deterministic checks against the
  production DSP modules.

### Updated files

- `Makefile`: add optional `rtl_raylib` and the `check-sdr-dsp`/`check-gsm-dsp`
  (aggregated by `check-dsp`, aliased `check-raylib-dsp`) targets without
  changing what default `make` builds.
- `AGENTS.md`: describe the new implementation specification.

## Signal Conventions

- A sample stream is unsigned 8-bit interleaved `I0 Q0 I1 Q1 ...`.
- One I/Q pair is one complex sample.
- Center raw bytes at `127.5`, producing I and Q in `[-127.5, 127.5]`.
- Magnitude is `sqrt(I*I + Q*Q)`, in `[0, about 180.31]` sample units.
- The standard sample block is `16*16384` bytes (256 KiB): 131,072 I/Q
  pairs, or 65.536 ms at the default 2 MS/s.
- Complex sampling at rate `Fs` covers `Fs` Hz, from approximately
  `center - Fs/2` through `center + Fs/2`.

`src/sdr_dsp.c` is the single owner of byte centering and DSP
normalization. Drawing code receives centered or derived values and must not
reinterpret raw bytes.

## Architecture

### Threads

There are two application threads:

1. **Main thread**: owns every raylib call, polls the latest sample block,
   processes new data, and redraws at 60 FPS.
2. **Acquisition worker**:
   - Receiver mode calls blocking `rtlsdr_read_async()`. Its callback executes
     in this acquisition path and publishes each callback buffer.
   - File mode fills and publishes paced sample blocks from the capture.

Do not call `rtlsdr_read_async()` on the main thread. It blocks until
`rtlsdr_cancel_async()` is called from another thread.

### Latest-block handoff

Use one mutex-guarded overwriteable slot, not a FIFO ring:

```c
#define SAMPLE_BLOCK_BYTES (16 * 16384)

struct latest_block {
    unsigned char data[SAMPLE_BLOCK_BYTES];
    uint32_t len;
    uint64_t generation;
    uint64_t published_blocks;
    uint64_t processed_blocks;
    uint64_t overwritten_blocks;
    uint64_t malformed_blocks;
    int ready;
    int worker_done;
    int worker_failed;
    char worker_error[160];
    pthread_mutex_t mutex;
};
```

Publishing validates the byte count, copies complete I/Q pairs into the slot,
increments `generation`, and marks it ready. Publishing over unread data
increments `overwritten_blocks`; replacing stale data is intentional because
the live display values freshness over continuity.

The callback buffer is owned and reused by librtlsdr and is valid only for the
callback duration, so publishing must copy it before returning. Reject a
callback larger than `SAMPLE_BLOCK_BYTES`. Ignore one unmatched trailing byte
from an odd callback length and increment `malformed_blocks`. Zero-length and
oversized callbacks are rejected and also increment `malformed_blocks`; they
do not increment `published_blocks`.

Once per render iteration, the main thread briefly locks the slot. If its
generation is new, it copies the bytes and length into main-thread storage,
marks the slot consumed, unlocks, and processes that sample block. It does not
repeat conversion, FFT work, peak updates, or scatter insertion when no new
sample block is available; it only redraws cached results and applies
time-based visual decay. Increment `processed_blocks` exactly once after a
new generation has been copied successfully. All counters are monotonic and
read under the same mutex.

### Source parity

Receiver and file workers publish through the same latest-block function, so
all downstream processing is source-independent.

File playback:

- Reject a capture with fewer than one complete I/Q pair.
- If the file ends with one unmatched byte, warn once and ignore that byte.
- Preserve I/Q-pair alignment while wrapping valid bytes from the start to
  fill each standard sample block.
- Loop by default.
- Pace with monotonic-clock deadlines according to
  `published_pairs / sample_rate`; a standard block at 2 MS/s advances the
  deadline by 65.536 ms. Do not sleep 131.072 ms.

### Receiver setup

Preserve the existing policy, not the unchecked implementation:

1. Require at least one supported receiver and open device index 0.
2. Enumerate supported gains before selecting manual gain.
3. Apply gain mode and gain.
4. Apply requested center frequency and sample rate.
5. Reset the receiver buffer after configuration.
6. Read back the applied center frequency and sample rate. In manual mode,
   read back gain for display and compare it with the selected value when the
   selected value is nonzero. librtlsdr reports `0` for either a valid 0 dB
   gain or a getter failure, so successful gain-mode/gain setters plus the
   advertised gain list are authoritative for a selected 0 dB value.
   librtlsdr does not expose a gain-mode getter; retain the successfully
   applied mode as acquisition state.

Check every librtlsdr return value and identify the failed operation. Require
the reported sample rate to equal the request and center frequency to be
within 1 kHz. On mismatch, print requested and reported values and fail.
Always use reported settings for the HUD and axes after validation.

Every initialization step has a corresponding ownership flag and one reverse-
order cleanup path. Failures while parsing, opening/configuring the receiver,
initializing the mutex, creating the window or render texture, or starting the
worker release only resources whose initialization completed. Check raylib
resource readiness before entering the render loop.

### Shutdown and errors

Normal receiver shutdown order:

1. Main thread observes `Esc`, window close, or a worker failure.
2. Set the shared stop flag.
3. Call `rtlsdr_cancel_async()`.
4. Join the acquisition worker.
5. Destroy synchronization state and close the receiver.
6. Unload raylib render textures and close the window.

Check the return from `rtlsdr_cancel_async()`. If cancellation fails but the
worker has already finished, join and clean up normally. If it fails while the
worker may still be inside `rtlsdr_read_async()`, report the cancellation
failure and exit nonzero without destroying state the worker can still access;
process termination is the final fallback because librtlsdr provides no safe
forced-cancellation contract. Never block indefinitely in an unconditional
join after a failed cancellation.

File mode sets the stop flag and joins its worker before releasing file and
GUI resources. No worker may access the slot, receiver, file, or GUI resources
after they are destroyed.

If acquisition fails after the window opens, publish the error in shared
state. The main thread renders it in the HUD for one cycle, then performs
orderly shutdown and returns nonzero.

## Frame Processing

After a new sample block is copied to main-thread storage:

1. Center all complete I/Q pairs and calculate their magnitudes once.
2. Update cached data for the magnitude view.
3. Calculate and cache a new spectrum if at least one full FFT window exists.
4. If the I/Q scatter view is active, insert a bounded, deterministic subset
   of the new I/Q pairs.

Short blocks are valid. Magnitude and I/Q scatter use all complete pairs. The
spectrum uses every complete 2048-pair window and retains its previous trace
if no complete window fits.

Before the first sample block, render empty axes and a `waiting for samples`
status. Never draw uninitialized arrays.

## View 1: Magnitude

The magnitude view is selected initially and by key `1`.

- Horizontal axis: elapsed time across the current sample block, derived from
  the applied sample rate.
- Vertical axis: absolute magnitude in centered sample units.
- Reduction: divide the sample block into at most one time bin per drawable
  horizontal pixel and retain the peak magnitude in each bin. Peak reduction
  preserves brief signal activity that averaging could hide.
- Manual range: start at `0..64` sample units. Up narrows and Down widens the
  upper bound, clamped to `1..180.31`; the lower bound remains zero.
- Labels: show both manual bounds on the axis and show absolute minimum, mean,
  and maximum magnitude in the HUD. The scale is not calibrated power.

Recompute time bins when a new sample block arrives or drawable width changes.

## View 2: Spectrum

The spectrum view is selected by key `2`.

```c
#define FFT_SIZE 2048
#define PEAK_DECAY_DB_PER_SECOND 20.0f
#define DBFS_FLOOR -120.0f
```

### Per-block calculation

For every non-overlapping complete 2048-pair window in the sample block (64
windows for a standard block):

1. Normalize centered I and Q by `127.5` and form complex input `I + jQ`.
2. Apply a Hann window. It reduces spectral leakage; it does not eliminate it.
3. Run a self-contained radix-2 2048-point complex FFT.
4. Normalize each complex FFT bin by the sum of the Hann coefficients.
5. Compute linear power `re*re + im*im` for each bin.

For each frequency bin, cache:

- The arithmetic mean of linear power across all complete windows, converted
  afterward with `10*log10(power)` and clamped to `DBFS_FLOOR`.
- The greatest per-window linear power, converted to dBFS, as the candidate
  for peak hold.

Do not average dB values. Averaging windows reduces variance; do not claim it
lowers the mean noise floor by 6 dB.

### dBFS definition

A bin-centered complex tone whose normalized magnitude is `1.0` is 0 dBFS
after Hann coherent-gain compensation. This deterministic reference does not
imply calibrated RF power. Individual centered I/Q pairs near square corners
can have magnitude greater than 1.0 and need not be clamped before the FFT.

### Frequency ordering and peak hold

FFT-shift the displayed bins so negative frequencies appear left of the
applied center frequency and positive frequencies appear right. Label the
axis from `center - sample_rate/2` to `center + sample_rate/2`; at 2 MS/s the
span is 2 MHz and bin spacing is approximately 976.5625 Hz.

Peak hold decays by 20 dB per elapsed wall-clock second, independent of render
rate and sample-block arrival rate. Materialize elapsed decay before comparing
the stored peak with a new per-block candidate. Draw the averaged trace and
peak-hold overlay distinctly.

## View 3: I/Q Scatter

The I/Q scatter view is selected by key `3`.

- Symmetric horizontal I and vertical Q axes normalized by `127.5`, initially
  `[-0.5, 0.5]`. Up narrows and Down widens both axes, clamped to bounds from
  `0.01` through `1.0` full scale. Use collision-aware labeled major ticks and
  lighter minor subdivisions.
- Insert no more than 4,096 deterministic, evenly spaced I/Q pairs from each
  new sample block. Do not use random sampling.
- Retain bounded CPU-side point batches for one second and reproject them into
  a persistent `RenderTexture2D` each frame, so historical points remain
  consistent when the manual axes change.
- Draw high-opacity 3-pixel points in a warm gold-to-orange scale, increasing
  brightness with distance from the origin. Fade by wall-clock age while
  retaining enough opacity for the oldest visible points to remain legible.
- Insert points only for new sample blocks, never repeatedly from cached data.
- Clear history when entering the view and when recreating the texture after
  a resize.

Create, clear, draw to, and unload the render texture only on the main thread.
Raylib render textures are vertically inverted when drawn to the screen; use
a source rectangle with negative height.

## Window, Controls, and HUD

- Create a resizable window and target 60 FPS.
- Start in the magnitude view.
- `1`: magnitude view.
- `2`: spectrum view.
- `3`: I/Q scatter view.
- `4`: waterfall view.
- Up/Down: narrow/widen the active view's manual scale. Magnitude changes its
  upper magnitude bound, spectrum changes its lower dBFS bound, scatter
  changes its symmetric full-scale bound, and waterfall changes its lower
  color-map dBFS bound and recolors retained dBFS history.
- `Settings` button or `s`: open an in-window panel for center frequency and
  live receiver gain. Applying receiver settings stops acquisition, configures
  and resets the receiver, then restarts acquisition; capture mode treats
  frequency as metadata and disables gain.
- Settings includes a signed PPM field and launches a dedicated cellular
  calibration screen.
- The Settings panel includes a default-on **Remove DC spike** toggle. It
  subtracts each sample block's mean I and Q only on the copied
  spectrum/waterfall path; magnitude and I/Q scatter continue to use the raw
  centered samples.
- `Esc` or window close: orderly exit.
- `SIGINT` or `SIGTERM`: set the stop flag and follow the same orderly shutdown
  path; signal handlers must not call raylib, pthread, or librtlsdr functions.
- On resize, recompute plot geometry and recreate the I/Q render texture.
- While the pointer is inside a chart, draw a crosshair and tooltip showing
  that view's horizontal and vertical axis values. Spectrum and waterfall
  tooltips also show signed frequency offset from the applied center frequency.

Every view displays:

- Signal source and acquisition state.
- Applied or capture-described center frequency and sample rate.
- Applied receiver gain, or `capture` in file mode.
- Current view and FPS.
- Published, processed, overwritten, and malformed block counts, plus worker
  completion/failure state.
- View-specific axis range and summary values.
- Signal quality from the latest sample block: p10 magnitude as a visual noise
  floor, p99.5 magnitude as signal level, their amplitude ratio in dB as an
  estimated SNR, the percentage of I/Q pairs touching an ADC rail, and
  full-scale component headroom in dB. These are diagnostic digital metrics,
  not calibrated RF measurements or decoded-message quality.

These diagnostics must make quiet input distinguishable from stalled
acquisition.

## Cellular Calibration

The calibration screen is a dedicated application screen reached from
Settings. The technology selector displays 2G, 4G, and 5G; this implementation
supports 2G GSM 900 only and marks the others unavailable.

- Band: GSM 900.
- Channel input: ARFCN 1-124.
- Downlink conversion: `935 MHz + ARFCN * 200 kHz`.
- Real-test channels: ARFCN 113 = 957.6 MHz, 117 = 958.4 MHz, and
  120 = 959.0 MHz.
- Requires a live receiver and at least 1 MS/s.
- Start tunes 400 kHz below the expected carrier, placing the channel away
  from the receiver's center-frequency DC artifact.
- The screen shows configuration, current correction, measured carrier,
  signed offset, observed/recent PPM, peak, guard-band floor, prominence,
  recent spread, and suggested correction.
- The waterfall marks expected and measured carrier positions.

Measurement uses coarse peak acquisition within +/-100 kHz of the expected
carrier, then a power-weighted centroid within +/-50 kHz of that peak. When the
BCCH's FCCH pure tone is present it refines the carrier instead of the centroid.
The noise estimate is a robust percentile from adjacent guard bands. Flat
spectra, weak prominence, and boundary candidates are rejected.

Discard the first two seconds after tuning. Enable Apply PPM only after at
least eight seconds, at least 32 accepted measurements, at least 8 dB
prominence, and a correction uncertainty (standard error of the median residual)
no greater than 1 PPM. See
[docs/adr/0004-calibration-stability-gate.md](./adr/0004-calibration-stability-gate.md)
for why the gate tests the standard error of a robust center rather than raw
standard deviation. Applying a correction restarts measurement to show residual
error. Back restores the pre-calibration center frequency while retaining an
applied PPM correction.

This is a power-center estimate, not a GSM decoder or proof that the selected
carrier is a BCCH. Confirm the ARFCN independently and compare residual error
after application.

## Makefile Integration

Default `make` builds `rtl_raylib`, so raylib and librtlsdr development headers
are required. The DSP checks link only the hardware-free core:

```make
SRC=src
TESTS=tests

rtl_raylib: $(SRC)/rtl_raylib.c $(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c \
		$(SRC)/sdr_dsp.h $(SRC)/gsm_dsp.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags raylib) -pthread \
		-o $@ $(SRC)/rtl_raylib.c $(SRC)/sdr_dsp.c $(SRC)/gsm_dsp.c \
		$(LDFLAGS) $(LDLIBS) $(shell pkg-config --libs raylib) -pthread

check-sdr-dsp: $(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	$(CC) $(CFLAGS) -I$(SRC) -o sdr_dsp_test \
		$(TESTS)/sdr_dsp_test.c $(SRC)/sdr_dsp.c -lm
	./sdr_dsp_test

check-gsm-dsp: $(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/gsm_dsp.h \
		$(SRC)/sdr_dsp.c $(SRC)/sdr_dsp.h
	$(CC) $(CFLAGS) -I$(SRC) -o gsm_dsp_test \
		$(TESTS)/gsm_dsp_test.c $(SRC)/gsm_dsp.c $(SRC)/sdr_dsp.c -lm
	./gsm_dsp_test

check-dsp: check-sdr-dsp check-gsm-dsp
check-raylib-dsp: check-dsp
```

Extend `clean` to remove `rtl_raylib`, `sdr_dsp_test`, and `gsm_dsp_test`. The
DSP checks must not include raylib headers or require raylib through pkg-config.

Raylib remains the only new package required to build the GUI target:

```sh
pkg-config --cflags --libs raylib
make rtl_raylib
```

## Verification

### Build and playback

1. `make` builds `rtl_raylib` (requires raylib + librtlsdr dev headers).
2. `make clean` removes all project and test executables.
3. `rtl_raylib --file testfiles/modes1.bin` runs paced, hardware-free playback.

### Deterministic DSP checks

`make check-raylib-dsp` must verify at least:

1. Raw `127` and `128` bytes center to `-0.5` and `+0.5`.
2. Magnitude is calculated from both centered components.
3. A synthetic bin-centered complex tone lands in the expected FFT-shifted
   frequency bin.
4. A normalized unit-magnitude complex tone measures 0 dBFS within a stated
   floating-point tolerance after Hann compensation.
5. Linear-power averaging occurs before dB conversion.
6. Empty, short, odd-length, and standard sample blocks do not overrun arrays
   or process an unmatched byte as an I/Q pair.

### Hardware-free GUI

```sh
./rtl_raylib --file testfiles/modes1.bin
```

Verify that playback loops, advances at approximately 2 million I/Q pairs per
second, all four views update, axes are labeled, resizing is safe, and keys
`1`/`2`/`3`/`4` switch views. Confirm that no source edit or temporary hack is
required.

Also run a generated raw complex-tone capture with matching `--frequency` and
`--sample-rate`; verify its spectrum peak appears at the expected labeled
frequency.

### Live receiver

1. Valid requested settings are applied and reported in the HUD.
2. Unsupported gain or materially mismatched settings fail with useful
   requested/reported values.
3. Overwritten blocks increase under induced render load while the display
   continues to process the newest data.
4. `Esc`, window close, worker failure, and signals cleanly stop and join the
   worker, close the receiver, and release raylib resources when librtlsdr
   cancellation succeeds. A cancellation failure follows the documented
   nonzero process-exit fallback without freeing worker-accessible state.
