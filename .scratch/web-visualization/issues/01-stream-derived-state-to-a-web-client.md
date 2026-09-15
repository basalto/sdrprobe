# 01 - Stream derived state to a web client

Status: ready-for-human

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

- [ ] Accept native acquisition and DSP with a web-native visualizer as the
      primary architecture.
- [ ] Accept binary WebSocket with explicit latest-frame replacement as the
      first transport.
- [ ] Confirm whether browser access is LAN-only or must be exposed remotely.
- [ ] Decide whether local capture playback through WebAssembly belongs in the
      first milestone or a later one.
- [ ] Decide whether a raylib/Emscripten build is worth maintaining alongside
      a web-native UI.
- [ ] Decide whether Chromium-only direct WebUSB support has product value.

## Prototype after approval

Build one native publisher and one browser page for the Scope spectrum,
waterfall and scatter plus receiver/status metadata. Measure encoded bytes per
second, send high-water mark, dropped frames, end-to-end frame age and browser
CPU. Exercise one fast and one deliberately slow client; the slow client must
remain current rather than replaying an old queue.

Only prototype raw-I/Q-to-Wasm processing if the derived-state prototype
cannot satisfy an identified requirement.

## Acceptance criteria

- [ ] The human review decisions above are recorded under `## Comments`.
- [ ] The selected boundary names which process owns acquisition, DSP,
      receiver control, persistence and rendering.
- [ ] The first transport has an explicit stale-frame and reconnect policy.
- [ ] Security expectations are stated before remote receiver commands are
      implemented.
- [ ] Approved work is split into independently verifiable implementation
      tickets; this assessment ticket does not silently become the project.

## Not in scope

- Implementing a socket server or browser client before review.
- Replacing the native raylib application.
- Designing a generic technology plugin interface.
- Promising direct receiver access in browsers without WebUSB.
- Selecting a serialization library before the prototype defines measured
  message shapes.

## Comments
