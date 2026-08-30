# sdrprobe architecture

A raylib front end over an RTL-SDR receiver: it acquires raw I/Q, shows it four
ways, decodes two radio technologies, and calibrates the receiver's frequency
error against a GSM 900 carrier.

For dump1090's internals — the acquisition conventions this program copies, and
the Mode S physical layer — see `dump1090-reference.md`. That source is not in
this repository.

## The shape of it

```
                    ┌──────────────────────────────────────────┐
   RTL-SDR  ──────► │ acquisition.c        worker thread       │
   or --file        │   256 KB blocks -> one overwriteable slot│
                    │   recording tees off here, not later     │
                    └───────────────┬──────────────────────────┘
                                    │ consume_latest()
                    ┌───────────────▼──────────────────────────┐
                    │ sdrprobe.c           frame loop          │
                    │   process_block(), input, draw dispatch   │
                    └───────────────┬──────────────────────────┘
                 ┌──────────────────┼───────────────────┐
                 ▼                  ▼                   ▼
        ┌────────────────┐  ┌──────────────┐  ┌──────────────────┐
        │ view_scope.c   │  │ view_gsm.c   │  │ overlay_*.c      │
        │ view_adsb.c    │  │              │  │ calibration/scan │
        └───────┬────────┘  └──────┬───────┘  └────────┬─────────┘
                └──────────────────┼───────────────────┘
                     ┌─────────────▼──────────────┐
                     │ sdrgui_*.c   components    │
                     │ plain data + geometry only │
                     └────────────────────────────┘
                                   │
                     ┌─────────────▼──────────────┐
                     │ sdr_dsp.c   generic core   │
                     │ gsm_dsp.c   adsb_dsp.c     │
                     └────────────────────────────┘
```

## Layers, and what each may know

**DSP** (`sdr_dsp.c`, `gsm_dsp.c`, `adsb_dsp.c`) knows nothing but samples. A
generic core supplies byte→float I/Q, DC removal, magnitudes, a hand-written
2048-point FFT to dBFS, a power centroid and a channel-power reducer; a
per-technology plugin adds a channel map and a reference-tone detector, and
reuses the core for the rest (ADR-0001). The FFT is deliberately not FFTW or
liquid-dsp (ADR-0003). The DSP checks link `-lm` only, no raylib and no
librtlsdr — anything added here must keep that true.

**Acquisition** (`acquisition.c`) owns a worker thread and `struct acquisition`.
Blocks pass to the renderer through one mutex-guarded overwriteable slot, not a
queue: a slow renderer drops blocks rather than lagging (ADR-0002). It includes
no application header and names no `struct app`; the device handle, playback
file and sample rate are handed to it by `acquisition_attach_source()` before a
worker starts. Recording writes from this thread, upstream of the lossy slot,
because a capture that inherits those drops is spliced and says nothing about it.

**Presentation** (`sdrgui_plot.c`, `sdrgui_scope.c`, `sdrgui_decode.c`,
`sdrgui_widgets.c`) takes plain data and geometry, never `struct app`
(ADR-0007). Every chart draws entirely inside the rectangle it is given,
reserving its own caption strip and label gutter through
`sdrgui_chart_area()` — a caller cannot compute that clearance, because label
width depends on the values only the component sees.

**Screens** (`view_scope.c`, `view_gsm.c`, `view_adsb.c`,
`overlay_calibration.c`, `overlay_scan.c`, `overlay_settings.c`,
`overlay_help.c`) read state, draw, and handle their own input. `view.h`
declares what they share.

**Layout** (`gsm_layout.h`, `chrome_layout.h`) are pure functions of the window
size, which is what lets `make check-layout` pin every rectangle without opening
a window.

## State

`struct app` was one 188-field record every file read. It is now 74 fields, of
which most are genuinely shared, and each area owns the rest:

| struct | lives in | holds |
| --- | --- | --- |
| `acquisition` | `acquisition.h` | block slot, worker, recording |
| `scope_view` | `app.h` | GPU textures, history, per-view scales |
| `gsm_view` | `app.h` | inspected channel, last SCH decode, options |
| `adsb_view` | `app.h` | position-pairing cache, message log |
| `calibration` | `app.h` | measurement, stability gate, drift re-check |
| `band_scan` | `app.h` | sweep position and step width |
| `settings_panel` | `app.h` | the text being typed, before it is applied |
| `help_overlay` | `app.h` | open flag, current topic, scroll position |

What stays in `struct app` is the handoff between screens — the scan tells the
GSM view which channel to open, calibration publishes the result that view
displays — plus the tuning every screen reads. Those are relationships between
screens, not one screen's private business.

Only `acquisition` is a module in the full sense: its state lives with its code
and it names nothing of the application. The others own their fields but are
declared beside the record they were carved out of.

## Two bounded contexts

`CONTEXT-MAP.md` splits the domain. The **Probe** context acquires and inspects
signals and stops at statistics — it never claims to have decoded a message.
The **Decoder** context starts where bits become a message. The vocabulary is
enforced by glossaries with explicit `_Avoid_` lines: the scatter view is never
a "constellation" in Probe language, a sample block is never a "packet".

## Verifying changes

```
make check-dsp      three hardware-free DSP checks, -lm only
make check-layout   every rectangle at five window sizes
make probe-gsm-chain   a walk through the SCH decode chain (diagnostic)
```

The DSP checks include two real captures. Those assertions matter more than
they look: the SCH decoder's field layout was wrong for months while every
synthetic test passed, because the test's encoder shared the decoder's mistake.
What caught it was checking the decode against something the code cannot
influence — the burst's own position in the capture. A check that only compares
the program against itself will agree with any consistent error.

Layout has the same property. It was tuned by eye until `check-layout` pinned
it; now a change that moves a rectangle says which one and by how much.

## Decisions

`docs/adr/` records eleven. The ones that constrain new work most:

- **0001** DSP is a generic core plus per-technology plugins
- **0002** the acquisition handoff is one overwriteable slot, not a queue
- **0003** the DSP is self-contained; no external DSP library
- **0004** calibration's stability gate needs a source-homogeneous residual buffer
- **0005** the rendering seam carries raw centred I/Q, not a reduced frame
- **0007** presentation components never see application state
- **0011** *superseded* — a soft-decision receiver was specified to fix the SCH
  frame number; the real fault was the field layout, and the measurement that
  rejected the proposed fix is recorded with it
