# 03 — A real capture, the command line, and the view

Status: resolved
Blocked by: 01, 02

- Record a band 20 cell at 1.92 MS/s into `testfiles/lte_b20_<detail>.bin` with
  its sidecar, and pin its PCI and MIB in `check-pipelines`.
- `--technology lte`, `--earfcn N`, and `--decode` printing the cell and MIB
  headless.
- Band plan: `BAND_PLAN_LTE` on the LTE downlink allocations.
- A Decode-tab view, `src/view_lte.c` with `src/lte_layout.h`, and the
  geometry pinned in `check-layout`.
- An ADR recording that the LTE plugin runs at its own sample rate, and the
  Probe/Decoder vocabulary for LTE in the two CONTEXT files.

## Answer

All of it, except the one item that needs a runtime sample-rate change:

- `testfiles/lte_b20_pci28.bin` and its sidecar, 796.0 MHz at 1.92 MS/s, cell
  32. `check-lte-dsp` asserts the identity and `check-pipelines` asserts the
  whole command-line path to it.
- `--earfcn`, `--technology lte`, `--view lte`, and `--decode` printing the
  cell headless. `--earfcn` also sets the sample grid, and a `--sample-rate`
  that disagrees is refused.
- `BAND_PLAN_LTE` on the band 20 and band 28 downlink allocations. The 900 MHz
  allocation stays pointed at GSM: two technologies share it, the field names
  one decoder, and the GSM view is the one that can find a channel unaided.
- `src/view_lte.c` and `src/lte_layout.h`, with the geometry pinned by property
  in `check-layout` -- the two panels are one row at every window size.
- `docs/adr/0014-lte-runs-on-lte-s-sample-grid.md`, and the LTE vocabulary in
  both CONTEXT files.

Not done: the Settings panel has no sample-rate field, so the LTE view cannot
be reached by switching to it mid-run. It says so and names the flag. Listed
in TODO.md.
