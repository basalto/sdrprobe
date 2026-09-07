# Context Map

`sdrprobe` spans two bounded contexts. The **Probe** context acquires and
inspects raw RTL-SDR signals and performs calibration-grade carrier detection.
The **Decoder** context recovers standardized transmitted information from
those signals: synchronization identities, symbols and bits, messages, and
audio. They deliberately keep separate vocabularies: the Probe context stops
before interpreting modulation as transmitted information, while the Decoder
context owns that interpretation whether or not its result is a message.

## Contexts

- [Probe](./CONTEXT.md): acquires raw sample streams from a signal source,
  inspects one tuning, surveys frequency ranges, measures signal quality, and
  performs channel calibration. Survey is a Probe subdomain even though it has
  its own top-level tab: its claims remain measurements, not decoded messages.
- [Decoder](./docs/contexts/decoder/CONTEXT.md): interprets standardized
  modulation from the same sample stream, including cellular synchronization
  and broadcast information, Mode S / ADS-B messages, TETRA network messages,
  and FM audio and RDS data.

## Relationships

- **Probe → Decoder**: both consume the same acquired sample stream (the Probe's
  *Latest block*). The Decoder may reuse generic Probe primitives, but owns the
  technology-specific interpretation that recovers transmitted information.
- **Shared tuning**: Decoder workflows borrow the receiver tuning and, where
  required, its sample rate; leaving them restores the Probe's previous
  acquisition settings.
- **UI composition only**: both contexts surface as peer tabs in one window.
  Tabs are a presentation concern, not a domain boundary.
