# Top-level tab navigation and active-tab state model

## Status

accepted

## Context and decision

The UI grew a set of ad-hoc mode flags (`settings_open`, `calibration_open`,
`scan_open`) layered over an `enum view_kind` (keys 1-4), resolved every frame by
a fixed if/else precedence chain in both the input and draw phases. To organise
the raw-signal views and the message decoders as peers we introduce two
top-level tabs, drawn as a right-aligned, high-contrast button row
(`enum active_tab`):

- **Scope** — the four raw-signal views (magnitude/spectrum/scatter/waterfall),
  still selected by keys 1-4 (`enum view_kind`, demoted to Scope sub-state).
- **Decode** — the message decoders, selected by number keys the same way Scope
  selects its views: **1 GSM** (band analysis: channel-power scan + ARFCN
  waterfall) and **2 ADS-B** (decoded-message log). New decoders slot in as
  further numbered sub-views (`enum decode_kind`) without adding tabs.

Calibration and Settings stay button-driven, global full-screen/modal overlays,
orthogonal to the tabs: either can be opened over any tab and returns to it. The
GSM decode view selects a channel to inspect (retuning the waterfall above to
it) rather than jumping into calibration.

A single uniform header is drawn on every tab: the application name (top-left),
the active tab's numbered options directly below it (Scope's four views, or
Decode's `1 GSM` / `2 ADS-B`, with the active option highlighted), and the
buttons (tabs, Settings, Calibration, health) on the right. Each mode's display
renders below this fixed header.

## Consequences

- The two central precedence chains (`sdrprobe.c` input and draw phases) are:
  settings overlay, then calibration overlay, then a switch on `active_tab`; the
  Decode tab then switches on `decode_kind`. `calibration_open`/`scan_open`
  remain the calibration overlay's own state, independent of the tabs.
- The GSM band scan retunes the receiver, so leaving the Decode tab (or the GSM
  sub-view) restores the pre-scan tuning.
- Adding a decoder is a new `decode_kind` value plus its view/input, not a new
  top-level tab.
