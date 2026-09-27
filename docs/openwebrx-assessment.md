# OpenWebRX assessment for `sdrprobe`

## Recommendation

**Do not adopt OpenWebRX's architecture.** It solves a different problem —
one shared receiver serving many independently-tuned strangers over the
internet — and ADR-0027 already declined that shape for sdrprobe on the
arithmetic: streaming raw I/Q and re-demodulating per client "multiplies the
processing by the number of Viewers", which is the exact cost OpenWebRX pays
to let each listener pick their own frequency and mode. sdrprobe's Viewer
link is a mirror of one operator's own screen, not a second product.

Two ideas are worth a ticket on their own, independent of anything else
OpenWebRX does:

1. **A server-side send-rate cap for the waterfall/spectrum stream**, shaped
   like `VIEWER_SESSION_STATE_INTERVAL_SECONDS`'s `viewer_update_due()` gate
   (`src/server/viewer_session.h:69`, `src/server/viewer_session.c:340`) but for
   `viewer_link_publish_waterfall_row()`/`_publish_spectrum()`
   (`src/server/viewer_session.c:311-312`). OpenWebRX's `FftChain._updateParameters()`
   (`csdr/chain/fft.py:75-85`) reaches the same end differently — it sizes
   the FFT's input block from `sample_rate / fft_fps` so the *generation*
   rate is capped, not just the send — but the send-side version is the one
   that fits here without touching `SAMPLE_BLOCK_PAIRS`, which is pinned to
   decode timing (GSM/LTE block-boundary behavior), not spectrum cosmetics.
2. **A background "keep decoding regardless of what's on screen" mode** is a
   real gap sdrprobe has and OpenWebRX's `owrx/service/` module names
   cleanly (below) — worth a ticket to decide deliberately rather than leave
   implicit.

Everything else below is either already how sdrprobe works (the binary
wire framing), already rejected by an ADR (per-client demod, raw I/Q
streaming), or in tension with a standing rule (ADR-0003, no external DSP
dependency) and not worth reopening for this.

## What OpenWebRX is

