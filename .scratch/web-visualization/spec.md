# Web visualization

Explore a browser visualization for sdrprobe without weakening the native
receiver, DSP and testability boundaries. The first decision is where the
network boundary belongs: after acquisition as raw I/Q, after DSP as derived
display state, or around an Emscripten build of most of the application.

The assessment is in `issues/01-stream-derived-state-to-a-web-client.md`. It
is deliberately awaiting human review before implementation tickets are
created. The choice affects the public protocol, deployment requirements,
browser support and whether each client repeats the DSP workload.

## Constraints

- Preserve ADR-0002's freshness rule: a slow visualizer drops obsolete frames
  rather than accumulating latency.
- Keep receiver control transactional and owned by the native runtime unless
  a browser-specific backend proves otherwise.
- Send domain measurements and events, not raylib draw calls or `struct app`.
- Keep control outcomes reliable and ordered; visualization frames may be
  replaceable.
- Do not make WebUSB-only browser support the primary product path.
- Do not introduce a uniform technology interface solely for serialization.

## Not yet decided

- Whether remote access beyond a trusted LAN is a product requirement.
- Whether the browser must process capture files locally.
- Whether preserving the current raylib presentation is more important than a
  web-native responsive and accessible interface.
- Whether direct browser access to an RTL-SDR is worth Chromium-only support.
