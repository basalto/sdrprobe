# sdrprobe architecture

A raylib front end over an RTL-SDR receiver: it acquires raw I/Q, surveys the
radio environment, shows one tuning four ways, recovers transmitted information
from several radio technologies, and calibrates the receiver's frequency error
against cellular references.

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
                    │   frame + input + draw dispatch           │
                    └───────────────┬──────────────────────────┘
                 ┌──────────────────┼───────────────────┐
                 ▼                  ▼                   ▼
              ┌────────────────┐  ┌──────────────┐  ┌──────────────────┐
              │ view_survey.c  │  │ view_*.c     │  │ overlay_*.c      │
              │ view_scope.c   │  │ decoders     │  │ global tools     │
              └───────┬────────┘  └──────┬───────┘  └────────┬─────────┘
                └──────────────────┼───────────────────┘
                     ┌─────────────▼──────────────┐
                     │ sdrgui_*.c   components    │
                     │ plain data + geometry only │
                     └────────────────────────────┘
                                   │
                     ┌─────────────▼──────────────┐
                     │ sdr_dsp.c   generic core   │
                     │ technology DSP + decoders  │
                     └────────────────────────────┘
```

## Layers, and what each may know

**DSP** (`sdr_dsp.c` and the technology modules) knows samples and transmitted
information, not application state. A generic core supplies byte→float I/Q,
DC removal, magnitudes, spectra, carrier estimates and channel-power reduction;
technology modules add their own detection, demodulation and decoding chains.
The FFT is deliberately not FFTW or liquid-dsp (ADR-0003). DSP checks link
`-lm` only, no raylib and no librtlsdr — anything added here must keep that
true.

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

**Screens** (`view_survey.c`, `view_scope.c`, and the technology-specific
`view_*.c` files, plus `overlay_*.c`) read state, draw, and handle their own
input. `view.h` declares what they share.

**Layout** (`*_layout.h`) is expressed as pure functions of the window size,
which is what lets `make check-layout` pin rectangles without opening a window.

## State

`struct app` keeps genuinely shared state and delegates each area's private
state to a named substructure:

| struct | lives in | holds |
| --- | --- | --- |
| `acquisition` | `acquisition.h` | block slot, worker, recording |
| `scope_view` | `app.h` | GPU textures, history, per-view scales |
| `survey_view` | `app.h` | sweep, candidates, history, confirmation |
| `gsm_view` | `app.h` | inspected channel, last SCH decode, options |
| `adsb_view` | `app.h` | position-pairing cache, message log |
| `lte_view` | `app.h` | band scan, cell search, broadcast state |
| `fm_view` | `app.h` | station scan, RDS state, audio playback |
| `tetra_view` | `app.h` | symbol stream, network messages, analysis |
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
declared beside the record they were carved out of. `receiver_lease.h` is the
second: plain integers, no receiver and no window, so it is checked without
either.

### Who borrowed the tuning

`applied_frequency` and `applied_sample_rate` say where the receiver *is*.
Where it is to be **put back** is a different question, and it used to be
answered nine times: every screen that retunes kept its own `return_frequency`
and restored it itself. Each was individually right and no rule connected them,
so nothing said that overlapping owners give the receiver back in the reverse
order they took it — and the overlaps are real. LTE opens calibration; GSM
starts a band scan; the survey asks its candidates again; the automatic drift
check interrupts whatever is on screen.

`struct receiver_lease` is that rule: a strict LIFO stack of prior
`{center_hz, sample_rate_hz}` snapshots, with each owner holding an opaque
token rather than a frequency. Screens call the five helpers in `view.h` —
borrow, borrow-and-tune, restore-while-held, return, commit — and never a bare
`retune_receiver()` for a tuning they mean to undo.

Three properties are load-bearing:

- **Out of order is refused**, changing neither hardware nor the stack. An
  owner returning out of turn would put back a frequency belonging to somebody
  else, which is silent and looks exactly like a correct return.
- **Returning is two-phase.** The receiver can refuse a frequency it accepted a
  minute ago, so the pop waits for the restore to succeed and a failure leaves
  the token live and retryable. The peek takes a `const` lease, so a one-phase
  return is a compile error rather than a check failure.
- **PPM is not leased.** Applying a calibration is deliberate persistent state
  that must survive every return, including the return of the overlay that
  measured it. A snapshot has no third field to undo it with, and restores use
  the *current* correction.

Giving the claim up without restoring is `receiver_commit()`, and it is written
as a commit rather than a cleared flag so that "keep this tuning" and "I forgot
to put it back" do not look the same in the source. The survey's **Open
waterfall** is what it is for.

## Two bounded contexts

`CONTEXT-MAP.md` splits the domain. The **Probe** context acquires, surveys and
measures signals, stopping before modulation is interpreted as transmitted
information. The **Decoder** context owns that interpretation, including
synchronisation identities, symbols and bits, messages, and audio (ADR-0021).
The vocabulary is enforced by glossaries with explicit `_Avoid_` lines: the
scatter view is never a "constellation" in Probe language, and a sample block
is never a "packet".

## Verifying changes

```
make check-touched     checks selected from the files changed
make check-dsp         hardware-free DSP checks, -lm only
make check-layout      screen geometry without opening a window
make check-pipelines   assembled program over recorded captures
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

`docs/adr/` records architectural decisions. The ones that constrain new work
most:

- **0023** DSP is a generic core plus technology modules with variable interfaces
- **0002** the acquisition handoff is one overwriteable slot, not a queue
- **0003** the DSP is self-contained; no external DSP library
- **0004** calibration's stability gate needs a source-homogeneous residual buffer
- **0005** the rendering seam carries raw centred I/Q, not a reduced frame
- **0007** presentation components never see application state
- **0011** *superseded* — a soft-decision receiver was specified to fix the SCH
  frame number; the real fault was the field layout, and the measurement that
  rejected the proposed fix is recorded with it
- **0012** every decision the program makes is reachable by a check that needs
  no window, no receiver and no person
- **0014** LTE runs on LTE's own 1.92 MS/s grid and refuses anything else
- **0015** the band plan is a lookup; a carrier inside an allocation has not
  thereby been identified
- **0018** calibration profiles identify both receiver and receiving site
- **0019** survey confirmation preserves intermittent as a third verdict
- **0020** Survey is a top-level tab while remaining part of Probe
- **0021** Decoder owns transmitted information, not only messages
- **0022** receiving setup history is keyed by receiver, site, and antenna
- **0023** technology DSP modules share boundaries, not a uniform interface