[jketterl/openwebrx](https://github.com/jketterl/openwebrx), AGPLv3: a
multi-user, browser-based SDR receiver server. Python orchestrates a native
DSP core (`pycsdr`/`csdr`, compiled separately, not in the Python checkout)
and a family of external decoder binaries; the browser is a thin canvas +
WebSocket client. Its defining constraint is the opposite of ADR-0027's:
many simultaneous strangers, each wanting their own frequency, mode and
demodulation, sharing one physical dongle.

## One device, N independently-tuned clients

`owrx/source/__init__.py`'s `SdrSource` wraps the receiver as an **external
subprocess** (`getCommand()`/`start()` build a CLI invocation and
`subprocess.Popen` it), piping I/Q over a loopback TCP socket into the
DSP core — `ConnectorSource` (`owrx/source/connector.py:11,37`) adds a
second control socket so a retune is a text line sent to the running
process rather than a restart. A `clients` list with an `SdrClientClass`
(`INACTIVE`/`BACKGROUND`/`USER`) does refcounting: the device starts when
the first client attaches and stops once neither a user nor a background
service holds it (`checkStatus()`), unless configured always-on. Named
**profiles** are just property-layer presets (center frequency, sample
rate, gain) swapped as a whole — not simultaneous independent sub-bands —
so changing profile retunes the device for everyone currently on it.

sdrprobe's equivalent is `src/runtime/device_backend.h`'s vtable plus
`struct acquisition`: one process, one thread pair (`receiver_worker`/
`file_worker`), no subprocess boundary. There is nothing to import here —
introducing an external-process boundary would add IPC and lifecycle
complexity in exchange for a capability (independent per-listener tuning)
that ADR-0027 already declined.

## The per-client DSP chain — why it doesn't map onto sdrprobe

This is the load-bearing architectural fact. `csdr/chain/demodulator.py`'s
`ClientDemodulatorChain` is instantiated **once per listening connection**:
a `Selector` (`csdr/chain/selector.py:69`) mixes the client's chosen offset
to baseband and decimates from the shared wideband rate to whatever
narrowband rate that client's mode needs, feeding a demodulator
(`csdr/chain/analog.py` — `Am`, `NFm`, `WFm`, each a few composed `pycsdr`
blocks) and a `ClientAudioChain`. CPU cost scales with listener count: each
one re-runs shift, filter, decimate and demodulate on their own slice of the
same raw feed.

sdrprobe's Viewer link carries **derived state**, not raw I/Q, and every
connected Viewer receives the *same* spectrum, waterfall and receiver state
— there is one DSP pass (`frame_advance()`) per block regardless of how many
Viewers are attached, and a `tune` command from any Viewer retunes the one
shared receiver for all of them (`viewer_session_handle_command()`,
`src/server/viewer_session.c:61`). ADR-0027 states the reason explicitly: at
15.26 blocks/second, "each Viewer would then repeat the most expensive
processing in the program" if it re-demodulated locally. OpenWebRX pays
that cost on purpose because its product *is* per-listener independence;
sdrprobe's is not, so there is nothing to port here — the two are the same
shape only if you erase the reason each one is built the way it is.

## Decoders: breadth via external binaries vs. sdrprobe's own modules

| Chain | Technology | External binary or in-process | Runs without a viewer? |
|---|---|---|---|
| `dump1090.py` | Mode S / ADS-B | wraps `dump1090` | yes (service) |
| `dumphfdl.py` | HFDL (aircraft HF datalink) | wraps `dumphfdl` | yes (service) |
| `dumpvdl2.py` | VDL2 (aircraft VHF datalink) | wraps `dumpvdl2` | yes (service) |
| `digiham.py` | D-STAR / DMR / NXDN / YSF + POCSAG | external `digiham` + AMBE vocoder | no |
| `digimodes.py` | APRS (via `direwolf`), MSK144, PSK/RTTY | mixed (binary + in-process) | mostly yes |
| `drm.py` | Digital Radio Mondiale | in-process | no |
| `dablin.py` | DAB / DAB+ | wraps `dablin` | no |
| `freedv.py` | FreeDV | in-process | no |
| `m17.py` | M17 digital voice | in-process | no |
| `redsea.py` | RDS | wraps `redsea` | tap off WFm |
| `rtl433.py` | 433/868 MHz ISM sensors | wraps `rtl_433` | yes (service) |
| `owrx/wsjt.py`, `owrx/js8.py` | FT8/FT4/WSPR/JT65, JS8Call | wraps WSJT-X / JS8Call | no |

OpenWebRX reaches this breadth by shelling out to a dozen independent GPL
projects, each with its own build, its own bugs and its own trust boundary.
ADR-0003 rules that out here on purpose: sdrprobe's decoders (GSM SCH/BCCH,
LTE cell search and MIB, TETRA sync, Mode S, RDS, SRD) are hand-derived from
the standards and verified to the bit level against real captures
(`dsp-validation` skill, `check-gsm-dsp`, `check-lte-dsp`, etc.) — a
narrower list, but one this repository can actually vouch for. Wrapping
`rtl_433` or `direwolf` for quick breadth would be the single biggest
capability jump available, but it directly reopens ADR-0003 and would need
its own ADR-level decision, not a quiet dependency addition.

## The waterfall/FFT pipeline: what's directly comparable

This part *is* an apples-to-apples comparison — both programs solve "send a
spectrum/waterfall row over a WebSocket" — and it's where OpenWebRX has
concrete, adoptable ideas:

- **Rate**: `FftChain` sizes its input block from
  `sample_rate / fft_fps / fft_averages` (`csdr/chain/fft.py:75-85`, default
  `fft_fps=9`), so the FFT is *computed* at the target rate rather than
  computed every block and later dropped. sdrprobe currently computes and
  publishes the waterfall on every completed acquisition block
  (`viewer_session.c:310-312`, ~15.3 rows/sec at 2 MS/s) with no cap at all —
  see the recommendation above.
