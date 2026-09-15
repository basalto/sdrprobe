# sdrprobe implementation contract

## Status

`sdrprobe` is the repository's application: an RF awareness probe with a
raylib window and equivalent scripted entry points. It acquires live or
recorded I/Q, surveys the radio environment, measures unknown signals, and
recovers transmitted information from supported technologies.

This document states the implementation guarantees. The complete module and
state relationships are in [ARCHITECTURE.md](ARCHITECTURE.md); the exhaustive
operator and contributor reference is [AGENTS.md](../AGENTS.md); build and
diagnostic recipes are in [CLAUDE.md](../CLAUDE.md).

## Product surfaces

The window has three peer tabs:

- **Survey** sweeps a range, groups maxima into signals, confirms uncertain
  findings, watches for change, and persists a completed survey.
- **Scope** shows the current tuning as magnitude, spectrum, I/Q scatter, or a
  waterfall.
- **Decode** provides FM, Mode S / ADS-B, GSM, LTE, TETRA, and SRD views.

Settings, Calibration, Help, and startup are overlays rather than tabs. A
plain windowed receiver run starts by establishing the installation: receiver,
site, antenna, gain, and a frequency reference. Survey is the normal landing
tab after that startup flow.

The command line is a peer interface, not a reduced demo. Headless paths can
survey, confirm, watch, save, record, decode, scan LTE bands, walk the LTE
chain, and calibrate. `./sdrprobe --help` is the authoritative option list;
[README.md](../README.md) gives representative invocations.

## Sources and sample containers

`device_backend.h` is the operational source boundary. The shipping backends
are RTL-SDR and capture playback; UHD is an optional placeholder until it can
be implemented and measured against hardware. `device_profile.h` carries the
facts that differ between receivers: sample format and full scale, tuning and
sample-rate reach, gain model, reference clock, and retune settle time.

The supported sample containers are interleaved unsigned 8-bit I/Q and signed
16-bit I/Q. Conversion happens once in `sdr_dsp_convert_iq()`. Downstream DSP
receives centred floats in the source device's own counts; it must not infer
full scale from the C container type.

One standard sample block is `SAMPLE_BLOCK_PAIRS` complex samples. Its byte
size is derived from the active profile, so a wider container covers the same
time span and does not split decoder evidence across twice as many blocks.
Capture sidecars describe the tuning and container; a missing container field
retains the historical U8 convention.

## Runtime pipeline

`acquisition` owns the worker lifecycle and publishes through one
mutex-protected overwriteable block slot. A live receiver never blocks behind
the renderer: stale unread blocks are replaced so the window remains current
(ADR-0002). Headless capture playback may switch the same slot to lossless
publication so a scripted decode sees every block. Recording and the
retrospective I/Q ring tee off before this lossy handoff.

`signal_frame` owns one consumed block after conversion: centred I/Q,
magnitudes and statistics, the optional DC-filtered spectrum input, transform,
peak hold, and readiness. The DC filter applies only to spectrum and
waterfall data; decoders and raw-signal measurements receive the unfiltered
centred samples.

Receiver changes go through `receiver_runtime`, a checked
stop/apply/flush/read-back/restart transaction with rollback at every partial
failure. Temporary owners use `receiver_lease` to restore frequency and sample
rate in strict LIFO order. PPM is not leased: an accepted calibration is meant
to survive the overlay that measured it.

## Probe domain

The Probe context measures without interpreting a standardized sequence.
`signal_probe` owns sequence-free carrier, burst, envelope, timing, and
periodicity measurements; `signal_findings` turns only supported measurements
into statements and explicit refusals.

`survey_session` is the state machine for sweep, candidate measurement,
confirmation, and watch. It requests retunes but touches neither receiver nor
filesystem. `survey_record` fixes the meaning of a completed survey before the
window, text report, or JSON writer formats it. The band plan remains a
frequency-allocation lookup, never evidence that a signal is the technology
named by that allocation (ADR-0015).

A calibration belongs to a receiver and site (ADR-0018). Survey history
belongs to the full receiving setup: receiver, site, and antenna (ADR-0022).
`startup_session` owns the GSM-first, LTE-fallback search for references;
calibration accepts only a source-homogeneous residual buffer through the
robust stability gate.

## Decoder domain

Technology DSP modules expose the operations their standards require and share
dependency and testability boundaries, not a uniform interface (ADR-0023).
GSM, ADS-B, LTE, TETRA, and FM have block sessions shared by window and
headless adapters. Their supported outcomes are:

| technology | recovered information |
| --- | --- |
| FM | stereo audio and RDS station information |
| GSM | SCH identity/timing and BCCH System Information |
| Mode S / ADS-B | validated frames, aircraft fields, and global CPR position |
| LTE | cell identity, Master Information Block, reference quality, channel shape, and antenna-port count |
| TETRA | synchronization and broadcast network identity |
| SRD | OOK/Manchester full and repeat frames; 2-FSK presence without frame decode |

LTE cell search and decode run only at 1.92 MS/s (ADR-0014).
`lte_chain_analysis` is a separate public diagnostic walk over every identity
on a carrier; it is shared by live and capture adapters rather than replacing
the lower-cost interactive LTE session.

SRD is the current orchestration exception: cross-block assembly and
deduplication still live in `view_srd.c`. The tracked architecture work moves
that state into a sixth technology session. Likewise, retrospective waterfall
analysis still reaches domain conclusions from an overlay and reconstructs
sample metadata at extraction time; the open deepening tickets record those
boundaries rather than treating them as established architecture.

## Presentation and input

Views and overlays are application adapters. They may read `struct app`, turn
input into intents, and draw module state. Reusable `sdrgui_` components take
plain data and geometry and never see application state (ADR-0007).
`*_layout.h` and geometry helpers contain pure positional decisions, allowing
checks to reach them without opening a window.

Input is resolved before drawing through one precedence order: startup, help,
settings, calibration and scan, tab switching, then the active screen. A new
surface that consumes typed characters must also participate in
`input_takes_typing()` so the global chart handler cannot drain its character
queue first.

## Verification contract

Every decision the program makes must be reachable without a window, receiver,
or person (ADR-0012). Drawing is the exception, and a drawing change is not
complete until its affected screen has been rendered and inspected.

Use the narrowest relevant check while editing, then the repository gate:

```sh
make check-touched
make check
```

Important focused checks include:

```sh
make check-dsp
make check-options
make check-acquisition
make check-signal-frame
make check-receiver-runtime
make check-survey-session
make check-startup-session
make check-lte-chain-analysis
make check-layout
make check-input
make check-pipelines
```

`make check-pipelines` drives the assembled program over committed captures and
asserts identities and messages that synthetic round trips cannot establish.
`make screens NAMES="..."` renders selected window states for visual review.
White-box `make probe-*` targets are diagnostics: they explain a result but do
not replace a checked invariant.

The DSP and domain checks must remain independent of raylib and librtlsdr.
There is no CI; `make hooks` installs the version-controlled pre-push gate.