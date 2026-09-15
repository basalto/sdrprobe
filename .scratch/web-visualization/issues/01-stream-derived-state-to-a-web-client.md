# 01 - Stream derived state to a web client

Status: resolved, 2026-09-16 -- all six decisions taken, recorded below and in
ADR-0027; the work is split into tickets 02-05. **Two of this assessment's own
claims were wrong** and are corrected below rather than edited away.

## Question

Should sdrprobe add a native publisher and web-native visualizer, with
browser-side WebAssembly and direct WebUSB acquisition retained as later,
optional paths?

## Recommendation

Keep acquisition, receiver control, DSP, decode sessions, recording and
survey state in the native process. Publish versioned derived measurements and
events over binary WebSocket to a web-native Canvas/WebGL client.

Do not stream raw I/Q for ordinary visualization. Permit it only as a separate
capability for capture playback or an experimental WebAssembly DSP worker.

Start with WebSocket and enforce backpressure in the application: retain at
most one unsent frame per replaceable visualization stream and overwrite it
when a newer frame arrives. This is the network form of ADR-0002. Commands,
command results, decoded messages, survey records and state transitions remain
reliable and ordered.

Consider WebTransport only after a WebSocket prototype demonstrates a material
latency or head-of-line problem. Its datagrams fit replaceable visualization
state, but HTTPS and an HTTP/3 server make it a more expensive first transport.

## Measured data budget

The source block is 131072 I/Q pairs. At the house 2 MS/s rate, one block spans
65.536 ms and about 15.26 blocks arrive per second.

| payload | bytes per block | approximate rate |
| --- | ---: | ---: |
| raw U8 interleaved I/Q | 262144 | 4.00 MB/s, 32 Mb/s |
| raw S16 interleaved I/Q | 524288 | 8.00 MB/s, 64 Mb/s |
| 2048-bin average and peak spectra, float32 | 16384 | 250 KB/s |
| 2048-bin waterfall row, uint8 | 2048 | 31 KB/s |
| 4096 I/Q scatter points, float32 | 32768 | 500 KB/s |
| 4096 normalized I/Q scatter points, int16 | 16384 | 250 KB/s |

## Corrections to this assessment

Two claims above were measured against the code on 2026-09-16 and do not hold.

**The budget table is quoted at one resolution, not at the maximum.** Every
row says "2048-bin", which is `SDR_DSP_FFT_SIZE`. But `SDR_DSP_FFT_MAX` is
**16384** (`src/sdr_dsp.h:38`) and the Scope resolution stepper walks 256 to
16384 as a user-reachable control, with `--fft points` setting it from the
command line. At 16384 bins, average and peak in float32 is 128 KB per block,
about **2 MB/s rather than 250 KB/s** -- eight times the figure tabulated. The
"about 0.53 MB/s" total is therefore a property of one setting, presented as a
property of the design. A Viewer's subscription must carry the bin count, and
the transform size is not the Viewer's to choose while the Scope owns the
spectrum (`input_scope_owns_spectrum()`).

**"Already shaped like the plain-data parameters in `src/sdrgui.h`" is true of
three charts and false of the two heaviest.** `sdrgui_scatter` and
`sdrgui_waterfall` both take a `Texture2D` -- the application has already
rasterized them before the chart sees them. What rescues this is that both
*update* functions split cleanly and the split is already in the code:
`update_waterfall()` (`src/view_scope.c:186-205`) is pure `memmove`/`memcpy`
into `app->sv.waterfall_dbfs`, a plain float ring, with only its last line
calling `render_waterfall()`; and `update_scatter()` (`:320`) decimates to
`SCATTER_SAMPLES` and normalizes by `app->device.full_scale` into plain
`block->i[]`/`block->q[]` before it reaches `BeginTextureMode`. So the plain
data a State update needs exists in both cases -- but the GPU upload has to be
lifted into the draw phase first, which is ticket 02 and is why ticket 02
exists at all.

The waterfall history should be reconstructed in the browser from rows, not
sent repeatedly. Decode, control and status traffic is negligible beside the
charts. A client subscribing to default spectra, quantized waterfall and
int16 scatter is about 0.53 MB/s before framing, and can be reduced further by
lowering scatter density or update cadence.

Raw U8 is viable for one LAN client but consumes 32 Mb/s before transport
overhead. Raw S16 consumes 64 Mb/s and leaves poor margin on 100 Mb Ethernet.
Neither is a suitable default for WAN access or multiple clients. Budget
20-30 percent above payload rates for framing, metadata and operational
margin; do not assume radio samples compress well.

## Proposed boundary

The protocol carries domain state, not C memory layouts. Each message has a
protocol version, type, sequence, tuning generation, monotonic timestamp and
payload length. Numeric arrays use an explicitly declared byte order and
element encoding; C structs are never copied onto the wire.

Initial message families:

