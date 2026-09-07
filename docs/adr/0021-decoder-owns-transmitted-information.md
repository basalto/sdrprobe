# Decoder owns transmitted information, not only messages

## Status

accepted

## Context and decision

ADR-0010 split Probe from Decoder at message decoding and described the
boundary as beginning when bits are recovered. Newer paths expose information
that does not fit that wording: LTE cell search recovers a standardized
physical identity without a message, FM recovers audio without bits, and TETRA
passes through symbols and dibits before recovering network messages.

The two-context split remains, but its boundary is broader: **Probe owns
acquisition, generic signal measurements, survey, and calibration references;
Decoder owns the interpretation of standardized modulation as transmitted
information**. Transmitted information includes synchronization identities,
symbols and bits, messages, and audio. The boundary follows the meaning of the
claim, not a file, tab, or processing stage.

## Considered options

- **Keep the boundary at recovered bits and messages** leaves LTE physical
  identity and FM audio without a domain home.
- **Put all technology-specific DSP in Decoder** moves calibration reference
  detection out of Probe even though it measures a carrier and recovers no
  transmitted content.
- **Create another context for audio or synchronization** adds boundaries for
  outputs that share Decoder's central act: interpreting standardized
  modulation.

## Consequences

- A technology module may contain operations from both contexts; vocabulary
  and claims, not source-file ownership, enforce the boundary.
- Signal presence, level, shape, and calibration remain Probe claims even when
  measured using a technology-specific reference.
- Cell identity, demodulated audio, symbols, bits, and parsed fields are Decoder
  claims even when no complete message exists.