# Context Map

`sdrprobe` now spans two bounded contexts. The **Probe** context acquires and
inspects raw RTL-SDR signals and performs calibration-grade carrier detection
(including GSM 900). The **Decoder** context demodulates and decodes transmitted
messages — Mode S / ADS-B aircraft data and the GSM Synchronisation Channel —
into human-readable fields. They deliberately keep separate vocabularies: the
Probe context stops at signal statistics and never claims to have decoded a
message, while the Decoder context begins exactly where a message is recovered
from the bits.

## Contexts

- [Probe](./CONTEXT.md): acquires raw sample streams from a signal source and
  renders magnitude / spectrum / scatter / waterfall views, signal quality, and
  GSM channel calibration. Never demodulates messages.
- [Decoder](./docs/contexts/decoder/CONTEXT.md): demodulates messages from the
  same sample stream — Mode S extended squitters (ICAO, callsign, altitude,
  velocity, position) and the GSM SCH (BSIC, NCC/BCC, frame number).

## Relationships

- **Probe → Decoder**: both consume the same acquired sample stream (the Probe's
  *Latest block*). The Decoder reuses the Probe's byte→float I/Q conversion and
  per-pair magnitude, then applies its own preamble/PPM demodulation. No Probe
  primitive is reused for the actual bit recovery.
- **Shared tuning**: the app's default tuning (1090 MHz, 2 MS/s) already suits
  the Decoder; the Probe's GSM calibration is the path that retunes away and
  back.
- **UI composition only**: both contexts surface as peer tabs in one window.
  Tabs are a presentation concern, not a domain boundary.