- `hello` and `capabilities`
- `receiver_state` and `signal_frame_summary`
- `spectrum`, `waterfall_row` and `scatter`
- `decode_event`, `survey_state` and `calibration_state`
- `command` and `command_result`

A subscription receives a current snapshot before incremental events. A
tuning generation prevents a delayed chart frame from being drawn against new
receiver metadata. Replaceable streams expose sent and dropped counts so the
prototype can establish whether the transport keeps up.

## Architecture options

### A. Native DSP and web-native presentation

Recommended first path. It processes every sample block once, supports
multiple lightweight clients, retains the existing hardware and filesystem
adapters, and exports inputs already shaped much like the plain-data
parameters in `src/sdrgui.h`.

### B. Native acquisition and WebAssembly DSP

Feasible in a Web Worker because the DSP and session modules generally link
only libm and avoid raylib. It is useful for local capture analysis and DSP
experiments. For live operation it moves 32-64 Mb/s to every client and repeats
the most expensive processing per client, so it should not be the normal
visualization path.

### C. Entire application in WebAssembly

Capture playback and a remote sample source are technically feasible. raylib
supports Emscripten, but `run_gui()` must become a browser-yielding frame
callback; Asyncify preserves the synchronous loop at a size and performance
cost. Pthreads require SharedArrayBuffer, COOP/COEP headers and separate
threaded and unthreaded builds, and blocking the browser main thread must be
avoided.

Direct RTL-SDR acquisition is not a recompile of `backend_rtlsdr.c`. It needs a
new WebUSB backend implementing device setup, control and bulk transfers.
WebUSB requires a secure context and explicit permission and remains absent
from Firefox and Safari, making this path Chromium-only. Browser persistence
also needs an IndexedDB or OPFS adapter, FM audio needs browser user activation,
and native signal/config/filesystem behavior needs browser-specific handling.

Compiling the current raylib interface to a canvas is therefore possible, but
it is a larger and less portable product than the native publisher. A
web-native presentation is also better placed to become responsive and
accessible.

## Human review decisions

- [x] Accept native acquisition and DSP with a web-native visualizer as the
      primary architecture.
- [x] Accept binary WebSocket with explicit latest-frame replacement as the
      first transport.
- [x] Confirm whether browser access is LAN-only or must be exposed remotely.
- [x] Decide whether local capture playback through WebAssembly belongs in the
      first milestone or a later one.
- [x] Decide whether a raylib/Emscripten build is worth maintaining alongside
      a web-native UI.
- [x] Decide whether Chromium-only direct WebUSB support has product value.

## Prototype after approval

Build one native publisher and one browser page for the Scope spectrum,
waterfall and scatter plus receiver/status metadata. Measure encoded bytes per
second, send high-water mark, dropped frames, end-to-end frame age and browser
CPU. Exercise one fast and one deliberately slow client; the slow client must
remain current rather than replaying an old queue.

Only prototype raw-I/Q-to-Wasm processing if the derived-state prototype
cannot satisfy an identified requirement.

## Acceptance criteria

- [x] The human review decisions above are recorded under `## Comments`.
- [x] The selected boundary names which process owns acquisition, DSP,
      receiver control, persistence and rendering.
- [x] The first transport has an explicit stale-frame and reconnect policy.
- [x] Security expectations are stated before remote receiver commands are
      implemented.
- [x] Approved work is split into independently verifiable implementation
      tickets; this assessment ticket does not silently become the project.

## Not in scope

- Implementing a socket server or browser client before review.
- Replacing the native raylib application.
- Designing a generic technology plugin interface.
- Promising direct receiver access in browsers without WebUSB.
- Selecting a serialization library before the prototype defines measured
  message shapes.

## Comments

### 2026-09-16 -- review decisions

Taken in one session against this assessment. The vocabulary changed while
taking them and `CONTEXT.md` now owns it: what this ticket calls a *publisher*
is a **Viewer link**, what it calls a *frame* is a **State update**, and the
browser is a **Viewer**. Both of this ticket's words were already taken --
ADR-0026 defines publication as one-shot, immutable and durable, which a
replaceable stream at 15 updates a second is the opposite of on all three
counts; and *frame* already means a Mode S frame, an SRD frame, a GSM frame
number and `struct signal_frame`, which is the very sample block a State
update would describe.

1. **Architecture: option A, native acquisition and DSP with a web-native
   Viewer.** The motivating case is remote eyes on a live receiver -- the
   receiver stays where it is and is watched from elsewhere. The raylib window
   remains the primary presentation and is not being wound down. Replacing it
   was considered and declined: this repository's correctness apparatus is
   raylib-shaped (ADR-0012's windowless checks, the `*_layout.h` headers that
   `check-layout` walks, `panel_rows.h`, `make screens`), and none of it
   transfers to a browser, so a browser-primary UI would be the untested half
   until it was all rebuilt in a second language.

