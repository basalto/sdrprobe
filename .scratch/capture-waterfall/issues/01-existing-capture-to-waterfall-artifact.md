# 01 - Existing capture to waterfall artifact

Status: ready-for-agent

## What to build

Add a hardware-free command that reads an existing capture and sidecar,
generates a fixed-format annotated PNG waterfall from the complete raw I/Q,
and records the PNG's relative sibling filename in the sidecar.

This is the reference path for the artifact. It must not initialize a window
or depend on the active Scope resolution. The raster calculation owns the
frequency and time mapping, level range, colour mapping and row reduction as
plain data; PNG writing and sidecar persistence are adapters over that result.

Deepen the existing live waterfall history at the same time. Spectrum append,
row ownership, retune overlap and elapsed-time reduction belong to one
presentation-free module. The Scope's GPU texture and the capture's PNG are
two adapters over that module; neither reimplements what a stored row means.

The sidecar image field is optional for old captures. A new field is written
only after the PNG has been closed successfully. Re-running generation replaces
the same sibling artifact rather than creating numbered duplicates.

## Implementation plan

1. Define a waterfall-history module that appends spectra, preserves overlap
      across retunes, reduces elapsed time and emits a stable raster independently
      of raylib drawing state.
2. Replay each complete sample block through the shipping conversion and
   spectrum path. Derive block time from pair count and recorded sample rate,
   and derive frequency from the recorded tuning and FFT bins.
3. Reduce the full capture into the fixed image height using an explicit rule
   that preserves short signals, such as maximum power per time bucket, and
   document the resulting time resolution.
4. Put frequency ticks, elapsed-time ticks and the dBFS scale around the
   raster. Keep label layout deterministic and covered separately from PNG
   encoding.
5. Export the PNG, then update the sidecar with a relative sibling filename
   using a write-and-rename operation so interruption cannot truncate the
   existing JSON.
6. Add the command-line entry point, user documentation and the required
   semantic version update for the sidecar/CLI extension.

## Tasks

- [ ] Specify the field name and bounded relative-path representation in the
      capture-sidecar value.
- [ ] Add backward-compatible sidecar reading and writing for the optional
      image path.
- [ ] Implement deterministic raw-I/Q-to-waterfall raster generation for U8
      and S16 captures using their own full scale and sample rate.
- [ ] Move live history append and retune-overlap behavior behind the same
      presentation-free interface used by artifact generation.
- [ ] Keep GPU texture upload and PNG encoding as separate adapters.
- [ ] Define and check the fixed width, height, FFT size, dBFS range, colour
      map and whole-duration row-reduction rule.
- [ ] Render frequency, elapsed-time and level annotations without requiring a
      window or GPU state.
- [ ] Add an explicit CLI operation for generating or regenerating the
      artifact of an existing capture.
- [ ] Preserve unknown sidecar fields when adding the image path.
- [ ] Update capture format documentation, CLI help and versioning.

## Acceptance criteria

- [ ] One command turns a committed capture into an annotated sibling PNG and
      a sidecar containing only the relative PNG filename.
- [ ] The same capture and parameters produce byte-identical raster pixels on
      two runs; PNG metadata that may vary is excluded from that claim.
- [ ] U8 and generated S16 forms of the same capture produce equivalent
      waterfall levels and geometry.
- [ ] A capture longer than the image height still represents its full
      duration and a short high-power event remains visible after reduction.
- [ ] Old sidecars without the field and reconstructed sidecars with null
      values remain readable.
- [ ] Export failure leaves the original sidecar valid and does not add a path
      to a missing image.
- [ ] Focused sidecar and waterfall checks need no window or receiver.
- [ ] The live waterfall and capture artifact consume the same row and retune
      semantics rather than parallel implementations.
- [ ] The generated PNG has been inspected at the documented dimensions and
      its axes, labels and spectrum do not overlap.

## Blocked by

None - can start immediately.

## Not in scope

- Automatic generation when a new recording finishes; ticket 02 owns it.
- Saving the live Scope texture or current window.
- Embedding raw spectra in the sidecar.
- Reconstructing missing tuning metadata for legacy captures.

## Comments

Updated from the 2026-09-15 architecture review. The second real adapter, PNG
artifact generation, makes a waterfall-history seam worth having; deleting
the module would put row stride, retune movement and time reduction back into
both adapters. Report: `/tmp/architecture-review-20260915-175714.html`.
