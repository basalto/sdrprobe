# Split the domain into Probe and Decoder bounded contexts

## Status

accepted

## Context and decision

The original `CONTEXT.md` was authored to deliberately exclude demodulation and
decoding: its context "ends at signal statistics and visualization," and terms
like *Probe*, *Signal activity*, and *ADS-B activity* explicitly avoid
"Decoder", "Message", and "aircraft detection". Adding ADS-B message decoding
crosses that boundary.

Rather than widen the existing context and dilute its carefully-drawn "no
decode" language, we **split** into two bounded contexts, recorded in
`CONTEXT-MAP.md`:

- **Probe** (the existing root `CONTEXT.md`): acquisition, inspection, signal
  quality, and calibration-grade carrier detection, including GSM.
- **Decoder** (`docs/contexts/decoder/CONTEXT.md`): Mode S demodulation and
  ADS-B message parsing.

## Consequences

- Each context keeps a clean, self-consistent glossary; "signal activity"
  (Probe) and "decoded message" (Decoder) stay distinct rather than blurring.
- The split is a domain-language boundary only. The UI still composes both
  contexts as peer tabs in one window (see ADR-0008); tabs are not contexts.