2. **Transport: hand-written HTTP and WebSocket, binary out, text in.** A
   browser cannot open a raw socket, so WebSocket is forced once commands are
   in scope -- Server-Sent Events are unidirectional. It is hand-written for
   the reason `vendor/` has stayed at one entry: the marginal cost over the
   HTTP parser needed anyway for the upgrade and for serving the page is SHA-1,
   base64 and frame masking, all of which have published test vectors and link
   `-lm` alone.

   **Encoding is asymmetric on purpose.** Outbound: binary WebSocket frames
   for the arrays, JSON text frames for state and metadata -- writing JSON is
   already solved by `survey_json_escape()` in `src/survey_store.c:39`.
   Inbound: whitespace-delimited command lines (`tune 948400000`), parsed with
   `sscanf`. That asymmetry exists to keep a standing refusal: this program
   writes JSON and has deliberately never parsed it, and
   `src/capture_sidecar.h:18` says so in as many words -- *"This is not a JSON
   parser and must not become one."* A JSON command channel would have been
   that parser. The side effect is that the whole control path is drivable by
   hand with `websocat`.

3. **Reach: loopback only.** `127.0.0.1`, with `ssh -L` for anywhere else.
   This program had no socket at all before this work and the repository is
   public, so the first one should add no attack surface. The bind address is
   the authorization boundary and that is what makes decision 4 cheap.

4. **Control from the start, unauthenticated by design.** A Viewer can retune,
   sweep and record. It needs no token because reaching loopback already
   requires an account on the machine. This pairs with 3 and only with 3:
   binding beyond loopback would owe authentication before control, and that
   is a separate decision nobody has taken.

5. **Headless serving is in scope: `--headless --serve`.** This is the
   decision that costs the most and it was taken deliberately -- the
   deployment in view is a receiver on a machine with no display. The one seam
   this assessment's prototype would have used, `src/sdrprobe.c:1953`, is
   *inside* `run_gui()` and does not exist without a window, and every
   `--headless` mode drives a loop of its own. So the per-block advance
   becomes a step both the window and a headless run drive, which is ticket 02.

6. **WebAssembly: B and C declined for live viewing, capture analysis left
   open.** Streaming raw I/Q inverts the bandwidth (0.5 MB/s against 32 Mb/s
   for U8 and 64 for S16, per Viewer) and repeats the most expensive
   processing per Viewer, and it is worst on exactly the small headless
   machine decision 5 is about. Compiling the whole application is a port:
   the frame loop must yield or pay Asyncify, pthreads need SharedArrayBuffer
   with COOP/COEP headers, the receiver needs a new WebUSB backend that
   Firefox and Safari do not have, and config, surveys and captures each need
   a storage adapter. **Capture analysis in wasm is explicitly not declined**
   -- it needs no receiver, no stream and no server, shares no code with the
   Viewer link, and stays available as its own decision. ADR-0027 records that
   carve-out so a later reader does not find "no wasm" and stop.

Recorded as **ADR-0027**, *The Viewer link carries derived state over
loopback*. Work split into tickets 02 (frame-loop extraction), 03 (the
WebSocket server in isolation), 04 (the Viewer link and the first State
updates) and 05 (Viewer commands). This ticket is closed as the assessment it
said it was.

### 2026-09-16 -- amended before any of it was built

Decision 1 above was taken and then sharpened within the hour, which is worth
recording because the second version is what is being built.

**The window and a Viewer are not run at the same time.** They are alternative
frontends, and what they share is a **view model** -- plain data saying what a
screen shows, produced once by the advance step and serialized into State
updates for a Viewer. The raylib views migrate onto the same object one at a
time, each independently verifiable, each declinable.

This was measured before it was agreed. The presentation layer is 17294 lines;
inside the views and overlays there are **153** calls into `sdrgui.h`'s
plain-data components and **174** direct raylib draw calls bypassing them, 104
of those bare `DrawText`. So the abstraction already exists and is
half-adopted, which makes finishing it a migration rather than an invention.

Two things that measurement ruled out. A **display list** -- views emitting
positioned primitives for either backend -- is wrong here, because
`MeasureText` sits at **57 sites across 13 files including `scope_layout.h`
and `sdrgui_geometry.h`**, so this program's geometry is computed from
raylib's font metrics and would be wrong under a browser's. And the **input
half is unsolved**: 161 raylib input call sites, hit-testing done inline
against rectangles that only exist during drawing. A Viewer command is not a
click, so ticket 06 does not need it; a browser reproducing the window's
interactions would, and no ticket claims to.

Recorded as the amendment to ADR-0027. The split is therefore seven tickets,
not four: 02 the advance step, 03 the Scope view model with the raylib Scope
as its first reader, 04 the WebSocket server in isolation, 05 the Viewer link
and the first State updates, 06 Viewer commands, 07 the remaining views.