- **Compression**: an optional `FftAdpcm` stage delta-codes the FFT's dB
  bins (`fft_compression: "adpcm"`), applied uniformly for every subscriber
  of one source — not adaptive per client. sdrprobe sends raw `float32`
  arrays. Worth a future ticket, not urgent: it would need a wire-format
  version bump and a `does-it-help`-style measurement of what it actually
  saves against sdrprobe's already-modest ~0.5 MB/s (ADR-0027's own figure).
- **Wire framing**: both use a one-byte message-type prefix over raw binary
  WebSocket frames with no JSON envelope (OpenWebRX's `bytes([0x01]) + data`
  in `owrx/connection.py`; sdrprobe's `VIEWER_MESSAGE_*` header in
  `src/server/viewer_link.c`'s `publish_binary()`). No change indicated — this is
  independent confirmation the current wire shape is a reasonable one, not
  a novelty to reconsider.
- **Backpressure**: OpenWebRX buffers up to 100 pending messages per client
  in a `Queue`, and closes the connection outright when that fills
  (`owrx/connection.py`'s `mp_send()`). sdrprobe's per-stream single
  overwriteable slot (`slot_ready_for_new_message()`,
  `src/server/viewer_link.c:961`) just drops the stale row and keeps the client
  connected. For a LAN diagnostic tool where a dropped Viewer means someone
  has to notice and reopen a browser tab, sdrprobe's more forgiving choice
  reads as correct as-is; OpenWebRX's harsher policy makes more sense for a
  public multi-tenant server that needs to reclaim slots from abandoned
  connections.

## Background decode, independent of what's on screen

`owrx/service/__init__.py`'s `ServiceHandler` registers itself as a
`BACKGROUND`-class client on a source, keeping the device attached and
decoding (APRS, HFDL, VDL2, ADS-B, RDS, ISM) with **no viewer present at
all** — `ServiceScheduler` even rotates the shared receiver's tuning across
configured decode frequencies when nobody is watching.

sdrprobe's `frame_advance()` gates its own decode dispatch on the *active
tab*: `update_adsb()`, `update_gsm_sch()`, `update_tetra()` and
`update_srd()` each run only `if have_new && app->tab == TAB_DECODE &&
app->decode == DECODE_*` (`src/runtime/frame_advance.c:33-47`). Tuned to an ADS-B
frequency but looking at the Scope tab, no Mode S message is parsed at all
— the decode simply isn't running. Headless mode already covers the
scripted case (`./sdrprobe headless --technology adsb --decode`), but the
interactive window has no equivalent of OpenWebRX's "decode continuously
regardless of who's looking." Whether that's wanted is a product question —
continuous background decode costs real CPU every frame whether or not it's
useful — but it's a clean, nameable gap this comparison surfaces, worth its
own ticket to decide on purpose rather than leave as an accident of how the
tab dispatch grew.

## What not to adopt, and why

- **External subprocess per SDR source.** Trades sdrprobe's tight
  single-process loop (ADR-0002's drop-not-lag acquisition, testable with no
  hardware per ADR-0012) for IPC and process-lifecycle management bought to
  support a capability — swapping receivers/backends at runtime — sdrprobe
  doesn't need; `src/runtime/device_backend.h`'s vtable already covers "more than
  one kind of receiver" without a subprocess boundary.
- **Per-client demodulator chains / independent tuning.** Directly
  contradicts ADR-0027's decision and its stated cost argument. Reopening
  it would mean reopening that ADR, not borrowing a technique.
- **Wrapping external GPL decoder binaries for breadth.** Contradicts
  ADR-0003 and this repository's verification culture — every decoder here
  is understood and pinned against real captures at the bit level, which a
  wrapped `rtl_433` or `direwolf` process cannot be.
